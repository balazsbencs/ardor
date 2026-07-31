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
    auto expressionJson = json;
    expressionJson["expression"] = {
      {"blockId", "block-2"},
      {"parameter", "mix"},
      {"minimum", 0.2},
      {"maximum", 1.0},
      {"inverted", false},
    };
    const auto expressionPreset = ardor::presetFromJson(expressionJson);
    require(expressionPreset.expression && expressionPreset.expression->blockId == "block-2"
              && expressionPreset.expression->parameter == "mix",
            "expression assignment target");
    require(std::fabs(ardor::expressionValueAt(*expressionPreset.expression, 0.5f) - 0.6f) < 1.0e-6f,
            "expression assignment range mapping");
    auto invertedExpression = *expressionPreset.expression;
    invertedExpression.inverted = true;
    require(std::fabs(ardor::expressionValueAt(invertedExpression, 0.25f) - 0.8f) < 1.0e-6f,
            "inverted expression mapping");

    const auto legacyEffectsJson = nlohmann::json::parse(R"({
      "version": 1,
      "name": "Legacy placeholders",
      "routing": "serial",
      "global": {},
      "blocks": [
        {"id":"old-delay", "type":"time", "enabled":true, "asset":"", "params":{}},
        {"id":"old-chorus", "type":"modulation", "enabled":true, "asset":"", "params":{}},
        {"id":"old-compressor", "type":"dynamics", "enabled":true, "asset":"", "params":{}}
      ]
    })");
    const auto migratedEffects = ardor::presetFromJson(legacyEffectsJson);
    require(migratedEffects.blocks[0].type == "delay"
              && migratedEffects.blocks[0].params.value("mode", "") == "tape",
            "legacy time placeholder should migrate to tape delay");
    require(migratedEffects.blocks[1].type == "mod"
              && migratedEffects.blocks[1].params.value("mode", "") == "chorus",
            "legacy modulation placeholder should migrate to chorus");
    require(migratedEffects.blocks[2].type == "dynamics"
              && migratedEffects.blocks[2].params.value("mode", "") == "compressor",
            "legacy dynamics placeholder should migrate to compressor");
    const auto migratedPlan = ardor::buildChainPlan(migratedEffects, {});
    require(migratedPlan.runnableBlockCount == 3, "all migrated placeholders should be runnable");

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
    const auto expressionRoundTrip = ardor::presetFromJson(ardor::toJson(expressionPreset));
    require(expressionRoundTrip.expression && expressionRoundTrip.expression->blockId == "block-2",
            "expression assignment round trip");

    bool rejectedMissingExpressionBlock = false;
    try {
      auto invalid = expressionJson;
      invalid["expression"]["blockId"] = "missing-block";
      (void)ardor::presetFromJson(invalid);
    } catch (const std::invalid_argument&) {
      rejectedMissingExpressionBlock = true;
    }
    require(rejectedMissingExpressionBlock, "reject expression assignment to missing block");

    bool rejectedExpressionRange = false;
    try {
      auto invalid = expressionJson;
      invalid["expression"]["minimum"] = 1.0;
      invalid["expression"]["maximum"] = 0.0;
      (void)ardor::presetFromJson(invalid);
    } catch (const std::invalid_argument&) {
      rejectedExpressionRange = true;
    }
    require(rejectedExpressionRange, "reject inverted expression range");

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

    auto dualJson = json;
    dualJson["blocks"] = nlohmann::json::array({{
      {"id", "dual"}, {"type", "dualAmp"}, {"enabled", true}, {"asset", ""},
      {"params", {
        {"leftNamAsset", "models/left.nam"}, {"leftIrAsset", "irs/left.wav"},
        {"rightNamAsset", "models/right.nam"}, {"rightIrAsset", "irs/right.wav"},
      }},
    }});
    const auto dualPreset = ardor::presetFromJson(dualJson);
    require(dualPreset.blocks[0].params.value("rightIrAsset", "") == "irs/right.wav",
            "dual amp nested assets parse");
    bool rejectedDualTraversal = false;
    try {
      dualJson["blocks"][0]["params"]["rightIrAsset"] = "../escape.wav";
      (void)ardor::presetFromJson(dualJson);
    } catch (const std::invalid_argument&) {
      rejectedDualTraversal = true;
    }
    require(rejectedDualTraversal, "reject dual amp traversal asset");

    const auto dualRigJson = nlohmann::json::parse(R"({
      "version": 2,
      "name": "Two independent rigs",
      "routing": "serial",
      "global": {},
      "blocks": [{
        "id": "rig",
        "type": "dualRig",
        "enabled": true,
        "asset": "",
        "params": {
          "inputMode": "sum",
          "leftLevelDb": 0.0,
          "rightLevelDb": -3.0,
          "leftPolarityInvert": false,
          "rightPolarityInvert": true
        },
        "lanes": {
          "left": {
            "blocks": [
              {"id":"left-nam","type":"nam","enabled":true,"asset":"models/left.nam","params":{}},
              {"id":"left-cab","type":"cab","enabled":true,"asset":"irs/left.wav","params":{}},
              {"id":"left-chorus","type":"mod","enabled":true,"asset":"","params":{"mode":"chorus"}}
            ]
          },
          "right": {
            "blocks": [
              {"id":"right-nam","type":"nam","enabled":true,"asset":"models/right.nam","params":{}},
              {"id":"right-cab","type":"cab","enabled":true,"asset":"irs/right.wav","params":{}},
              {"id":"right-delay","type":"delay","enabled":true,"asset":"","params":{"mode":"digital"}}
            ]
          }
        }
      }]
    })");
    const auto dualRigPreset = ardor::presetFromJson(dualRigJson);
    require(dualRigPreset.version == 2, "dual rig requires preset version 2");
    require(dualRigPreset.blocks.size() == 1 && dualRigPreset.blocks[0].type == "dualRig",
            "dual rig top-level block parses");
    require(dualRigPreset.blocks[0].lanes[0].size() == 3
              && dualRigPreset.blocks[0].lanes[1].size() == 3,
            "dual rig preserves two child chains");
    require(dualRigPreset.blocks[0].lanes[0][0].asset == "models/left.nam"
              && dualRigPreset.blocks[0].lanes[1][2].params.value("mode", "") == "digital",
            "dual rig child assets and parameters parse");
    const auto dualRigRoundTrip = ardor::presetFromJson(ardor::toJson(dualRigPreset));
    require(dualRigRoundTrip.blocks[0].lanes[0][2].id == "left-chorus"
              && dualRigRoundTrip.blocks[0].lanes[1][1].asset == "irs/right.wav",
            "dual rig child chains round trip");

    bool rejectedNestedDualRig = false;
    try {
      auto nested = dualRigJson;
      nested["blocks"][0]["lanes"]["left"]["blocks"][0] = nested["blocks"][0];
      (void)ardor::presetFromJson(nested);
    } catch (const std::invalid_argument&) {
      rejectedNestedDualRig = true;
    }
    require(rejectedNestedDualRig, "reject nested dual rig split regions");

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

    const auto empty = store.loadOrEmpty(slot);
    require(empty.name == "Empty 4", "missing preset should load as an empty slot");
    require(empty.blocks.empty(), "missing preset should have an empty effect chain");
    require(!std::filesystem::exists(store.pathFor(slot)), "loading an empty slot should not save it implicitly");

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
    chainPreset.blocks.push_back({"noise-gate", "dynamics", true, "", nlohmann::json{{"mode", "noise_gate"}}});
    chainPreset.blocks.push_back({"cab-ready", "cab", true, "irs/ok.wav", nlohmann::json{{"mix", 1.0f}}});

    const ardor::ChainPlan plan = ardor::buildChainPlan(chainPreset, dataRoot);
    require(plan.blocks.size() == 11, "chain plan block count");
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
    require(plan.blocks[9].status == ardor::ChainBlockStatus::Ready, "noise gate block");
    require(plan.blocks.back().assetPath == dataRoot / "irs/ok.wav", "resolved cab asset");
    require(plan.runnableBlockCount == 5, "runnable block count");

    auto rigPlanPreset = dualRigPreset;
    for (auto& lane : rigPlanPreset.blocks[0].lanes) {
      lane[0].asset = "models/ok.nam";
      lane[1].asset = "irs/ok.wav";
    }
    const auto rigPlan = ardor::buildChainPlan(rigPlanPreset, dataRoot);
    require(rigPlan.blocks[0].status == ardor::ChainBlockStatus::Ready,
            "dual rig parent plan is runnable");
    require(rigPlan.blocks[0].lanes[0].size() == 3
              && rigPlan.blocks[0].lanes[1][0].assetPath == dataRoot / "models/ok.nam",
            "dual rig child plans resolve assets recursively");
    require(rigPlan.runnableBlockCount == 7,
            "dual rig runnable count includes the split and both child chains");
    rigPlanPreset.blocks[0].enabled = false;
    require(ardor::buildChainPlan(rigPlanPreset, dataRoot).runnableBlockCount == 0,
            "disabled dual rig excludes its child chains from runnable count");

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

    ardor::Preset noiseGatePreset;
    noiseGatePreset.name = "Noise Gate";
    noiseGatePreset.blocks.push_back({"gate", "dynamics", true, "", {
      {"mode", "noise_gate"}, {"threshold_db", -20.0f}, {"reduction_db", 80.0f},
      {"attack_ms", 1.0f}, {"hold_ms", 0.0f}, {"release_ms", 50.0f},
      {"hysteresis_db", 6.0f}, {"sidechain_hpf_hz", 80.0f},
    }});
    ardor::PedalEngine noiseGateEngine;
    std::string noiseGateError;
    require(ardor::applyPreset(
              noiseGateEngine, noiseGatePreset, dataRoot, {48000, 64, 8192}, noiseGateError),
            "noise gate preset should load through the prepared runtime path");
    std::array<float, 64> gateInput{};
    std::array<float, 64> gateLeft{};
    std::array<float, 64> gateRight{};
    gateInput.fill(0.001f);
    for (int block = 0; block < 1000; ++block) {
      noiseGateEngine.processBlock(
        gateInput.data(), gateLeft.data(), gateRight.data(), gateInput.size());
    }
    require(std::fabs(gateLeft.back()) < 1.0e-6f,
            "prepared noise gate runtime should attenuate quiet input");

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

    const auto telemetry = ardor::makeRuntimeTelemetry(100, 5, 1, 0.8, 0.2, 1.33, true,
                                                       3, 2, 1, 0.4);
    require(telemetry.callbacks == 100, "telemetry callbacks");
    require(telemetry.overBudget == 5, "telemetry over budget");
    require(std::fabs(telemetry.overBudgetPercent - 5.0) < 0.0001, "telemetry over percent");
    require(telemetry.callbackGaps == 1, "telemetry callback gaps");
    require(telemetry.bypassed, "telemetry bypassed");
    require(std::fabs(telemetry.recentAverageMs - 0.4) < 0.0001,
            "telemetry stores recent callback average");
    require(std::fabs(telemetry.bufferFreePercent - 69.9248) < 0.001,
            "telemetry calculates recent callback headroom");
    require(std::fabs(ardor::recentCallbackAverageMs(100, 40.0, 200, 90.0, 0.2) - 0.5)
              < 0.0001,
            "recent callback average uses only the current sample window");
    const auto line = ardor::formatRuntimeTelemetry(telemetry);
    require(line.find("callbacks=100") != std::string::npos, "formatted callbacks");
    require(line.find("over%=5.00") != std::string::npos, "formatted over percent");
    require(line.find("gaps=1") != std::string::npos, "formatted callback gaps");
    require(line.find("recent_avg=0.40ms") != std::string::npos, "formatted recent average");
    require(line.find("buffer_free=69.92%") != std::string::npos, "formatted buffer headroom");
    require(line.find("bypassed=1") != std::string::npos, "formatted bypass");
    require(line.find("worker_over=3") != std::string::npos, "formatted worker overruns");
    require(line.find("nonfinite=2") != std::string::npos, "formatted non-finite blocks");
    require(line.find("block_mismatch=1") != std::string::npos, "formatted block mismatch");

    const auto telemetryRoot = std::filesystem::temp_directory_path()
      / ("ardor-telemetry-smoke-"
         + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(telemetryRoot);
    const auto telemetryPath = telemetryRoot / "runtime.telemetry";
    std::string telemetryError;
    require(ardor::writeRuntimeTelemetrySnapshot(telemetryPath, telemetry, telemetryError),
            telemetryError);
    std::ifstream telemetryInput(telemetryPath);
    std::string telemetrySnapshot;
    std::getline(telemetryInput, telemetrySnapshot);
    require(telemetrySnapshot == line, "telemetry snapshot matches formatted runtime state");
    require(!std::filesystem::exists(telemetryPath.string() + ".tmp"),
            "telemetry snapshot publish leaves no temporary file");
    std::filesystem::remove_all(telemetryRoot);

    return 0;
  } catch (const std::exception& error) {
    std::cerr << "preset_smoke failed: " << error.what() << '\n';
    return 1;
  }
}
