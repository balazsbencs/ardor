#include "daisyfx/DaisyFxCatalog.h"

#include "daisyfx/hosted/config/delay_mode_id.h"
#include "daisyfx/hosted/config/mod_mode_id.h"
#include "daisyfx/hosted/config/reverb_mode_id.h"
#include "daisyfx/hosted/params/delay_param_map.h"
#include "daisyfx/hosted/params/mod_param_map.h"
#include "daisyfx/hosted/params/param_range.h"
#include "daisyfx/hosted/params/reverb_param_map.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string_view>
#include <utility>

namespace ardor {

namespace {

std::vector<DaisyFxParamDescriptor> modParams()
{
  return {
    {"speed", "Speed", 0.35f},
    {"depth", "Depth", 0.70f},
    {"mix", "Mix", 1.0f},
    {"tone", "Tone", 0.50f},
    {"p1", "P1", 0.0f},
    {"p2", "P2", 0.0f},
    // The hosted range is 0..2 linear; normalized 0.5 is unity gain.
    {"level", "Level", 0.5f},
  };
}

std::vector<DaisyFxParamDescriptor> delayParams()
{
  return {
    {"time", "Time", 0.25f},
    {"repeats", "Repeats", 0.35f},
    {"mix", "Mix", 0.25f},
    {"filter", "Filter", 0.50f},
    {"grit", "Grit", 0.0f},
    {"mod_spd", "Mod Spd", 0.0f},
    {"mod_dep", "Mod Dep", 0.0f},
  };
}

std::vector<DaisyFxParamDescriptor> reverbParams()
{
  return {
    {"decay", "Decay", 0.45f},
    {"pre_delay", "Pre", 0.15f},
    {"mix", "Mix", 0.25f},
    {"tone", "Tone", 0.50f},
    {"mod", "Mod", 0.0f},
    {"param1", "P1", 0.50f},
    {"param2", "P2", 0.50f},
  };
}

DaisyFxDescriptor mod(std::string mode, std::string name, std::string p1 = "P1", std::string p2 = "P2",
                      float mix = 1.0f, std::string speed = "Speed", std::string depth = "Depth")
{
  auto params = modParams();
  params[0].label = std::move(speed);
  params[1].label = std::move(depth);
  params[2].defaultValue = mix;
  params[4].label = std::move(p1);
  params[5].label = std::move(p2);
  return {DaisyFxKind::Mod, "mod", std::move(mode), std::move(name), std::move(params)};
}

DaisyFxDescriptor delay(std::string mode, std::string name, std::string grit = "Grit",
                        std::string modSpeed = "Mod Rate", std::string modDepth = "Mod Depth",
                        std::string filter = "Filter")
{
  auto params = delayParams();
  params[3].label = std::move(filter);
  params[4].label = std::move(grit);
  params[5].label = std::move(modSpeed);
  params[6].label = std::move(modDepth);
  return {DaisyFxKind::Delay, "delay", std::move(mode), std::move(name), std::move(params)};
}

DaisyFxDescriptor reverb(std::string mode, std::string name, std::string param1 = "Param 1",
                         std::string param2 = "Param 2", float param1Default = 0.50f,
                         float param2Default = 0.50f, std::string mod = "Mod",
                         std::string preDelay = "Pre-delay")
{
  auto params = reverbParams();
  params[1].label = std::move(preDelay);
  params[4].label = std::move(mod);
  params[5].label = std::move(param1);
  params[6].label = std::move(param2);
  params[5].defaultValue = param1Default;
  params[6].defaultValue = param2Default;
  return {DaisyFxKind::Reverb, "reverb", std::move(mode), std::move(name), std::move(params)};
}

template <std::size_t N>
std::string choice(float normalized, const std::array<std::string_view, N>& choices)
{
  const auto index = std::min(static_cast<std::size_t>(std::clamp(normalized, 0.0f, 1.0f) * N), N - 1);
  return std::string(choices[index]);
}

std::string number(float value, int precision, std::string_view suffix = {})
{
  char buffer[48]{};
  std::snprintf(buffer, sizeof(buffer), "%.*f%.*s", precision, value,
                static_cast<int>(suffix.size()), suffix.data());
  return buffer;
}

std::string percent(float fraction)
{
  return number(std::clamp(fraction, 0.0f, 1.0f) * 100.0f, 0, "%");
}

std::string frequency(float hz)
{
  if (hz >= 1000.0f) return number(hz / 1000.0f, hz < 10000.0f ? 1 : 0, " kHz");
  if (hz < 1.0f) return number(hz, 2, " Hz");
  if (hz < 100.0f) return number(hz, 1, " Hz");
  return number(hz, 0, " Hz");
}

std::string milliseconds(float ms)
{
  if (ms < 100.0f) return number(ms, 1, " ms");
  return number(ms, 0, " ms");
}

std::string seconds(float seconds)
{
  if (seconds < 1.0f) return milliseconds(seconds * 1000.0f);
  if (seconds < 10.0f) return number(seconds, 2, " s");
  return number(seconds, 1, " s");
}

std::string decibels(float linear, std::string_view suffix = " dB")
{
  if (linear <= 0.000001f) return std::string{"-inf"} + std::string{suffix};
  return number(20.0f * std::log10(linear), 1, suffix);
}

std::string qValue(float q)
{
  return std::string{"Q "} + number(q, q < 10.0f ? 1 : 0);
}

std::string signedSemitones(float value)
{
  char buffer[24]{};
  const int precision = std::fabs(value - std::round(value)) < 0.01f ? 0 : 1;
  std::snprintf(buffer, sizeof(buffer), "%+.*f st", precision, value);
  return buffer;
}

std::vector<float> evenlySpacedChoices(std::size_t count)
{
  std::vector<float> values;
  values.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    values.push_back(count == 1 ? 0.0f : static_cast<float>(index) / static_cast<float>(count - 1));
  }
  return values;
}

std::string tone(float normalized, float sampleRate)
{
  normalized = std::clamp(normalized, 0.0f, 1.0f);
  if (std::fabs(normalized - 0.5f) < 0.001f) return "Flat";
  const float maxCutoff = std::min(20000.0f, sampleRate * 0.45f);
  if (normalized < 0.5f) {
    const float amount = 1.0f - normalized * 2.0f;
    const float cutoff = std::exp(std::log(maxCutoff) + amount * (std::log(200.0f) - std::log(maxCutoff)));
    return std::string{"LP "} + frequency(cutoff);
  }
  const float amount = (normalized - 0.5f) * 2.0f;
  const float cutoff = std::exp(std::log(20.0f) + amount * (std::log(3000.0f) - std::log(20.0f)));
  return std::string{"HP "} + frequency(cutoff);
}

pedal::ModModeId modMode(std::string_view mode)
{
  if (mode == "chorus") return pedal::ModModeId::Chorus;
  if (mode == "flanger") return pedal::ModModeId::Flanger;
  if (mode == "rotary") return pedal::ModModeId::Rotary;
  if (mode == "vibe") return pedal::ModModeId::Vibe;
  if (mode == "phaser") return pedal::ModModeId::Phaser;
  if (mode == "vintage_trem") return pedal::ModModeId::VintTrem;
  if (mode == "poly_octave") return pedal::ModModeId::PolyOctave;
  if (mode == "pattern_trem") return pedal::ModModeId::PatternTrem;
  if (mode == "auto_swell") return pedal::ModModeId::AutoSwell;
  if (mode == "filter") return pedal::ModModeId::FilterFx;
  if (mode == "formant") return pedal::ModModeId::FormantFx;
  if (mode == "quadrature") return pedal::ModModeId::Quadrature;
  return pedal::ModModeId::Destroyer;
}

pedal::DelayModeId delayMode(std::string_view mode)
{
  if (mode == "digital") return pedal::DelayModeId::Digital;
  if (mode == "tape") return pedal::DelayModeId::Tape;
  if (mode == "dual") return pedal::DelayModeId::Dual;
  if (mode == "filter") return pedal::DelayModeId::Filter;
  if (mode == "lofi") return pedal::DelayModeId::Lofi;
  if (mode == "dbucket") return pedal::DelayModeId::DBucket;
  if (mode == "duck") return pedal::DelayModeId::Duck;
  if (mode == "pattern") return pedal::DelayModeId::Pattern;
  if (mode == "swell") return pedal::DelayModeId::Swell;
  return pedal::DelayModeId::Trem;
}

pedal::ReverbModeId reverbMode(std::string_view mode)
{
  if (mode == "room") return pedal::ReverbModeId::Room;
  if (mode == "hall") return pedal::ReverbModeId::Hall;
  if (mode == "plate") return pedal::ReverbModeId::Plate;
  if (mode == "spring") return pedal::ReverbModeId::Spring;
  if (mode == "bloom") return pedal::ReverbModeId::Bloom;
  if (mode == "cloud") return pedal::ReverbModeId::Cloud;
  if (mode == "shimmer") return pedal::ReverbModeId::Shimmer;
  if (mode == "chorale") return pedal::ReverbModeId::Chorale;
  if (mode == "nonlinear") return pedal::ReverbModeId::Nonlinear;
  if (mode == "swell") return pedal::ReverbModeId::Swell;
  if (mode == "magneto") return pedal::ReverbModeId::Magneto;
  return pedal::ReverbModeId::Reflections;
}

float mappedMod(float normalized, std::string_view mode, pedal::mod_fx::ParamId id)
{
  return pedal::map_param(normalized, pedal::mod_fx::get_param_range(modMode(mode), id));
}

float mappedDelay(float normalized, std::string_view mode, pedal::delay_fx::ParamId id)
{
  return pedal::map_param(normalized, pedal::delay_fx::get_param_range(delayMode(mode), id));
}

float mappedReverb(float normalized, std::string_view mode, pedal::reverb_fx::ParamId id)
{
  return pedal::map_param(normalized, pedal::reverb_fx::get_param_range(reverbMode(mode), id));
}

std::string formatMod(std::string_view mode, std::string_view key, float normalized)
{
  using Id = pedal::mod_fx::ParamId;
  if (key == "speed") {
    const float physical = mappedMod(normalized, mode, Id::Speed);
    if (mode == "pattern_trem") return number(physical * 60.0f, 0, " BPM");
    if (mode == "auto_swell") return milliseconds(physical * 1000.0f);
    if (mode == "destroyer") return number(physical, physical < 10.0f ? 1 : 0, "x");
    if (mode == "poly_octave") return percent((physical - 0.05f) / 9.95f);
    return frequency(physical);
  }
  if (key == "depth") {
    if (mode == "destroyer") return number(16.0f - static_cast<int>(normalized * 15.0f), 0, " bit");
    if (mode == "auto_swell") return number(20.0f * std::log10(1.0f + normalized), 1, " dB");
    if (mode == "quadrature") return std::string{"+/-"} + frequency(normalized * 80.0f);
    return percent(normalized);
  }
  if (key == "mix") return percent(normalized);
  if (key == "tone") {
    if (mode == "filter") return frequency(80.0f + normalized * 11920.0f);
    if (mode == "destroyer") return frequency(80.0f + normalized * (48000.0f * 0.45f - 80.0f));
    if (mode == "rotary") return frequency(500.0f + normalized * 1500.0f);
    if (mode == "phaser") return frequency(300.0f * std::pow(10000.0f / 300.0f, normalized));
    if (mode == "flanger" || mode == "chorus") return percent(normalized);
    return tone(normalized, 48000.0f);
  }
  if (key == "p1") {
    if (mode == "flanger" || mode == "phaser") return percent(normalized * 0.95f);
    if (mode == "vibe") return percent(normalized * 0.70f);
    if (mode == "filter") return qValue(0.5f + normalized * 19.5f);
    if (mode == "formant") return qValue(2.0f + normalized * 8.0f);
    if (mode == "destroyer") return qValue(0.5f + normalized * 8.0f);
    if (mode == "pattern_trem") return "Pattern " + std::to_string(std::min(16, static_cast<int>(normalized * 16.0f) + 1));
    if (mode == "auto_swell") return milliseconds(50.0f + normalized * 1950.0f);
    if (mode == "rotary") return number(1.0f + normalized * normalized * 0.6f, 1, "x");
    return percent(normalized);
  }
  if (key == "p2") {
    if (mode == "chorus") return choice(normalized, std::array<std::string_view, 5>{"dBucket", "Multi", "Vibrato", "Detune", "Digital"});
    if (mode == "flanger") return choice(normalized, std::array<std::string_view, 6>{"Silver", "Grey", "Black+", "Black-", "Zero+", "Zero-"});
    if (mode == "rotary") return normalized < 0.5f ? "Slow" : "Fast";
    if (mode == "phaser") return choice(normalized, std::array<std::string_view, 7>{"2 stages", "4 stages", "6 stages", "8 stages", "12 stages", "16 stages", "Barber pole"});
    if (mode == "vintage_trem") return choice(normalized, std::array<std::string_view, 3>{"Tube", "Harmonic", "Photoresistor"});
    if (mode == "pattern_trem") return choice(normalized, std::array<std::string_view, 3>{"16th", "8th", "Triplet"});
    if (mode == "filter") return choice(normalized, std::array<std::string_view, 8>{"Sine", "Triangle", "Square", "Ramp up", "Ramp down", "Sample & hold", "Envelope+", "Envelope-"});
    if (mode == "formant") return choice(normalized, std::array<std::string_view, 7>{"Ah", "Oh", "Oo", "Ee", "Ay", "Ah-Oh", "Oo-Oh"});
    if (mode == "quadrature") return choice(normalized, std::array<std::string_view, 4>{"AM", "FM", "Shift +", "Shift -"});
    if (mode == "auto_swell") return percent(normalized * 0.30f);
    return percent(normalized);
  }
  if (key == "level") return decibels(mappedMod(normalized, mode, Id::Level));
  return percent(normalized);
}

std::string modulationDepth(std::string_view mode, float normalized)
{
  float samples = 0.0f;
  if (mode == "digital") samples = 30.0f;
  else if (mode == "tape") samples = 50.0f;
  else if (mode == "filter") samples = 1500.0f;
  else if (mode == "lofi" || mode == "dbucket") samples = 20.0f;
  else if (mode == "duck") samples = 15.0f;
  else if (mode == "pattern") samples = 25.0f;
  if (samples > 0.0f) return milliseconds(normalized * samples * 1000.0f / 48000.0f);
  if (mode == "dual") return number(normalized * 0.5f, 2, "%");
  return percent(normalized);
}

std::string formatDelay(std::string_view mode, std::string_view key, float normalized)
{
  using Id = pedal::delay_fx::ParamId;
  if (key == "time") return milliseconds(mappedDelay(normalized, mode, Id::Time) * 1000.0f);
  if (key == "repeats") return percent(mappedDelay(normalized, mode, Id::Repeats));
  if (key == "mix") return percent(normalized);
  if (key == "filter") {
    if (mode == "filter") return qValue(0.5f + normalized * 14.5f);
    if (mode == "lofi" || mode == "dbucket") return percent(normalized);
    return tone(normalized, 48000.0f);
  }
  if (key == "grit") {
    if (mode == "filter") return choice(normalized, std::array<std::string_view, 3>{"Low-pass", "Band-pass", "High-pass"});
    if (mode == "pattern") return choice(normalized, std::array<std::string_view, 3>{"Straight", "Dotted 8th", "Triplet"});
    if (mode == "lofi") {
      const int bits = 16 - static_cast<int>(normalized * 12.0f);
      return std::to_string(bits) + " bit / " + number(1.0f + normalized * 15.0f, 1, "x");
    }
    if (mode == "swell") return number(20.0f * std::log10(0.05f + normalized * 0.20f), 1, " dBFS");
    return percent(normalized);
  }
  if (key == "mod_spd") {
    if (mode == "swell") return seconds(1.5f - 1.48f * normalized * normalized);
    return frequency(mappedDelay(normalized, mode, Id::ModSpd));
  }
  if (key == "mod_dep") {
    if (mode == "swell") return seconds(2.5f - 2.42f * normalized);
    return modulationDepth(mode, normalized);
  }
  return percent(normalized);
}

std::string formatReverb(std::string_view mode, std::string_view key, float normalized)
{
  using Id = pedal::reverb_fx::ParamId;
  if (key == "decay") {
    const float physical = mappedReverb(normalized, mode, Id::Decay);
    return mode == "reflections" ? percent(physical) : seconds(physical);
  }
  if (key == "pre_delay") {
    const float physical = mappedReverb(normalized, mode, Id::PreDelay);
    return mode == "magneto" ? percent(physical) : milliseconds(physical * 1000.0f);
  }
  if (key == "mix") return percent(normalized);
  if (key == "tone") return tone(normalized, 24000.0f);
  if (key == "mod") {
    if (mode == "plate") return frequency(0.3f + normalized * 1.7f);
    if (mode == "magneto") return percent(0.4f + normalized * 0.4f);
    if (mode == "reflections") return frequency(0.05f + normalized * 0.45f);
    if (mode == "shimmer") return percent(normalized);
    const float samples = (mode == "spring" || mode == "chorale" || mode == "swell") ? 4.0f : 8.0f;
    return milliseconds(normalized * samples * 1000.0f / 24000.0f);
  }
  if (key == "param1") {
    const float physical = mappedReverb(normalized, mode, Id::Param1);
    if (mode == "bloom" || mode == "swell") return seconds(physical);
    if (mode == "shimmer") return signedSemitones(physical);
    if (mode == "chorale") {
      const auto vowel = std::clamp(static_cast<int>(std::lround(physical)), 0, 6);
      return std::array<std::string, 7>{"Ah", "Oh", "Oo", "Ee", "Ay", "Ah-Oh", "Oo-Oh"}[vowel];
    }
    if (mode == "spring") return number(std::pow(2.0f, normalized * 3.0f), 1, "x");
    if (mode == "hall") return percent(0.35f + normalized * 0.45f);
    if (mode == "plate") return milliseconds((4.0f + normalized * 16.0f) * 1000.0f / 24000.0f);
    if (mode == "cloud") return percent(0.5f + normalized * 0.35f);
    if (mode == "nonlinear") return choice(normalized, std::array<std::string_view, 6>{"Swoosh", "Reverse", "Ramp", "Gate", "Gauss", "Bounce"});
    if (mode == "magneto") {
      if (physical < 3.4f) return "3 heads";
      if (physical <= 5.1f) return "4 heads";
      return "6 heads";
    }
    return percent(normalized);
  }
  if (key == "param2") {
    const float physical = mappedReverb(normalized, mode, Id::Param2);
    if (mode == "bloom") return percent(physical);
    if (mode == "shimmer") return signedSemitones(physical);
    if (mode == "spring" || mode == "chorale") return choice(normalized, std::array<std::string_view, 3>{"Mild", "Medium", "High"});
    if (mode == "nonlinear") return percent(0.4f + normalized * 0.4f);
    if (mode == "swell") return normalized < 0.5f ? "Wet swell" : "Dry swell";
    if (mode == "magneto") return normalized <= 0.5f ? "Even" : "Golden";
    return percent(normalized);
  }
  return percent(normalized);
}

} // namespace

const std::vector<DaisyFxDescriptor>& daisyFxCatalog()
{
  static const std::vector<DaisyFxDescriptor> catalog{
    // Chorus' vendor mode returns wet signal only. A 50/50 default retains
    // dry signal and produces an actual chorus rather than vibrato.
    mod("chorus", "Chorus", "Delay", "Type", 0.5f),
    mod("flanger", "Flanger", "Regen", "Type", 0.5f),
    mod("rotary", "Rotary", "Drive", "Speed"),
    mod("vibe", "Vibe", "Regen", "Shape"),
    mod("phaser", "Phaser", "Regen", "Stages", 0.5f),
    mod("vintage_trem", "Vintage Trem", "Shape", "Type"),
    mod("poly_octave", "Poly Octave", "Oct Up", "Oct Down", 1.0f, "Tracking", "Oct Down 2"),
    mod("pattern_trem", "Pattern Trem", "Pattern", "Division", 1.0f, "Tempo"),
    mod("auto_swell", "Auto Swell", "Release", "Doubling", 1.0f, "Attack", "Boost"),
    mod("filter", "Filter", "Resonance", "Shape / Source"),
    mod("formant", "Formant", "Resonance", "Vowel"),
    mod("quadrature", "Quadrature", "Blend / Spread", "Mode", 1.0f, "Frequency", "FM Depth"),
    mod("destroyer", "Destroyer", "Filter Resonance", "Noise", 1.0f, "Decimation", "Bits"),
    delay("digital", "Digital Delay", "Saturation", "Mod Rate", "Mod Depth"),
    delay("tape", "Tape Delay", "Saturation", "Flutter Rate", "Flutter"),
    delay("dual", "Dual Delay", "Ping-Pong"),
    delay("filter", "Filter Delay", "Filter Type", "Sweep Rate", "Sweep Depth", "Resonance"),
    delay("lofi", "Lo-fi Delay", "Crush", "Mod Rate", "Mod Depth", "Anti-alias"),
    delay("dbucket", "Bucket Brigade Delay", "Drive"),
    delay("duck", "Duck Delay", "Ducking"),
    delay("pattern", "Pattern Delay", "Pattern"),
    delay("swell", "Swell Delay", "Threshold", "Attack", "Release"),
    delay("trem", "Tremolo Delay", "Shape", "Trem Rate", "Trem Depth"),
    reverb("room", "Room Reverb", "Size", "Diffusion", 0.50f, 0.50f, "Mod Depth"),
    reverb("hall", "Hall Reverb", "Diffusion", "Mid EQ", 0.50f, 0.50f, "Mod Depth"),
    reverb("plate", "Plate Reverb", "Size", "Character", 0.50f, 0.50f, "Mod Rate"),
    reverb("spring", "Spring Reverb", "Dwell", "Springs", 0.50f, 0.50f, "Wobble"),
    reverb("bloom", "Bloom Reverb", "Bloom Time", "Feedback", 0.50f, 0.50f, "Mod Depth"),
    reverb("cloud", "Cloud Reverb", "Diffusion", "Darkness", 0.50f, 0.50f, "Mod Depth"),
    // +12 and +7 semitones make an immediately musical default instead of
    // the former unison-plus-unison setting.
    reverb("shimmer", "Shimmer Reverb", "Pitch 1", "Pitch 2", 24.0f / 36.0f, 19.0f / 36.0f, "Shimmer"),
    reverb("chorale", "Chorale Reverb", "Vowel", "Resonance", 0.50f, 0.50f, "Mod Depth"),
    reverb("nonlinear", "Nonlinear Reverb", "Shape", "Diffusion", 0.50f, 0.50f, "Mod Depth"),
    reverb("swell", "Swell Reverb", "Rise Time", "Direction", 0.50f, 0.50f, "Mod Depth"),
    reverb("magneto", "Magneto Reverb", "Heads", "Spacing", 0.50f, 0.50f, "Diffusion", "Feedback"),
    reverb("reflections", "Reflections Reverb", "Depth", "Width", 0.50f, 0.50f, "Motion"),
  };
  return catalog;
}

const DaisyFxDescriptor* findDaisyFxDescriptor(std::string_view blockType, std::string_view mode)
{
  for (const auto& descriptor : daisyFxCatalog()) {
    if (descriptor.blockType == blockType && descriptor.mode == mode) {
      return &descriptor;
    }
  }
  return nullptr;
}

nlohmann::json defaultDaisyFxParams(const DaisyFxDescriptor& descriptor)
{
  nlohmann::json params = nlohmann::json::object();
  params["mode"] = descriptor.mode;
  for (const auto& param : descriptor.params) {
    params[param.key] = param.defaultValue;
  }
  return params;
}

std::string formatDaisyFxParamValue(const DaisyFxDescriptor& effect,
                                    const DaisyFxParamDescriptor& param,
                                    float normalized)
{
  normalized = std::clamp(normalized, 0.0f, 1.0f);
  switch (effect.kind) {
    case DaisyFxKind::Mod: return formatMod(effect.mode, param.key, normalized);
    case DaisyFxKind::Delay: return formatDelay(effect.mode, param.key, normalized);
    case DaisyFxKind::Reverb: return formatReverb(effect.mode, param.key, normalized);
  }
  return percent(normalized);
}

DaisyFxParamControlSpec daisyFxParamControlSpec(const DaisyFxDescriptor& effect,
                                                const DaisyFxParamDescriptor& param)
{
  const auto& mode = effect.mode;
  const auto& key = param.key;
  std::size_t choiceCount = 0;

  if (effect.kind == DaisyFxKind::Mod) {
    if (mode == "destroyer" && key == "depth") choiceCount = 16;
    if (mode == "pattern_trem" && key == "p1") choiceCount = 16;
    if (key == "p2") {
      if (mode == "chorus") choiceCount = 5;
      else if (mode == "flanger") choiceCount = 6;
      else if (mode == "rotary") choiceCount = 2;
      else if (mode == "phaser" || mode == "formant") choiceCount = 7;
      else if (mode == "vintage_trem" || mode == "pattern_trem") choiceCount = 3;
      else if (mode == "filter") choiceCount = 8;
      else if (mode == "quadrature") choiceCount = 4;
    }
  } else if (effect.kind == DaisyFxKind::Delay) {
    if (key == "grit" && (mode == "filter" || mode == "pattern")) choiceCount = 3;
  } else {
    if (key == "param1") {
      if (mode == "chorale") choiceCount = 7;
      else if (mode == "nonlinear") choiceCount = 6;
      else if (mode == "magneto") return {1.0f, {0.0f, 0.5f, 1.0f}};
    }
    if (key == "param2") {
      if (mode == "spring" || mode == "chorale") choiceCount = 3;
      else if (mode == "swell" || mode == "magneto") choiceCount = 2;
    }
  }

  if (choiceCount > 0) return {1.0f, evenlySpacedChoices(choiceCount)};

  float step = 0.01f;
  if (key == "time" || key == "speed" || key == "mod_spd"
      || key == "decay" || key == "pre_delay") {
    step = 0.001f;
  }
  if (effect.kind == DaisyFxKind::Reverb
      && ((mode == "shimmer" && (key == "param1" || key == "param2"))
          || (key == "param1" && (mode == "bloom" || mode == "swell")))) {
    step = 0.001f;
  }
  if (key == "level") step = 0.005f;
  return {step, {}};
}

} // namespace ardor
