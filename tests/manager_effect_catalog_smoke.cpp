#include "daisyfx/DaisyFxCatalog.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_set>

#ifndef ARDOR_MANAGER_EFFECT_CATALOG
#error "ARDOR_MANAGER_EFFECT_CATALOG must name the manager catalog"
#endif

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

std::string pairKey(const std::string& blockType, const std::string& mode)
{
  return blockType + "\n" + mode;
}

} // namespace

int main()
{
  std::ifstream input(ARDOR_MANAGER_EFFECT_CATALOG);
  require(input.good(), "manager effect catalog can be opened");

  nlohmann::json catalog;
  input >> catalog;
  require(catalog.value("version", 0) == 1, "manager effect catalog version is 1");
  require(catalog.contains("definitions") && catalog.at("definitions").is_array(),
          "manager effect catalog has definitions");

  const auto& definitions = catalog.at("definitions");
  require(definitions.size() == 39, "manager effect catalog has 39 definitions");

  std::unordered_set<std::string> managerDaisyPairs;
  bool foundCompressor = false;
  bool foundEq = false;
  for (const auto& definition : definitions) {
    const auto blockType = definition.value("blockType", std::string{});
    const auto mode = definition.value("mode", std::string{});
    if (blockType == "dynamics" && mode == "compressor") {
      foundCompressor = definition.value("id", std::string{}) == "dynamics:compressor";
    }
    if (blockType == "eq" && mode == "parametric_eq_5") {
      foundEq = definition.value("id", std::string{}) == "eq:parametric_eq_5";
    }
    if (blockType != "mod" && blockType != "delay" && blockType != "reverb") {
      continue;
    }

    const auto* runtime = ardor::findDaisyFxDescriptor(blockType, mode);
    require(runtime != nullptr, "manager Daisy definition exists at runtime: " + blockType + "/" + mode);
    require(managerDaisyPairs.insert(pairKey(blockType, mode)).second,
            "manager Daisy definition is unique: " + blockType + "/" + mode);
    require(definition.value("name", std::string{}) == runtime->name,
            "display name matches for " + blockType + "/" + mode);

    const auto& controls = definition.at("controls");
    require(controls.size() == runtime->params.size(), "parameter count matches for " + blockType + "/" + mode);
    for (std::size_t index = 0; index < runtime->params.size(); ++index) {
      const auto& manager = controls.at(index);
      const auto& parameter = runtime->params[index];
      require(manager.value("kind", std::string{}) == "number", "Daisy control is numeric");
      require(manager.value("key", std::string{}) == parameter.key,
              "parameter key order matches for " + blockType + "/" + mode);
      require(manager.value("label", std::string{}) == parameter.label,
              "parameter label matches for " + blockType + "/" + mode);
      require(std::fabs(manager.value("defaultValue", -100.0f) - parameter.defaultValue) <= 0.0001f,
              "parameter default matches for " + blockType + "/" + mode + "/" + parameter.key);
    }
  }

  require(foundCompressor, "manager compressor identifier matches runtime");
  require(foundEq, "manager EQ identifier matches runtime");
  require(managerDaisyPairs.size() == ardor::daisyFxCatalog().size(), "manager has every runtime Daisy definition");
  for (const auto& runtime : ardor::daisyFxCatalog()) {
    require(managerDaisyPairs.contains(pairKey(runtime.blockType, runtime.mode)),
            "runtime Daisy definition exists in manager: " + runtime.blockType + "/" + runtime.mode);
  }
}
