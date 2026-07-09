#include "audio/EngineLoader.h"

#include "audio/WavIo.h"

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

bool applyChainPlan(PedalEngine& engine, const ChainPlan& plan, const EngineLoadOptions& options, std::string& error)
{
  error.clear();
  engine.clearEffects();
  engine.prepareBlockSize(options.blockSize);
  engine.setInputGain(plan.inputGain);
  engine.setOutputGain(plan.outputGain);
  engine.setSafetyLimit(plan.safetyLimit);
  engine.setSafetyLimiterEnabled(true);

  bool loadedNam = false;
  bool loadedCab = false;
  for (const auto& block : plan.blocks) {
    if (block.status != ChainBlockStatus::Ready) {
      if (block.status != ChainBlockStatus::Disabled) {
        error = "block not ready: " + block.id + " (" + statusName(block.status) + ")";
        return false;
      }
      continue;
    }
    if (block.type == "nam" && !loadedNam) {
      if (!engine.loadNam(block.assetPath, options.sampleRate, static_cast<int>(options.blockSize))) {
        error = "failed to load NAM: " + block.assetPath.string();
        return false;
      }
      loadedNam = true;
      continue;
    }
    if (block.type == "cab" && !loadedCab) {
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
      if (options.irSamples > 0 && wav.samples.size() > options.irSamples) {
        wav.samples.resize(options.irSamples);
      }
      engine.loadIr(std::move(wav.samples));
      engine.setCabLevel(block.level);
      engine.setCabMix(block.mix);
      loadedCab = true;
      continue;
    }
  }

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
