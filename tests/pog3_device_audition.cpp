// Device-only live playing diagnostic. The audio thread owns all DSP state;
// the control thread handles footswitches, commands, logging and status files.
#include "daisyfx/pog3/Pog3Processor.h"
#include "dsp/DenormalGuard.h"
#include <alsa/asoundlib.h>
#include <fftw3.h>
#include <linux/input.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr unsigned kFrames = 128;
constexpr double kIntScale = 2147483648.0;
constexpr double kPeriodUs = kFrames * 1000000.0 / 48000;
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> interrupted{false};
void onSignal(int) { interrupted.store(true, std::memory_order_relaxed); }
void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
std::uint64_t nowNs() {
  timespec t{};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return static_cast<std::uint64_t>(t.tv_sec) * 1000000000ULL + t.tv_nsec;
}
float decode(std::int32_t value) { return static_cast<float>(value / kIntScale); }
float limit(float value) {
  constexpr float ceiling = .8912509f, knee = ceiling * .95f;
  if (!std::isfinite(value)) return 0;
  const float magnitude = std::fabs(value);
  if (magnitude <= knee) return value;
  const float width = ceiling - knee, excess = magnitude - knee;
  return std::copysign(knee + width * excess / (width + excess), value);
}
std::int32_t encode(float value) {
  return static_cast<std::int32_t>(static_cast<double>(limit(value)) * kIntScale);
}
enum class Mode { Dry, Up, Down, Blend };
const char* name(Mode mode) {
  constexpr std::array<const char*, 4> names{"dry", "octave-up", "octave-down", "blend"};
  return names[static_cast<unsigned>(mode)];
}
void setMode(ardor::pog3::Pog3Processor& core, Mode mode) {
  core.setParameterTarget("dry_level", mode == Mode::Dry || mode == Mode::Blend ? 1 : 0);
  core.setParameterTarget("up1_level", mode == Mode::Up ? 1 : mode == Mode::Blend ? .5f : 0);
  core.setParameterTarget("down1_level", mode == Mode::Down ? 1 : mode == Mode::Blend ? .5f : 0);
}
bool configure(ardor::pog3::Pog3Processor& core, std::string& error) {
  // Unity input; -3.10 dB master in every mode. Open filter and no intentional
  // attack, modulation, spread or expression delay. Only the pitch algorithm
  // and physical audio transport contribute to the audition's response.
  return core.configure({{"input_gain", .2}, {"master_level", .35},
    {"dry_level", 1}, {"down1_level", 0}, {"up1_level", 0},
    {"down2_level", 0}, {"fifth_level", 0}, {"up2_level", 0},
    {"attack", 0}, {"dry_attack", 0}, {"focus", 0},
    {"filter_frequency", 1}, {"filter_env", .5}, {"filter_q", 0},
    {"dry_filter", 0}, {"detune", 0}, {"spread", 0},
    {"dry_detune", 0}, {"expression_mode", 0}}, 48000, error);
}
struct Pcm {
  snd_pcm_t* handle = nullptr;
  ~Pcm() { if (handle) { snd_pcm_drop(handle); snd_pcm_close(handle); } }
  void open(snd_pcm_stream_t stream) {
    int result = snd_pcm_open(&handle, "hw:Zero,0", stream, 0);
    require(result >= 0, snd_strerror(result));
    snd_pcm_hw_params_t* p;
    snd_pcm_hw_params_alloca(&p);
    require(snd_pcm_hw_params_any(handle, p) >= 0, "ALSA any");
    require(snd_pcm_hw_params_set_access(handle, p, SND_PCM_ACCESS_RW_INTERLEAVED) >= 0, "ALSA access");
    require(snd_pcm_hw_params_set_format(handle, p, SND_PCM_FORMAT_S32_LE) >= 0, "ALSA format");
    require(snd_pcm_hw_params_set_channels(handle, p, 2) >= 0, "ALSA channels");
    require(snd_pcm_hw_params_set_rate(handle, p, 48000, 0) >= 0, "ALSA rate");
    require(snd_pcm_hw_params_set_period_size(handle, p, kFrames, 0) >= 0, "ALSA period");
    require(snd_pcm_hw_params_set_buffer_size(handle, p, 3 * kFrames) >= 0, "ALSA buffer");
    require(snd_pcm_hw_params(handle, p) >= 0, "ALSA hardware commit");
    snd_pcm_sw_params_t* sw;
    snd_pcm_sw_params_alloca(&sw);
    require(snd_pcm_sw_params_current(handle, sw) >= 0, "ALSA software parameters");
    require(snd_pcm_sw_params_set_avail_min(handle, sw, kFrames) >= 0, "ALSA avail");
    require(snd_pcm_sw_params_set_start_threshold(handle, sw,
      stream == SND_PCM_STREAM_PLAYBACK ? 3 * kFrames : 1) >= 0, "ALSA threshold");
    require(snd_pcm_sw_params(handle, sw) >= 0, "ALSA software commit");
    require(snd_pcm_prepare(handle) >= 0, "ALSA prepare");
  }
  int transfer(std::int32_t* data, unsigned frames, bool capture) {
    unsigned done = 0;
    while (done < frames && !interrupted) {
      const auto n = capture ? snd_pcm_readi(handle, data + 2 * done, frames - done)
                             : snd_pcm_writei(handle, data + 2 * done, frames - done);
      if (n == -EINTR) continue;
      if (n < 0) return static_cast<int>(n);
      if (n == 0) return -EIO;
      done += static_cast<unsigned>(n);
    }
    return done == frames ? 0 : -EINTR;
  }
};
struct InputDevice {
  int fd = -1;
  InputDevice(const std::filesystem::path& path, const char* expected) {
    fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    require(fd >= 0, "cannot open audition controls: " + path.string());
    char actual[128]{};
    require(ioctl(fd, EVIOCGNAME(sizeof(actual)), actual) >= 0 && std::string(actual) == expected,
            "unexpected audition control device");
  }
  ~InputDevice() { if (fd >= 0) ::close(fd); }
  bool next(input_event& event) { return ::read(fd, &event, sizeof(event)) == sizeof(event); }
};
struct Telemetry {
  std::atomic<bool> ready{false}, finished{false}, stop{false};
  std::atomic<std::uint64_t> callbacks{0}, totalNs{0}, maximumNs{0}, overPeriod{0};
  std::atomic<unsigned> xruns{0};
  std::atomic<float> inputPeak{0}, outputPeak{0};
  // Written by the worker, read only after joining it.
  std::string error;
};
void audio(ardor::pog3::Pog3Processor& core, Telemetry& t, unsigned seconds, bool silent,
           std::stop_token stop) {
  try {
    cpu_set_t cpus;
    CPU_ZERO(&cpus); CPU_SET(2, &cpus);
    require(sched_setaffinity(0, sizeof(cpus), &cpus) == 0, "audio CPU2 affinity");
    Pcm capture, playback;
    capture.open(SND_PCM_STREAM_CAPTURE); playback.open(SND_PCM_STREAM_PLAYBACK);
    sched_param p{}; p.sched_priority = 70;
    require(sched_setscheduler(0, SCHED_FIFO, &p) == 0, "audio FIFO70");
    std::array<std::int32_t, 6 * kFrames> prime{};
    require(playback.transfer(prime.data(), 3 * kFrames, false) == 0, "playback prime");
    t.ready.store(true, std::memory_order_release);
    const auto endTime = nowNs() + seconds * 1000000000ULL;
    std::array<std::int32_t, 2 * kFrames> input{}, output{};
    std::uint64_t calls = 0, total = 0, maximum = 0, over = 0;
    while (!stop.stop_requested() && !t.stop.load(std::memory_order_relaxed)
           && !interrupted && nowNs() < endTime) {
      int result = capture.transfer(input.data(), kFrames, true);
      if (result < 0) {
        if (result == -EINTR && interrupted) break;
        if (result == -EPIPE) ++t.xruns;
        throw std::runtime_error(std::string("capture: ") + snd_strerror(result));
      }
      const auto start = nowNs();
      float inputPeak = 0, outputPeak = 0;
      {
        ardor::ScopedDenormalGuard denormals;
        for (unsigned i = 0; i < kFrames; ++i) {
          // Matches the installed app's input channel 0; feed that mono guitar
          // into both sides of the stereo effect and drive both hardware outs.
          const float x = decode(input[2 * i]);
          const auto y = core.process({x, x}).mixed;
          inputPeak = std::max(inputPeak, std::fabs(x));
          outputPeak = std::max({outputPeak, std::fabs(y.left), std::fabs(y.right)});
          output[2 * i] = encode(y.left);
          output[2 * i + 1] = encode(y.right);
        }
      }
      if (silent) output.fill(0);
      const auto duration = nowNs() - start;
      ++calls; total += duration; maximum = std::max(maximum, duration);
      over += duration / 1000.0 > kPeriodUs;
      t.callbacks.store(calls, std::memory_order_relaxed);
      t.totalNs.store(total, std::memory_order_relaxed);
      t.maximumNs.store(maximum, std::memory_order_relaxed);
      t.overPeriod.store(over, std::memory_order_relaxed);
      t.inputPeak.store(inputPeak, std::memory_order_relaxed);
      t.outputPeak.store(outputPeak, std::memory_order_relaxed);
      result = playback.transfer(output.data(), kFrames, false);
      if (result < 0) {
        if (result == -EINTR && interrupted) break;
        if (result == -EPIPE) ++t.xruns;
        throw std::runtime_error(std::string("playback: ") + snd_strerror(result));
      }
    }
    require(core.healthy() && core.deadlineMisses() == 0, "POG3 DSP health/deadline");
  } catch (const std::exception& e) { t.error = e.what(); }
  t.finished.store(true, std::memory_order_release);
}
void status(const std::filesystem::path& directory, const Telemetry& t, Mode mode, bool focus) {
  const auto calls = t.callbacks.load();
  std::ofstream out(directory / "status.tmp");
  out << "{\"mode\":\"" << name(mode) << "\",\"focus\":" << focus
      << ",\"ready\":" << t.ready.load() << ",\"finished\":" << t.finished.load()
      << ",\"callbacks\":" << calls << ",\"mean_us\":"
      << (calls ? t.totalNs.load() / (1000.0 * calls) : 0)
      << ",\"max_us\":" << t.maximumNs.load() / 1000.0
      << ",\"over_period\":" << t.overPeriod.load() << ",\"xruns\":" << t.xruns.load()
      << ",\"input_peak\":" << t.inputPeak.load() << ",\"output_peak\":" << t.outputPeak.load() << "}\n";
  out.close();
  std::filesystem::rename(directory / "status.tmp", directory / "status.json");
}
int selfTest() {
  require(decode(std::numeric_limits<std::int32_t>::min()) == -1, "S32 minimum");
  require(encode(0) == 0 && encode(.25f) == 536870912, "S32 encoding");
  for (float x : {-100.0f, -1.0f, -.25f, 0.0f, .25f, 1.0f, 100.0f})
    require(std::isfinite(limit(x)) && std::fabs(limit(x)) <= .891251f, "output ceiling");
  require(encode(std::numeric_limits<float>::infinity()) == 0, "nonfinite output");
  ardor::pog3::Pog3Processor core;
  std::string error;
  require(configure(core, error), error);
  ardor::ScopedDenormalGuard denormals;
  float worstDryError = 0;
  for (unsigned i = 0; i < 12000; ++i) {
    const float x = .1f * std::sin(6.283185307179586 * 110 * i / 48000);
    const auto y = core.process({x, x}).mixed;
    worstDryError = std::max(worstDryError, std::fabs(y.left - .7f * x));
    require(y.left == y.right, "mono dry stereo copy");
  }
  require(worstDryError < 1e-6f, "dry path has unintended delay or processing");
  for (bool focus : {false, true}) {
    core.setParameterTarget("focus", focus ? 1 : 0);
    for (Mode mode : {Mode::Up, Mode::Down, Mode::Blend, Mode::Dry}) {
      setMode(core, mode);
      double energy = 0;
      for (unsigned i = 0; i < 24000; ++i) {
        const float x = .1f * std::sin(6.283185307179586 * 110 * i / 48000);
        const auto y = core.process({x, x}).mixed;
        require(std::isfinite(y.left) && std::isfinite(y.right), "audition mode output");
        if (i > 12000) energy += y.left * y.left + y.right * y.right;
      }
      require(energy > .001 && core.healthy() && core.deadlineMisses() == 0, "audition mode health/energy");
    }
  }
  std::cout << "self-test passed: dry error=" << worstDryError
            << ", four modes at both Focus settings, S32 conversion and output ceiling\n";
  return 0;
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") return selfTest();
    require(argc == 4 || (argc == 5 && std::string(argv[4]) == "--silent"),
            "usage: audition wisdom session-directory seconds [--silent] | --self-test");
    const unsigned seconds = static_cast<unsigned>(std::stoul(argv[3]));
    require(seconds >= 1 && seconds <= 1800, "audition duration must be 1..1800 seconds");
    const std::filesystem::path directory = argv[2];
    require(std::filesystem::is_directory(directory), "session directory missing");
    require(fftwf_import_wisdom_from_filename(argv[1]) != 0, "wisdom import");
    ardor::pog3::Pog3Processor core;
    std::string error;
    require(configure(core, error), error);
    InputDevice switches("/dev/input/event0", "gpio-keys"), encoder("/dev/input/event1", "rotary-encoder");
    std::signal(SIGINT, onSignal); std::signal(SIGTERM, onSignal);
    Telemetry telemetry;
    Mode mode = Mode::Dry;
    bool focus = false;
    std::jthread worker([&](std::stop_token stop) { audio(core, telemetry, seconds, argc == 5, stop); });
    auto lastStatus = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    std::cout << "F1=dry F2=octave-up F3=octave-down F4=blend; encoder: left=Focus off, right=Focus on\n";
    while (!telemetry.finished.load(std::memory_order_acquire) && !interrupted) {
      input_event event{};
      bool changed = false;
      while (switches.next(event)) {
        if (event.type == EV_KEY && event.value == 1 && event.code >= KEY_F1 && event.code <= KEY_F4) {
          mode = static_cast<Mode>(event.code - KEY_F1); setMode(core, mode); changed = true;
        }
      }
      while (encoder.next(event)) {
        if (event.type == EV_REL && event.code == REL_X && event.value) {
          focus = event.value > 0; core.setParameterTarget("focus", focus ? 1 : 0); changed = true;
        }
      }
      std::ifstream command(directory / "command");
      std::string token;
      if (command >> token) {
        std::filesystem::remove(directory / "command");
        if (token == "stop") telemetry.stop.store(true);
        else if (token == "focus-on" || token == "focus-off") {
          focus = token == "focus-on"; core.setParameterTarget("focus", focus ? 1 : 0); changed = true;
        } else {
          constexpr std::array<const char*, 4> commands{"dry", "up", "down", "blend"};
          for (unsigned i = 0; i < commands.size(); ++i) if (token == commands[i]) {
            mode = static_cast<Mode>(i); setMode(core, mode); changed = true;
          }
        }
      }
      const auto now = std::chrono::steady_clock::now();
      if (changed || now - lastStatus >= std::chrono::seconds(1)) {
        status(directory, telemetry, mode, focus); lastStatus = now;
        if (changed) std::cout << "mode=" << name(mode) << " focus=" << focus << std::endl;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    telemetry.stop.store(true);
    worker.join();
    status(directory, telemetry, mode, focus);
    if (!telemetry.error.empty()) { std::cerr << telemetry.error << '\n'; return telemetry.xruns.load() ? 3 : 1; }
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
