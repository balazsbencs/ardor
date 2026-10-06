#include "daisyfx/pog3/Pog3VoiceStages.h"
#include "pog3_artifacts.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace ardor::pog3;
constexpr double pi = 3.14159265358979323846;
void require(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
Values isolated(std::size_t voice) {
  auto values = defaultValues();
  for (std::size_t v = 0; v < kVoiceCount; ++v) values[index(Parameter::DryLevel) + v] = v == voice ? 1 : 0;
  return values;
}
void set(Values& v, Parameter p, float x) { v[index(p)] = x; }
float frequencyControl(float hz) { return std::log(hz / 40) / std::log(500.0f); }
void ad() {
  FilterAd e;
  require(e.setTimes(.005, .02), "AD times accepted");
  require(!e.setTimes(std::numeric_limits<float>::infinity(), .1), "AD nonfinite rejected");
  e.trigger();
  for (std::size_t i = 1; i <= 240; ++i) require(std::fabs(e.process() - i / 240.0f) < 1e-6, "AD finite attack timing");
  require(e.state() == FilterAd::State::Decay, "AD enters decay despite sustained input");
  for (std::size_t i = 1; i <= 960; ++i) require(std::fabs(e.process() - (1 - i / 960.0f)) < 1e-6, "AD finite decay timing");
  require(e.state() == FilterAd::State::Idle && e.value() == 0, "AD returns exactly to base");
  e.trigger(); for (int i = 0; i < 120; ++i) e.process();
  const float before = e.value(); e.trigger();
  require(e.value() == before && e.process() - before < .005, "retrigger retains current excursion");
  e.reset(); require(e.value() == 0 && e.state() == FilterAd::State::Idle, "AD reset clears excursion");
}
void detector() {
  for (const double f : {40.0, 65.4, 82.4, 196.0, 659.3}) for (const float sensitivity : {0.0f, .5f, 1.0f}) {
    PlayingDetector mono, anti;
    mono.setSensitivity(sensitivity); anti.setSensitivity(sensitivity);
    mono.reset(); anti.reset();
    std::size_t count = 0;
    for (std::size_t i = 0; i < 96000; ++i) {
      const float x = .1 * std::sin(2 * pi * f * i / kSampleRate);
      const bool a = mono.process({x, x}), b = anti.process({x, -x});
      require(a == b, "detector preserves anti-phase onsets");
      count += a;
    }
    std::cout << "Held detector frequency=" << f << " sensitivity=" << sensitivity << " triggers=" << count << '\n';
    require(count == 1, "one held low/high tone must not cycle-retrigger");
  }
  PlayingDetector low, high, repeat;
  low.setSensitivity(0); high.setSensitivity(1); repeat.setSensitivity(1);
  low.reset(); high.reset(); repeat.reset();
  for (std::size_t i = 0; i < 48000; ++i) {
    const float soft = .0015 * std::sin(2 * pi * 196 * i / kSampleRate);
    low.process({soft, 0}); high.process({soft, 0});
    const float amplitude = i < 24000 ? .03f : .1f;
    const float x = amplitude * std::sin(2 * pi * 196 * i / kSampleRate);
    repeat.process({x, -x});
  }
  require(low.triggers() == 0 && high.triggers() == 1, "sensitivity admits soft plucks");
  require(repeat.triggers() == 2, "a stronger re-pluck triggers one new excursion");
  PlayingDetector falling;
  falling.setSensitivity(1); falling.reset();
  for (std::size_t i = 0; i < 96000; ++i) {
    const double t = static_cast<double>(i) / kSampleRate;
    const float amplitude = t < 1.5 ? .1 * std::exp(-3 * t) : .04 * std::exp(-3 * (t - 1.5));
    const float x = amplitude * std::sin(2 * pi * 196 * t);
    falling.process({x, -x});
  }
  require(falling.triggers() == 2, "peak reference releases with a decaying note to admit a softer re-pluck");
  for (const float sensitivity : {.5f, 1.0f}) {
    PlayingDetector chord;
    chord.setSensitivity(sensitivity); chord.reset();
    for (std::size_t i = 0; i < 3 * 48000; ++i) {
      const float x = .03 * (std::sin(2 * pi * 196 * i / kSampleRate)
        + std::sin(2 * pi * 329.6276 * i / kSampleRate) + std::sin(2 * pi * 493.8833 * i / kSampleRate));
      chord.process({x, -x});
    }
    std::cout << "Held chord sensitivity=" << sensitivity << " triggers=" << chord.triggers() << '\n';
    require(chord.triggers() == 1, "held chord must not repeatedly trigger filter AD");
  }
}
void spread() {
  for (const float u : {0.0f, .125f, .5f, 1.0f}) {
    StereoSpread delay;
    delay.setAmount(u); delay.prepare();
    for (std::size_t i = 0; i < 10000; ++i) {
      const auto y = delay.process(i == 0 ? PitchStereo{1, -.3f} : PitchStereo{});
      const auto leftTime = static_cast<std::size_t>(2400 * u), rightTime = static_cast<std::size_t>(7200 * u);
      require(y.left == (i == leftTime ? 1 : 0), "Spread left impulse at 50u ms only");
      require(y.right == (i == rightTime ? -.3f : 0), "Spread right impulse at 150u ms only");
    }
  }
  StereoSpread queued;
  queued.prepare(); queued.setAmount(1);
  for (std::size_t i = 0; i < 960; ++i) {
    if (i == 100) queued.setAmount(.5f);
    if (i == 200) queued.setAmount(.25f);
    const auto y = queued.process({1, -.3f});
    require(std::isfinite(y.left) && std::isfinite(y.right), "Spread queue finite");
    if (i < 479) require(queued.rightAnchor() == 7200, "new target never slides active tap anchors");
  }
  require(!queued.transitioning() && queued.rightAnchor() == 1800, "latest Spread target completes queued fade");
  queued.setAmount(0); for (int i = 0; i < 1000; ++i) queued.process({.23f, -.37f});
  const auto off = queued.process({.123f, -.456f});
  require(off.left == .123f && off.right == -.456f, "Spread exact zero returns current stereo input");
  queued.setAmount(.0000001f); queued.reset(); queued.setAmount(0);
  queued.process({.23f, .4f});
  const auto tinyOff = queued.process({.123f, -.456f});
  require(tinyOff.left == .123f && tinyOff.right == -.456f, "Spread exact-off overrides the helper's tiny deadband");
  require(!queued.setAmount(std::numeric_limits<float>::quiet_NaN()), "Spread nonfinite target rejected");
}
void panAndEligibility() {
  for (std::size_t voice = 0; voice < kVoiceCount; ++voice) for (const float p : {0.0f, .5f, 1.0f}) {
    Pog3VoiceStages stage;
    auto values = isolated(voice); values[index(Parameter::DryPan) + voice] = p;
    stage.setValues(values); stage.prepare();
    PitchVoices voices{}; voices[voice] = {0, .2f};
    const auto y = stage.process({}, {0, .2f}, voices).mixed;
    if (p == .5) require(y.left == 0 && y.right == .2f, "center preserves independent stereo");
    else if (p == 0) require(std::fabs(y.left - .14142136f) < 1e-6 && y.right == 0, "hard left folds a right-only voice");
    else require(y.left == 0 && std::fabs(y.right - .14142136f) < 1e-6, "hard right exact opposite-channel mute");
  }
  for (std::size_t voice = 0; voice < kVoiceCount; ++voice) {
    Pog3VoiceStages stage;
    auto values = isolated(voice); set(values, Parameter::Spread, 1); set(values, Parameter::Detune, 1);
    stage.setValues(values); stage.prepare();
    PitchVoices voices{}; voices[voice] = {.2f, -.1f};
    const auto y = stage.process({}, {.2f, -.1f}, voices).mixed;
    if (voice < 3) require(y.left == .2f && y.right == -.1f, "dry-off and both suboctaves exclude detune and spread");
    else require(y.left == 0 && y.right == 0, "fifth/upper Spread has no implicit direct parallel tap");
  }
  // Fifth has spread but no doubling. Settle a new registration before impulse.
  Pog3VoiceStages fifth;
  auto values = isolated(3); set(values, Parameter::Detune, 1);
  fifth.setValues(values); fifth.prepare();
  PitchVoices voices{}; voices[3] = {.2, -.1};
  auto y = fifth.process({}, {}, voices).mixed;
  require(y.left == .2f && y.right == -.1f, "fifth excludes doubling even at maximum Detune");
  for (const bool dryEnabled : {false, true}) {
    Pog3VoiceStages dry;
    values = isolated(0); set(values, Parameter::DryDetune, dryEnabled); set(values, Parameter::Spread, 1);
    dry.setValues(values); dry.prepare();
    y = dry.process({}, {.2f, -.1f}, {}).mixed;
    require(dryEnabled ? y.left == 0 && y.right == 0 : y.left == .2f && y.right == -.1f,
            "Dry Detune controls dry Spread eligibility even with depth zero");
  }
  Pog3VoiceStages hardLeft;
  values = isolated(3); set(values, Parameter::FifthPan, 0); set(values, Parameter::Spread, 1);
  hardLeft.setValues(values); hardLeft.prepare();
  for (std::size_t i = 0; i < 7600; ++i) {
    voices = {}; if (i == 0) voices[3] = {0, .2f};
    y = hardLeft.process({}, {}, voices).mixed;
    require(y.right == 0, "hard-left pan precedes the asymmetric lines");
    require(std::fabs(y.left - (i == 2400 ? .14142136f : 0)) < 1e-6, "hard-left Spread remains 50 ms");
  }
}
double filterLevel(float frequency, int mode, float q, float sourceHz, bool dry = false, bool route = true) {
  Pog3VoiceStages stage;
  auto v = isolated(dry ? 0 : 3);
  set(v, Parameter::FilterFrequency, frequencyControl(frequency)); set(v, Parameter::FilterMode, mode * .5f);
  set(v, Parameter::FilterQ, q); set(v, Parameter::DryFilter, route ? 1 : 0);
  stage.setValues(v); stage.prepare();
  double input = 0, output = 0;
  for (std::size_t i = 0; i < 48000; ++i) {
    const float x = .1 * std::sin(2 * pi * sourceHz * i / kSampleRate);
    PitchVoices voices{}; if (!dry) voices[3] = {x, -x};
    const auto y = stage.process({}, dry ? PitchStereo{x, -x} : PitchStereo{}, voices).mixed;
    if (i > 12000) { input += x * x; output += y.left * y.left; }
  }
  return 10 * std::log10(output / input);
}
void filters() {
  const auto lpLow = filterLevel(1000, 0, 0, 100), lpHigh = filterLevel(1000, 0, 0, 10000);
  const auto hpLow = filterLevel(1000, 2, 0, 100), hpHigh = filterLevel(1000, 2, 0, 10000);
  const auto bpLow = filterLevel(1000, 1, 0, 100), bpCenter = filterLevel(1000, 1, 0, 1000), bpHigh = filterLevel(1000, 1, 0, 10000);
  const auto bpResonant = filterLevel(1000, 1, 1, 1000), lpResonant = filterLevel(1000, 0, 1, 1000);
  std::cout << "Filter LP low/high=" << lpLow << '/' << lpHigh << " HP=" << hpLow << '/' << hpHigh
            << " BP=" << bpLow << '/' << bpCenter << '/' << bpHigh << " BP Q8=" << bpResonant << " LP Q8=" << lpResonant << " dB\n";
  require(lpLow > -.1 && lpHigh < -40 && hpLow < -35 && hpHigh > -.1, "LP/HP shape");
  require(bpLow < -15 && bpHigh < -15 && std::fabs(bpCenter) < .1 && std::fabs(bpResonant) < .1, "BP damping normalization holds at Q8");
  require(lpResonant > 17 && lpResonant < 19, "LP resonance grows to the Q8 bound");
  require(std::fabs(filterLevel(1000, 0, 1, 10000, true, false)) < .001, "Dry Filter off remains immediate and exact");
  require(filterLevel(1000, 0, 1, 10000, true, true) < -35, "Dry Filter uses independently filtered dry");
  require(std::fabs(filterLevel(20000, 0, 0, 10000)) < .001, "LP open extension is exact at its neutral endpoint");
  require(filterLevel(20000, 1, 0, 1000) < -25 && filterLevel(20000, 2, 0, 1000) < -50,
          "BP and HP never use the LP open extension");
}
void automationAndReset() {
  const auto run = [](std::size_t chunk) {
    Pog3VoiceStages stage;
    auto v = defaultValues();
    for (std::size_t voice = 0; voice < kVoiceCount; ++voice) v[index(Parameter::DryLevel) + voice] = .2f;
    set(v, Parameter::DryDetune, 1); set(v, Parameter::DryFilter, 1);
    stage.setValues(v); stage.prepare();
    std::vector<PitchStereo> output; output.reserve(24000);
    for (std::size_t start = 0; start < 24000; start += chunk)
      for (std::size_t i = start; i < std::min(start + chunk, std::size_t{24000}); ++i) {
        if (i % 137 == 0) {
          const float u = (i / 137) % 11 / 10.0f;
          set(v, Parameter::Spread, u); set(v, Parameter::Detune, 1 - u);
          set(v, Parameter::FilterFrequency, u); set(v, Parameter::FilterQ, 1 - u);
          set(v, Parameter::FilterMode, (i / 137) % 3 * .5f);
          set(v, Parameter::DryFilter, (i / 137) % 2);
          set(v, Parameter::Up1Pan, u); stage.setValues(v);
        }
        const float x = .03 * std::sin(i * .0257);
        PitchVoices voices{};
        for (std::size_t voice = 1; voice < kVoiceCount; ++voice) voices[voice] = {x * static_cast<float>(voice), -x};
        output.push_back(stage.process({x, -x}, {x, -x}, voices).mixed);
      }
    require(stage.recoveries() == 0, "rapid routing/space/mode automation needs no recovery");
    stage.reset(); stage.reset();
    for (int i = 0; i < 10000; ++i) {
      const auto y = stage.process({}, {}, {}).mixed;
      require(y.left == 0 && y.right == 0, "reset clears filters, chorus and spread histories during automation");
    }
    return output;
  };
  const auto reference = run(1);
  for (const auto chunk : {64U, 127U, 512U}) {
    const auto output = run(chunk);
    for (std::size_t i = 0; i < output.size(); ++i)
      require(output[i].left == reference[i].left && output[i].right == reference[i].right,
              "absolute control timestamps give identical output across callback partitions");
  }
}
void sweepAndDoubling() {
  for (const float direction : {0.0f, 1.0f}) {
    Pog3VoiceStages stage, differentAttack;
    auto v = isolated(0); set(v, Parameter::FilterFrequency, frequencyControl(1000));
    set(v, Parameter::FilterEnv, direction); set(v, Parameter::FilterAttack, 0); set(v, Parameter::FilterDecay, 0);
    stage.setValues(v); set(v, Parameter::Attack, 1); differentAttack.setValues(v);
    stage.prepare(); differentAttack.prepare();
    float extreme = 1000;
    for (std::size_t i = 0; i < 24000; ++i) {
      const float x = .1 * std::sin(2 * pi * 196 * i / kSampleRate);
      stage.process({x, -x}, {x, -x}, {}); differentAttack.process({x, -x}, {x, -x}, {});
      require(stage.filterEnvelope() == differentAttack.filterEnvelope(), "volume Attack cannot change filter AD timing");
      require(stage.filterCutoff() >= 40 && stage.filterCutoff() <= 20000, "envelope cutoff clamped to bounds");
      extreme = direction == 0 ? std::min(extreme, stage.filterCutoff()) : std::max(extreme, stage.filterCutoff());
    }
    require(direction == 0 ? extreme < 500 : extreme > 2000, "filter Envelope polarity changes sweep direction");
    require(stage.filterEnvelope() == 0 && std::fabs(stage.filterCutoff() - 1000) < .1, "AD returns to base during sustain");
  }
  StereoDoubling chorus;
  chorus.prepare(4);
  for (std::size_t i = 0; i < 96000; ++i) {
    const PitchStereo x{.1f * static_cast<float>(std::sin(i * .1)), -.07f * static_cast<float>(std::cos(i * .07))};
    auto y = chorus.process(x);
    if (i < 10000) require(y.left == x.left && y.right == x.right, "Detune zero has no 8 ms base delay");
    if (i == 10000) chorus.setDepth(1);
    if (i == 48000) chorus.setDepth(0);
    require(std::isfinite(y.left) && std::isfinite(y.right), "doubling remains finite");
    if (i > 49000) require(y.left == x.left && y.right == x.right, "Detune returns exactly to zero");
  }
  std::cout << "Largest chorus delay step=" << chorus.largestReadStep() << " samples/sample\n";
  require(chorus.largestReadStep() <= .00501f, "bounded doubling derivative even during automation");
}
void recoveryAndGains() {
  Pog3SignalPath path;
  auto v = isolated(0); set(v, Parameter::InputGain, .6); set(v, Parameter::MasterLevel, .25f);
  path.setSoundValues(v); path.prepare();
  for (std::size_t i = 0; i < 5000; ++i) {
    const PitchStereo x{.2f * static_cast<float>(std::sin(i * .17)), -.1f * static_cast<float>(std::cos(i * .03))};
    const auto y = path.process(x);
    require(std::fabs(y.mixed.left - x.left) < 1e-7 && std::fabs(y.mixed.right - x.right) < 1e-7,
            "input gain 2 and master .5 applied once, dry immediate");
    require(y.wet.left == 0 && y.wet.right == 0, "wet contains generated voices only");
  }
  auto bad = v; set(bad, Parameter::FilterQ, std::numeric_limits<float>::quiet_NaN());
  require(!path.setSoundValues(bad), "sound controls reject nonfinite transactionally");
  set(v, Parameter::MasterLevel, 0); path.setSoundValues(v); path.reset();
  for (int i = 0; i < 2000; ++i) {
    const auto y = path.process({.2f, -.1f});
    require(y.mixed.left == 0 && y.mixed.right == 0 && y.wet.left == 0, "master zero exact mute");
  }
  Pog3VoiceStages stage;
  v = isolated(3); set(v, Parameter::FilterQ, 1); set(v, Parameter::FilterFrequency, frequencyControl(1000));
  stage.setValues(v); stage.prepare();
  for (std::size_t i = 0; i < 96000; ++i) {
    if (i < 24000 && i % 48 == 0) {
      set(v, Parameter::FilterMode, (i / 48) % 3 * .5f);
      set(v, Parameter::FilterFrequency, (i / 48) % 128 / 127.0f); stage.setValues(v);
    }
    PitchVoices voices{};
    if (i < 24000) voices[3] = {12 * static_cast<float>(std::sin(i * .1)), -9 * static_cast<float>(std::cos(i * .1))};
    const auto y = stage.process({}, {}, voices).mixed;
    require(std::isfinite(y.left) && std::isfinite(y.right), "Q8 overload/mode automation finite");
    if (i > 90000) require(std::fabs(y.left) < 1e-6 && std::fabs(y.right) < 1e-6, "filter rings drain to silence");
  }
  require(stage.recoveries() == 0, "documented ±12 overload needs no state recovery");
  PitchVoices poisoned{}; poisoned[3] = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
  stage.process({}, {}, poisoned);
  require(stage.recoveries() >= 2, "nonfinite wet state counted and muted");
  for (int i = 0; i < 1000; ++i) {
    const auto y = stage.process({}, {}, {}).mixed;
    require(y.left == 0 && y.right == 0, "wet state reset recovers immediately");
  }
  stage.reset(); stage.reset(); require(stage.recoveries() == 0 && stage.filterEnvelope() == 0, "reset clears stage state and counters");
  Pog3VoiceStages overflow;
  v = isolated(0); set(v, Parameter::MasterLevel, 1);
  overflow.setValues(v); overflow.prepare();
  const auto y = overflow.process({}, {std::numeric_limits<float>::max(), 0}, {}).mixed;
  require(y.left == 0 && y.right == 0 && overflow.recoveries() > 0, "output overflow is muted and diagnosed");
}
void render(const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);
  std::vector<PitchStereo> input(48000 * 4);
  for (std::size_t i = 0; i < input.size(); ++i) {
    const double t = static_cast<double>(i) / kSampleRate;
    if (t >= 2.8) continue;
    const double a = .035 * std::exp(-1.2 * std::fmod(t, .7));
    const float x = a * (std::sin(2 * pi * 82.4069 * t) + std::sin(2 * pi * 130.8128 * t)
      + std::sin(2 * pi * 196 * t) + (t > .8 ? .7 * std::sin(2 * pi * 329.6276 * t) : 0));
    input[i] = {x, -.73f * x};
  }
  pog3_test::writeRender(directory / "voice-stages-source.wav", input);
  for (const auto name : {"organ", "filter-up", "filter-down", "doubling", "spread", "dry-routing", "automation"}) {
    Pog3SignalPath path;
    auto v = defaultValues();
    for (std::size_t voice = 0; voice < kVoiceCount; ++voice) v[index(Parameter::DryLevel) + voice] = voice == 0 ? .3f : .2f;
    set(v, Parameter::Attack, .25f); set(v, Parameter::Focus, 1);
    const std::string_view scene{name};
    if (scene == "filter-up" || scene == "filter-down" || scene == "automation") {
      set(v, Parameter::FilterFrequency, frequencyControl(700)); set(v, Parameter::FilterQ, .6f);
      set(v, Parameter::FilterEnv, scene == "filter-down" ? .1f : .9f);
      set(v, Parameter::FilterAttack, .3f); set(v, Parameter::FilterDecay, .45f);
    }
    if (scene == "doubling" || scene == "dry-routing") set(v, Parameter::Detune, 1);
    if (scene == "spread" || scene == "dry-routing") set(v, Parameter::Spread, .5f);
    if (scene == "dry-routing") {
      set(v, Parameter::DryAttack, 1); set(v, Parameter::DryDetune, 1); set(v, Parameter::DryFilter, 1);
      set(v, Parameter::FilterFrequency, frequencyControl(1800));
    }
    path.setSoundValues(v); path.prepare();
    std::vector<PitchStereo> output; output.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
      if (scene == "automation" && i % 480 == 0) {
        const float u = .5f + .5f * std::sin(i * 2 * pi / 48000);
        set(v, Parameter::Spread, u); set(v, Parameter::Detune, 1 - u);
        set(v, Parameter::Up1Pan, u); set(v, Parameter::Up2Pan, 1 - u);
        set(v, Parameter::FilterMode, (i / 24000) % 3 * .5f); path.setSoundValues(v);
      }
      output.push_back(path.process(input[i]).mixed);
    }
    require(path.healthy() && path.deadlineMisses() == 0, "render path healthy with no scheduled deadline misses");
    pog3_test::writeRender(directory / ("voice-stages-" + std::string{name} + ".wav"), output);
  }
}
}

int main(int argc, char** argv) {
  try {
    if (argc != 1 && !(argc == 3 && std::string_view(argv[1]) == "--render"))
      throw std::runtime_error("usage: pedal-pog3-voice-stages [--render directory]");
    ad(); detector(); spread(); panAndEligibility(); filters(); sweepAndDoubling(); automationAndReset(); recoveryAndGains();
    std::cout << "POG3 filter, doubling, spread, pan and static sound-path gates passed\n";
    if (argc == 3) render(argv[2]);
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
