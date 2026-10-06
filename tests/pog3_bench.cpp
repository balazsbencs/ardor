#include "daisyfx/pog3/SpectralFrameStream.h"
#include "daisyfx/pog3/PolyphonicPitchBank.h"
#include "daisyfx/pog3/Pog3VoiceStages.h"
#include "daisyfx/pog3/Pog3Processor.h"
#include "pog3_granular_reference.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace allocation_probe {
thread_local bool enabled = false;
thread_local std::size_t calls = 0, bytes = 0;
void record(std::size_t size) noexcept {
  if (enabled) { ++calls; bytes += size; }
}
struct Scope {
  Scope() { calls = bytes = 0; enabled = true; }
  ~Scope() { enabled = false; }
};
}

// This executable owns these replacements; other Ardor binaries are unaffected.
void* operator new(std::size_t size) {
  if (auto* p = std::malloc(size ? size : 1)) {
    allocation_probe::record(size);
    return p;
  }
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t size, std::align_val_t alignment) {
  void* p = nullptr;
  if (posix_memalign(&p, static_cast<std::size_t>(alignment), size ? size : 1) != 0)
    throw std::bad_alloc();
  allocation_probe::record(size);
  return p;
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
  return ::operator new(size, alignment);
}
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }

namespace {
class ExpressionWorkload {
public:
  ExpressionWorkload() {
    std::string error;
    if (!processor_.configure({{"dry_level", .25}, {"down2_level", .25}, {"down1_level", .25},
      {"fifth_level", .25}, {"up1_level", .25}, {"up2_level", .25}, {"attack", .4}, {"filter_q", 1},
      {"dry_attack", 1}, {"dry_filter", 1}, {"dry_detune", 1}, {"detune", 1}, {"spread", 1},
      {"crossfade_heel", {{"filter_frequency", .1}, {"up1_pan", 0}, {"spread", 0}, {"detune", .1}}},
      {"crossfade_toe", {{"filter_frequency", .9}, {"up1_pan", 1}, {"spread", 1}, {"detune", 1}}}}, 48000, error))
      throw std::runtime_error(error);
  }
  void reset() noexcept { processor_.reset(); samples_ = 0; }
  ardor::StereoSample process(ardor::StereoSample input) noexcept {
    using namespace ardor::pog3;
    if (samples_ % 480 == 0) {
      const auto step = samples_ / 480;
      (void)processor_.setParameterTarget("expression_position", step % 128 / 127.0f);
      (void)processor_.setParameterTarget(index(Parameter::ExpressionMode), (step / 10) % 7 / 6.0f);
      (void)processor_.setParameterTarget(index(Parameter::ExpressionReverse), (step / 15) % 2);
      (void)processor_.setParameterTarget(index(Parameter::Focus), (step / 10) % 2);
      (void)processor_.setParameterTarget(index(Parameter::FilterEnv), step % 3 * .5f);
      (void)processor_.setParameterTarget(index(Parameter::FilterMode), step % 3 * .5f);
    }
    ++samples_;
    const auto y = processor_.process({input.left, input.right}).mixed;
    return {y.left, y.right};
  }
  bool healthy() const noexcept { return processor_.healthy() && processor_.deadlineMisses() == 0; }
  std::size_t transformCount() const noexcept { return processor_.transformCount(); }
private:
  ardor::pog3::Pog3Processor processor_;
  std::size_t samples_ = 0;
};

// A full-path hold/gliss workload: changing dense stereo chords, all voices,
// processed dry, space/filter, toe latch and heel release/recapture. Require
// actual captures and assignments so allocation/timing probes exercise them.
class FreezeGlissWorkload {
public:
  FreezeGlissWorkload() {
    std::string error;
    if (!processor_.configure({{"expression_mode", 5.0 / 6}, {"expression_position", 0}, {"focus", 1},
        {"dry_level", .25}, {"down2_level", .25}, {"down1_level", .25}, {"fifth_level", .25},
        {"up1_level", .25}, {"up2_level", .25}, {"attack", .11}, {"dry_attack", 1},
        {"dry_filter", 1}, {"dry_detune", 1}, {"detune", 1}, {"spread", 1}, {"filter_q", 1}}, 48000, error))
      throw std::runtime_error(error);
  }
  void reset() {
    if (samples_ == 4 * 48000) {
      if (processor_.freezeCaptures() < 2 || processor_.freezeTargets() < 2
          || !processor_.healthy() || processor_.deadlineMisses())
        throw std::runtime_error("freeze benchmark did not complete captures/targets or violated staged bounds");
      checked_ = true;
    }
    processor_.reset(); samples_ = 0;
  }
  ardor::StereoSample process(ardor::StereoSample input) noexcept {
    using namespace ardor::pog3;
    if (samples_ == 14400 || samples_ == 177600)
      (void)processor_.setParameterTarget(index(Parameter::ExpressionPosition), .65f);
    if (samples_ == 134400) (void)processor_.setParameterTarget(index(Parameter::ExpressionPosition), 1);
    if (samples_ == 163200) (void)processor_.setParameterTarget(index(Parameter::ExpressionPosition), 0);
    ++samples_;
    const auto out = processor_.process({input.left, input.right}).mixed;
    return {out.left, out.right};
  }
  bool healthy() const noexcept { return checked_ && processor_.healthy() && processor_.deadlineMisses() == 0; }
  std::size_t transformCount() const noexcept { return processor_.transformCount(); }
private:
  ardor::pog3::Pog3Processor processor_;
  std::size_t samples_ = 0;
  bool checked_ = false;
};

class StaticSoundWorkload {
public:
  StaticSoundWorkload() {
    values_ = ardor::pog3::defaultValues();
    for (std::size_t v = 0; v < 6; ++v) values_[ardor::pog3::index(ardor::pog3::Parameter::DryLevel) + v] = .25f;
    values_[ardor::pog3::index(ardor::pog3::Parameter::Detune)] = 1;
    values_[ardor::pog3::index(ardor::pog3::Parameter::Spread)] = 1;
    values_[ardor::pog3::index(ardor::pog3::Parameter::DryDetune)] = 1;
    values_[ardor::pog3::index(ardor::pog3::Parameter::DryFilter)] = 1;
    values_[ardor::pog3::index(ardor::pog3::Parameter::DryAttack)] = 1;
    values_[ardor::pog3::index(ardor::pog3::Parameter::Attack)] = .4f;
    values_[ardor::pog3::index(ardor::pog3::Parameter::FilterQ)] = 1;
    path_.setSoundValues(values_); path_.prepare();
  }
  void reset() noexcept { path_.reset(); samples_ = 0; }
  ardor::StereoSample process(ardor::StereoSample input) noexcept {
    using namespace ardor::pog3;
    if (samples_ % 480 == 0) {
      const float u = ((samples_ / 480) % 128) / 127.0f;
      values_[index(Parameter::FilterFrequency)] = u;
      values_[index(Parameter::FilterEnv)] = ((samples_ / 480) % 3) * .5f;
      values_[index(Parameter::FilterMode)] = ((samples_ / 480) % 3) * .5f;
      values_[index(Parameter::Spread)] = u;
      values_[index(Parameter::Detune)] = 1 - u;
      values_[index(Parameter::Up1Pan)] = u;
      values_[index(Parameter::Focus)] = (samples_ / 4800) % 2;
      (void)path_.setSoundValues(values_);
    }
    ++samples_;
    const auto result = path_.process({input.left, input.right}).mixed;
    return {result.left, result.right};
  }
  bool healthy() const noexcept { return path_.healthy() && path_.deadlineMisses() == 0; }
  std::size_t transformCount() const noexcept { return path_.transformCount(); }
private:
  ardor::pog3::Pog3SignalPath path_;
  ardor::pog3::Values values_{};
  std::size_t samples_ = 0;
};

template<bool AttackEnabled = false>
class PitchBankWorkload {
public:
  PitchBankWorkload() {
    if constexpr (AttackEnabled) (void)bank_.setAttackSeconds(.5f);
    bank_.prepare();
  }
  void reset() noexcept { bank_.reset(); dry_.reset(); samples_ = 0; }
  ardor::StereoSample process(ardor::StereoSample input) noexcept {
    if (samples_ % 4800 == 0) bank_.setFocus((samples_ / 4800) % 2);
    if (samples_ % 480 == 0) (void)bank_.setWarp(.25f + .75f * ((samples_ / 480) % 128) / 127.0f);
    if constexpr (AttackEnabled) if (samples_ % 2400 == 0) {
      const float position = ((samples_ / 2400) % 128) / 127.0f;
      (void)bank_.setAttackSeconds(3 * position * position);
    }
    ++samples_;
    const auto voices = bank_.process({input.left, input.right});
    ardor::StereoSample output{};
    for (const auto voice : voices) { output.left += voice.left; output.right += voice.right; }
    if constexpr (AttackEnabled) {
      const auto dry = dry_.process({input.left, input.right}, voices[0], bank_.attackSeconds() > 0 && (samples_ / 4800) % 2);
      output.left += dry.left; output.right += dry.right;
    }
    return output;
  }
  bool healthy() const noexcept { return bank_.healthy() && bank_.deadlineMisses() == 0; }
  std::size_t transformCount() const noexcept { return bank_.transformCount(); }
private:
  ardor::pog3::PolyphonicPitchBank bank_;
  ardor::pog3::DryAttackRouter dry_;
  std::size_t samples_ = 0;
};
// Transform workload only: six long and two short stereo identity renderers.
// This includes both upper paths during a future Focus transition, but excludes
// partial tracking, pitch mapping, envelopes, chorus, freeze, and filters.
class SpectralWorkload {
public:
  SpectralWorkload() {
    auto longPlan = std::make_shared<ardor::pog3::SpectralPlan>(2048, 256);
    auto shortPlan = std::make_shared<ardor::pog3::SpectralPlan>(1024, 128);
    for (auto& a : longAnalysis_) a.prepare(longPlan);
    for (auto& a : shortAnalysis_) a.prepare(shortPlan);
    for (auto& voice : longVoices_) for (auto& channel : voice) channel.prepare(longPlan);
    for (auto& voice : shortVoices_) for (auto& channel : voice) channel.prepare(shortPlan);
  }
  void reset() noexcept {
    transforms_ = 0;
    for (auto& a : longAnalysis_) a.reset();
    for (auto& a : shortAnalysis_) a.reset();
    for (auto& voice : longVoices_) for (auto& channel : voice) channel.reset();
    for (auto& voice : shortVoices_) for (auto& channel : voice) channel.reset();
  }
  ardor::StereoSample process(ardor::StereoSample input) noexcept {
    const std::array<float, 2> source{input.left, input.right};
    std::array<float, 2> output{};
    for (std::size_t channel = 0; channel < 2; ++channel) {
      for (auto& voice : longVoices_) output[channel] += voice[channel].pop();
      for (auto& voice : shortVoices_) output[channel] += voice[channel].pop();
      if (longAnalysis_[channel].push(source[channel])) {
        transforms_ += 1 + longVoices_.size();
        for (auto& voice : longVoices_)
          if (!voice[channel].addFrame(longAnalysis_[channel].spectrum())) healthy_ = false;
      }
      if (shortAnalysis_[channel].push(source[channel])) {
        transforms_ += 1 + shortVoices_.size();
        for (auto& voice : shortVoices_)
          if (!voice[channel].addFrame(shortAnalysis_[channel].spectrum())) healthy_ = false;
      }
    }
    return {output[0], output[1]};
  }
  bool healthy() const noexcept { return healthy_; }
  std::size_t transformCount() const noexcept { return transforms_; }
private:
  std::array<ardor::pog3::SpectralAnalysis, 2> longAnalysis_, shortAnalysis_;
  std::array<std::array<ardor::pog3::SpectralSynthesis, 2>, 6> longVoices_;
  std::array<std::array<ardor::pog3::SpectralSynthesis, 2>, 2> shortVoices_;
  bool healthy_ = true;
  std::size_t transforms_ = 0;
};

template<class Processor>
void measure(const char* name, std::size_t callback, std::ostream& csv,
             const std::vector<ardor::StereoSample>& input) {
  std::unique_ptr<Processor> processor;
  std::size_t preparationBytes = 0;
  {
    allocation_probe::Scope scope;
    processor = std::make_unique<Processor>();
    preparationBytes = allocation_probe::bytes;
  }
  for (const auto x : input) (void)processor->process(x); // Warm code/data caches.
  processor->reset();
  std::vector<double> times(input.size() / callback);
  double checksum = 0;
  double resetUs = 0;
  std::size_t allocations = 0;
  std::size_t maxTransforms = 0;
  std::size_t worstBlock = 0, worstTransforms = 0;
  double worstTime = -1;
  {
    allocation_probe::Scope scope;
    for (std::size_t block = 0; block < times.size(); ++block) {
      std::size_t previousTransforms = 0;
      if constexpr (requires { processor->transformCount(); }) previousTransforms = processor->transformCount();
      const auto start = std::chrono::steady_clock::now();
      for (std::size_t i = block * callback; i < (block + 1) * callback; ++i) {
        const auto out = processor->process(input[i]);
        checksum += out.left + out.right;
      }
      times[block] = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
      std::size_t transforms = 0;
      if constexpr (requires { processor->transformCount(); })
        transforms = processor->transformCount() - previousTransforms;
      maxTransforms = std::max(maxTransforms, transforms);
      // Preserve the worst callback's original timeline before sorting times.
      // Diagnostics run after its timer stops, outside the measured callback.
      if (times[block] > worstTime) {
        worstTime = times[block]; worstBlock = block; worstTransforms = transforms;
      }
    }
    // Reset clears health/deadline counters. Reject a failed measured stream
    // before that reset can erase the evidence, outside callback timing.
    if constexpr (requires { processor->healthy(); })
      if (!processor->healthy()) throw std::runtime_error("measured spectral workload violated staged bounds");
    const auto resetStart = std::chrono::steady_clock::now();
    processor->reset(); // Public reset must also retain all capacities.
    resetUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - resetStart).count();
    // Exercise the pure expression and Warp helpers under the same guard.
    ardor::pog3::Configuration config;
    config.base = config.heel = config.toe = ardor::pog3::defaultValues();
    config.morphed.set(ardor::pog3::index(ardor::pog3::Parameter::DryLevel));
    ardor::pog3::ParameterTargets targets;
    for (int mode = 0; mode < 7; ++mode) {
      auto base = config.base;
      base[ardor::pog3::index(ardor::pog3::Parameter::ExpressionMode)] = mode / 6.0f;
      const auto values = ardor::pog3::effectiveValues(config, base);
      if (!targets.store(base) || !targets.setTarget("expression_position", .5f)
          || !targets.setTarget(32, .25f)) throw std::runtime_error("valid target rejected");
      checksum += targets.read()[ardor::pog3::index(ardor::pog3::Parameter::ExpressionPosition)];
      checksum += values[1] + ardor::pog3::warpSemitones(1, .5f, true);
    }
    allocations = allocation_probe::calls;
  }
  if (allocations || !std::isfinite(checksum))
    throw std::runtime_error("allocation/nonfinite failure in prepared workload");
  if constexpr (requires { processor->healthy(); })
    if (!processor->healthy()) throw std::runtime_error("spectral frame rejected");
  // Mean wall time exposes total callback demand when a scheduling change
  // moves work between callbacks without reducing it. Keep all outliers.
  const double meanUs = std::accumulate(times.begin(), times.end(), 0.0) / times.size();
  std::sort(times.begin(), times.end());
  const auto percentile = [&](double p) { return times[static_cast<std::size_t>(p * (times.size() - 1))]; };
  csv << name << ',' << callback << ',' << times.size() << ',' << callback / .048 << ','
      << percentile(.5) << ',' << percentile(.95) << ',' << percentile(.99) << ','
      << percentile(.999) << ',' << times.back() << ',' << allocations << ',' << preparationBytes << ',' << resetUs << ',' << maxTransforms << ','
      << worstBlock << ',' << ((worstBlock + 1) * callback) % 512 << ',' << worstTransforms << ',' << meanUs << '\n';
}
}

int main(int argc, char** argv) {
  try {
    std::ofstream file;
    if (argc == 3 && std::string_view(argv[1]) == "--csv") {
      // Explicit destination selected by the local benchmark operator, with
      // the same filesystem permissions as the process. This is test-only.
      file.open(argv[2]);
      if (!file) throw std::runtime_error("cannot create benchmark CSV");
    } else if (argc != 1) throw std::runtime_error("usage: pedal-pog3-bench [--csv path]");
    auto& csv = file.is_open() ? static_cast<std::ostream&>(file) : std::cout;
    csv << std::fixed << std::setprecision(3)
        << "workload,callback_frames,callbacks,budget_us,median_us,p95_us,p99_us,p999_us,max_us,callback_allocations,preparation_allocated_bytes,reset_us,max_transforms_per_callback,worst_callback_index,worst_callback_end_mod_512,worst_callback_transforms,mean_us\n";
    std::vector<ardor::StereoSample> input(48000 * 4);
    std::vector<ardor::StereoSample> freezeInput(input.size());
    std::uint32_t random = 0x504f4733;
    for (std::size_t i = 0; i < input.size(); ++i) {
      random = 1664525 * random + 1013904223;
      const float noise = (static_cast<double>(random) / 4294967296.0 - .5) * .03;
      const double t = static_cast<double>(i) / 48000;
      const float chord = .06 * (std::sin(6.283185307179586 * 82.4069 * t)
        + std::sin(6.283185307179586 * 130.8128 * t) + std::sin(6.283185307179586 * 164.8138 * t)
        + std::sin(6.283185307179586 * 196 * t));
      input[i] = {chord + noise, -.73f * chord + .4f * noise};
      const double fundamental = i < 38400 ? 82.4069 : i < 86400 ? 110 : i < 105600 ? 146.8324 : 196;
      const float l = .04 * (std::sin(6.283185307179586 * fundamental * t)
        + std::sin(6.283185307179586 * fundamental * 1.5 * t) + std::sin(6.283185307179586 * fundamental * 2 * t));
      const float r = .03 * (std::sin(6.283185307179586 * fundamental * 1.25 * t)
        + std::sin(6.283185307179586 * fundamental * 2.5 * t));
      freezeInput[i] = {l + .1f * input[i].left, r + .1f * input[i].right};
    }
    for (const auto callback : {64U, 128U}) {
      measure<pog3_test::GranularReference>("granular_reference_10_shifters", callback, csv, input);
      measure<SpectralWorkload>("spectral_identity_focus_transition", callback, csv, input);
      measure<PitchBankWorkload<>>("spectral_pitch_bank_warp_focus", callback, csv, input);
      measure<PitchBankWorkload<true>>("spectral_pitch_bank_attack_warp_focus", callback, csv, input);
      measure<StaticSoundWorkload>("static_sound_path_filter_space_pan_attack", callback, csv, input);
      measure<ExpressionWorkload>("expression_modes_filter_space_pan_attack", callback, csv, input);
      measure<FreezeGlissWorkload>("freeze_gliss_capture_assignment_filter_space", callback, csv, freezeInput);
    }
    csv.flush();
    if (!csv) throw std::runtime_error("benchmark CSV write failed");
    std::cerr << "Prepared reference/foundation/pitch workloads; optimized host timings do not certify target-device or full-block performance.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
