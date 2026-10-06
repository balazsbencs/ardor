#include "daisyfx/pog3/Pog3Parameters.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void close(float actual, float expected, const char* message, float tolerance = 1e-5f) {
  require(std::fabs(actual - expected) < tolerance, message);
}
}

int main() {
  try {
    using namespace ardor::pog3;
    // This list is an independent persisted ABI assertion, not generated from
    // the registry: accidental reorder/reassignment must fail the test.
    constexpr std::array<std::string_view, 33> expected{
      "input_gain", "dry_level", "down2_level", "down1_level", "fifth_level", "up1_level", "up2_level",
      "dry_pan", "down2_pan", "down1_pan", "fifth_pan", "up1_pan", "up2_pan", "master_level",
      "attack", "dry_attack", "filter_frequency", "filter_mode", "filter_q", "filter_env",
      "filter_attack", "filter_decay", "trigger_sensitivity", "dry_filter", "detune", "spread",
      "dry_detune", "focus", "expression_mode", "expression_position", "expression_heel",
      "expression_toe", "expression_reverse"};
    require(parameterSpecs().size() == expected.size(), "POG3 has 33 parameters");
    for (std::size_t i = 0; i < expected.size(); ++i) {
      require(parameterSpecs()[i].key == expected[i], "parameter ABI order");
      require(findParameter(expected[i]) == static_cast<Parameter>(i), "key resolves to its stable index");
      for (float u : {0.0f, parameterSpecs()[i].defaultValue, 1.0f})
        require(!formatValue(static_cast<Parameter>(i), u).empty(), "format available at endpoints/default");
    }
    require(!findParameter("p1") && !findParameter("mix") && !findParameter("bogus"), "no legacy aliases");
    ParameterTargets byKey, byIndex;
    require(byKey.read() == defaultValues(), "target publication initializes all defaults");
    for (std::size_t i = 0; i < expected.size(); ++i) {
      for (float u : {-.5f, 0.0f, .123f, 1.0f, 1.5f}) {
        require(byKey.setTarget(expected[i], u) && byIndex.setTarget(i, u), "all stable setters accepted");
        require(byKey.read() == byIndex.read(), "key and index target paths agree");
        close(byKey.read()[i], std::clamp(u, 0.0f, 1.0f), "finite publication is clamped");
      }
    }
    const auto beforeInvalid = byKey.read();
    for (float u : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
      require(!byKey.setTarget("input_gain", u) && !byKey.setTarget(32, u), "reject nonfinite publication");
    require(!byKey.setTarget("mix", .5f) && !byKey.setTarget(33, .5f)
            && !byKey.setTarget(std::numeric_limits<std::size_t>::max(), .5f), "reject unknown targets");
    auto invalidTargets = defaultValues();
    invalidTargets.back() = std::numeric_limits<float>::quiet_NaN();
    require(!byKey.store(invalidTargets) && byKey.read() == beforeInvalid,
            "invalid bulk publication changes no target");
    require(byKey.store(defaultValues()) && byKey.read() == defaultValues(), "validated bulk publication");
    close(physicalValue(Parameter::InputGain, .2f), 1, "unity input default");
    close(physicalValue(Parameter::MasterLevel, .5f), 1, "unity master default");
    close(physicalValue(Parameter::Attack, 1), 3, "three-second maximum attack");
    close(physicalValue(Parameter::Attack, .5f), .75f, "square attack taper");
    close(physicalValue(Parameter::FilterFrequency, 0), 40, "minimum cutoff");
    close(physicalValue(Parameter::FilterFrequency, 1), 20000, "maximum cutoff", .01f);
    close(physicalValue(Parameter::FilterEnv, .5f), 0, "neutral filter envelope");
    require(formatValue(Parameter::MasterLevel, 0) == "Mute", "zero master is explicit mute");
    require(choiceIndex(.249f, 3) == 0 && choiceIndex(.25f, 3) == 1
            && choiceIndex(.749f, 3) == 1 && choiceIndex(.75f, 3) == 2, "nearest-choice boundaries");
    require(choiceIndex(.5f, 2) == 1, "binary tie goes On");

    Configuration config;
    std::string error;
    require(parseConfiguration(nlohmann::json::object(), config, error), "missing controls use defaults");
    require(config.base == defaultValues(), "all default values retained");
    config.base[index(Parameter::DryAttack)] = 1;
    for (float attack : {.099f, .100f, .101f}) {
      config.base[index(Parameter::Attack)] = attack;
      require(dryFreezeEligible(config.base) == (attack > .1f), "strict normalized dry-freeze threshold");
    }
    config.base[index(Parameter::DryAttack)] = 0;
    require(!dryFreezeEligible(config.base), "dry route must be enabled for freeze");

    const nlohmann::json snapshot{
      {"dry_level", .8}, {"expression_mode", 2.0 / 6},
      {"crossfade_heel", {{"dry_level", .1}, {"up1_pan", 0}}},
      {"crossfade_toe", {{"dry_level", .9}, {"input_gain", .6}}}};
    require(parseConfiguration(snapshot, config, error), "valid partial morph endpoints");
    auto base = config.base;
    base[index(Parameter::ExpressionPosition)] = 0;
    auto effective = effectiveValues(config, base);
    close(effective[index(Parameter::DryLevel)], .1f, "heel snapshot");
    close(effective[index(Parameter::InputGain)], .2f, "missing heel falls back to configured base");
    base[index(Parameter::ExpressionPosition)] = .5f;
    effective = effectiveValues(config, base);
    close(effective[index(Parameter::DryLevel)], .5f, "morph midpoint");
    close(effective[index(Parameter::Up1Pan)], .25f, "missing toe falls back to configured base");
    close(base[index(Parameter::DryLevel)], .8f, "effective values do not overwrite base");
    base[index(Parameter::ExpressionReverse)] = 1;
    base[index(Parameter::ExpressionPosition)] = 0;
    close(effectiveValues(config, base)[index(Parameter::DryLevel)], .9f, "Reverse swaps heel/toe");
    base[index(Parameter::ExpressionMode)] = 0;
    require(effectiveValues(config, base) == base, "Off ignores endpoints");

    base = defaultValues();
    base[index(Parameter::ExpressionMode)] = 4.0f / 6;
    base[index(Parameter::ExpressionHeel)] = .15f;
    base[index(Parameter::ExpressionToe)] = .85f;
    base[index(Parameter::ExpressionPosition)] = .5f;
    close(effectiveValues(config, base)[index(Parameter::FilterFrequency)], .5f, "expression filter base");
    constexpr std::array<float, 6> halfWarp{0, -12, -6, 3.5f, 6, 12};
    for (std::size_t i = 0; i < kVoiceCount; ++i) {
      close(warpSemitones(i, .5f, true), halfWarp[i], "proportional warp midpoint");
      close(warpSemitones(i, 0, true), 0, "warp heel collapses to unison");
      close(warpSemitones(i, 1, true), kVoiceSemitones[i], "warp toe restores nominal intervals");
    }
    close(warpSemitones(4, .5f, false), 12, "Focus-off upper octave fixed");
    close(warpSemitones(5, 0, false), 24, "Focus-off upper two fixed");
    close(warpSemitones(99, 1, true), 0, "out-of-range voice is bounded");

    Configuration previous = config;
    const std::array<nlohmann::json, 7> invalid{
      nullptr, nlohmann::json::array(), nlohmann::json{{"focus", true}},
      nlohmann::json{{"input_gain", "1"}}, nlohmann::json{{"crossfade_heel", 1}},
      nlohmann::json{{"crossfade_toe", {{"expression_mode", .5}}}},
      nlohmann::json{{"attack", std::numeric_limits<double>::infinity()}}};
    for (const auto& value : invalid) {
      require(!parseConfiguration(value, config, error) && !error.empty(), "invalid known value rejected");
      require(config.base == previous.base && config.heel == previous.heel
              && config.toe == previous.toe && config.morphed == previous.morphed, "failure is transactional");
    }
    require(parseConfiguration({{"input_gain", 1e100}, {"attack", -3}, {"future_data", "kept by editor"}}, config, error),
            "finite out-of-range and unknown data accepted");
    close(config.base[index(Parameter::InputGain)], 1, "clamp before float conversion avoids overflow");
    close(config.base[index(Parameter::Attack)], 0, "negative finite value clamps");
    std::cout << "POG3 parameter ABI, mappings, morph ownership, Warp and validation passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
