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
    {"level", "Level", 1.0f},
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

DaisyFxDescriptor mod(std::string mode, std::string name)
{
  return {DaisyFxKind::Mod, "mod", std::move(mode), std::move(name), modParams()};
}

DaisyFxDescriptor delay(std::string mode, std::string name)
{
  return {DaisyFxKind::Delay, "delay", std::move(mode), std::move(name), delayParams()};
}

DaisyFxDescriptor reverb(std::string mode, std::string name)
{
  return {DaisyFxKind::Reverb, "reverb", std::move(mode), std::move(name), reverbParams()};
}

} // namespace

const std::vector<DaisyFxDescriptor>& daisyFxCatalog()
{
  static const std::vector<DaisyFxDescriptor> catalog{
    mod("chorus", "Chorus"),
    mod("flanger", "Flanger"),
    mod("rotary", "Rotary"),
    mod("vibe", "Vibe"),
    mod("phaser", "Phaser"),
    mod("vintage_trem", "Vintage Trem"),
    mod("poly_octave", "Poly Octave"),
    mod("pattern_trem", "Pattern Trem"),
    mod("auto_swell", "Auto Swell"),
    mod("filter", "Filter"),
    mod("formant", "Formant"),
    mod("quadrature", "Quadrature"),
    mod("destroyer", "Destroyer"),
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
    reverb("room", "Room Reverb"),
    reverb("hall", "Hall Reverb"),
    reverb("plate", "Plate Reverb"),
    reverb("spring", "Spring Reverb"),
    reverb("bloom", "Bloom Reverb"),
    reverb("cloud", "Cloud Reverb"),
    reverb("shimmer", "Shimmer Reverb"),
    reverb("chorale", "Chorale Reverb"),
    reverb("nonlinear", "Nonlinear Reverb"),
    reverb("swell", "Swell Reverb"),
    reverb("magneto", "Magneto Reverb"),
    reverb("reflections", "Reflections Reverb"),
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
