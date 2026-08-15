#include "audio/EngineLoader.h"

#include "audio/WavIo.h"

#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"
#include "dsp/DualAmpProcessor.h"
#include "dsp/DualRigProcessor.h"
#include "dynamics/CompressorProcessor.h"
#include "dynamics/NoiseGateProcessor.h"
#include "equalizer/EqParameters.h"
#include "wah/WahProcessor.h"

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

bool validateLoadOptions(const EngineLoadOptions& options, std::string& error)
{
  if (options.sampleRate != 48000) {
    error = "the pedal engine requires a 48000 Hz sample rate";
    return false;
  }
  if (options.blockSize == 0) {
    error = "audio block size must be greater than zero";
    return false;
  }
  return true;
}

bool validateDaisyParameters(const ChainBlockPlan& block, std::string& error)
{
  const auto* descriptor = findDaisyFxDescriptor(block.type, block.params.value("mode", ""));
  if (!descriptor) {
    error = "unsupported Daisy effect: " + block.type + "/" + block.params.value("mode", "");
    return false;
  }
  for (const auto& parameter : descriptor->params) {
    const auto it = block.params.find(parameter.key);
    if (it != block.params.end() && (!it->is_number() || !std::isfinite(it->get<float>()))) {
      error = "Daisy parameter must be finite: " + parameter.key;
      return false;
    }
  }
  return true;
}

bool namSlimmableSize(const ChainBlockPlan& block, float& size, std::string& error)
{
  // SlimmableContainer uses 0 for its smallest (nano) submodel and 1 for its
  // full submodel. Presets without an explicit preference retain the historic
  // full-model behavior.
  size = 1.0f;
  const auto useNano = block.params.find("useNano");
  if (useNano != block.params.end()) {
    if (!useNano->is_boolean()) {
      error = "NAM Use nano model must be a boolean";
      return false;
    }
    size = useNano->get<bool>() ? 0.0f : 1.0f;
    return true;
  }

  // Compatibility for presets created by the short-lived numeric Quality
  // control. New writes use the explicit boolean above.
  const auto it = block.params.find("quality");
  if (it == block.params.end()) {
    return true;
  }
  if (!it->is_number() || !std::isfinite(it->get<float>())) {
    error = "NAM Quality must be finite";
    return false;
  }
  size = it->get<float>();
  if (size < 0.0f || size > 1.0f) {
    error = "NAM Quality must be between 0 and 1";
    return false;
  }
  return true;
}

bool namInputMode(const ChainBlockPlan& block, NamInputMode& mode, std::string& error)
{
  mode = NamInputMode::Sum;
  const auto it = block.params.find("inputMode");
  if (it == block.params.end()) {
    return true;
  }
  if (!it->is_string()) {
    error = "NAM Input source must be sum, left, or right";
    return false;
  }
  const auto value = it->get<std::string>();
  if (value == "sum") {
    mode = NamInputMode::Sum;
  } else if (value == "left") {
    mode = NamInputMode::Left;
  } else if (value == "right") {
    mode = NamInputMode::Right;
  } else {
    error = "NAM Input source must be sum, left, or right";
    return false;
  }
  return true;
}

bool boolParameter(const ChainBlockPlan& block, const char* key, bool fallback,
                   bool& value, std::string& error)
{
  value = fallback;
  const auto it = block.params.find(key);
  if (it == block.params.end()) return true;
  if (!it->is_boolean()) {
    error = std::string{"parallel rig parameter must be boolean: "} + key;
    return false;
  }
  value = it->get<bool>();
  return true;
}

bool numberParameter(const ChainBlockPlan& block, const char* key, float fallback,
                     float minimum, float maximum, float& value, std::string& error)
{
  value = fallback;
  const auto it = block.params.find(key);
  if (it == block.params.end()) return true;
  if (!it->is_number()) {
    error = std::string{"parallel rig parameter must be finite: "} + key;
    return false;
  }
  value = it->get<float>();
  if (!std::isfinite(value) || value < minimum || value > maximum) {
    error = std::string{"parallel rig parameter out of range: "} + key;
    return false;
  }
  return true;
}

bool validateDualAmpParameters(const ChainBlockPlan& block, std::string& error)
{
  NamInputMode ignoredMode = NamInputMode::Sum;
  if (!namInputMode(block, ignoredMode, error)) return false;
  for (const char* prefix : {"left", "right"}) {
    const std::string nanoKey = std::string{prefix} + "UseNano";
    const std::string levelKey = std::string{prefix} + "CabLevelDb";
    const std::string mixKey = std::string{prefix} + "CabMix";
    const std::string polarityKey = std::string{prefix} + "PolarityInvert";
    bool ignoredBool = false;
    float ignoredNumber = 0.0f;
    if (!boolParameter(block, nanoKey.c_str(), false, ignoredBool, error)
        || !numberParameter(block, levelKey.c_str(), 0.0f, -60.0f, 12.0f,
                            ignoredNumber, error)
        || !numberParameter(block, mixKey.c_str(), 1.0f, 0.0f, 1.0f,
                            ignoredNumber, error)
        || !boolParameter(block, polarityKey.c_str(), false, ignoredBool, error)) {
      return false;
    }
  }
  return true;
}

float reverbParam(const nlohmann::json& params, const char* key, float fallback)
{
  if (!params.is_object()) return fallback;
  const auto it = params.find(key);
  if (it == params.end() || !it->is_number()) return fallback;
  const float value = it->get<float>();
  return std::isfinite(value) ? value : fallback;
}

// Splits an interleaved impulse into per-channel buffers and scales it so a
// dense tail comes back at roughly the level that went in.
//
// The cabinet path normalises against its estimated frequency-response peak,
// which is right for a short colouring filter but wrong here: a reverb impulse
// is thousands of times longer and it is total energy, not peak response, that
// sets how loud the tail returns. Normalising by energy keeps two impulses of
// very different length and loudness at a comparable wet level, so the Mix
// control means the same thing whichever one is loaded.
bool prepareReverbIr(const InterleavedWav& wav, std::vector<float>& left,
                     std::vector<float>& right, std::string& error)
{
  if (wav.samples.empty() || wav.channels == 0) {
    error = "impulse is empty";
    return false;
  }
  const std::size_t frames = wav.samples.size() / wav.channels;
  if (frames < 2) {
    error = "impulse is too short";
    return false;
  }

  left.assign(frames, 0.0f);
  right.assign(frames, 0.0f);
  for (std::size_t i = 0; i < frames; ++i) {
    left[i] = wav.samples[i * wav.channels];
    right[i] = wav.channels > 1 ? wav.samples[i * wav.channels + 1] : left[i];
  }

  double energy = 0.0;
  for (std::size_t i = 0; i < frames; ++i) {
    energy += static_cast<double>(left[i]) * left[i];
    energy += static_cast<double>(right[i]) * right[i];
  }
  energy *= 0.5;   // per channel
  if (!(energy > 0.0)) {
    error = "impulse carries no energy";
    return false;
  }
  // Attenuate only. A quiet capture stays quiet rather than being blown up into
  // whatever noise floor it was recorded with.
  const float scale = static_cast<float>(1.0 / std::sqrt(energy));
  if (scale < 1.0f) {
    for (std::size_t i = 0; i < frames; ++i) {
      left[i] *= scale;
      right[i] *= scale;
    }
  }
  return true;
}

bool loadPreparedIr(const std::filesystem::path& path, const EngineLoadOptions& options,
                    std::vector<float>& samples, std::string& error)
{
  MonoWav wav;
  try {
    wav = readMonoWav(path);
  } catch (const std::exception& e) {
    error = "failed to load IR: " + path.string() + ": " + e.what();
    return false;
  }
  if (wav.sampleRate != options.sampleRate) {
    error = "IR sample rate mismatch: " + path.string();
    return false;
  }
  std::string irError;
  if (!prepareMonoIr(wav, options.irSamples, irError)) {
    error = "invalid IR: " + path.string() + ": " + irError;
    return false;
  }
  samples = std::move(wav.samples);
  return true;
}

bool makeDualAmpLane(const ChainBlockPlan& block, std::size_t laneIndex,
                     const EngineLoadOptions& options, DualAmpLaneConfig& lane,
                     std::string& error)
{
  const char* prefix = laneIndex == 0 ? "left" : "right";
  const std::string nanoKey = std::string{prefix} + "UseNano";
  const std::string levelKey = std::string{prefix} + "CabLevelDb";
  const std::string mixKey = std::string{prefix} + "CabMix";
  const std::string polarityKey = std::string{prefix} + "PolarityInvert";
  bool useNano = false;
  float levelDb = 0.0f;
  if (!boolParameter(block, nanoKey.c_str(), false, useNano, error)
      || !numberParameter(block, levelKey.c_str(), 0.0f, -60.0f, 12.0f, levelDb, error)
      || !numberParameter(block, mixKey.c_str(), 1.0f, 0.0f, 1.0f, lane.cabMix, error)
      || !boolParameter(block, polarityKey.c_str(), false, lane.polarityInverted, error)) {
    return false;
  }
  lane.modelPath = block.dualAmpLanes[laneIndex].modelPath;
  lane.slimmableSize = useNano ? 0.0f : 1.0f;
  lane.cabLevel = std::pow(10.0f, levelDb / 20.0f);
  return loadPreparedIr(block.dualAmpLanes[laneIndex].cabPath, options, lane.impulse, error);
}

bool prepareLaneChain(RuntimeChain& chain, const std::vector<ChainBlockPlan>& blocks,
                      const EngineLoadOptions& options, std::string& error)
{
  chain.prepareBlockSize(options.blockSize);
  bool loadedNam = false;
  bool loadedCab = false;
  bool stereoEstablished = false;
  for (const auto& block : blocks) {
    if (block.status != ChainBlockStatus::Ready) {
      if (block.status != ChainBlockStatus::Disabled) {
        error = "dual rig lane block not ready: " + block.id + " (" + statusName(block.status) + ")";
        return false;
      }
      continue;
    }
    if (block.type == "dualRig" || block.type == "dualAmp") {
      error = "nested split blocks are not supported in a dual rig lane: " + block.id;
      return false;
    }
    if (block.type == "nam") {
      if (loadedNam) {
        error = "multiple NAM blocks are not supported in one dual rig lane: " + block.id;
        return false;
      }
      float slimmableSize = 1.0f;
      NamInputMode inputMode = NamInputMode::Sum;
      if (!namSlimmableSize(block, slimmableSize, error)
          || !namInputMode(block, inputMode, error)) {
        return false;
      }
      if (!chain.addNam(block.assetPath, options.sampleRate,
                        static_cast<int>(options.blockSize), block.id,
                        slimmableSize, inputMode)) {
        error = "failed to load dual rig NAM: " + block.assetPath.string();
        return false;
      }
      loadedNam = true;
      stereoEstablished = false;
      continue;
    }
    if (block.type == "cab") {
      if (loadedCab) {
        error = "multiple cabinet blocks are not supported in one dual rig lane: " + block.id;
        return false;
      }
      if (stereoEstablished) {
        error = "cabinet must precede stereo effects in a dual rig lane: " + block.id;
        return false;
      }
      std::vector<float> impulse;
      if (!loadPreparedIr(block.assetPath, options, impulse, error)) return false;
      chain.addCab(std::move(impulse), block.level, block.mix, block.id);
      loadedCab = true;
      continue;
    }
    if (block.type == "mod" || block.type == "delay" || block.type == "reverb") {
      if (!validateDaisyParameters(block, error)) return false;
      DaisyFxProcessor processor;
      if (!processor.configure(block.type, block.params,
                               static_cast<float>(options.sampleRate), error)) {
        return false;
      }
      chain.addDaisy(block.id, std::move(processor));
      stereoEstablished = true;
      continue;
    }
    if (block.type == "dynamics") {
      const auto mode = block.params.value("mode", std::string{});
      if (mode == "compressor") {
        CompressorProcessor processor;
        if (!processor.configure(block.params, static_cast<float>(options.sampleRate), error)) {
          return false;
        }
        chain.addCompressor(block.id, std::move(processor));
      } else if (mode == "noise_gate") {
        NoiseGateProcessor processor;
        if (!processor.configure(block.params, static_cast<float>(options.sampleRate), error)) {
          return false;
        }
        chain.addNoiseGate(block.id, std::move(processor));
      } else if (mode == "transient_shaper") {
        TransientShaperProcessor processor;
        if (!processor.configure(block.params, static_cast<float>(options.sampleRate), error)) {
          return false;
        }
        chain.addTransientShaper(block.id, std::move(processor));
      } else {
        error = "unsupported dynamics mode in dual rig lane: " + block.id;
        return false;
      }
      continue;
    }
    if (block.type == "eq") {
      if (!chain.addParametricEq(block.id, parametricEqParamsFromJson(block.params),
                                 static_cast<float>(options.sampleRate), error)) {
        return false;
      }
      continue;
    }
    if (block.type == "wah") {
      WahProcessor processor;
      if (!processor.configure(block.params, static_cast<float>(options.sampleRate),
                               block.assetPath, error)) {
        return false;
      }
      chain.addWah(block.id, std::move(processor));
      continue;
    }
    error = "unsupported block in dual rig lane: " + block.id;
    return false;
  }
  return true;
}

bool makeDualRigLane(const ChainBlockPlan& block, std::size_t laneIndex,
                     const EngineLoadOptions& options, DualRigLaneConfig& lane,
                     std::string& error)
{
  const char* prefix = laneIndex == 0 ? "left" : "right";
  const std::string levelKey = std::string{prefix} + "LevelDb";
  const std::string polarityKey = std::string{prefix} + "PolarityInvert";
  float levelDb = 0.0f;
  if (!numberParameter(block, levelKey.c_str(), 0.0f, -60.0f, 12.0f, levelDb, error)
      || !boolParameter(block, polarityKey.c_str(), false,
                        lane.polarityInverted, error)) {
    return false;
  }
  lane.level = std::pow(10.0f, levelDb / 20.0f);
  lane.chain = std::make_unique<RuntimeChain>();
  return prepareLaneChain(*lane.chain, block.lanes[laneIndex], options, error);
}

bool preflightChainPlan(const ChainPlan& plan, const EngineLoadOptions& options, std::string& error)
{
  error.clear();
  if (!validateLoadOptions(options, error)) {
    return false;
  }

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
      float ignoredSize = 1.0f;
      if (!namSlimmableSize(block, ignoredSize, error)) {
        return false;
      }
      NamInputMode ignoredInputMode = NamInputMode::Sum;
      if (!namInputMode(block, ignoredInputMode, error)) {
        return false;
      }
      loadedNam = true;
      stereoEstablished = false;
      continue;
    }
    if (block.type == "dualAmp") {
      if (loadedNam || loadedCab) {
        error = "dual amp cannot be combined with standalone NAM or cabinet blocks: " + block.id;
        return false;
      }
      if (!validateDualAmpParameters(block, error)) {
        return false;
      }
      for (const auto& lane : block.dualAmpLanes) {
        std::vector<float> ignored;
        if (!loadPreparedIr(lane.cabPath, options, ignored, error)) return false;
      }
      loadedNam = true;
      loadedCab = true;
      stereoEstablished = true;
      continue;
    }
    if (block.type == "dualRig") {
      if (loadedNam || loadedCab) {
        error = "dual rig cannot be combined with standalone NAM or cabinet blocks: " + block.id;
        return false;
      }
      NamInputMode ignoredMode = NamInputMode::Sum;
      DualRigLaneConfig left;
      DualRigLaneConfig right;
      if (!namInputMode(block, ignoredMode, error)
          || !makeDualRigLane(block, 0, options, left, error)
          || !makeDualRigLane(block, 1, options, right, error)) {
        return false;
      }
      loadedNam = true;
      loadedCab = true;
      stereoEstablished = true;
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
      loadedCab = true;
      continue;
    }
    if (block.type == "mod" || block.type == "delay" || block.type == "reverb") {
      if (!validateDaisyParameters(block, error)) {
        return false;
      }
      stereoEstablished = true;
    }
  }
  return true;
}

bool prepareChainPlan(PedalEngine& engine, const ChainPlan& plan, const EngineLoadOptions& options, std::string& error)
{
  error.clear();
  if (!validateLoadOptions(options, error)) {
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
      float slimmableSize = 1.0f;
      if (!namSlimmableSize(block, slimmableSize, error)) {
        return false;
      }
      NamInputMode inputMode = NamInputMode::Sum;
      if (!namInputMode(block, inputMode, error)) {
        return false;
      }
      if (!engine.loadNam(block.assetPath, options.sampleRate, static_cast<int>(options.blockSize),
                          block.id, slimmableSize, inputMode)) {
        error = "failed to load NAM: " + block.assetPath.string();
        return false;
      }
      loadedNam = true;
      stereoEstablished = false;
      engine.setBlockEnabled(block.id, block.enabled);
      continue;
    }
    if (block.type == "dualAmp") {
      if (loadedNam || loadedCab) {
        error = "dual amp cannot be combined with standalone NAM or cabinet blocks: " + block.id;
        return false;
      }
      DualAmpLaneConfig left;
      DualAmpLaneConfig right;
      NamInputMode inputMode = NamInputMode::Sum;
      if (!namInputMode(block, inputMode, error)
          || !makeDualAmpLane(block, 0, options, left, error)
          || !makeDualAmpLane(block, 1, options, right, error)) {
        return false;
      }
      if (!engine.addDualAmp(block.id, std::move(left), std::move(right), inputMode,
                             options.sampleRate, static_cast<int>(options.blockSize),
                             options.parallelRigs, options.rigWorkerCpu, error)) {
        return false;
      }
      loadedNam = true;
      loadedCab = true;
      stereoEstablished = true;
      engine.setBlockEnabled(block.id, block.enabled);
      continue;
    }
    if (block.type == "dualRig") {
      if (loadedNam || loadedCab) {
        error = "dual rig cannot be combined with standalone NAM or cabinet blocks: " + block.id;
        return false;
      }
      DualRigLaneConfig left;
      DualRigLaneConfig right;
      NamInputMode inputMode = NamInputMode::Sum;
      if (!namInputMode(block, inputMode, error)
          || !makeDualRigLane(block, 0, options, left, error)
          || !makeDualRigLane(block, 1, options, right, error)) {
        return false;
      }
      if (!engine.addDualRig(block.id, std::move(left), std::move(right), inputMode,
                             options.sampleRate, static_cast<int>(options.blockSize),
                             options.parallelRigs, options.rigWorkerCpu, error)) {
        return false;
      }
      loadedNam = true;
      loadedCab = true;
      stereoEstablished = true;
      engine.setBlockEnabled(block.id, block.enabled);
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
      engine.addCab(std::move(wav.samples), block.level, block.mix, block.id);
      loadedCab = true;
      engine.setBlockEnabled(block.id, block.enabled);
      continue;
    }
    if (block.type == "irreverb") {
      InterleavedWav wav;
      try {
        wav = readInterleavedWav(block.assetPath);
      } catch (const std::exception& e) {
        error = "failed to load reverb impulse: " + block.assetPath.string() + ": " + e.what();
        return false;
      }
      if (wav.sampleRate != options.sampleRate) {
        error = "reverb impulse sample rate mismatch: " + block.assetPath.string();
        return false;
      }
      std::vector<float> left;
      std::vector<float> right;
      std::string irError;
      if (!prepareReverbIr(wav, left, right, irError)) {
        error = "invalid reverb impulse: " + block.assetPath.string() + ": " + irError;
        return false;
      }
      if (!engine.addIrReverb(block.id, std::move(left), std::move(right),
                              static_cast<float>(options.sampleRate), error)) {
        return false;
      }
      const auto& params = block.params;
      engine.setIrReverbParameter(block.id, "mix", reverbParam(params, "mix", 0.35f));
      engine.setIrReverbParameter(block.id, "levelDb", reverbParam(params, "levelDb", 0.0f));
      engine.setIrReverbParameter(block.id, "preDelayMs", reverbParam(params, "preDelayMs", 0.0f));
      engine.setIrReverbParameter(block.id, "lowCutHz", reverbParam(params, "lowCutHz", 20.0f));
      engine.setIrReverbParameter(block.id, "highCutHz", reverbParam(params, "highCutHz", 20000.0f));
      stereoEstablished = true;
      engine.setBlockEnabled(block.id, block.enabled);
      continue;
    }
    if (block.type == "stereo") {
      if (!engine.addStereoWidener(block.id, static_cast<float>(options.sampleRate), error)) {
        return false;
      }
      const auto& params = block.params;
      engine.setStereoWidenerParameter(block.id, "width", reverbParam(params, "width", 1.0f));
      engine.setStereoWidenerParameter(block.id, "delayMs", reverbParam(params, "delayMs", 0.0f));
      engine.setStereoWidenerParameter(block.id, "bassMonoHz", reverbParam(params, "bassMonoHz", 0.0f));
      engine.setStereoWidenerParameter(block.id, "levelDb", reverbParam(params, "levelDb", 0.0f));
      stereoEstablished = true;
      engine.setBlockEnabled(block.id, block.enabled);
      continue;
    }
    if (block.type == "mod" || block.type == "delay" || block.type == "reverb") {
      if (!engine.addDaisyFx(block.id, block.type, block.params, static_cast<float>(options.sampleRate), error)) {
        return false;
      }
      stereoEstablished = true;
      engine.setBlockEnabled(block.id, block.enabled);
      continue;
    }
    if (block.type == "dynamics") {
      const auto mode = block.params.value("mode", std::string{});
      if (mode == "compressor") {
        if (!engine.addCompressor(
              block.id, block.params, static_cast<float>(options.sampleRate), error)) {
          return false;
        }
      } else if (mode == "noise_gate") {
        if (!engine.addNoiseGate(
              block.id, block.params, static_cast<float>(options.sampleRate), error)) {
          return false;
        }
      } else if (mode == "transient_shaper") {
        if (!engine.addTransientShaper(
              block.id, block.params, static_cast<float>(options.sampleRate), error)) {
          return false;
        }
      } else {
        error = "unsupported dynamics mode: " + block.id;
        return false;
      }
      engine.setBlockEnabled(block.id, block.enabled);
      continue;
    }
    if (block.type == "eq") {
      if (!engine.addParametricEq(block.id, block.params, static_cast<float>(options.sampleRate), error)) {
        return false;
      }
      engine.setBlockEnabled(block.id, block.enabled);
      continue;
    }
    if (block.type == "wah") {
      if (!engine.addWah(block.id, block.params, static_cast<float>(options.sampleRate),
                         block.assetPath, error)) {
        return false;
      }
      engine.setBlockEnabled(block.id, block.enabled);
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

bool preflightPreset(const Preset& preset, const std::filesystem::path& dataRoot,
                     const EngineLoadOptions& options, std::string& error)
{
  try {
    return preflightChainPlan(buildChainPlan(preset, dataRoot), options, error);
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}

bool preflightPresetSlot(const PresetStore& store, PresetSlot slot,
                         const std::filesystem::path& dataRoot,
                         const EngineLoadOptions& options, std::string& error)
{
  try {
    return preflightPreset(store.load(slot), dataRoot, options, error);
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
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
