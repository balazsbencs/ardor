#include "daisyfx/DaisyFxCatalog.h"

namespace ardor {

const std::vector<DaisyFxDescriptor>& daisyFxCatalog()
{
  static const std::vector<DaisyFxDescriptor> catalog{
    {
      DaisyFxKind::Mod,
      "mod",
      "vintage_trem",
      "Vintage Trem",
      {
        {"speed", "Speed", 0.35f},
        {"depth", "Depth", 0.70f},
        {"mix", "Mix", 1.0f},
        {"tone", "Tone", 0.50f},
        {"p1", "P1", 0.0f},
        {"p2", "P2", 0.0f},
        {"level", "Level", 1.0f},
      },
    },
    {
      DaisyFxKind::Delay,
      "delay",
      "digital",
      "Digital Delay",
      {
        {"time", "Time", 0.25f},
        {"repeats", "Repeats", 0.35f},
        {"mix", "Mix", 0.25f},
        {"filter", "Filter", 0.50f},
        {"grit", "Grit", 0.0f},
        {"mod_spd", "Mod Spd", 0.0f},
        {"mod_dep", "Mod Dep", 0.0f},
      },
    },
    {
      DaisyFxKind::Reverb,
      "reverb",
      "room",
      "Room Reverb",
      {
        {"decay", "Decay", 0.45f},
        {"pre_delay", "Pre", 0.15f},
        {"mix", "Mix", 0.25f},
        {"tone", "Tone", 0.50f},
        {"mod", "Mod", 0.0f},
        {"param1", "P1", 0.50f},
        {"param2", "P2", 0.50f},
      },
    },
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
