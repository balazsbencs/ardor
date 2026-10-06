#include "daisyfx/pog3/Pog3Processor.h"
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
void close(float actual, float expected, const char* message, float tolerance = 1e-6f) {
  require(std::fabs(actual - expected) <= tolerance, message);
}
void configure(Pog3Processor& processor, const nlohmann::json& params) {
  std::string error;
  if (!processor.configure(params, 48000, error)) throw std::runtime_error(error);
}
void target(Pog3Processor& p, Parameter key, float value) {
  require(p.setParameterTarget(index(key), value), "valid expression target accepted");
}
PitchStereo inputAt(std::size_t i) {
  const double t = static_cast<double>(i) / kSampleRate;
  const float chord = .035 * (std::sin(2 * pi * 82.4069 * t) + std::sin(2 * pi * 130.8128 * t)
    + std::sin(2 * pi * 196 * t) + std::sin(2 * pi * 329.6276 * t));
  return {chord, -.7f * chord + .013f * static_cast<float>(std::sin(2 * pi * 233.0819 * t))};
}

void publicationAndLifecycle() {
  Pog3Processor p, twin;
  p.reset();
  require(p.process({1, -1}).mixed.left == 0 && !p.healthy(), "unconfigured processor returns silence");
  configure(p, nlohmann::json::object()); configure(twin, nlohmann::json::object());
  for (std::size_t i = 0; i < kParameterCount; ++i) {
    const float value = i == index(Parameter::ExpressionMode) ? .5f : .123f;
    require(p.setParameterTarget(parameterSpecs()[i].key, value) && twin.setParameterTarget(i, value),
            "all 33 processor key/index setters agree");
  }
  require(p.targetValues() == twin.targetValues(), "key/index publication produces identical controls");
  const auto saved = p.targetValues();
  require(!p.setParameterTarget(33, .3f) && !p.setParameterTarget("mix", .3f), "unknown processor targets rejected");
  require(!p.setParameterTarget(32, std::numeric_limits<float>::quiet_NaN()), "nonfinite processor target rejected");
  require(!p.setParameterTarget("expression_mode", 5.0f / 6)
          && !p.setParameterTarget(28, 1), "unfinished freeze modes rejected by both setters");
  std::string error;
  for (const auto& invalid : {nlohmann::json{{"filter_q", "bad"}},
       nlohmann::json{{"crossfade_toe", {{"focus", 1}}}}, nlohmann::json{{"expression_mode", 1}}}) {
    require(!p.configure(invalid, 48000, error) && !error.empty(), "invalid or unfinished configuration rejected");
    require(p.targetValues() == saved, "failed configuration retains all published targets");
  }
  require(!p.configure({}, 44100, error) && !p.configure({}, std::numeric_limits<float>::quiet_NaN(), error),
          "native sample-rate contract enforced transactionally");
  for (std::size_t i = 0; i < 5000; ++i) {
    const auto a = p.process(inputAt(i)), b = twin.process(inputAt(i));
    require(a.mixed.left == b.mixed.left && a.mixed.right == b.mixed.right,
            "failed reconfigure leaves old audio state and control timing intact");
  }
  configure(p, nlohmann::json::object());
  p.process({});
  require(p.controlUpdates() == 1, "first sample consumes targets");
  target(p, Parameter::DryLevel, .2f);
  for (int i = 1; i < 48; ++i) {
    p.process({});
    require(p.baseValues()[index(Parameter::DryLevel)] == 1 && p.controlUpdates() == 1,
            "targets wait for the next 48-sample boundary");
  }
  p.process({});
  require(p.baseValues()[index(Parameter::DryLevel)] == .2f && p.controlUpdates() == 2,
          "target consumed at absolute sample 48");
  // Ordinary scene-like sequential setters, without claiming an atomic scene.
  target(p, Parameter::DryPan, .1f); target(p, Parameter::MasterLevel, .7f);
  target(p, Parameter::ExpressionMode, 1.0f / 6); target(p, Parameter::ExpressionPosition, .3f);
  for (int i = 49; i < 96; ++i) p.process({});
  require(p.baseValues()[index(Parameter::DryPan)] == .5f, "scene targets preserve audio-side ownership until cadence");
  p.process({});
  close(p.generatedGainTarget(), .3f, "scene mode/position resolve together when already published");
  require(p.baseValues() == p.targetValues(), "consumed base matches published scene values");
  p.reset(); p.reset();
  require(p.controlUpdates() == 0 && p.baseValues() == p.targetValues(), "reset retains targets and reseeds controls");
  for (int i = 0; i < 10000; ++i) {
    const auto y = p.process({}).mixed;
    require(y.left == 0 && y.right == 0, "reset removes every audio and space tail");
  }
  require(p.healthy() && p.deadlineMisses() == 0, "publication/lifecycle path healthy");
}

void ownershipAndEndpoints() {
  Pog3Processor p;
  configure(p, {{"dry_level", .8}, {"expression_mode", 2.0 / 6}, {"expression_position", 0},
    {"crossfade_heel", {{"dry_level", .1}, {"up1_pan", 0}, {"filter_frequency", .15}, {"attack", .2}}},
    {"crossfade_toe", {{"dry_level", .9}, {"input_gain", .6}, {"filter_frequency", .85}, {"attack", .8}}}});
  for (const float position : {0.0f, .5f, 1.0f}) for (const bool reverse : {false, true}) {
    target(p, Parameter::ExpressionPosition, position); target(p, Parameter::ExpressionReverse, reverse);
    p.reset();
    const float q = reverse ? 1 - position : position;
    const auto sound = p.soundValues(), base = p.baseValues();
    close(sound[index(Parameter::DryLevel)], .1f + .8f * q, "compiled level interpolation");
    close(sound[index(Parameter::Up1Pan)], .5f * q, "missing toe retains configured base");
    close(sound[index(Parameter::InputGain)], .2f + .4f * q, "missing heel retains configured base");
    close(sound[index(Parameter::FilterFrequency)], .15f + .7f * q, "cutoff interpolates in normalized/log domain");
    close(sound[index(Parameter::Attack)], .2f + .6f * q, "Attack interpolates before its physical square mapping");
    require(base[index(Parameter::DryLevel)] == .8f && p.targetValues() == base, "expression never writes saved base controls");
  }
  target(p, Parameter::ExpressionReverse, 0); target(p, Parameter::ExpressionPosition, .25f);
  target(p, Parameter::InputGain, .8f); target(p, Parameter::DryLevel, .55f);
  target(p, Parameter::DryPan, .17f); p.reset();
  close(p.soundValues()[index(Parameter::InputGain)], .3f, "active snapshot owns input gain despite a saved base edit");
  close(p.soundValues()[index(Parameter::DryLevel)], .3f, "active snapshot owns level despite a saved base edit");
  require(p.soundValues()[index(Parameter::DryPan)] == .17f, "unowned control follows its base target");
  target(p, Parameter::ExpressionMode, 0); p.reset();
  require(p.soundValues() == p.targetValues(), "exiting Crossfade restores latest saved base edits");
  require(formatExpressionEndpoint(ExpressionMode::Volume, 0) == "Mute"
          && formatExpressionEndpoint(ExpressionMode::Volume, 1) == "0.0 dB", "Volume endpoint gain units");
  require(formatExpressionEndpoint(ExpressionMode::Warp, .5f) == "6.00 st extent", "Warp endpoint extent units");
  require(formatExpressionEndpoint(ExpressionMode::Filter, 0) == "40 Hz"
          && formatExpressionEndpoint(ExpressionMode::Filter, 1) == "20000 Hz", "Filter endpoint frequency units");
  require(formatExpressionEndpoint(ExpressionMode::Crossfade, .5f) == "Unused", "Crossfade uses snapshots, not scalar endpoints");
  // Exact endpoints must not be perturbed by subtract/add cancellation.
  Configuration config;
  config.base = config.heel = config.toe = defaultValues();
  config.morphed.set(index(Parameter::DryLevel));
  config.heel[index(Parameter::DryLevel)] = .913579f;
  config.toe[index(Parameter::DryLevel)] = .000000013f;
  auto values = defaultValues(); values[index(Parameter::ExpressionMode)] = 2.0f / 6;
  require(effectiveValues(config, values)[index(Parameter::DryLevel)] == config.toe[index(Parameter::DryLevel)],
          "Crossfade toe is bit-exact even with a very small value");
  values[index(Parameter::ExpressionHeel)] = .913579f; values[index(Parameter::ExpressionToe)] = .000000013f;
  require(expressionEndpointValue(values) == values[index(Parameter::ExpressionToe)], "scalar toe is bit-exact");
}

void volumeAndDry() {
  const nlohmann::json params{{"dry_level", .3}, {"down2_level", .2}, {"down1_level", .2},
    {"fifth_level", .2}, {"up1_level", .2}, {"up2_level", .2}, {"input_gain", .6}, {"master_level", .25},
    {"attack", .3}, {"dry_attack", 1}, {"dry_filter", 1}, {"dry_detune", 1}, {"detune", .7}, {"spread", .15},
    {"filter_frequency", .7}, {"filter_q", .5}, {"focus", 1}};
  auto volumeParams = params;
  volumeParams["expression_mode"] = 1.0 / 6;
  volumeParams["expression_heel"] = .15; volumeParams["expression_toe"] = .85;
  volumeParams["expression_position"] = .4;
  Pog3Processor reference, volume;
  configure(reference, params); configure(volume, volumeParams);
  float maximumDryDifference = 0, maximumWetError = 0, largestAutomationStep = 0;
  PitchStereo priorError{};
  for (std::size_t i = 0; i < 72000; ++i) {
    if (i == 24001) { target(volume, Parameter::ExpressionHeel, 0); target(volume, Parameter::ExpressionPosition, 0); }
    if (i == 48001) target(volume, Parameter::ExpressionMode, 0);
    const auto x = inputAt(i);
    const auto r = reference.process(x), y = volume.process(x);
    maximumDryDifference = std::max(maximumDryDifference, std::max(std::fabs(y.dry.left - r.dry.left), std::fabs(y.dry.right - r.dry.right)));
    if (i < 24000) maximumWetError = std::max(maximumWetError, std::fabs(y.wet.left - .43f * r.wet.left));
    if (i > 24600 && i < 48000) require(y.wet.left == 0 && y.wet.right == 0, "Volume heel mutes filter ringing exactly");
    if (i > 48600) require(y.wet.left == r.wet.left && y.wet.right == r.wet.right, "Volume exit restores warm generated audio exactly");
    close(y.mixed.left, y.wet.left + y.dry.left, "mixed output applies Master once to each bus", 1e-7f);
    const PitchStereo error{y.wet.left - r.wet.left, y.wet.right - r.wet.right};
    if ((i > 23990 && i < 24700) || (i > 47990 && i < 48700))
      largestAutomationStep = std::max(largestAutomationStep,
        std::max(std::fabs(error.left - priorError.left), std::fabs(error.right - priorError.right)));
    priorError = error;
  }
  std::cout << "Volume processed-dry difference=" << maximumDryDifference << " wet gain error=" << maximumWetError
            << " largest automation error step=" << largestAutomationStep << '\n';
  require(maximumDryDifference == 0 && maximumWetError < 1e-7, "Volume scales only generated audio, including fully processed dry");
  require(largestAutomationStep < .003, "Volume entry/exit automation is bounded without clearing audio history");
  require(volume.healthy() && volume.deadlineMisses() == 0, "Volume path healthy");
}

double measuredFrequency(const std::vector<float>& x) {
  std::size_t count = 0;
  double first = 0, last = 0;
  for (std::size_t i = 12001; i < x.size(); ++i) if (x[i - 1] < 0 && x[i] >= 0) {
    const double at = i - 1 + x[i - 1] / (x[i - 1] - x[i]);
    if (count == 0) first = at;
    ++count; last = at;
  }
  require(count > 10, "enough settled Warp cycles");
  return (count - 1) * kSampleRate / (last - first);
}
void warpAudio() {
  Pog3Processor p;
  configure(p, {{"expression_mode", .5}, {"dry_level", .3}, {"down1_level", 0}, {"up1_level", 0}});
  double worstCents = 0;
  for (const bool focus : {false, true}) for (const float extent : {0.0f, .5f, 1.0f}) {
    target(p, Parameter::Focus, focus); target(p, Parameter::ExpressionPosition, extent);
    for (std::size_t voice = 1; voice < kVoiceCount; ++voice) {
      for (std::size_t v = 1; v < kVoiceCount; ++v) target(p, static_cast<Parameter>(index(Parameter::DryLevel) + v), v == voice ? 1 : 0);
      p.reset();
      std::vector<float> output(48000);
      for (std::size_t i = 0; i < output.size(); ++i) {
        const float x = .1 * std::sin(2 * pi * 196 * i / kSampleRate);
        const auto y = p.process({x, -.73f * x}); output[i] = y.wet.left;
        require(y.dry.left == .3f * x && y.dry.right == .3f * (-.73f * x), "Warp leaves raw dry unchanged");
      }
      const double expected = 196 * std::exp2(warpSemitones(voice, extent, focus) / 12);
      const double cents = 1200 * std::log2(measuredFrequency(output) / expected);
      worstCents = std::max(worstCents, std::fabs(cents));
      require(std::fabs(cents) < 3, "processor Warp produces every expected interval in both Focus settings");
      require(p.healthy() && p.deadlineMisses() == 0, "Warp path healthy");
    }
  }
  std::cout << "Processor Warp 30 cases worst tuning=" << worstCents << " cents\n";
  Pog3Processor reference, warped;
  nlohmann::json params{{"attack", .3}, {"dry_attack", 1}, {"dry_filter", 1}, {"dry_detune", 1},
    {"detune", .8}, {"spread", .3}, {"filter_frequency", .75}, {"focus", 1}};
  configure(reference, params); params["expression_mode"] = .5; params["expression_position"] = .2;
  configure(warped, params);
  for (std::size_t i = 0; i < 16000; ++i) {
    const auto a = reference.process(inputAt(i)), b = warped.process(inputAt(i));
    require(a.dry.left == b.dry.left && a.dry.right == b.dry.right, "Warp never retunes processed unison/dry");
  }
}

void filterAudio() {
  for (const float depth : {0.0f, 1.0f}) {
    Pog3Processor ordinary, differentVolumeAttack;
    nlohmann::json params{{"expression_mode", 4.0 / 6}, {"expression_heel", .2}, {"expression_toe", .8},
      {"expression_position", .25}, {"expression_reverse", 1}, {"filter_frequency", .1},
      {"filter_env", depth}, {"filter_attack", 0}, {"filter_decay", 0}};
    configure(ordinary, params); params["attack"] = 1; configure(differentVolumeAttack, params);
    const float base = physicalValue(Parameter::FilterFrequency, .65f);
    float extreme = base;
    for (std::size_t i = 0; i < 12000; ++i) {
      const float x = .1 * std::sin(2 * pi * 196 * i / kSampleRate);
      ordinary.process({x, -x}); differentVolumeAttack.process({x, -x});
      require(ordinary.filterEnvelope() == differentVolumeAttack.filterEnvelope()
              && ordinary.filterCutoff() == differentVolumeAttack.filterCutoff(), "Filter expression/AD remains independent of volume Attack");
      require(ordinary.filterCutoff() >= 40 && ordinary.filterCutoff() <= 20000, "Filter expression plus envelope is bounded");
      extreme = depth == 0 ? std::min(extreme, ordinary.filterCutoff()) : std::max(extreme, ordinary.filterCutoff());
    }
    close(ordinary.soundValues()[index(Parameter::FilterFrequency)], .65f, "Reverse resolves Filter endpoints in normalized space");
    require(depth == 0 ? extreme < base * .5f : extreme > base * 2, "expression-derived base still receives both envelope polarities");
    close(ordinary.filterCutoff(), base, "Filter sweep settles around expression-derived base", .1f);
    require(ordinary.baseValues()[index(Parameter::FilterFrequency)] == .1f, "Filter expression preserves stored base frequency");
  }
}

void partitionAndAutomation() {
  const auto render = [](std::size_t chunk) {
    Pog3Processor p;
    configure(p, {{"dry_level", .3}, {"down2_level", .2}, {"fifth_level", .2}, {"up2_level", .2},
      {"crossfade_heel", {{"up1_pan", 0}, {"filter_frequency", .2}, {"spread", 0}, {"attack", .1}}},
      {"crossfade_toe", {{"up1_pan", 1}, {"filter_frequency", .8}, {"spread", .8}, {"attack", .4}}}});
    std::vector<VoiceStageOutput> output(24000);
    for (std::size_t start = 0; start < output.size(); start += chunk)
      for (std::size_t i = start; i < std::min(start + chunk, output.size()); ++i) {
        if (i % 113 == 0) {
          require(p.setParameterTarget("expression_position", (i / 113) % 128 / 127.0f), "7-bit position key dispatch");
          target(p, Parameter::ExpressionMode, (i / 4000) % 5 / 6.0f);
          target(p, Parameter::ExpressionReverse, (i / 7000) % 2);
          target(p, Parameter::Focus, (i / 1500) % 2);
          target(p, Parameter::DryAttack, (i / 3000) % 2);
          target(p, Parameter::FilterEnv, (i / 9000) % 2);
          target(p, Parameter::Detune, .3f);
        }
        output[i] = p.process(inputAt(i));
        require(std::isfinite(output[i].mixed.left) && std::isfinite(output[i].mixed.right), "rapid mode/reverse/Focus/scene automation remains finite");
      }
    require(p.controlUpdates() == (output.size() + 47) / 48, "control cadence is sample-owned across callbacks");
    require(p.healthy() && p.deadlineMisses() == 0, "expression automation keeps spectra and scheduling healthy");
    return output;
  };
  const auto reference = render(1);
  for (const auto chunk : {17U, 48U, 64U, 128U, 256U}) {
    const auto split = render(chunk);
    for (std::size_t i = 0; i < split.size(); ++i) {
      require(split[i].mixed.left == reference[i].mixed.left && split[i].mixed.right == reference[i].mixed.right
        && split[i].wet.left == reference[i].wet.left && split[i].wet.right == reference[i].wet.right
        && split[i].dry.left == reference[i].dry.left && split[i].dry.right == reference[i].dry.right,
        "expression audio is bit-identical across callback partitions with matching event times");
    }
  }
}

void render(const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);
  std::vector<PitchStereo> source(48000 * 4);
  for (std::size_t i = 0; i < 48000 * 3; ++i) source[i] = inputAt(i);
  pog3_test::writeRender(directory / "expression-source.wav", source);
  for (const auto name : {"volume", "crossfade", "warp-short", "warp-focused", "filter", "off"}) {
    const std::string_view scene{name};
    nlohmann::json params{{"dry_level", .3}, {"down2_level", .15}, {"down1_level", .2}, {"fifth_level", .15},
      {"up1_level", .2}, {"up2_level", .15}, {"attack", .2}, {"dry_attack", 1}, {"dry_detune", 1},
      {"detune", .3}, {"spread", .2}, {"focus", scene != "warp-short" ? 1 : 0}, {"expression_position", 0},
      {"crossfade_heel", {{"dry_level", .3}, {"up1_level", .05}, {"up2_pan", 0}, {"filter_frequency", .2}}},
      {"crossfade_toe", {{"dry_level", .15}, {"up1_level", .5}, {"up2_pan", 1}, {"filter_frequency", 1}}}};
    params["expression_mode"] = scene == "volume" ? 1.0 / 6 : scene == "crossfade" ? 2.0 / 6
      : scene == "warp-short" || scene == "warp-focused" ? .5 : scene == "filter" ? 4.0 / 6 : 0;
    if (scene == "filter") {
      params["filter_env"] = .8; params["filter_attack"] = .2; params["filter_decay"] = .4;
      params["expression_heel"] = .2; params["expression_toe"] = .9; params["filter_q"] = .6;
    }
    Pog3Processor p; configure(p, params);
    std::vector<PitchStereo> output; output.reserve(source.size());
    for (std::size_t i = 0; i < source.size(); ++i) {
      if (i % 480 == 0) {
        const auto digit = (i / 480) % 256;
        target(p, Parameter::ExpressionPosition, (digit <= 127 ? digit : 255 - digit) / 127.0f);
        if (i == 96000) target(p, Parameter::ExpressionReverse, 1);
      }
      output.push_back(p.process(source[i]).mixed);
    }
    require(p.healthy() && p.deadlineMisses() == 0, "expression render healthy");
    pog3_test::writeRender(directory / ("expression-" + std::string{name} + ".wav"), output);
  }
}
}

int main(int argc, char** argv) {
  try {
    if (argc != 1 && !(argc == 3 && std::string_view(argv[1]) == "--render"))
      throw std::runtime_error("usage: pedal-pog3-expression-quality [--render directory]");
    publicationAndLifecycle(); ownershipAndEndpoints(); volumeAndDry(); warpAudio(); filterAudio(); partitionAndAutomation();
    std::cout << "POG3 expression publication, ownership, Volume, Crossfade, Warp, Filter and lifecycle gates passed\n";
    if (argc == 3) render(argv[2]);
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
