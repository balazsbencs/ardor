#pragma once

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ardor::pog3 {

// Persisted descriptor/scene indexes. Append future parameters; never reorder.
enum class Parameter : std::size_t {
  InputGain, DryLevel, Down2Level, Down1Level, FifthLevel, Up1Level, Up2Level,
  DryPan, Down2Pan, Down1Pan, FifthPan, Up1Pan, Up2Pan, MasterLevel,
  Attack, DryAttack, FilterFrequency, FilterMode, FilterQ, FilterEnv,
  FilterAttack, FilterDecay, TriggerSensitivity, DryFilter, Detune, Spread,
  DryDetune, Focus, ExpressionMode, ExpressionPosition, ExpressionHeel,
  ExpressionToe, ExpressionReverse, Count
};
inline constexpr std::size_t kParameterCount = static_cast<std::size_t>(Parameter::Count);
inline constexpr std::size_t kVoiceCount = 6;
inline constexpr float kSampleRate = 48000.0f;
inline constexpr std::array<float, kVoiceCount> kVoiceSemitones{0, -24, -12, 7, 12, 24};

enum class ExpressionMode { Off, Volume, Crossfade, Warp, Filter, FreezeGliss, FreezeVolume };
enum class Scale { Linear, Logarithmic, Attack, Pan, Choice };

struct ParameterSpec {
  std::string_view key;
  std::string_view label;
  float defaultValue;
  Scale scale;
  float minimum;
  float maximum;
  std::size_t choices;
  bool crossfade;
};

using Values = std::array<float, kParameterCount>;
struct Configuration {
  Values base{};
  Values heel{};
  Values toe{};
  std::bitset<kParameterCount> morphed;
};

constexpr std::size_t index(Parameter parameter) noexcept { return static_cast<std::size_t>(parameter); }
const std::array<ParameterSpec, kParameterCount>& parameterSpecs() noexcept;
std::optional<Parameter> findParameter(std::string_view key) noexcept;
Values defaultValues() noexcept;
int choiceIndex(float normalized, std::size_t count) noexcept;
float physicalValue(Parameter parameter, float normalized) noexcept;
std::string formatValue(Parameter parameter, float normalized);
std::string formatExpressionEndpoint(ExpressionMode mode, float normalized);
bool parseConfiguration(const nlohmann::json& params, Configuration& result, std::string& error);
Values effectiveValues(const Configuration& config, const Values& base) noexcept;
float expressionPosition(const Values& values) noexcept;
float expressionEndpointValue(const Values& values) noexcept;
float warpSemitones(std::size_t voice, float extent, bool focus) noexcept;
bool dryFreezeEligible(const Values& values) noexcept;

// Per-control publication only; a read is not an atomic whole-scene snapshot.
// Configure/store occurs off the callback; the processor reads at its
// control cadence. Key and index setters share exactly the same validation.
class ParameterTargets {
public:
  ParameterTargets() noexcept;
  bool setTarget(std::string_view key, float normalized) noexcept;
  bool setTarget(std::size_t parameterIndex, float normalized) noexcept;
  bool store(const Values& values) noexcept;
  Values read() const noexcept;

private:
  static_assert(std::atomic<float>::is_always_lock_free,
                "POG3 requires lock-free parameter publication");
  std::array<std::atomic<float>, kParameterCount> targets_{};
};

} // namespace ardor::pog3
