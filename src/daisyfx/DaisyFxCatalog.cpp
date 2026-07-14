#include "daisyfx/DaisyFxCatalog.h"

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

DaisyFxDescriptor mod(std::string mode, std::string name, std::string p1 = "P1", std::string p2 = "P2")
{
  auto params = modParams();
  params[4].label = std::move(p1);
  params[5].label = std::move(p2);
  return {DaisyFxKind::Mod, "mod", std::move(mode), std::move(name), std::move(params)};
}

DaisyFxDescriptor delay(std::string mode, std::string name)
{
  return {DaisyFxKind::Delay, "delay", std::move(mode), std::move(name), delayParams()};
}

DaisyFxDescriptor reverb(std::string mode, std::string name, std::string param1 = "Param 1", std::string param2 = "Param 2")
{
  auto params = reverbParams();
  params[5].label = std::move(param1);
  params[6].label = std::move(param2);
  return {DaisyFxKind::Reverb, "reverb", std::move(mode), std::move(name), std::move(params)};
}

} // namespace

const std::vector<DaisyFxDescriptor>& daisyFxCatalog()
{
  static const std::vector<DaisyFxDescriptor> catalog{
    mod("chorus", "Chorus", "Delay", "Type"),
    mod("flanger", "Flanger", "Regen", "Type"),
    mod("rotary", "Rotary", "Drive", "Speed"),
    mod("vibe", "Vibe", "Regen", "Shape"),
    mod("phaser", "Phaser", "Regen", "Stages"),
    mod("vintage_trem", "Vintage Trem", "Shape", "Type"),
    mod("poly_octave", "Poly Octave", "Oct Up", "Oct Down"),
    mod("pattern_trem", "Pattern Trem", "Pattern", "Shape"),
    mod("auto_swell", "Auto Swell", "Attack", "Sensitivity"),
    mod("filter", "Filter", "Mode", "Resonance"),
    mod("formant", "Formant", "Vowel", "Resonance"),
    mod("quadrature", "Quadrature", "Frequency", "Feedback"),
    mod("destroyer", "Destroyer", "Bits", "Drive"),
    delay("digital", "Digital Delay"),
    delay("tape", "Tape Delay"),
    delay("dual", "Dual Delay"),
    delay("filter", "Filter Delay"),
    delay("lofi", "Lo-fi Delay"),
    delay("dbucket", "Bucket Brigade Delay"),
    delay("duck", "Duck Delay"),
    delay("pattern", "Pattern Delay"),
    delay("swell", "Swell Delay"),
    delay("trem", "Tremolo Delay"),
    reverb("room", "Room Reverb", "Size", "Diffusion"),
    reverb("hall", "Hall Reverb", "Size", "Mid EQ"),
    reverb("plate", "Plate Reverb", "Size", "Character"),
    reverb("spring", "Spring Reverb", "Dwell", "Springs"),
    reverb("bloom", "Bloom Reverb", "Bloom Time", "Feedback"),
    reverb("cloud", "Cloud Reverb", "Diffusion", "Darkness"),
    reverb("shimmer", "Shimmer Reverb", "Pitch 1", "Pitch 2"),
    reverb("chorale", "Chorale Reverb", "Vowel", "Resonance"),
    reverb("nonlinear", "Nonlinear Reverb", "Shape", "Diffusion"),
    reverb("swell", "Swell Reverb", "Rise Time", "Direction"),
    reverb("magneto", "Magneto Reverb", "Heads", "Spacing"),
    reverb("reflections", "Reflections Reverb", "Depth", "Width"),
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

} // namespace ardor
