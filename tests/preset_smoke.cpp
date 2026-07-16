#include "preset/RuntimeState.h"
#include "preset/ChainPlan.h"
#include "audio/EngineLoader.h"
#include "preset/PresetStore.h"
#include "preset/Preset.h"

#include <array>
#include <filesystem>
#include <chrono>
#include <cmath>
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

    // Corrupt preset: garbage bytes → load must throw
    {
      const auto corruptPath = store.pathFor(slot);
      {
        std::ofstream out(corruptPath, std::ios::trunc);
        out << "this is not json {{{{";
      }
      bool threw = false;
      try {
        store.load(slot);
      } catch (const std::exception&) {
        threw = true;
      }
      require(threw, "load should throw on corrupt preset");

      // Stale .tmp from interrupted save: save should remove it
      const auto tmpPath = corruptPath.parent_path() / (corruptPath.filename().string() + ".tmp");
      {
        std::ofstream(tmpPath, std::ios::trunc) << "stale content";
      }
      require(std::filesystem::exists(tmpPath), "stale tmp exists before save");
      ardor::Preset recover;
      recover.name = "Recovered";
      store.save(slot, recover);
      require(!std::filesystem::exists(tmpPath), "save removes stale tmp");
      require(store.load(slot).name == "Recovered", "load succeeds after recovery save");
    }

    const auto dataRoot = std::filesystem::temp_directory_path() / ("ardor-chain-smoke-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(dataRoot);
    std::filesystem::create_directories(dataRoot / "models");
    std::filesystem::create_directories(dataRoot / "irs");
    std::ofstream(dataRoot / "models/ok.nam").put('\n');
    std::ofstream(dataRoot / "irs/ok.wav").put('\n');

    ardor::Preset chainPreset;
    chainPreset.global.inputGainDb = -6.0f;
    chainPreset.global.outputGainDb = -3.0f;
    chainPreset.global.safetyLimitDb = -2.0f;
    chainPreset.blocks.push_back({"ready", "nam", true, "models/ok.nam", nlohmann::json::object()});
    chainPreset.blocks[0].params = nlohmann::json{{"levelDb", -1.0f}};
    chainPreset.blocks.push_back({"disabled", "cab", false, "irs/missing.wav", nlohmann::json::object()});
    chainPreset.blocks.push_back({"empty", "cab", true, "", nlohmann::json::object()});
    chainPreset.blocks.push_back({"missing", "cab", true, "irs/missing.wav", nlohmann::json::object()});
    chainPreset.blocks.push_back({"escape", "nam", true, "../outside.nam", nlohmann::json::object()});
    chainPreset.blocks.push_back({"trem", "mod", true, "", nlohmann::json{{"mode", "vintage_trem"}}});
    chainPreset.blocks.push_back({"bad-mod", "mod", true, "", nlohmann::json{{"mode", "bogus"}}});
    chainPreset.blocks.push_back({"future", "delay", true, "", nlohmann::json::object()});
    chainPreset.blocks.push_back({"compressor", "dynamics", true, "", nlohmann::json{{"mode", "compressor"}}});
    chainPreset.blocks.push_back({"cab-ready", "cab", true, "irs/ok.wav", nlohmann::json{{"mix", 1.0f}}});

    const ardor::ChainPlan plan = ardor::buildChainPlan(chainPreset, dataRoot);
    require(plan.blocks.size() == 10, "chain plan block count");
    require(std::fabs(plan.inputGain - ardor::dbToGain(-6.0f)) < 0.0001f, "chain input gain");
    require(std::fabs(plan.outputGain - ardor::dbToGain(-3.0f)) < 0.0001f, "chain output gain");
    require(std::fabs(plan.safetyLimit - ardor::dbToGain(-2.0f)) < 0.0001f, "chain safety limit");
    require(plan.blocks[0].status == ardor::ChainBlockStatus::Ready, "ready block");
    require(plan.blocks[0].assetPath == dataRoot / "models/ok.nam", "resolved nam asset");
    require(plan.blocks[0].params.at("levelDb").get<float>() == -1.0f, "chain params copied");
    require(plan.blocks[1].status == ardor::ChainBlockStatus::Disabled, "disabled block");
    require(plan.blocks[2].status == ardor::ChainBlockStatus::MissingAsset, "empty asset");
    require(plan.blocks[3].status == ardor::ChainBlockStatus::MissingAsset, "missing asset");
    require(plan.blocks[4].status == ardor::ChainBlockStatus::MissingAsset, "escaped asset");
    require(plan.blocks[5].status == ardor::ChainBlockStatus::Ready, "daisy mod block");
    require(plan.blocks[6].status == ardor::ChainBlockStatus::Unsupported, "unsupported daisy mode");
    require(plan.blocks[7].status == ardor::ChainBlockStatus::Unsupported, "unsupported block");
    require(plan.blocks[8].status == ardor::ChainBlockStatus::Ready, "compressor block");
    require(plan.blocks.back().assetPath == dataRoot / "irs/ok.wav", "resolved cab asset");
    require(plan.runnableBlockCount == 4, "runnable block count");

    ardor::PresetStore preflightStore(dataRoot);
    ardor::Preset preflightReady;
    preflightReady.name = "Preflight ready";
    preflightReady.blocks.push_back({"trem", "mod", true, "", {{"mode", "vintage_trem"}}});
    preflightStore.save({0, 0}, preflightReady);
    std::string preflightError;
    require(ardor::preflightPresetSlot(preflightStore, {0, 0}, dataRoot, {48000, 64, 8192}, preflightError),
            "side-effect-free preset preflight should accept a valid plan");

    ardor::Preset preflightBadCab;
    preflightBadCab.name = "Preflight invalid cabinet";
    preflightBadCab.blocks.push_back({"cab", "cab", true, "irs/ok.wav", nlohmann::json::object()});
    preflightStore.save({0, 1}, preflightBadCab);
    require(!ardor::preflightPresetSlot(preflightStore, {0, 1}, dataRoot, {48000, 64, 8192}, preflightError),
            "preflight must reject a bad cabinet before a live-engine swap");
    require(preflightError.find("failed to load IR") != std::string::npos,
            "preflight cabinet error should explain the rejection");

    ardor::Preset duplicateDaisy;
    duplicateDaisy.name = "Duplicate Daisy";
    duplicateDaisy.blocks.push_back({"mod-a", "mod", true, "", {{"mode", "vintage_trem"}}});
    duplicateDaisy.blocks.push_back({"mod-b", "mod", true, "", {{"mode", "phaser"}}});
    ardor::PedalEngine duplicateDaisyEngine;
    std::string duplicateDaisyError;
    require(ardor::applyPreset(duplicateDaisyEngine, duplicateDaisy, dataRoot, {48000, 64, 8192}, duplicateDaisyError),
            "multiple Daisy blocks in one category should have independent state");
    std::array<float, 64> duplicateInput{};
    std::array<float, 64> duplicateLeft{};
    std::array<float, 64> duplicateRight{};
    duplicateInput[0] = 0.5f;
    duplicateDaisyEngine.processBlock(duplicateInput.data(), duplicateLeft.data(), duplicateRight.data(), duplicateLeft.size());
    for (const float sample : duplicateLeft) {
      require(std::isfinite(sample), "duplicate Daisy chain left output must be finite");
    }
    for (const float sample : duplicateRight) {
      require(std::isfinite(sample), "duplicate Daisy chain right output must be finite");
    }

    std::filesystem::remove_all(dataRoot);
    std::filesystem::remove_all(root);

    ardor::RuntimeState runtime;
    require(!runtime.effectsBypassed(), "runtime starts enabled");
    runtime.observeRealtimeStats(0, 100, 0, 1);
    runtime.observeRealtimeStats(100, 200, 1, 2);
    runtime.observeRealtimeStats(200, 300, 2, 3);
    require(!runtime.effectsBypassed(), "single over-budget callbacks should not bypass");
    runtime.observeRealtimeStats(300, 400, 3, 10);
    runtime.observeRealtimeStats(400, 500, 10, 17);
    require(!runtime.effectsBypassed(), "second high-rate second should not bypass");
    runtime.observeRealtimeStats(500, 600, 17, 24);
    require(runtime.effectsBypassed(), "third high-rate second bypasses effects");
    runtime.observeRealtimeStats(600, 700, 24, 24);
    runtime.observeRealtimeStats(700, 800, 24, 24);
    runtime.observeRealtimeStats(800, 900, 24, 24);
    require(runtime.effectsBypassed(), "stable seconds must not automatically retry a latched overload bypass");
    runtime.clearEffectsBypass();
    require(!runtime.effectsBypassed(), "explicit recovery clears bypass");
    runtime.changePreset();
    require(!runtime.effectsBypassed(), "preset change clears bypass");

    const auto telemetry = ardor::makeRuntimeTelemetry(100, 5, 1, 0.8, 0.2, 1.33, true);
    require(telemetry.callbacks == 100, "telemetry callbacks");
    require(telemetry.overBudget == 5, "telemetry over budget");
    require(std::fabs(telemetry.overBudgetPercent - 5.0) < 0.0001, "telemetry over percent");
    require(telemetry.callbackGaps == 1, "telemetry callback gaps");
    require(telemetry.bypassed, "telemetry bypassed");
    const auto line = ardor::formatRuntimeTelemetry(telemetry);
    require(line.find("callbacks=100") != std::string::npos, "formatted callbacks");
    require(line.find("over%=5.00") != std::string::npos, "formatted over percent");
    require(line.find("gaps=1") != std::string::npos, "formatted callback gaps");
    require(line.find("bypassed=1") != std::string::npos, "formatted bypass");

    return 0;
  } catch (const std::exception& error) {
    std::cerr << "preset_smoke failed: " << error.what() << '\n';
    return 1;
  }
}
