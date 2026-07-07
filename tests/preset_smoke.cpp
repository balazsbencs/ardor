#include "preset/RuntimeState.h"
#include "preset/ChainPlan.h"
#include "preset/PresetStore.h"
#include "preset/Preset.h"

#include <filesystem>
#include <chrono>
#include <cassert>
#include <fstream>
#include <stdexcept>
#include <string>

int main()
{
  const auto json = nlohmann::json::parse(R"({
    "version": 1,
    "name": "Clean Lead",
    "routing": "serial",
    "global": {
      "inputGainDb": -12.0,
      "outputGainDb": -6.0,
      "safetyLimitDb": -1.0
    },
    "blocks": [
      {
        "id": "block-1",
        "type": "nam",
        "enabled": true,
        "asset": "models/clean.nam",
        "params": { "levelDb": 0.0 }
      },
      {
        "id": "block-2",
        "type": "cab",
        "enabled": false,
        "asset": "irs/open-back.wav",
        "params": { "mix": 1.0, "levelDb": -3.0 }
      }
    ]
  })");

  const ardor::Preset preset = ardor::presetFromJson(json);
  assert(preset.version == 1);
  assert(preset.name == "Clean Lead");
  assert(preset.routing == "serial");
  assert(preset.global.inputGainDb == -12.0f);
  assert(preset.global.outputGainDb == -6.0f);
  assert(preset.global.safetyLimitDb == -1.0f);
  assert(preset.blocks.size() == 2);
  assert(preset.blocks[0].id == "block-1");
  assert(preset.blocks[0].type == "nam");
  assert(preset.blocks[0].enabled);
  assert(preset.blocks[0].asset == "models/clean.nam");
  assert(preset.blocks[1].type == "cab");
  assert(!preset.blocks[1].enabled);
  assert(preset.blocks[1].params.at("mix").get<float>() == 1.0f);

  bool rejectedParallel = false;
  try {
    (void)ardor::presetFromJson(nlohmann::json::parse(R"({
      "version": 1,
      "name": "Parallel Fail",
      "routing": "parallel",
      "global": {},
      "blocks": []
    })"));
  } catch (const std::invalid_argument&) {
    rejectedParallel = true;
  }
  assert(rejectedParallel);

  ardor::Preset badWrite = preset;
  badWrite.routing = "parallel";
  bool rejectedWrite = false;
  try {
    (void)ardor::toJson(badWrite);
  } catch (const std::invalid_argument&) {
    rejectedWrite = true;
  }
  assert(rejectedWrite);

  const ardor::Preset roundTrip = ardor::presetFromJson(ardor::toJson(preset));
  assert(roundTrip.blocks.size() == 2);
  assert(roundTrip.blocks[1].id == "block-2");
  assert(roundTrip.blocks[1].params.at("levelDb").get<float>() == -3.0f);

  const auto root = std::filesystem::temp_directory_path() / ("ardor-preset-smoke-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::remove_all(root);
  ardor::PresetStore store(root);
  const ardor::PresetSlot slot{2, 3};

  ardor::Preset saved;
  saved.name = "Bank 2 Slot 3";
  saved.blocks.push_back({"block-a", "nam", true, "models/a.nam", nlohmann::json::object()});
  store.save(slot, saved);
  assert(std::filesystem::exists(root / "presets/bank-002/preset-3.json"));

  ardor::PresetSession session;
  session.load(store, slot);
  assert(!session.isDirty());
  session.working().name = "Edited";
  assert(session.isDirty());
  session.discard();
  assert(session.working().name == "Bank 2 Slot 3");
  assert(!session.isDirty());
  session.working().name = "Saved Edit";
  session.save();
  assert(!session.isDirty());
  assert(store.load(slot).name == "Saved Edit");

  ardor::Preset diskChanged = saved;
  diskChanged.name = "Disk Changed";
  store.save(slot, diskChanged);
  session.working().name = "Stale Edit";
  assert(session.isDirty());
  session.discard();
  assert(session.working().name == "Disk Changed");
  assert(!session.isDirty());

  const auto dataRoot = std::filesystem::temp_directory_path() / ("ardor-chain-smoke-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::remove_all(dataRoot);
  std::filesystem::create_directories(dataRoot / "models");
  std::ofstream(dataRoot / "models/ok.nam").put('\n');

  ardor::Preset chainPreset;
  chainPreset.blocks.push_back({"ready", "nam", true, "models/ok.nam", nlohmann::json::object()});
  chainPreset.blocks.push_back({"empty", "cab", true, "", nlohmann::json::object()});
  chainPreset.blocks.push_back({"missing", "cab", true, "irs/missing.wav", nlohmann::json::object()});
  chainPreset.blocks.push_back({"future", "delay", true, "", nlohmann::json::object()});

  const ardor::ChainPlan plan = ardor::buildChainPlan(chainPreset, dataRoot);
  assert(plan.blocks.size() == 4);
  assert(plan.blocks[0].status == ardor::ChainBlockStatus::Ready);
  assert(plan.blocks[1].status == ardor::ChainBlockStatus::MissingAsset);
  assert(plan.blocks[2].status == ardor::ChainBlockStatus::MissingAsset);
  assert(plan.blocks[3].status == ardor::ChainBlockStatus::Unsupported);
  assert(plan.runnableBlockCount == 1);

  std::filesystem::remove_all(dataRoot);

  std::filesystem::remove_all(root);

  ardor::RuntimeState runtime;
  assert(!runtime.effectsBypassed());
  runtime.reportOverload();
  assert(runtime.effectsBypassed());
  runtime.reportStableCallback();
  assert(runtime.effectsBypassed());
  runtime.clearEffectsBypass();
  assert(!runtime.effectsBypassed());
  runtime.reportOverload();
  assert(runtime.effectsBypassed());
  runtime.changePreset();
  assert(!runtime.effectsBypassed());

  return 0;
}
