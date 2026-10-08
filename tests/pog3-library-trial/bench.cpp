#include "daisyfx/pog3/PolyphonicPitchBank.h"
#ifndef ARDOR_POG3_ONLY
#include "daisyfx/hosted/dsp/band_shifter.h"
#include "daisyfx/hosted/dsp/multirate.h"
#include "signalsmith-stretch.h"
#include "rubberband/RubberBandStretcher.h"
#include "rubberband/RubberBandLiveShifter.h"
#endif
#include "erb_ps2_reference.h"
#include "erb_shared_bank.h"
#include "erb_cadence_bank.h"
#include "erb_attack_freeze.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

// Use Ardor's existing Linux/glibc LD_PRELOAD probe in a separate invocation.
extern "C" void pog3_malloc_begin() __attribute__((weak));
extern "C" void pog3_malloc_end(std::size_t*, std::size_t*) __attribute__((weak));

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr std::size_t maxBlock = 512, maxVoices = 8;
using Channel = std::array<float, maxBlock>;
using Stereo = std::array<Channel, 2>;
using Outputs = std::array<Stereo, maxVoices>;

float pitch(std::size_t voice, std::size_t count, float extent) {
  if (count == 5) return ardor::pog3::kVoiceSemitones[voice + 1] * extent;
  constexpr std::array<float, 8> semitones{0, -24, -12, 7, 12, 24, 12, 24};
  return semitones[voice] * (voice >= 6 ? 1 : extent);
}

struct Processor {
  Outputs output{};
  std::size_t count, faults = 0, latency = 0;
  explicit Processor(std::size_t n) : count(n) {}
  virtual ~Processor() = default;
  virtual void reset() = 0;
  virtual void transpose(float extent) = 0;
  virtual void process(const Stereo& input, std::size_t n) = 0;
  virtual void diagnostics() const {}
};

struct Ardor final : Processor {
  ardor::pog3::PolyphonicPitchBank bank;
  explicit Ardor(std::size_t n) : Processor(n) {
    if (n != 8) throw std::runtime_error("Ardor always renders 6 long + 2 short stereo paths; use voices=8");
    bank.prepare(); latency = bank.kLongDelay;
  }
  void reset() override { bank.reset(); faults = 0; }
  void transpose(float extent) override { (void)bank.setWarp(extent); }
  void process(const Stereo& input, std::size_t n) override {
    for (std::size_t i = 0; i < n; ++i) {
      const auto y = bank.process({input[0][i], input[1][i]});
      for (std::size_t v = 0; v < 6; ++v) {
        output[v][0][i] = y[v].left; output[v][1][i] = y[v].right;
      }
    }
    if (!bank.healthy() || bank.deadlineMisses()) ++faults;
  }
};

#ifndef ARDOR_POG3_ONLY
struct Signalsmith final : Processor {
  std::vector<std::unique_ptr<signalsmith::stretch::SignalsmithStretch<float>>> voices;
  Signalsmith(std::size_t n, const std::string& preset) : Processor(n) {
    for (std::size_t v = 0; v < n; ++v) {
      auto s = std::make_unique<signalsmith::stretch::SignalsmithStretch<float>>(1234L);
      if (preset == "ss-default") s->presetDefault(2, 48000, true);
      else if (preset == "ss-cheap") s->presetCheaper(2, 48000, true);
      else if (preset == "ss-pog3") s->configure(2, v >= 6 ? 1024 : 2048, v >= 6 ? 128 : 256, true);
      else if (preset == "ss-balanced") s->configure(2, v >= 6 ? 1024 : 2048, v >= 6 ? 256 : 512, true);
      else throw std::runtime_error("unknown Signalsmith configuration");
      latency = std::max<std::size_t>(latency, s->inputLatency() + s->outputLatency());
      voices.push_back(std::move(s));
    }
    transpose(1);
  }
  void reset() override { for (auto& s : voices) s->reset(); faults = 0; }
  void transpose(float extent) override {
    for (std::size_t v = 0; v < count; ++v) voices[v]->setTransposeSemitones(pitch(v, count, extent));
  }
  void process(const Stereo& input, std::size_t n) override {
    const float* in[]{input[0].data(), input[1].data()};
    for (std::size_t v = 0; v < count; ++v) {
      float* out[]{output[v][0].data(), output[v][1].data()};
      voices[v]->process(in, static_cast<int>(n), out, static_cast<int>(n));
    }
  }
};

// Fixed-capacity streaming FIFO: no resize or blocking. Count every underrun
// and overflow rather than hiding it by feeding more future input. The R2/R3
// screening wrapper adds 4096 samples of reservoir delay after startup padding
// and trimming. This is intentionally reported, not a production design.
struct Fifo {
  static constexpr std::size_t capacity = 32768;
  std::array<std::array<float, capacity>, 2> data{};
  std::size_t read = 0, size = 0;
  void reset() { read = size = 0; }
  bool push(const float* const* in, std::size_t n) {
    if (n > capacity - size) return false;
    for (std::size_t c = 0; c < 2; ++c) for (std::size_t i = 0; i < n; ++i)
      data[c][(read + size + i) % capacity] = in[c][i];
    size += n; return true;
  }
  bool pop(Stereo& out, std::size_t n) {
    if (n > size) { for (auto& c : out) std::fill_n(c.begin(), n, 0); return false; }
    for (std::size_t c = 0; c < 2; ++c) for (std::size_t i = 0; i < n; ++i)
      out[c][i] = data[c][(read + i) % capacity];
    read = (read + n) % capacity; size -= n; return true;
  }
};

struct Rubber final : Processor {
  using R = RubberBand::RubberBandStretcher;
  std::vector<std::unique_ptr<R>> voices;
  std::vector<Fifo> queues;
  std::array<std::size_t, 8> trim{};
  Stereo scratch{};
  explicit Rubber(std::size_t n, bool finer) : Processor(n), queues(n) {
    const int options = R::OptionProcessRealTime | R::OptionThreadingNever
      | R::OptionPitchHighConsistency | R::OptionChannelsTogether | R::OptionWindowShort
      | (finer ? R::OptionEngineFiner : R::OptionEngineFaster);
    for (std::size_t v = 0; v < n; ++v) {
      auto r = std::make_unique<R>(48000, 2, options, 1, std::exp2(pitch(v, count, 1) / 12));
      r->setMaxProcessSize(maxBlock); voices.push_back(std::move(r));
    }
    latency = 4096; reset();
  }
  void drain(std::size_t v) {
    float* out[]{scratch[0].data(), scratch[1].data()};
    while (voices[v]->available() > 0) {
      const auto got = voices[v]->retrieve(out, std::min<std::size_t>(maxBlock, voices[v]->available()));
      if (!got) { ++faults; break; }
      const auto skip = std::min(trim[v], got); trim[v] -= skip;
      const float* remaining[]{out[0] + skip, out[1] + skip};
      if (!queues[v].push(remaining, got - skip)) ++faults;
    }
  }
  void reset() override {
    faults = 0; Stereo silence{};
    const float* in[]{silence[0].data(), silence[1].data()};
    for (std::size_t v = 0; v < count; ++v) {
      voices[v]->reset(); queues[v].reset(); trim[v] = voices[v]->getStartDelay();
      for (std::size_t i = 0; i < 4096 / maxBlock; ++i) (void)queues[v].push(in, maxBlock);
      std::size_t pad = voices[v]->getPreferredStartPad();
      while (pad) {
        const auto n = std::min(pad, maxBlock); voices[v]->process(in, n, false); drain(v); pad -= n;
      }
    }
  }
  void transpose(float extent) override {
    for (std::size_t v = 0; v < count; ++v) voices[v]->setPitchScale(std::exp2(pitch(v, count, extent) / 12));
  }
  void process(const Stereo& input, std::size_t n) override {
    const float* in[]{input[0].data(), input[1].data()};
    for (std::size_t v = 0; v < count; ++v) {
      voices[v]->process(in, n, false); drain(v);
      if (!queues[v].pop(output[v], n)) ++faults;
    }
  }
};

struct RubberLive final : Processor {
  using R = RubberBand::RubberBandLiveShifter;
  std::vector<std::unique_ptr<R>> voices;
  std::vector<Fifo> queues;
  Stereo pending{}, scratch{};
  std::size_t filled = 0, blockSize = 0;
  explicit RubberLive(std::size_t n) : Processor(n), queues(n) {
    for (std::size_t v = 0; v < n; ++v) {
      auto r = std::make_unique<R>(48000, 2, R::OptionWindowShort | R::OptionChannelsTogether);
      r->setPitchScale(std::exp2(pitch(v, count, 1) / 12));
      blockSize = r->getBlockSize();
      if (blockSize > maxBlock) throw std::runtime_error("live block exceeds prepared capacity");
      latency = std::max(latency, r->getStartDelay() + blockSize);
      voices.push_back(std::move(r));
    }
    reset();
  }
  void reset() override {
    filled = faults = 0; pending = {}; Stereo silence{};
    const float* in[]{silence[0].data(), silence[1].data()};
    for (std::size_t v = 0; v < count; ++v) {
      voices[v]->reset(); queues[v].reset(); (void)queues[v].push(in, blockSize);
    }
  }
  void transpose(float extent) override {
    for (std::size_t v = 0; v < count; ++v) voices[v]->setPitchScale(std::exp2(pitch(v, count, extent) / 12));
  }
  void process(const Stereo& input, std::size_t n) override {
    for (std::size_t i = 0; i < n; ++i) {
      for (std::size_t c = 0; c < 2; ++c) pending[c][filled] = input[c][i];
      if (++filled == blockSize) {
        const float* in[]{pending[0].data(), pending[1].data()};
        float* out[]{scratch[0].data(), scratch[1].data()};
        for (std::size_t v = 0; v < count; ++v) {
          voices[v]->shift(in, out); if (!queues[v].push(out, blockSize)) ++faults;
        }
        filled = 0;
      }
    }
    for (std::size_t v = 0; v < count; ++v) if (!queues[v].pop(output[v], n)) ++faults;
  }
};

// Stereo, separately exposed voices using Ardor's existing Terrarium-derived
// filters. Compare original 80-band spacing and Ardor's 48-band spacing. This
// is a three-fixed-octave reference, not an implementation of the POG3 bank.
struct Terrarium final : Processor {
  std::array<std::vector<BandShifter>, 2> bands;
  std::array<Decimator, 2> decimators;
  std::array<std::array<Interpolator, 3>, 2> interpolators;
  std::array<std::array<float, 6>, 2> pending{};
  std::array<std::array<std::array<float, 6>, 3>, 2> generated{};
  std::size_t filled = 0, reading = 0, bandCount;
  explicit Terrarium(std::size_t n) : Processor(3), bandCount(n) { reset(); }
  void reset() override {
    filled = reading = faults = 0; pending = {}; generated = {};
    const float step = bandCount == 80 ? .027f : .0453f;
    const auto center = [&](int i) { return 480.0f * std::pow(2.0f, step * i) - 420.0f; };
    for (std::size_t c = 0; c < 2; ++c) {
      bands[c].clear(); bands[c].reserve(bandCount);
      for (int i = 0; i < static_cast<int>(bandCount); ++i) {
        const float a = center(i + 1) - center(i), b = center(i) - center(i - 1);
        bands[c].emplace_back(center(i), 8000, 2 * a * b / (a + b));
      }
      decimators[c].Reset(); for (auto& r : interpolators[c]) r.Reset();
    }
  }
  void transpose(float) override {} // Caller prohibits dynamic workloads.
  void process(const Stereo& input, std::size_t n) override {
    for (std::size_t i = 0; i < n; ++i) {
      for (std::size_t c = 0; c < 2; ++c) pending[c][filled] = input[c][i];
      if (++filled == 6) {
        for (std::size_t c = 0; c < 2; ++c) {
          const auto sample = decimators[c](std::span<const float, 6>{pending[c]});
          std::array<float, 3> sum{};
          for (auto& band : bands[c]) {
            band.update(sample); sum[0] += band.down2(); sum[1] += band.down1(); sum[2] += band.up1();
          }
          for (std::size_t v = 0; v < 3; ++v) generated[c][v] = interpolators[c][v](sum[v]);
        }
        filled = reading = 0;
      }
      for (std::size_t c = 0; c < 2; ++c) for (std::size_t v = 0; v < 3; ++v)
        output[v][c][i] = generated[c][v][reading];
      reading = (reading + 1) % 6;
    }
  }
};

#endif

struct ErbPs2 final : Processor {
  pog3_trial::ErbPs2Reference reference;
  ErbPs2() : Processor(1) {}
  void reset() override { reference.reset(); faults = 0; }
  void transpose(float) override {} // Octave-up only; no dynamic Warp claim.
  void process(const Stereo& input, std::size_t n) override {
    for (std::size_t i = 0; i < n; ++i) {
      const auto y = reference.process({input[0][i], input[1][i]});
      output[0][0][i] = y[0]; output[0][1][i] = y[1];
    }
  }
};

template<bool Wide, bool Lookup = false>
struct ErbShared final : Processor {
  pog3_trial::ErbSharedBank<Wide, Lookup> bank;
  ErbShared() : Processor(8) {}
  void reset() override { bank.reset(); faults = 0; }
  void transpose(float extent) override { bank.setWarp(extent); }
  void process(const Stereo& input, std::size_t n) override {
    for (std::size_t i = 0; i < n; ++i) {
      const auto y = bank.process({input[0][i], input[1][i]});
      for (std::size_t v = 0; v < count; ++v) {
        output[v][0][i] = y[v][0]; output[v][1][i] = y[v][1];
      }
    }
  }
};

std::unique_ptr<Processor> create(const std::string& name, std::size_t voices) {
  if (name.starts_with("erb-effects-")) {
    if (voices != 8) throw std::runtime_error("ERB Attack/freeze trial runs all 8 warm paths");
    using Stage = pog3_trial::ErbAttackFreezeBank::Stage;
    struct Adapter final : Processor {
      pog3_trial::ErbAttackFreezeBank bank;
      bool freeze, gliss;
      std::size_t sample = 0, maximumHeld = 0;
      Adapter(Stage stage, bool glide = false) : Processor(8), bank(stage), freeze(stage == Stage::Freeze), gliss(glide) {
        bank.setAttack(stage == Stage::Ownership ? 0 : .5f); latency = 32;
      }
      void reset() override {
        bank.setFreeze(ardor::pog3::ExpressionMode::FreezeVolume, 0);
        bank.reset(); sample = faults = maximumHeld = 0;
      }
      void transpose(float extent) override { bank.transpose(extent); }
      void process(const Stereo& input, std::size_t n) override {
        for (std::size_t i = 0; i < n; ++i, ++sample) {
          // The timed/probed run includes capture, fully-held and release work;
          // warm live analysis/Attack and all eight ERB paths continue throughout.
          if (freeze && sample == 48000) bank.setFreeze(gliss ? ardor::pog3::ExpressionMode::FreezeGliss : ardor::pog3::ExpressionMode::FreezeVolume, gliss ? .65f : 1);
          if (freeze && sample == 144000) bank.setFreeze(ardor::pog3::ExpressionMode::FreezeVolume, 0);
          const auto y = bank.process({input[0][i], input[1][i]});
          if (freeze && sample % 256 == 32) {
            std::size_t n = 0;
            for (std::size_t r = 0; r < 3; ++r) for (std::size_t c = 0; c < 2; ++c)
              n += bank.ownership().freeze.band(r, c).count;
            maximumHeld = std::max(maximumHeld, n);
          }
          for (std::size_t v = 0; v < 8; ++v) {
            output[v][0][i] = y[v][0]; output[v][1][i] = y[v][1];
          }
        }
      }
      void diagnostics() const override {
        const auto& owner = bank.ownership();
        std::size_t expected = 0;
        for (const auto hop : {128U, 256U, 512U}) expected += 2 * (sample / hop) - (sample && sample % hop == 0);
        if (owner.transforms() != expected) throw std::runtime_error("ERB ownership analysis did not remain warm");
        if (freeze && sample == 192000 && owner.freeze.captures() != 1)
          throw std::runtime_error("ERB freeze workload did not capture exactly once");
        std::cerr << "ERB ownership transforms=" << owner.transforms()
          << " families=" << owner.attack.familyCount() << " capacity_events=" << owner.capacityEvents()
          << " captures=" << owner.freeze.captures() << " targets=" << owner.freeze.targets()
          << " max_held_partial_slots=" << maximumHeld << '\n';
      }
    };
    if (name == "erb-effects-ownership-32") return std::make_unique<Adapter>(Stage::Ownership);
    if (name == "erb-effects-attack-32") return std::make_unique<Adapter>(Stage::Attack);
    if (name == "erb-effects-freeze-32") return std::make_unique<Adapter>(Stage::Freeze);
    if (name == "erb-effects-gliss-32") return std::make_unique<Adapter>(Stage::Freeze, true);
    throw std::runtime_error("unknown ERB Attack/freeze stage");
  }
  if (name.starts_with("erb-cadence-")) {
    if (voices != 8) throw std::runtime_error("cadence ERB trial runs all 8 warm paths");
    // Local adapter keeps the same independent warm stereo outputs.
    auto make = []<std::size_t Stride, bool CountCycles>() -> std::unique_ptr<Processor> {
      struct Adapter final : Processor {
        pog3_trial::ErbCadenceBank<Stride, CountCycles> bank;
        Adapter() : Processor(8) { latency = Stride; }
        void reset() override { bank.reset(); faults = 0; }
        void transpose(float extent) override { bank.setWarp(extent); }
        void process(const Stereo& input, std::size_t n) override {
          for (std::size_t i = 0; i < n; ++i) {
            const auto y = bank.process({input[0][i], input[1][i]});
            for (std::size_t v = 0; v < 8; ++v) {
              output[v][0][i] = y[v][0]; output[v][1][i] = y[v][1];
            }
          }
        }
      };
      return std::make_unique<Adapter>();
    };
    if (name == "erb-cadence-center-8") return make.template operator()<8, false>();
    if (name == "erb-cadence-count-4") return make.template operator()<4, true>();
    if (name == "erb-cadence-count-8") return make.template operator()<8, true>();
    if (name == "erb-cadence-count-16") return make.template operator()<16, true>();
    if (name == "erb-cadence-count-32") return make.template operator()<32, true>();
    throw std::runtime_error("unknown ERB cadence");
  }
  if (name == "erb-shared-43" || name == "erb-shared-wide" || name == "erb-shared-lut-43" || name == "erb-shared-lut-wide") {
    if (voices != 8) throw std::runtime_error("shared ERB trial runs all 8 warm paths");
    if (name == "erb-shared-lut-wide") return std::make_unique<ErbShared<true, true>>();
    if (name == "erb-shared-lut-43") return std::make_unique<ErbShared<false, true>>();
    if (name == "erb-shared-wide") return std::make_unique<ErbShared<true>>();
    return std::make_unique<ErbShared<false>>();
  }
  if (name == "erb-ps2") {
    if (voices != 1) throw std::runtime_error("ERB-PS2 reference is octave-up only");
    return std::make_unique<ErbPs2>();
  }
  if (name == "ardor") return std::make_unique<Ardor>(voices);
#ifndef ARDOR_POG3_ONLY
  if (name == "terrarium-48" || name == "terrarium-80") {
    if (voices != 3) throw std::runtime_error("Terrarium reference has exactly 3 voices");
    return std::make_unique<Terrarium>(name == "terrarium-80" ? 80 : 48);
  }
  if (name.starts_with("ss-")) return std::make_unique<Signalsmith>(voices, name);
  if (name == "rb-r2" || name == "rb-r3") return std::make_unique<Rubber>(voices, name == "rb-r3");
  if (name == "rb-live") return std::make_unique<RubberLive>(voices);
#endif
  throw std::runtime_error("unknown processor");
}

std::vector<Stereo> fixture(std::size_t callback, const std::string& kind) {
  std::vector<Stereo> input(4 * 48000 / callback);
  std::uint32_t random = 0x504f4733;
  for (std::size_t b = 0; b < input.size(); ++b) for (std::size_t i = 0; i < callback; ++i) {
    const auto sample = b * callback + i; const double t = static_cast<double>(sample) / 48000;
    float x = 0, noise = 0;
    if (kind == "tone") x = .2 * std::sin(2 * pi * 329.628 * t);
    else if (kind == "alias") x = .2 * std::sin(2 * pi * 17003.2 * t);
    else if (kind == "low") for (double f : {82.4069, 87.3071}) x += .05 * std::sin(2 * pi * f * t);
    else if (kind == "resolved") for (double f : {196., 329.628, 493.883, 733.31}) x += .04 * std::sin(2 * pi * f * t);
    else {
      random = 1664525 * random + 1013904223;
      noise = (static_cast<double>(random) / 4294967296.0 - .5) * .03;
      const bool alternate = kind == "cpu-events" && (sample / 24000) % 2;
      const std::array<double, 4> notes = alternate ? std::array<double, 4>{110., 164.8138, 220., 293.6648}
        : std::array<double, 4>{82.4069, 130.8128, 164.8138, 196.};
      for (double f : notes) x += .06 * std::sin(2 * pi * f * t);
      if (kind == "cpu-events") {
        const double age = sample % 24000;
        const double envelope = std::min(1.0, age / 480) * std::exp(-age / 24000);
        x *= envelope; noise *= envelope;
      }
    }
    input[b][0][i] = x + noise; input[b][1][i] = -.73f * x + .4f * noise;
  }
  return input;
}

void quality(const std::string& name, std::size_t count, const std::string& kind) {
  constexpr std::size_t callback = 128, fftSize = 65536;
  auto processor = create(name, count); processor->transpose(1);
  const auto input = fixture(callback, kind);
  std::array<std::vector<float>, maxVoices> rendered;
  for (auto& r : rendered) r.resize(4 * 48000);
  for (std::size_t b = 0; b < input.size(); ++b) {
    processor->process(input[b], callback);
    for (std::size_t v = 0; v < count; ++v) std::copy_n(processor->output[v][0].begin(), callback, rendered[v].begin() + b * callback);
  }
  std::cout << "backend,fixture,voice,input_hz,expected_hz,cents,target_level_db,peak_level_db,spur_db,faults\n";
  ardor::RealtimeFft fft; fft.prepare(fftSize);
  std::vector<std::complex<float>> spectrum(fftSize);
  const std::vector<double> frequencies = kind == "tone" ? std::vector<double>{329.628}
    : kind == "alias" ? std::vector<double>{17003.2} : kind == "low" ? std::vector<double>{82.4069, 87.3071}
    : std::vector<double>{196., 329.628, 493.883, 733.31};
  const double reference = kind == "tone" || kind == "alias" ? .2 : kind == "low" ? .05 : .04;
  const auto used = name == "ardor" ? 6U : count;
  for (std::size_t v = 0; v < used; ++v) {
    double energy = 0;
    for (std::size_t i = 0; i < fftSize; ++i) {
      const double sample = rendered[v][rendered[v].size() - fftSize + i];
      if (!std::isfinite(sample)) throw std::runtime_error("nonfinite output");
      energy += sample * sample;
      spectrum[i] = sample * (.5 - .5 * std::cos(2 * pi * i / fftSize));
    }
    fft.transform(spectrum, false);
    constexpr std::array<float, 3> octavePitch{-24, -12, 12};
    const double semitones = name == "erb-ps2" ? 12 : name.starts_with("terrarium-") ? octavePitch[v]
      : name == "ardor" ? ardor::pog3::kVoiceSemitones[v] : pitch(v, count, 1);
    const double ratio = std::exp2(semitones / 12);
    for (double f : frequencies) {
      const double expected = f * ratio;
      if (expected >= 24000) {
        std::cout << name << ',' << kind << ',' << v << ',' << f << ',' << expected
          << ",unmeasured," << 10 * std::log10(std::max(energy / fftSize / (reference * reference / 2), 1e-30))
          << ",unmeasured,0," << processor->faults << '\n';
        continue;
      }
      const auto center = static_cast<std::size_t>(std::round(expected * fftSize / 48000));
      const auto first = center > 8 ? center - 8 : 1;
      const auto last = std::min(fftSize / 2 - 2, center + 8);
      std::size_t peak = first;
      for (std::size_t i = first; i <= last; ++i) if (std::abs(spectrum[i]) > std::abs(spectrum[peak])) peak = i;
      const auto logMagnitude = [&](std::size_t i) { return std::log(std::max<double>(std::abs(spectrum[i]), 1e-30)); };
      const double a = logMagnitude(peak - 1), b = logMagnitude(peak), c = logMagnitude(peak + 1);
      const double denominator = a - 2 * b + c;
      const double offset = denominator < -1e-12 ? std::clamp(.5 * (a - c) / denominator, -.5, .5) : 0;
      const double detected = (peak + offset) * 48000 / fftSize;
      // Measure level at the actual expected frequency, not a nearby louder
      // note. A two-second Hann DTFT limits low-pair window cross-talk. Cents
      // from peak interpolation are meaningful only for the single-tone case.
      std::complex<double> projection{}; double windowSum = 0;
      constexpr std::size_t projectionSize = 96000;
      for (std::size_t i = 0; i < projectionSize; ++i) {
        const double w = .5 - .5 * std::cos(2 * pi * i / projectionSize);
        const double angle = 2 * pi * expected * i / 48000;
        projection += rendered[v][rendered[v].size() - projectionSize + i] * w
          * std::complex<double>{std::cos(angle), -std::sin(angle)};
        windowSum += w;
      }
      const double amplitude = 2 * std::abs(projection) / windowSum;
      const double peakAmplitude = std::exp(b - .25 * (a - c) * offset) * 4 / fftSize;
      double spur = 0;
      for (std::size_t i = 1; i + 1 < fftSize / 2; ++i) {
        bool expectedRegion = false;
        for (double partial : frequencies) if (std::fabs(static_cast<double>(i) - partial * ratio * fftSize / 48000) <= 8) expectedRegion = true;
        if (!expectedRegion && std::abs(spectrum[i]) >= std::abs(spectrum[i - 1]) && std::abs(spectrum[i]) >= std::abs(spectrum[i + 1]))
          spur = std::max<double>(spur, std::abs(spectrum[i]));
      }
      std::cout << name << ',' << kind << ',' << v << ',' << f << ',' << expected << ','
        << (kind == "tone" ? std::to_string(1200 * std::log2(detected / expected)) : "unmeasured") << ',' << 20 * std::log10(std::max(amplitude / reference, 1e-30))
        << ',' << (kind == "tone" ? std::to_string(20 * std::log10(std::max(peakAmplitude / reference, 1e-30))) : "unmeasured")
        << ',' << 20 * std::log10(std::max(spur / std::abs(spectrum[peak]), 1e-30)) << ',' << processor->faults << '\n';
    }
  }
}

void benchmark(const std::string& name, std::size_t count, std::size_t callback, bool dynamic, bool probe, bool events = false) {
  const auto input = fixture(callback, events ? "cpu-events" : "cpu");
  auto processor = create(name, count);
  float extent = 1;
  auto run = [&](bool timed, std::vector<double>& times, double& checksum) {
    for (std::size_t b = 0; b < input.size(); ++b) {
      const auto start = std::chrono::steady_clock::now();
      if (dynamic) {
        const std::size_t step = b * callback / 480;
        const float target = .25f + .75f * (step % 128) / 127.0f;
        const float distance = callback / 960.0f;
        extent += std::clamp(target - extent, -distance, distance);
        processor->transpose(extent);
      }
      processor->process(input[b], callback);
      if (timed) times[b] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
      for (std::size_t v = 0; v < count; ++v) for (std::size_t i = 0; i < callback; ++i)
        checksum += processor->output[v][0][i] + processor->output[v][1][i];
    }
  };
  std::vector<double> times(input.size()); double warmChecksum = 0, checksum = 0;
  run(false, times, warmChecksum); processor->transpose(1); processor->reset(); extent = 1;
  std::size_t allocations = 0, frees = 0;
  if (probe) {
    if (!pog3_malloc_begin || !pog3_malloc_end) throw std::runtime_error("allocation probe not loaded");
    pog3_malloc_begin();
  }
  run(true, times, checksum);
  if (probe) pog3_malloc_end(&allocations, &frees);
  processor->diagnostics();
  if (!std::isfinite(checksum)) throw std::runtime_error("nonfinite checksum");
  const auto mean = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
  const auto overruns = std::count_if(times.begin(), times.end(), [&](double t) { return t > callback / .048; });
  const auto segment = [&](std::size_t begin, std::size_t end) {
    return std::accumulate(times.begin() + begin, times.begin() + end, 0.0) / (end - begin);
  };
  const double first = segment(0, 48000 / callback), middle = segment(48000 / callback, 144000 / callback);
  const double last = segment(144000 / callback, times.size());
  std::sort(times.begin(), times.end());
  const auto p = [&](double fraction) { return times[static_cast<std::size_t>(fraction * (times.size() - 1))]; };
  std::cout << "backend,voices,callback,dynamic,callbacks,mean_us,cpu_percent,p99_us,max_us,overruns,allocations,frees,faults,latency_frames,checksum,first_second_mean_us,middle_two_seconds_mean_us,last_second_mean_us,workload\n"
    << name << ',' << count << ',' << callback << ',' << dynamic << ',' << times.size() << ',' << mean << ','
    << mean / (callback / .048) * 100 << ',' << p(.99) << ',' << times.back() << ',' << overruns << ','
    << (probe ? std::to_string(allocations) : "unmeasured") << ',' << (probe ? std::to_string(frees) : "unmeasured")
    << ',' << processor->faults << ',' << processor->latency << ',' << checksum
    << ',' << first << ',' << middle << ',' << last << ',' << (events ? "events" : dynamic ? "dynamic" : "static") << '\n';
}
}

int main(int argc, char** argv) {
  try {
    if (argc < 5 || argc > 6) throw std::runtime_error("usage: trial backend voices callback static|dynamic|events|tone|resolved|low|alias [--allocation]");
    const std::string name = argv[1], kind = argv[4];
    const auto voices = std::stoul(argv[2]), callback = std::stoul(argv[3]);
    if ((voices != 1 && voices != 3 && voices != 5 && voices != 8) || (callback != 64 && callback != 128)) throw std::runtime_error("use 1/3/5/8 voices and 64/128 callbacks");
    if (voices == 1 && name != "erb-ps2") throw std::runtime_error("1 voice is only for the ERB-PS2 reference");
    if (voices == 3 && !name.starts_with("terrarium-")) throw std::runtime_error("3 voices is only for the Terrarium reference");
    if (name.starts_with("terrarium-") && kind == "dynamic") throw std::runtime_error("Terrarium cannot implement continuous Warp");
    if (name == "erb-ps2" && kind == "dynamic") throw std::runtime_error("ERB-PS2 reference is octave-up only");
    const bool probe = argc == 6 && std::string(argv[5]) == "--allocation";
    if (argc == 6 && !probe) throw std::runtime_error("unknown option");
    if (kind == "static" || kind == "dynamic" || kind == "events") benchmark(name, voices, callback, kind == "dynamic", probe, kind == "events");
    else if (kind == "tone" || kind == "resolved" || kind == "low" || kind == "alias") {
      if (probe) throw std::runtime_error("allocation mode is for CPU workloads");
      quality(name, voices, kind);
    } else throw std::runtime_error("unknown workload");
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
