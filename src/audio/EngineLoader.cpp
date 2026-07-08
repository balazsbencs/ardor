#include "audio/EngineLoader.h"

#include "audio/WavIo.h"

#include <utility>

namespace ardor {

bool applyChainPlan(PedalEngine& engine, const ChainPlan& plan, const EngineLoadOptions& options, std::string& error)
{
  engine.prepareBlockSize(options.blockSize);
  engine.setInputGain(plan.inputGain);
  engine.setOutputGain(plan.outputGain);
  engine.setSafetyLimit(plan.safetyLimit);
  engine.setSafetyLimiterEnabled(true);

  bool loadedNam = false;
  bool loadedCab = false;
  for (const auto& block : plan.blocks) {
    if (block.status != ChainBlockStatus::Ready) {
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
      auto wav = readMonoWav(block.assetPath);
      if (wav.sampleRate != options.sampleRate) {
        error = "IR sample rate mismatch: " + block.assetPath.string();
        return false;
      }
      if (options.irSamples > 0 && wav.samples.size() > options.irSamples) {
        wav.samples.resize(options.irSamples);
      }
      engine.loadIr(std::move(wav.samples));
      loadedCab = true;
      continue;
    }
  }

  return true;
}

} // namespace ardor
