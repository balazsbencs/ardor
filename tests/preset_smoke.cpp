#include "preset/RuntimeState.h"
#include "preset/ChainPlan.h"
#include "preset/PresetStore.h"
#include "preset/Preset.h"

#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main()
{
  try {
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
    require(preset.version == 1, "preset version");
    require(preset.name == "Clean Lead", "preset name");
    require(preset.routing == "serial", "preset routing");
    require(preset.global.inputGainDb == -12.0f, "input gain");
    require(preset.global.outputGainDb == -6.0f, "output gain");
    require(preset.global.safetyLimitDb == -1.0f, "safety limit");
    require(preset.blocks.size() == 2, "block count");
    require(preset.blocks[0].id == "block-1", "block id");
    require(preset.blocks[0].type == "nam", "block type");
    require(preset.blocks[0].enabled, "block enabled");
    require(preset.blocks[0].asset == "models/clean.nam", "block asset");
    require(preset.blocks[1].type == "cab", "second block type");
    require(!preset.blocks[1].enabled, "disabled block");
    require(preset.blocks[1].params.at("mix").get<float>() == 1.0f, "block param");

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
    require(rejectedParallel, "reject non-serial preset");

    ardor::Preset badWrite = preset;
    badWrite.routing = "parallel";
    bool rejectedWrite = false;
    try {
      (void)ardor::toJson(badWrite);
    } catch (const std::invalid_argument&) {
      rejectedWrite = true;
    }
    require(rejectedWrite, "reject non-serial write");

    const ardor::Preset roundTrip = ardor::presetFromJson(ardor::toJson(preset));
    require(roundTrip.blocks.size() == 2, "round trip block count");
    require(roundTrip.blocks[1].id == "block-2", "round trip block id");
    require(roundTrip.blocks[1].params.at("levelDb").get<float>() == -3.0f, "round trip block param");

    bool rejectedAbsoluteAsset = false;
    try {
      auto invalid = json;
      invalid["blocks"][0]["asset"] = "/tmp/clean.nam";
      (void)ardor::presetFromJson(invalid);
    } catch (const std::invalid_argument&) {
      rejectedAbsoluteAsset = true;
    }
    require(rejectedAbsoluteAsset, "reject absolute asset path");

    bool rejectedTraversalAsset = false;
    try {
      auto invalid = json;
      invalid["blocks"][0]["asset"] = "../models/clean.nam";
      (void)ardor::presetFromJson(invalid);
    } catch (const std::invalid_argument&) {
      rejectedTraversalAsset = true;
    }
    require(rejectedTraversalAsset, "reject traversal asset path");

    bool rejectedAbsoluteWrite = false;
    try {
      auto invalid = preset;
      invalid.blocks[0].asset = "/tmp/clean.nam";
      (void)ardor::toJson(invalid);
    } catch (const std::invalid_argument&) {
      rejectedAbsoluteWrite = true;
    }
    require(rejectedAbsoluteWrite, "reject absolute asset on write");

    const auto root = std::filesystem::temp_directory_path() / ("ardor-preset-smoke-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(root);
    ardor::PresetStore store(root);
    const ardor::PresetSlot slot{2, 3};

    ardor::Preset saved;
    saved.name = "Bank 2 Slot 3";
    saved.blocks.push_back({"block-a", "nam", true, "models/a.nam", nlohmann::json::object()});
    store.save(slot, saved);
    require(std::filesystem::exists(root / "presets/bank-002/preset-3.json"), "saved preset exists");

    ardor::PresetSession session;
    session.load(store, slot);
    require(!session.isDirty(), "clean after load");
    session.working().name = "Edited";
    require(session.isDirty(), "dirty after edit");
    session.discard();
    require(session.working().name == "Bank 2 Slot 3", "discard restores saved name");
    require(!session.isDirty(), "clean after discard");
    session.working().name = "Saved Edit";
    session.save();
    require(!session.isDirty(), "clean after save");
    require(store.load(slot).name == "Saved Edit", "saved edit persisted");

    ardor::Preset diskChanged = saved;
    diskChanged.name = "Disk Changed";
    store.save(slot, diskChanged);
    session.working().name = "Stale Edit";
    require(session.isDirty(), "dirty before discard reload");
    session.discard();
    require(session.working().name == "Disk Changed", "discard reloads disk change");
    require(!session.isDirty(), "clean after reload");

    const auto dataRoot = std::filesystem::temp_directory_path() / ("ardor-chain-smoke-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(dataRoot);
    std::filesystem::create_directories(dataRoot / "models");
    std::ofstream(dataRoot / "models/ok.nam").put('\n');

    ardor::Preset chainPreset;
    chainPreset.blocks.push_back({"ready", "nam", true, "models/ok.nam", nlohmann::json::object()});
    chainPreset.blocks.push_back({"disabled", "cab", false, "irs/missing.wav", nlohmann::json::object()});
    chainPreset.blocks.push_back({"empty", "cab", true, "", nlohmann::json::object()});
    chainPreset.blocks.push_back({"missing", "cab", true, "irs/missing.wav", nlohmann::json::object()});
    chainPreset.blocks.push_back({"escape", "nam", true, "../outside.nam", nlohmann::json::object()});
    chainPreset.blocks.push_back({"future", "delay", true, "", nlohmann::json::object()});

    const ardor::ChainPlan plan = ardor::buildChainPlan(chainPreset, dataRoot);
    require(plan.blocks.size() == 6, "chain plan block count");
    require(plan.blocks[0].status == ardor::ChainBlockStatus::Ready, "ready block");
    require(plan.blocks[1].status == ardor::ChainBlockStatus::Disabled, "disabled block");
    require(plan.blocks[2].status == ardor::ChainBlockStatus::MissingAsset, "empty asset");
    require(plan.blocks[3].status == ardor::ChainBlockStatus::MissingAsset, "missing asset");
    require(plan.blocks[4].status == ardor::ChainBlockStatus::MissingAsset, "escaped asset");
    require(plan.blocks[5].status == ardor::ChainBlockStatus::Unsupported, "unsupported block");
    require(plan.runnableBlockCount == 1, "runnable block count");

    std::filesystem::remove_all(dataRoot);
    std::filesystem::remove_all(root);

    ardor::RuntimeState runtime;
    require(!runtime.effectsBypassed(), "runtime starts enabled");
    runtime.reportOverload();
    require(!runtime.effectsBypassed(), "first overload does not bypass");
    runtime.reportOverload();
    require(!runtime.effectsBypassed(), "second overload does not bypass");
    runtime.reportStableCallback();
    runtime.reportOverload();
    require(!runtime.effectsBypassed(), "stable callback resets overload streak");
    runtime.reportOverload();
    runtime.reportOverload();
    require(runtime.effectsBypassed(), "third consecutive overload bypasses effects");
    runtime.reportStableCallback();
    require(runtime.effectsBypassed(), "stable callback keeps latched bypass");
    runtime.clearEffectsBypass();
    require(!runtime.effectsBypassed(), "clear bypass unlatches");
    runtime.reportOverload();
    runtime.reportOverload();
    require(!runtime.effectsBypassed(), "clear also resets overload count");
    runtime.reportOverload();
    require(runtime.effectsBypassed(), "three new overloads latch again");
    runtime.changePreset();
    require(!runtime.effectsBypassed(), "preset change clears bypass");

    return 0;
  } catch (const std::exception& error) {
    std::cerr << "preset_smoke failed: " << error.what() << '\n';
    return 1;
  }
}
