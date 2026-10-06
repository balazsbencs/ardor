#include "daisyfx/pog3/Pog3Parameters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ardor::pog3 {
namespace {
constexpr ParameterSpec linear(std::string_view key, std::string_view label, float value,
                               float minimum = 0, float maximum = 1, bool morph = true) {
  return {key, label, value, Scale::Linear, minimum, maximum, 0, morph};
}
constexpr ParameterSpec pan(std::string_view key, std::string_view label) {
  return {key, label, .5f, Scale::Pan, -1, 1, 0, true};
}
constexpr ParameterSpec logarithmic(std::string_view key, std::string_view label, float value,
                                    float minimum, float maximum) {
  return {key, label, value, Scale::Logarithmic, minimum, maximum, 0, true};
}
constexpr ParameterSpec choice(std::string_view key, std::string_view label, std::size_t count) {
  return {key, label, 0, Scale::Choice, 0, static_cast<float>(count - 1), count, false};
}
constexpr std::array<ParameterSpec, kParameterCount> kSpecs{
  linear("input_gain", "Input Gain", .2f, .5f, 3),
  linear("dry_level", "Dry", 1),
  linear("down2_level", "−2 Oct", 0),
  linear("down1_level", "−1 Oct", .3f),
  linear("fifth_level", "+5th", 0),
  linear("up1_level", "+1 Oct", .25f),
  linear("up2_level", "+2 Oct", 0),
  pan("dry_pan", "Dry Pan"), pan("down2_pan", "−2 Pan"), pan("down1_pan", "−1 Pan"),
  pan("fifth_pan", "+5th Pan"), pan("up1_pan", "+1 Pan"), pan("up2_pan", "+2 Pan"),
  linear("master_level", "Master", .5f, 0, 2),
  {"attack", "Attack", 0, Scale::Attack, 0, 3, 0, true},
  choice("dry_attack", "Dry Attack", 2),
  logarithmic("filter_frequency", "Filter", 1, 40, 20000),
  choice("filter_mode", "Filter Type", 3),
  logarithmic("filter_q", "Resonance", 0, .70710678f, 8),
  linear("filter_env", "Envelope", .5f, -6, 6),
  logarithmic("filter_attack", "Sweep Attack", .5f, .005f, 3),
  logarithmic("filter_decay", "Sweep Decay", .5f, .020f, 3),
  linear("trigger_sensitivity", "Sensitivity", .5f),
  choice("dry_filter", "Dry Filter", 2),
  linear("detune", "Detune", 0), linear("spread", "Spread", 0, 0, .15f),
  choice("dry_detune", "Dry Detune / Spread", 2), choice("focus", "Focus", 2),
  choice("expression_mode", "Expression", 7),
  linear("expression_position", "Pedal", 1, 0, 1, false),
  linear("expression_heel", "Heel", 0, 0, 1, false),
  linear("expression_toe", "Toe", 1, 0, 1, false),
  choice("expression_reverse", "Reverse", 2),
};

float unit(float value) noexcept {
  return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}
bool readNumber(const nlohmann::json& value, float& result) {
  if (!value.is_number()) return false;
  const double number = value.get<double>();
  if (!std::isfinite(number)) return false;
  result = static_cast<float>(std::clamp(number, 0.0, 1.0));
  return true;
}
} // namespace

const std::array<ParameterSpec, kParameterCount>& parameterSpecs() noexcept { return kSpecs; }

std::optional<Parameter> findParameter(std::string_view key) noexcept {
  for (std::size_t i = 0; i < kSpecs.size(); ++i)
    if (key == kSpecs[i].key) return static_cast<Parameter>(i);
  return std::nullopt;
}

Values defaultValues() noexcept {
  Values values{};
  for (std::size_t i = 0; i < values.size(); ++i) values[i] = kSpecs[i].defaultValue;
  return values;
}

int choiceIndex(float normalized, std::size_t count) noexcept {
  if (count <= 1) return 0;
  return static_cast<int>(std::floor(unit(normalized) * static_cast<float>(count - 1) + .5f));
}

float physicalValue(Parameter parameter, float normalized) noexcept {
  if (index(parameter) >= kSpecs.size()) return 0;
  const auto& spec = kSpecs[index(parameter)];
  const float u = unit(normalized);
  switch (spec.scale) {
    case Scale::Choice: return static_cast<float>(choiceIndex(u, spec.choices));
    case Scale::Attack: return spec.maximum * u * u;
    case Scale::Logarithmic: return spec.minimum * std::pow(spec.maximum / spec.minimum, u);
    case Scale::Linear:
    case Scale::Pan: return spec.minimum + (spec.maximum - spec.minimum) * u;
  }
  return 0;
}

std::string formatValue(Parameter parameter, float normalized) {
  if (index(parameter) >= kSpecs.size()) return {};
  const auto& spec = kSpecs[index(parameter)];
  const float value = physicalValue(parameter, normalized);
  if (spec.choices) {
    const auto selected = static_cast<std::size_t>(value);
    if (parameter == Parameter::ExpressionMode) {
      constexpr std::array<std::string_view, 7> labels{
        "Off", "Volume", "Crossfade", "Warp", "Filter", "Freeze + Gliss", "Freeze + Volume"};
      return std::string(labels[selected]);
    }
    if (parameter == Parameter::FilterMode) {
      constexpr std::array<std::string_view, 3> labels{"Low-pass", "Band-pass", "High-pass"};
      return std::string(labels[selected]);
    }
    return selected == 0 ? "Off" : "On";
  }
  char text[64];
  if (spec.scale == Scale::Pan) {
    if (std::fabs(value) < .005f) return "Center";
    std::snprintf(text, sizeof text, "%s %.0f%%", value < 0 ? "L" : "R", std::fabs(value) * 100);
  } else if (parameter == Parameter::InputGain) {
    std::snprintf(text, sizeof text, "%.2fx", value);
  } else if (parameter == Parameter::MasterLevel) {
    if (value == 0) return "Mute";
    std::snprintf(text, sizeof text, "%.1f dB", 20.0f * std::log10(value));
  } else if (parameter == Parameter::FilterFrequency) {
    std::snprintf(text, sizeof text, "%.0f Hz", value);
  } else if (parameter == Parameter::FilterQ) {
    std::snprintf(text, sizeof text, "Q %.2f", value);
  } else if (parameter == Parameter::FilterEnv) {
    if (std::fabs(value) < .00001f) return "Off";
    std::snprintf(text, sizeof text, "%+.2f oct", value);
  } else if (parameter == Parameter::Attack || parameter == Parameter::FilterAttack
             || parameter == Parameter::FilterDecay || parameter == Parameter::Spread) {
    if (value == 0) return "Off";
    std::snprintf(text, sizeof text, "%.0f ms", value * 1000);
  } else {
    std::snprintf(text, sizeof text, "%.0f%%", unit(normalized) * 100);
  }
  return text;
}

bool parseConfiguration(const nlohmann::json& params, Configuration& result, std::string& error) {
  error.clear();
  if (!params.is_object()) { error = "POG3 parameters must be an object"; return false; }
  Configuration next;
  next.base = defaultValues();
  for (std::size_t i = 0; i < kSpecs.size(); ++i) {
    const auto found = params.find(std::string(kSpecs[i].key));
    if (found != params.end() && !readNumber(*found, next.base[i])) {
      error = "POG3 parameter must be finite and numeric: " + std::string(kSpecs[i].key);
      return false;
    }
  }
  next.heel = next.toe = next.base;
  for (const auto* key : {"crossfade_heel", "crossfade_toe"}) {
    const auto found = params.find(key);
    if (found == params.end()) continue;
    if (!found->is_object()) { error = std::string("POG3 endpoint must be an object: ") + key; return false; }
    auto& values = std::string_view(key) == "crossfade_heel" ? next.heel : next.toe;
    for (auto it = found->begin(); it != found->end(); ++it) {
      const auto parameter = findParameter(it.key());
      if (!parameter) continue; // Forward-compatible editor data does not control audio.
      const auto i = index(*parameter);
      if (!kSpecs[i].crossfade) {
        error = "POG3 endpoint cannot morph: " + it.key();
        return false;
      }
      if (!readNumber(*it, values[i])) { error = "POG3 endpoint must be finite and numeric: " + it.key(); return false; }
      next.morphed.set(i);
    }
  }
  result = next; // Failed configuration never partially mutates the caller.
  return true;
}

float expressionPosition(const Values& values) noexcept {
  const float position = unit(values[index(Parameter::ExpressionPosition)]);
  return choiceIndex(values[index(Parameter::ExpressionReverse)], 2) ? 1 - position : position;
}

Values effectiveValues(const Configuration& config, const Values& base) noexcept {
  auto values = base;
  const auto mode = static_cast<ExpressionMode>(choiceIndex(base[index(Parameter::ExpressionMode)], 7));
  const float position = expressionPosition(base);
  if (mode == ExpressionMode::Crossfade) {
    for (std::size_t i = 0; i < values.size(); ++i)
      if (config.morphed[i]) values[i] = config.heel[i] + position * (config.toe[i] - config.heel[i]);
  } else if (mode == ExpressionMode::Filter) {
    values[index(Parameter::FilterFrequency)] = base[index(Parameter::ExpressionHeel)]
      + position * (base[index(Parameter::ExpressionToe)] - base[index(Parameter::ExpressionHeel)]);
  }
  return values;
}

float warpSemitones(std::size_t voice, float extent, bool focus) noexcept {
  if (voice >= kVoiceCount) return 0;
  return kVoiceSemitones[voice] * ((voice >= 4 && !focus) ? 1.0f : unit(extent));
}

bool dryFreezeEligible(const Values& values) noexcept {
  return choiceIndex(values[index(Parameter::DryAttack)], 2) != 0
    && values[index(Parameter::Attack)] > .1f;
}

ParameterTargets::ParameterTargets() noexcept { (void)store(defaultValues()); }

bool ParameterTargets::setTarget(std::string_view key, float normalized) noexcept {
  const auto parameter = findParameter(key);
  return parameter && setTarget(index(*parameter), normalized);
}

bool ParameterTargets::setTarget(std::size_t parameterIndex, float normalized) noexcept {
  if (parameterIndex >= targets_.size() || !std::isfinite(normalized)) return false;
  targets_[parameterIndex].store(unit(normalized), std::memory_order_relaxed);
  return true;
}

bool ParameterTargets::store(const Values& values) noexcept {
  for (float value : values) if (!std::isfinite(value)) return false;
  for (std::size_t i = 0; i < targets_.size(); ++i)
    targets_[i].store(unit(values[i]), std::memory_order_relaxed);
  return true;
}

Values ParameterTargets::read() const noexcept {
  Values values{};
  for (std::size_t i = 0; i < targets_.size(); ++i)
    values[i] = targets_[i].load(std::memory_order_relaxed);
  return values;
}

} // namespace ardor::pog3
