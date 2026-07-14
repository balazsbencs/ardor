#include "audio/EngineLoader.h"

#include "audio/WavIo.h"

#include <cmath>
#include <exception>
#include <utility>

namespace ardor {

namespace {

const char* statusName(ChainBlockStatus status)
{
  switch (status) {
  case ChainBlockStatus::Ready:
    return "ready";
  case ChainBlockStatus::MissingAsset:
    return "missing asset";
  case ChainBlockStatus::Unsupported:
    return "unsupported";
  case ChainBlockStatus::Disabled:
    return "disabled";
  }
  return "unknown";
}

} // namespace

namespace {

bool prepareChainPlan(PedalEngine& engine, const ChainPlan& plan, const EngineLoadOptions& options, std::string& error)
{
  error.clear();
  if (options.sampleRate != 48000) {
    error = "the pedal engine requires a 48000 Hz sample rate";
    return false;
  }
  if (options.blockSize == 0) {
    error = "audio block size must be greater than zero";
    return false;
  }
  engine.clearEffects();
  engine.setSampleRate(options.sampleRate);
  engine.prepareBlockSize(options.blockSize);
  engine.setInputGain(plan.inputGain);
  engine.setOutputGain(plan.outputGain);
  engine.setSafetyLimit(plan.safetyLimit);
  engine.setSafetyLimiterEnabled(true);

  bool loadedNam = false;
  bool loadedCab = false;
  bool stereoEstablished = false;
  for (const auto& block : plan.blocks) {
    if (block.status != ChainBlockStatus::Ready) {
      if (block.status != ChainBlockStatus::Disabled) {
        error = "block not ready: " + block.id + " (" + statusName(block.status) + ")";
        return false;
      }
      continue;
    }
    if (block.type == "nam") {
      if (loadedNam) {
        error = "multiple NAM blocks are not supported: " + block.id;
        return false;
      }
      if (stereoEstablished) {
        error = "NAM must precede stereo effects: " + block.id;
        return false;
      }
      if (!engine.loadNam(block.assetPath, options.sampleRate, static_cast<int>(options.blockSize))) {
        error = "failed to load NAM: " + block.assetPath.string();
        return false;
      }
      loadedNam = true;
      continue;
    }
    if (block.type == "cab") {
      if (loadedCab) {
        error = "multiple cabinet blocks are not supported: " + block.id;
        return false;
      }
      if (stereoEstablished) {
        error = "cabinet must precede stereo effects: " + block.id;
        return false;
      }
      MonoWav wav;
      try {
        wav = readMonoWav(block.assetPath);
      } catch (const std::exception& e) {
        error = "failed to load IR: " + block.assetPath.string() + ": " + e.what();
        return false;
      }
      if (wav.sampleRate != options.sampleRate) {
        error = "IR sample rate mismatch: " + block.assetPath.string();
        return false;
      }
      std::string irError;
      if (!prepareMonoIr(wav, options.irSamples, irError)) {
        error = "invalid IR: " + block.assetPath.string() + ": " + irError;
        return false;
      }
      engine.addCab(std::move(wav.samples), block.level, block.mix);
      loadedCab = true;
      continue;
    }
    if (block.type == "mod" || block.type == "delay" || block.type == "reverb") {
      if (!engine.addDaisyFx(block.type, block.params, static_cast<float>(options.sampleRate), error)) {
        return false;
      }
      stereoEstablished = true;
      continue;
    }
    if (block.type == "dynamics") {
      if (!engine.addCompressor(block.params, static_cast<float>(options.sampleRate), error)) {
        return false;
      }
      continue;
    }
  }

  return true;
}

} // namespace

bool applyChainPlan(PedalEngine& engine, const ChainPlan& plan, const EngineLoadOptions& options, std::string& error)
{
  PedalEngine prepared;
  if (!prepareChainPlan(prepared, plan, options, error)) {
    return false;
  }
  engine.replacePreparedProgram(std::move(prepared));
  return true;
}

bool applyPreset(PedalEngine& engine, const Preset& preset, const std::filesystem::path& dataRoot,
                 const EngineLoadOptions& options, std::string& error)
{
  return applyChainPlan(engine, buildChainPlan(preset, dataRoot), options, error);
}

bool applyPresetSlot(PedalEngine& engine, const PresetStore& store, PresetSlot slot,
                     const std::filesystem::path& dataRoot, const EngineLoadOptions& options, std::string& error)
{
  try {
    return applyPreset(engine, store.load(slot), dataRoot, options, error);
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}

} // namespace ardor
