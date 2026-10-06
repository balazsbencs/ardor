#include "daisyfx/pog3/Pog3Processor.h"
#include "pog3_artifacts.h"

#include <bit>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace ardor::pog3;
constexpr double pi = 3.14159265358979323846;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void configure(Pog3Processor& p, const nlohmann::json& params) {
  std::string error;
  if (!p.configure(params, 48000, error)) throw std::runtime_error(error);
}
void target(Pog3Processor& p, Parameter parameter, float value) {
  require(p.setParameterTarget(index(parameter), value), "freeze target accepted");
}
nlohmann::json isolated(std::size_t voice, int mode = 5, float position = 0) {
  nlohmann::json result{{"expression_mode", mode / 6.0f}, {"expression_position", position}, {"focus", 1}};
  for (std::size_t v = 0; v < kVoiceCount; ++v) result[std::string(parameterSpecs()[index(Parameter::DryLevel) + v].key)] = v == voice ? 1 : 0;
  return result;
}
float sine(double hz, std::size_t i, float gain = .1f) { return gain * std::sin(2 * pi * hz * i / kSampleRate); }
double amplitude(const std::vector<float>& x, double hz) {
  std::complex<double> sum{}; double weight = 0;
  for (std::size_t i = 0; i < x.size(); ++i) {
    const double w = .5 - .5 * std::cos(2 * pi * i / x.size());
    sum += static_cast<double>(x[i]) * w * std::polar(1.0, -2 * pi * hz * i / kSampleRate); weight += w;
  }
  return 2 * std::abs(sum) / weight;
}
double frequency(const std::vector<float>& x) {
  std::size_t count = 0; double first = 0, last = 0;
  for (std::size_t i = 1; i < x.size(); ++i) if (x[i - 1] < 0 && x[i] >= 0) {
    const double at = i - 1 + x[i - 1] / (x[i - 1] - x[i]);
    if (!count) first = at;
    ++count; last = at;
  }
  require(count > 5, "enough held cycles");
  return (count - 1) * kSampleRate / (last - first);
}
void stationary() {
  double worstCents = 0, worstLevel = 0;
  for (const double source : {82.4069, 196.0, 659.3}) for (std::size_t voice = 1; voice < 6; ++voice) {
    Pog3Processor p; configure(p, isolated(voice));
    std::vector<float> held(48000); float antiError = 0;
    for (std::size_t i = 0; i < 120000; ++i) {
      if (i == 24000) target(p, Parameter::ExpressionPosition, 1);
      const float x = i < 48000 ? sine(source, i) : sine(source * 1.4, i);
      const auto y = p.process({x, -.73f * x}).mixed;
      if (i >= 72000) held[i - 72000] = y.left;
      if (i >= 60000) antiError = std::max(antiError, std::fabs(y.right + .73f * y.left));
    }
    const double expected = source * std::exp2(kVoiceSemitones[voice] / 12);
    const double cents = 1200 * std::log2(frequency(held) / expected);
    const double level = 20 * std::log10(amplitude(held, expected) / .1);
    worstCents = std::max(worstCents, std::fabs(cents)); worstLevel = std::max(worstLevel, std::fabs(level));
    std::cout << "Held source=" << source << " voice=" << voice << " cents=" << cents << " gain=" << level << " dB anti=" << antiError << '\n';
    require(antiError < 1e-5f, "held unequal anti-phase stereo remains independent");
    require(std::fabs(cents) < 3 && std::fabs(level) < 1, "held pitch/amplitude survives replacement live input");
    require(p.freezeCaptures() == 1 && p.freezeTargets() == 0 && p.freezeLatched(), "toe hold never retargets");
    require(p.healthy() && p.deadlineMisses() == 0, "stationary hold healthy");
  }
  std::cout << "Stationary 15 cases worst cents=" << worstCents << " worst level=" << worstLevel << " dB\n";
}
void stateAndSilence() {
  Pog3Processor p; configure(p, isolated(2, 6, 1));
  for (int i = 0; i < 10000; ++i) require(p.process({}).mixed.left == 0, "initial silence never resurrects a capture");
  require(p.freezeState() == SpectralFreeze::State::CapturePending && p.freezeCaptures() == 0, "initial toe waits for nonsilent analysis");
  std::vector<float> initial;
  for (std::size_t i = 0; i < 16000; ++i) {
    const float x = sine(196, i); const auto y = p.process({x, -x}).mixed;
    if (i >= 8000) initial.push_back(y.left);
  }
  require(p.freezeCaptures() == 1, "initial nonheel captures first valid nonsilent frame");
  require(std::fabs(1200 * std::log2(frequency(initial) / 98)) < 3,
          "initial nonheel after silence waits for a valid pitched capture");
  target(p, Parameter::ExpressionPosition, .012f);
  for (int i = 0; i < 12000; ++i) p.process({});
  require(p.freezeState() == SpectralFreeze::State::Live, "heel clears held data after release");
  for (int i = 0; i < 4000; ++i) require(p.process({}).mixed.left == 0, "released hold drains completely");
  target(p, Parameter::ExpressionPosition, .028f);
  for (int i = 0; i < 1000; ++i) p.process({});
  require(p.freezeCaptures() == 1, "heel jitter below exit threshold cannot recapture");
  target(p, Parameter::ExpressionPosition, .04f);
  for (int i = 0; i < 10000; ++i) require(p.process({}).mixed.left == 0, "later silence captures a silent hold");
  require(p.freezeCaptures() == 2, "later silent capture is distinct from pending startup");
  target(p, Parameter::ExpressionPosition, .025f);
  for (int i = 0; i < 1000; ++i) p.process({});
  require(p.freezeCaptures() == 2 && p.freezeState() == SpectralFreeze::State::Held, "heel hysteresis retains active hold above enter threshold");
  p.reset(); p.reset();
  require(p.freezeCaptures() == 0, "reset clears hold identities and diagnostics");
  for (int i = 0; i < 10000; ++i) require(p.process({}).mixed.left == 0, "reset cannot retain stale held audio");
}
void gliss() {
  Pog3Processor p; configure(p, isolated(2));
  std::vector<float> middle, settled, latched;
  for (std::size_t i = 0; i < 168000; ++i) {
    if (i == 24000) target(p, Parameter::ExpressionPosition, .75f);
    if (i == 108000) target(p, Parameter::ExpressionPosition, 1);
    const double hz = i < 48000 ? 196 : i < 120000 ? 293.6648 : 391.9954;
    const float x = sine(hz, i);
    const auto y = p.process({x, -x}).mixed;
    if (i >= 69600 && i < 79200) middle.push_back(y.left);
    if (i >= 100800 && i < 108000) settled.push_back(y.left);
    if (i >= 132000) latched.push_back(y.left);
  }
  const double mid = frequency(middle), end = frequency(settled), toe = frequency(latched);
  std::cout << "Gliss frequencies middle/settled/toe=" << mid << '/' << end << '/' << toe << " Hz targets=" << p.freezeTargets() << '\n';
  require(mid > 100 && mid < 146, "gliss passes through intermediate frequency rather than crossfading two fixed notes");
  const double midTone = amplitude(middle, mid);
  const double endpoints = std::max(amplitude(middle, 98), amplitude(middle, 293.6648 / 2));
  std::cout << "Gliss middle-carrier advantage=" << 20 * std::log10(midTone / endpoints) << " dB\n";
  require(midTone > endpoints * 3, "gliss audio contains a moving carrier, not only the two endpoint frequencies");
  require(std::fabs(1200 * std::log2(end / (293.6648 / 2))) < 3, "gliss reaches new target pitch");
  require(std::fabs(1200 * std::log2(toe / end)) < 3 && p.freezeLatched(), "full toe stops new target motion");
  require(p.freezeCaptures() == 1 && p.freezeTargets() > 0 && p.healthy(), "targets preserve held capture identity");
}
void eligibilityAndVolume() {
  for (const std::size_t voice : {0U, 2U}) for (const float position : {.25f, .75f}) {
    auto params = isolated(voice, 6); params["attack"] = .11; params["dry_attack"] = 1;
    params["expression_heel"] = .6; params["expression_toe"] = .8;
    Pog3Processor p; configure(p, params);
    std::vector<float> held;
    for (std::size_t i = 0; i < 96000; ++i) {
      if (i == 24000) target(p, Parameter::ExpressionPosition, position);
      const float x = i < 48000 ? sine(196, i) : 0;
      const auto y = p.process({x, -x}).mixed;
      if (i >= 72000) held.push_back(y.left);
    }
    const double actual = amplitude(held, voice ? 98 : 196);
    std::cout << "Freeze Volume voice=" << voice << " q=" << position << " amplitude=" << actual << '\n';
    require(std::fabs(actual - .1 * position) < .0001, "held generated/eligible dry Volume applies q exactly once and ignores scalar endpoints");
    target(p, Parameter::ExpressionMode, 5.0f / 6); target(p, Parameter::ExpressionPosition, 1);
    held.clear();
    for (std::size_t i = 0; i < 16000; ++i) {
      const auto y = p.process({}).mixed;
      if (i >= 4000) held.push_back(y.left);
    }
    require(p.freezeCaptures() == 1 && std::fabs(amplitude(held, voice ? 98 : 196) - .1) < .0001,
            "switching freeze modes retains capture and fades held gain");
  }
  for (const float attack : {.099f, .1f, .101f}) {
    auto params = isolated(0); params["attack"] = attack; params["dry_attack"] = 1;
    Pog3Processor p; configure(p, params);
    std::vector<float> held;
    for (std::size_t i = 0; i < 96000; ++i) {
      if (i == 24000) target(p, Parameter::ExpressionPosition, 1);
      const float x = sine(i < 48000 ? 196 : 293.6648, i);
      const auto y = p.process({x, -x}).mixed;
      if (i >= 72000) held.push_back(y.left);
    }
    const double expected = attack > .1f ? 196 : 293.6648;
    require(std::fabs(1200 * std::log2(frequency(held) / expected)) < 3, "dry freeze uses strict normalized Attack > 0.10 eligibility");
    target(p, Parameter::DryAttack, 0);
    for (std::size_t i = 0; i < 10000; ++i) {
      const float x = sine(293.6648, i); const auto y = p.process({x, -x}).mixed;
      if (i > 6000) require(y.left == x && y.right == -x, "disabling Dry Attack returns immediate playable dry during hold");
    }
    target(p, Parameter::ExpressionPosition, 0);
    for (int i = 0; i < 10000; ++i) p.process({});
    target(p, Parameter::DryAttack, 1);
    for (int i = 0; i < 10000; ++i) require(p.process({}).mixed.left == 0, "later Dry Attack toggle cannot resurrect a cleared chord");
  }
  for (const std::size_t voice : {4U, 5U}) {
    auto params = isolated(voice); params["focus"] = 0;
    Pog3Processor p; configure(p, params);
    std::vector<float> live, held, returned;
    for (std::size_t i = 0; i < 144000; ++i) {
      if (i == 24000) target(p, Parameter::ExpressionPosition, 1);
      if (i == 72000) target(p, Parameter::Focus, 1);
      if (i == 108000) target(p, Parameter::Focus, 0);
      const float x = sine(i < 48000 ? 196 : 293.6648, i);
      const auto y = p.process({x, -x}).mixed;
      if (i >= 60000 && i < 72000) live.push_back(y.left);
      if (i >= 84000 && i < 108000) held.push_back(y.left);
      if (i >= 120000) returned.push_back(y.left);
    }
    const double ratio = std::exp2(kVoiceSemitones[voice] / 12);
    require(std::fabs(1200 * std::log2(frequency(live) / (293.6648 * ratio))) < 3
         && std::fabs(1200 * std::log2(frequency(returned) / (293.6648 * ratio))) < 3,
            "Focus-off uppers stay live and return to live on a hold reversal");
    require(std::fabs(1200 * std::log2(frequency(held) / (196 * ratio))) < 3 && p.freezeCaptures() == 1,
            "Focus-on uppers join the existing captured spectrum without recapture");
  }
}
void toeAndReverse() {
  Pog3Processor p; auto params = isolated(2); params["expression_position"] = 1; params["expression_reverse"] = 1;
  configure(p, params);
  for (std::size_t i = 0; i < 24000; ++i) { const float x = sine(196, i); p.process({x, -x}); }
  require(p.freezeCaptures() == 0, "Reverse applies before heel edge detection");
  target(p, Parameter::ExpressionPosition, 0);
  for (std::size_t i = 24000; i < 72000; ++i) { const float x = sine(i < 48000 ? 196 : 293.6648, i); p.process({x, -x}); }
  target(p, Parameter::ExpressionPosition, .025f); // q=.975 retains toe latch.
  for (int i = 0; i < 5000; ++i) p.process({sine(293.6648, i), 0});
  require(p.freezeLatched() && p.freezeCaptures() == 1, "toe jitter above exit threshold preserves latch");
  target(p, Parameter::ExpressionPosition, .04f);
  for (int i = 0; i < 3000; ++i) p.process({sine(293.6648, i), 0});
  require(!p.freezeLatched(), "toe latch releases at reversed q<=.965");
  target(p, Parameter::ExpressionPosition, 1);
  for (int i = 0; i < 12000; ++i) p.process({});
  require(p.freezeState() == SpectralFreeze::State::Live, "reversed heel clears the hold");
}
void midGlideLatchAndStereo() {
  Pog3Processor p; configure(p, isolated(2));
  std::vector<float> first, later, resumed;
  for (std::size_t i = 0; i < 192000; ++i) {
    if (i == 24000) target(p, Parameter::ExpressionPosition, .75f);
    if (i == 67200) target(p, Parameter::ExpressionPosition, 1); // Latch during the glide.
    if (i == 110400) target(p, Parameter::ExpressionPosition, .75f);
    const float x = sine(i < 48000 ? 196 : i < 72000 ? 293.6648 : 391.9954, i);
    const auto y = p.process({x, -x}).mixed;
    if (i >= 74400 && i < 86400) first.push_back(y.left);
    if (i >= 96000 && i < 108000) later.push_back(y.left);
    if (i >= 168000) resumed.push_back(y.left);
  }
  const double a = frequency(first), b = frequency(later), c = frequency(resumed);
  std::cout << "Mid-glide latch first/later/resumed=" << a << '/' << b << '/' << c << " Hz\n";
  require(a > 100 && a < 145, "toe latches an intermediate carrier during the glide");
  require(std::fabs(1200 * std::log2(b / a)) < .01, "latched glide cannot creep toward either new target");
  require(std::fabs(1200 * std::log2(c / (391.9954 / 2))) < 3 && p.freezeCaptures() == 1,
          "unlatching resumes toward the pending live target without recapture");
  require(p.healthy() && p.deadlineMisses() == 0, "mid-glide latch stays healthy");

  Pog3Processor stereo; configure(stereo, isolated(2));
  std::vector<float> left, right;
  for (std::size_t i = 0; i < 96000; ++i) {
    if (i == 24000) target(stereo, Parameter::ExpressionPosition, 1);
    const auto y = stereo.process({sine(i < 48000 ? 196 : 293.6648, i),
                                  sine(i < 48000 ? 329.6276 : 440, i, .07f)}).mixed;
    if (i >= 72000) { left.push_back(y.left); right.push_back(y.right); }
  }
  require(std::fabs(1200 * std::log2(frequency(left) / 98)) < 3
       && std::fabs(1200 * std::log2(frequency(right) / 164.8138)) < 3,
          "distinct stereo carriers retain independent frozen frequencies");
  require(std::fabs(amplitude(left, 98) - .1) < .0001 && std::fabs(amplitude(right, 164.8138) - .07) < .0001,
          "distinct stereo frozen magnitudes stay independent");
  require(stereo.healthy() && stereo.freezeCaptures() == 1, "independent stereo capture stays healthy");
}
void partition() {
  const auto run = [](std::size_t chunk) {
    Pog3Processor p; configure(p, {{"dry_level", .3}, {"down1_level", .3}, {"attack", .2}, {"dry_attack", 1}, {"focus", 1}});
    std::vector<VoiceStageOutput> output(40000);
    for (std::size_t start = 0; start < output.size(); start += chunk)
      for (std::size_t i = start; i < std::min(start + chunk, output.size()); ++i) {
        if (i == 5037 || i == 21317) { target(p, Parameter::ExpressionMode, 1); target(p, Parameter::ExpressionPosition, .7f); }
        if (i == 11053) target(p, Parameter::ExpressionMode, 5.0f / 6);
        if (i == 15017) target(p, Parameter::ExpressionPosition, 1);
        if (i == 19001) target(p, Parameter::ExpressionPosition, 0);
        if (i == 26003) target(p, Parameter::Focus, 0);
        if (i == 30001) target(p, Parameter::DryAttack, 0);
        if (i == 33001) target(p, Parameter::ExpressionMode, 0);
        const float x = sine(i < 13000 ? 82.4069 : 146.8324, i);
        output[i] = p.process({x, -.73f * x});
      }
    require(p.healthy() && p.deadlineMisses() == 0, "freeze transitions keep staged jobs valid");
    return output;
  };
  const auto reference = run(1);
  for (const auto chunk : {17U, 48U, 64U, 128U, 256U}) {
    const auto split = run(chunk);
    for (std::size_t i = 0; i < split.size(); ++i)
      require(split[i].mixed.left == reference[i].mixed.left && split[i].mixed.right == reference[i].mixed.right,
              "freeze/gliss/control transitions are bit-identical across callback partitions");
  }
}
void warmLiveRelease() {
  // A fully held renderer must advance the same live histories as a renderer
  // which stays audible. After old OLA drains, release must reproduce that
  // independent live reference exactly, including notes born during the hold.
  double resumedEnergy = 0;
  for (const std::size_t n : {1024U, 2048U}) for (const float semitones : {-24.0f, 0.0f, 7.0f, 24.0f}) {
    const auto hop = n / 8;
    auto interpolation = std::make_shared<PitchInterpolation>();
    auto plan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(n, hop), interpolation);
    auto lowPlan = std::make_shared<PitchPlan>(std::make_shared<SpectralPlan>(4096, 512), interpolation);
    SpectralAnalysis analysis, lowAnalysis;
    analysis.prepare(plan->spectral); lowAnalysis.prepare(lowPlan->spectral);
    PitchFrame frame, lowFrame; frame.prepare(plan); lowFrame.prepare(lowPlan, 400);
    PitchRenderer reference, frozen; reference.prepare(plan, true); frozen.prepare(plan, true);
    FrozenBand held{}, lowHeld{};
    held.count = lowHeld.count = 1;
    held.partials[0].id = 1; held.partials[0].frequency = 196; held.partials[0].magnitude = .05f;
    lowHeld.partials[0].id = 2; lowHeld.partials[0].frequency = 82.4069f; lowHeld.partials[0].magnitude = .03f;
    constexpr std::size_t release = 32768;
    for (std::size_t i = 0; i < 49152; ++i) {
      const float live = reference.pop(), actual = frozen.pop();
      if (i < 8192 || i >= release + n + 2 * hop) {
        require(std::bit_cast<std::uint32_t>(live) == std::bit_cast<std::uint32_t>(actual),
                "fully held renderer retains bit-identical warm live state for release");
        if (i >= release + n + 2 * hop) resumedEnergy += live * live;
      }
      const float input = sine(82.4069, i) + .6f * sine(i < 16384 ? 196 : 293.6648, i);
      if (lowAnalysis.push(input)) lowFrame.update(lowAnalysis.spectrum());
      if (!analysis.push(input)) continue;
      frame.update(analysis.spectrum());
      const float mix = i >= 8192 && i < release ? 1 : 0;
      require(reference.render(frame, semitones, &lowFrame, hop, nullptr, nullptr, true)
                && frozen.render(frame, semitones, &lowFrame, hop, nullptr, nullptr, true,
                                 &held, &lowHeld, mix),
              "held and live reference accept every staged frame");
    }
  }
  require(resumedEnergy > 1, "warm release comparison exercises audible shifted and unison output");
}

void endurance() {
  for (const bool chord : {false, true}) {
    Pog3Processor p; configure(p, isolated(2));
    std::vector<float> first(48000), last(48000);
    float peak = 0, anti = 0;
    // Sixty seconds of hold after capture; input becomes silence after one second.
    for (std::size_t i = 0; i < 61 * 48000; ++i) {
      if (i == 24000) target(p, Parameter::ExpressionPosition, 1);
      const float x = i < 48000 ? sine(82.4069, i, .05f)
        + (chord ? sine(130.8128, i, .04f) + sine(196, i, .03f) + sine(329.6276, i, .02f) : 0) : 0;
      const auto y = p.process({x, -.73f * x}).mixed;
      require(std::isfinite(y.left) && std::isfinite(y.right), "60-second hold remains finite");
      if (i >= 48000 && i < 96000) first[i - 48000] = y.left;
      if (i >= 60 * 48000) last[i - 60 * 48000] = y.left;
      if (i > 48000) { peak = std::max(peak, std::fabs(y.left)); anti = std::max(anti, std::fabs(y.right + .73f * y.left)); }
    }
    for (const double hz : chord ? std::vector<double>{41.20345, 65.4064, 98, 164.8138} : std::vector<double>{41.20345}) {
      const double delta = 20 * std::log10(amplitude(last, hz) / amplitude(first, hz));
      std::cout << "60-second hold chord=" << chord << " frequency=" << hz << " level change=" << delta << " dB peak=" << peak << " anti=" << anti << '\n';
      require(std::fabs(delta) < .1, "held chord/sine has no amplitude decay or compensation drift over sixty seconds");
    }
    require(anti < 1e-5 && p.freezeCaptures() == 1 && p.freezeTargets() == 0 && p.healthy() && p.deadlineMisses() == 0,
            "60-second held phase remains stereo coherent and never recaptures silence");
    target(p, Parameter::ExpressionMode, 0);
    for (int i = 0; i < 12000; ++i) p.process({});
    for (int i = 0; i < 3000; ++i) require(p.process({}).mixed.left == 0, "long hold drains completely after mode exit");
  }
}
void denseAndReset() {
  Pog3Processor p;
  configure(p, {{"expression_mode", 5.0 / 6}, {"expression_position", 0}, {"dry_level", .2},
    {"down2_level", .2}, {"down1_level", .2}, {"fifth_level", .2}, {"up1_level", .2}, {"up2_level", .2}, {"focus", 1}});
  std::uint32_t random = 0x46524545;
  for (std::size_t i = 0; i < 35000; ++i) {
    if (i == 6001) target(p, Parameter::ExpressionPosition, .6f);
    if (i == 17003) target(p, Parameter::ExpressionPosition, 1);
    if (i == 21007) p.reset();
    if (i == 30001) target(p, Parameter::ExpressionMode, 0);
    random = 1664525 * random + 1013904223;
    const float l = i < 30000 ? 12 * (static_cast<double>(random) / 4294967296.0 - .5) : 0;
    random = 1664525 * random + 1013904223;
    const float r = i < 30000 ? 12 * (static_cast<double>(random) / 4294967296.0 - .5) : 0;
    const auto y = p.process({l, r}).mixed;
    require(std::isfinite(y.left) && std::isfinite(y.right), "dense independent stereo overload/capture/assignment remains finite");
  }
  require(p.healthy() && p.deadlineMisses() == 0, "dense freeze model preserves prepared/scheduled bounds");
  p.reset();
  for (int i = 0; i < 10000; ++i) require(p.process({}).mixed.left == 0, "dense reset cannot retain captured noise");
}
void render(const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);
  std::vector<PitchStereo> source(5 * 48000);
  for (std::size_t i = 0; i < 216000; ++i) {
    const double fundamental = i < 72000 ? 82.4069 : i < 144000 ? 110 : 146.8324;
    const float x = sine(fundamental, i, .035f) + sine(fundamental * 1.5, i, .025f) + sine(fundamental * 2, i, .02f);
    source[i] = {x, -.73f * x};
  }
  pog3_test::writeRender(directory / "freeze-source.wav", source);
  for (const auto name : {"gliss-focused", "gliss-short", "volume-live-dry", "volume-held-dry", "toe-latch", "off"}) {
    const std::string_view scene{name};
    const bool volume = scene == "volume-live-dry" || scene == "volume-held-dry";
    nlohmann::json params{{"dry_level", .3}, {"down2_level", .2}, {"down1_level", .2}, {"fifth_level", .15},
      {"up1_level", .15}, {"up2_level", .1}, {"focus", scene == "gliss-short" ? 0 : 1},
      {"expression_mode", scene == "off" ? 0 : volume ? 1 : 5.0 / 6}, {"expression_position", 0}};
    if (scene == "volume-held-dry") { params["attack"] = .15; params["dry_attack"] = 1; }
    Pog3Processor p; configure(p, params);
    std::vector<PitchStereo> output; output.reserve(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
      if (i == 36000) target(p, Parameter::ExpressionPosition, scene == "toe-latch" ? 1 : .7f);
      if (i == 120000) target(p, Parameter::ExpressionPosition, volume ? .3f : 1);
      if (i == 192000) target(p, Parameter::ExpressionPosition, 0);
      output.push_back(p.process(source[i]).mixed);
    }
    require(p.healthy() && p.deadlineMisses() == 0, "freeze listening render healthy");
    pog3_test::writeRender(directory / ("freeze-" + std::string{name} + ".wav"), output);
  }
}
}
int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string_view(argv[1]) == "--render") { render(argv[2]); return 0; }
    if (argc == 2 && std::string_view(argv[1]) == "--warm-live") { warmLiveRelease(); return 0; }
    const bool quick = argc == 2 && std::string_view(argv[1]) == "--quick";
    if (argc != 1 && !quick) throw std::runtime_error("usage: pedal-pog3-freeze-quality [--quick | --warm-live | --render directory]");
    stationary(); stateAndSilence(); gliss(); eligibilityAndVolume(); toeAndReverse(); midGlideLatchAndStereo(); partition(); denseAndReset();
    warmLiveRelease();
    if (!quick) endurance();
    std::cout << "POG3 stationary freeze, gliss, eligibility, routing and lifecycle gates passed\n"; return 0;
  }
  catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
