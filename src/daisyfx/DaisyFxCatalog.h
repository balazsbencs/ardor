#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace ardor {

enum class DaisyFxKind {
  Mod,
  Delay,
  Reverb
};

struct DaisyFxParamDescriptor {
  std::string key;
  std::string label;
  float defaultValue = 0.0f;
};

struct DaisyFxDescriptor {
  DaisyFxKind kind = DaisyFxKind::Mod;
  std::string blockType;
  std::string mode;
  std::string name;
  std::vector<DaisyFxParamDescriptor> params;
};

struct DaisyFxParamControlSpec {
  float step = 0.01f;
  std::vector<float> choiceValues;
};

const std::vector<DaisyFxDescriptor>& daisyFxCatalog();
const DaisyFxDescriptor* findDaisyFxDescriptor(std::string_view blockType, std::string_view mode);
nlohmann::json defaultDaisyFxParams(const DaisyFxDescriptor& descriptor);
std::string formatDaisyFxParamValue(const DaisyFxDescriptor& effect,
                                    const DaisyFxParamDescriptor& param,
                                    float normalized);
DaisyFxParamControlSpec daisyFxParamControlSpec(const DaisyFxDescriptor& effect,
                                                const DaisyFxParamDescriptor& param);

} // namespace ardor
