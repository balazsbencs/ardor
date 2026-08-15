#include "dsp/RuntimeChain.h"

#include "dsp/DualAmpProcessor.h"
#include "dsp/DualRigProcessor.h"
#include "dsp/IrConvolver.h"
#include "dsp/NamProcessor.h"
#include "equalizer/ParametricEqProcessor.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <utility>

namespace ardor {

namespace {

struct LevelState {
  std::atomic<uint32_t> peakBits{0};
  std::atomic<uint64_t> overloadFrames{0};
};

void observeLevel(LevelState& state, float left, float right)
{
  const float peak = std::max(std::fabs(left), std::fabs(right));
  const uint32_t bits = std::bit_cast<uint32_t>(peak);
  uint32_t previous = state.peakBits.load(std::memory_order_relaxed);
  while (previous < bits
         && !state.peakBits.compare_exchange_weak(previous, bits, std::memory_order_relaxed)) {
  }
  if (peak > 1.0f) {
    state.overloadFrames.fetch_add(1, std::memory_order_relaxed);
  }
}

ClipStageSnapshot takeLevel(LevelState& state, SignalStageKind kind, const std::string& id)
{
  return {
    kind,
    id,
    std::bit_cast<float>(state.peakBits.exchange(0, std::memory_order_relaxed)),
    state.overloadFrames.exchange(0, std::memory_order_relaxed),
  };
}

} // namespace

struct RuntimeChain::Block {
  enum class Kind {
    Nam,
    Cab,
    IrReverb,
    Daisy,
    Compressor,
    NoiseGate,
    Equalizer,
    Wah,
    DualAmp,
    DualRig
  };

  Kind kind = Kind::Cab;
  std::string id;
  std::unique_ptr<NamProcessor> nam;
  std::unique_ptr<IrConvolver> cab;
  std::unique_ptr<IrReverbProcessor> irReverb;
  std::unique_ptr<DaisyFxProcessor> daisy;
  std::unique_ptr<CompressorProcessor> compressor;
  std::unique_ptr<NoiseGateProcessor> noiseGate;
  std::unique_ptr<ParametricEqProcessor> equalizer;
  std::unique_ptr<WahProcessor> wah;
  std::unique_ptr<DualAmpProcessor> dualAmp;
  std::unique_ptr<DualRigProcessor> dualRig;
  std::unique_ptr<LevelState> meter = std::make_unique<LevelState>();
  std::unique_ptr<std::atomic<bool>> enabled = std::make_unique<std::atomic<bool>>(true);
  float level = 1.0f;
  float mix = 1.0f;
  NamInputMode namInputMode = NamInputMode::Sum;
};

struct RuntimeChain::FaultState {
  std::atomic<uint64_t> count{0};
  std::atomic<int> firstIndex{-1};
};

RuntimeChain::RuntimeChain() : faults_(std::make_shared<FaultState>()) {}
RuntimeChain::~RuntimeChain() = default;
RuntimeChain::RuntimeChain(RuntimeChain&&) noexcept = default;
RuntimeChain& RuntimeChain::operator=(RuntimeChain&&) noexcept = default;

void RuntimeChain::prepareBlockSize(size_t frames)
{
  if (frames == 0) {
    return;
  }

  if (blockSize_ == frames && leftA_.size() == frames) {
    return;
  }

  blockSize_ = frames;
  leftA_.assign(frames, 0.0f);
  rightA_.assign(frames, 0.0f);
  leftB_.assign(frames, 0.0f);
  rightB_.assign(frames, 0.0f);
  monoScratch_.assign(frames, 0.0f);
  for (auto& block : blocks_) {
    if (block.cab) {
      block.cab->prepareBlockSize(frames);
    }
    if (block.dualAmp) {
      block.dualAmp->prepareBlockSize(frames);
    }
    if (block.dualRig) {
      block.dualRig->prepareBlockSize(frames);
    }
  }
}

void RuntimeChain::clear()
{
  blocks_.clear();
  faults_ = std::make_shared<FaultState>();
}

bool RuntimeChain::addNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize,
                          std::string id, float slimmableSize, NamInputMode inputMode)
{
  auto nam = std::make_unique<NamProcessor>();
  if (!nam->load(modelPath, sampleRate, maxBlockSize, slimmableSize)) {
    return false;
  }
  Block block;
  block.kind = Block::Kind::Nam;
  block.id = std::move(id);
  block.nam = std::move(nam);
  block.namInputMode = inputMode;
  blocks_.push_back(std::move(block));
  return true;
}

bool RuntimeChain::addDualAmp(std::string id, DualAmpLaneConfig left, DualAmpLaneConfig right,
                              NamInputMode inputMode, double sampleRate, int maxBlockSize,
                              bool requestParallel, int workerCpu, std::string& error)
{
  auto dualAmp = std::make_unique<DualAmpProcessor>();
  if (!dualAmp->configure(std::move(left), std::move(right), inputMode, sampleRate,
                          maxBlockSize, requestParallel, workerCpu, error)) {
    return false;
  }
  Block block;
  block.kind = Block::Kind::DualAmp;
  block.id = std::move(id);
  block.dualAmp = std::move(dualAmp);
  blocks_.push_back(std::move(block));
  return true;
}

bool RuntimeChain::addDualRig(std::string id, DualRigLaneConfig left, DualRigLaneConfig right,
                              NamInputMode inputMode, double sampleRate, std::size_t blockSize,
                              bool requestParallel, int workerCpu, std::string& error)
{
  auto dualRig = std::make_unique<DualRigProcessor>();
  if (!dualRig->configure(std::move(left), std::move(right), inputMode, sampleRate,
                          blockSize, requestParallel, workerCpu, error)) {
    return false;
  }
  Block block;
  block.kind = Block::Kind::DualRig;
  block.id = std::move(id);
  block.dualRig = std::move(dualRig);
  blocks_.push_back(std::move(block));
  return true;
}

void RuntimeChain::addCab(std::vector<float> impulse, float level, float mix, std::string id)
{
  auto cab = std::make_unique<IrConvolver>();
  cab->loadImpulse(std::move(impulse));
  cab->prepareBlockSize(blockSize_);

  Block block;
  block.kind = Block::Kind::Cab;
  block.id = std::move(id);
  block.cab = std::move(cab);
  block.level = std::max(0.0f, level);
  block.mix = std::clamp(mix, 0.0f, 1.0f);
  blocks_.push_back(std::move(block));
}

bool RuntimeChain::addIrReverb(std::string id, std::vector<float> left, std::vector<float> right,
                               float sampleRate, std::string& error)
{
  auto reverb = std::make_unique<IrReverbProcessor>();
  if (!reverb->load(std::move(left), std::move(right), sampleRate, error)) {
    return false;
  }

  Block block;
  block.kind = Block::Kind::IrReverb;
  block.id = std::move(id);
  block.irReverb = std::move(reverb);
  blocks_.push_back(std::move(block));
  return true;
}

bool RuntimeChain::setIrReverbParameter(const std::string& id, const std::string& key, float value)
{
  for (auto& block : blocks_) {
    if (block.kind != Block::Kind::IrReverb || block.id != id) continue;
    if (key == "mix") block.irReverb->setMix(value);
    else if (key == "levelDb") block.irReverb->setLevelDb(value);
    else if (key == "preDelayMs") block.irReverb->setPreDelayMs(value);
    else if (key == "lowCutHz") block.irReverb->setLowCutHz(value);
    else if (key == "highCutHz") block.irReverb->setHighCutHz(value);
    else return false;
    return true;
  }
  return false;
}

void RuntimeChain::addDaisy(std::string id, DaisyFxProcessor processor)
{
  Block block;
  block.kind = Block::Kind::Daisy;
  block.id = std::move(id);
  block.daisy = std::make_unique<DaisyFxProcessor>(std::move(processor));
  blocks_.push_back(std::move(block));
}

bool RuntimeChain::setDaisyParameter(const std::string& id, const std::string& key, float normalized)
{
  for (auto& block : blocks_) {
    if (block.kind == Block::Kind::Daisy && block.id == id) {
      return block.daisy->setParameterTarget(key, normalized);
    }
    if (block.kind == Block::Kind::DualRig
        && block.dualRig->setDaisyParameter(id, key, normalized)) return true;
  }
  return false;
}

void RuntimeChain::addCompressor(std::string id, CompressorProcessor processor)
{
  Block block;
  block.kind = Block::Kind::Compressor;
  block.id = std::move(id);
  block.compressor = std::make_unique<CompressorProcessor>(std::move(processor));
  blocks_.push_back(std::move(block));
}

bool RuntimeChain::setCompressorParameter(const std::string& id, const std::string& key, float value)
{
  for (auto& block : blocks_) {
    if (block.kind == Block::Kind::Compressor && block.id == id) {
      return block.compressor->setParameterTarget(key, value);
    }
    if (block.kind == Block::Kind::DualRig
        && block.dualRig->setCompressorParameter(id, key, value)) return true;
  }
  return false;
}

bool RuntimeChain::compressorGainReductionDb(const std::string& id, float& outDb) const
{
  for (const auto& block : blocks_) {
    if (block.kind == Block::Kind::Compressor && block.id == id) {
      outDb = block.compressor->currentGainReductionDb();
      return true;
    }
    if (block.kind == Block::Kind::DualRig
        && block.dualRig->compressorGainReductionDb(id, outDb)) return true;
  }
  return false;
}

void RuntimeChain::addNoiseGate(std::string id, NoiseGateProcessor processor)
{
  Block block;
  block.kind = Block::Kind::NoiseGate;
  block.id = std::move(id);
  block.noiseGate = std::make_unique<NoiseGateProcessor>(std::move(processor));
  blocks_.push_back(std::move(block));
}

bool RuntimeChain::setNoiseGateParameter(const std::string& id, const std::string& key, float value)
{
  for (auto& block : blocks_) {
    if (block.kind == Block::Kind::NoiseGate && block.id == id) {
      return block.noiseGate->setParameterTarget(key, value);
    }
    if (block.kind == Block::Kind::DualRig
        && block.dualRig->setNoiseGateParameter(id, key, value)) return true;
  }
  return false;
}

void RuntimeChain::addWah(std::string id, WahProcessor processor)
{
  Block block;
  block.kind = Block::Kind::Wah;
  block.id = std::move(id);
  block.wah = std::make_unique<WahProcessor>(std::move(processor));
  blocks_.push_back(std::move(block));
}

bool RuntimeChain::setWahParameter(const std::string& id, const std::string& key, float value)
{
  for (auto& block : blocks_) {
    if (block.kind == Block::Kind::Wah && block.id == id) {
      return block.wah->setParameterTarget(key, value);
    }
    if (block.kind == Block::Kind::DualRig
        && block.dualRig->setWahParameter(id, key, value)) return true;
  }
  return false;
}

bool RuntimeChain::setBlockEnabled(const std::string& id, bool enabled)
{
  for (auto& block : blocks_) {
    if (block.id == id) {
      block.enabled->store(enabled, std::memory_order_relaxed);
      return true;
    }
    if (block.kind == Block::Kind::DualRig
        && block.dualRig->setBlockEnabled(id, enabled)) return true;
  }
  return false;
}

bool RuntimeChain::addParametricEq(std::string id, const ParametricEqParams& params,
                                   float sampleRate, std::string& error)
{
  auto equalizer = std::make_unique<ParametricEqProcessor>();
  if (!equalizer->configure(params, sampleRate, error)) {
    return false;
  }

  Block block;
  block.kind = Block::Kind::Equalizer;
  block.id = std::move(id);
  block.equalizer = std::move(equalizer);
  blocks_.push_back(std::move(block));
  return true;
}

bool RuntimeChain::setParametricEqBand(const std::string& id, std::size_t band, const EqBandParams& params)
{
  for (auto& block : blocks_) {
    if (block.kind == Block::Kind::Equalizer && block.id == id) {
      return block.equalizer->setBandTarget(band, params);
    }
    if (block.kind == Block::Kind::DualRig
        && block.dualRig->setParametricEqBand(id, band, params)) return true;
  }
  return false;
}

bool RuntimeChain::setParametricEqPassFilter(const std::string& id, EqPassFilterKind kind,
                                             const EqPassFilterParams& params)
{
  for (auto& block : blocks_) {
    if (block.kind == Block::Kind::Equalizer && block.id == id) {
      return block.equalizer->setPassFilterTarget(kind, params);
    }
    if (block.kind == Block::Kind::DualRig
        && block.dualRig->setParametricEqPassFilter(id, kind, params)) return true;
  }
  return false;
}

StereoSample RuntimeChain::process(StereoSample input, float cabLevel, float cabMix)
{
  StereoSample current = input;
  for (size_t index = 0; index < blocks_.size(); ++index) {
    auto& block = blocks_[index];
    if (!block.enabled->load(std::memory_order_relaxed)) {
      observeLevel(*block.meter, current.left, current.right);
      continue;
    }
    switch (block.kind) {
    case Block::Kind::Nam: {
      const float mono = block.nam->process(
        routeNamInput(block.namInputMode, current.left, current.right));
      current = {mono, mono};
      break;
    }
    case Block::Kind::Cab: {
      const float dry = current.left;
      const float level = cabLevel >= 0.0f ? cabLevel : block.level;
      const float mix = cabMix >= 0.0f ? cabMix : block.mix;
      const float wet = block.cab->processSample(dry) * level;
      const float mixed = (wet * mix) + (dry * (1.0f - mix));
      current = {mixed, mixed};
      break;
    }
    case Block::Kind::IrReverb:
      current = block.irReverb->process(current);
      break;
    case Block::Kind::Daisy:
      current = block.daisy->process(current);
      break;
    case Block::Kind::Compressor:
      current = block.compressor->process(current);
      break;
    case Block::Kind::NoiseGate:
      current = block.noiseGate->process(current);
      break;
    case Block::Kind::Equalizer:
      block.equalizer->process(current.left, current.right);
      break;
    case Block::Kind::Wah:
      current = block.wah->process(current);
      break;
    case Block::Kind::DualAmp:
      block.dualAmp->process(current.left, current.right, current.left, current.right);
      break;
    case Block::Kind::DualRig:
      block.dualRig->process(current.left, current.right, current.left, current.right);
      break;
    }
    if (!std::isfinite(current.left) || !std::isfinite(current.right)) {
      current = {};
      faults_->count.fetch_add(1, std::memory_order_relaxed);
      int expected = -1;
      faults_->firstIndex.compare_exchange_strong(expected, static_cast<int>(index),
                                                   std::memory_order_relaxed);
    }
    observeLevel(*block.meter, current.left, current.right);
  }
  return current;
}

void RuntimeChain::processBlock(const float* input, float* left, float* right, size_t frames,
                                const float* cabLevels, const float* cabMixes)
{
  if (frames == 0) {
    return;
  }

  // This path is normally prepared by EngineLoader/MiniaudioBackend before
  // audio begins. The fallback keeps the offline API usable, but callers in a
  // realtime callback must prepare a fixed quantum ahead of time.
  if (blockSize_ < frames || leftA_.size() < frames) {
    prepareBlockSize(frames);
  }

  std::copy(input, input + frames, leftA_.begin());
  std::copy(input, input + frames, rightA_.begin());

  float* currentLeft = leftA_.data();
  float* currentRight = rightA_.data();
  float* nextLeft = leftB_.data();
  float* nextRight = rightB_.data();
  bool currentIsStereo = false;

  for (auto& block : blocks_) {
    if (!block.enabled->load(std::memory_order_relaxed)) {
      std::copy(currentLeft, currentLeft + frames, nextLeft);
      std::copy(currentRight, currentRight + frames, nextRight);
    } else switch (block.kind) {
    case Block::Kind::Nam: {
      for (size_t i = 0; i < frames; ++i) {
        monoScratch_[i] = routeNamInput(block.namInputMode, currentLeft[i], currentRight[i]);
      }
      block.nam->processBlock(monoScratch_.data(), nextLeft, frames);
      std::copy(nextLeft, nextLeft + frames, nextRight);
      currentIsStereo = false;
      break;
    }
    case Block::Kind::Cab: {
      const float* cabInput = currentLeft;
      if (currentIsStereo) {
        for (size_t i = 0; i < frames; ++i) {
          monoScratch_[i] = (currentLeft[i] + currentRight[i]) * 0.5f;
        }
        cabInput = monoScratch_.data();
      }
      block.cab->processBlock(cabInput, nextLeft, frames);
      for (size_t i = 0; i < frames; ++i) {
        const float level = cabLevels ? cabLevels[i] : block.level;
        const float mix = cabMixes ? cabMixes[i] : block.mix;
        const float wet = nextLeft[i] * level;
        nextLeft[i] = wet * mix + cabInput[i] * (1.0f - mix);
      }
      std::copy(nextLeft, nextLeft + frames, nextRight);
      currentIsStereo = false;
      break;
    }
    case Block::Kind::IrReverb:
      // The processor buffers internally to its own partition size, so a
      // per-sample loop here costs nothing extra and keeps both paths identical.
      for (size_t i = 0; i < frames; ++i) {
        const auto processed = block.irReverb->process({currentLeft[i], currentRight[i]});
        nextLeft[i] = processed.left;
        nextRight[i] = processed.right;
      }
      currentIsStereo = true;
      break;
    case Block::Kind::Daisy:
      for (size_t i = 0; i < frames; ++i) {
        const auto processed = block.daisy->process({currentLeft[i], currentRight[i]});
        nextLeft[i] = processed.left;
        nextRight[i] = processed.right;
      }
      currentIsStereo = true;
      break;
    case Block::Kind::Compressor:
      for (size_t i = 0; i < frames; ++i) {
        const auto processed = block.compressor->process({currentLeft[i], currentRight[i]});
        nextLeft[i] = processed.left;
        nextRight[i] = processed.right;
      }
      break;
    case Block::Kind::NoiseGate:
      for (size_t i = 0; i < frames; ++i) {
        const auto processed = block.noiseGate->process({currentLeft[i], currentRight[i]});
        nextLeft[i] = processed.left;
        nextRight[i] = processed.right;
      }
      break;
    case Block::Kind::Equalizer:
      block.equalizer->processBlock(currentLeft, currentRight, nextLeft, nextRight, frames);
      currentIsStereo = true;
      break;
    case Block::Kind::Wah:
      for (size_t i = 0; i < frames; ++i) {
        const auto processed = block.wah->process({currentLeft[i], currentRight[i]});
        nextLeft[i] = processed.left;
        nextRight[i] = processed.right;
      }
      currentIsStereo = false;
      break;
    case Block::Kind::DualAmp:
      block.dualAmp->processBlock(currentLeft, currentRight, nextLeft, nextRight, frames);
      currentIsStereo = true;
      break;
    case Block::Kind::DualRig:
      block.dualRig->processBlock(currentLeft, currentRight, nextLeft, nextRight, frames);
      currentIsStereo = true;
      break;
    }

    bool nonFinite = false;
    for (size_t i = 0; i < frames; ++i) {
      if (!std::isfinite(nextLeft[i]) || !std::isfinite(nextRight[i])) {
        nextLeft[i] = 0.0f;
        nextRight[i] = 0.0f;
        nonFinite = true;
      }
    }
    if (nonFinite) {
      faults_->count.fetch_add(1, std::memory_order_relaxed);
      const int index = static_cast<int>(&block - blocks_.data());
      int expected = -1;
      faults_->firstIndex.compare_exchange_strong(expected, index, std::memory_order_relaxed);
    }

    float peak = 0.0f;
    uint64_t overloadFrames = 0;
    for (size_t i = 0; i < frames; ++i) {
      const float framePeak = std::max(std::fabs(nextLeft[i]), std::fabs(nextRight[i]));
      peak = std::max(peak, framePeak);
      overloadFrames += framePeak > 1.0f ? 1U : 0U;
    }
    const uint32_t peakBits = std::bit_cast<uint32_t>(peak);
    uint32_t previousPeak = block.meter->peakBits.load(std::memory_order_relaxed);
    while (previousPeak < peakBits
           && !block.meter->peakBits.compare_exchange_weak(previousPeak, peakBits,
                                                           std::memory_order_relaxed)) {
    }
    if (overloadFrames > 0) {
      block.meter->overloadFrames.fetch_add(overloadFrames, std::memory_order_relaxed);
    }

    std::swap(currentLeft, nextLeft);
    std::swap(currentRight, nextRight);
  }

  std::copy(currentLeft, currentLeft + frames, left);
  std::copy(currentRight, currentRight + frames, right);
}

uint64_t RuntimeChain::nonFiniteBlockCount() const noexcept
{
  return faults_->count.load(std::memory_order_relaxed);
}

uint64_t RuntimeChain::parallelWaitOverBudgetCount() const noexcept
{
  uint64_t count = 0;
  for (const auto& block : blocks_) {
    if (block.dualAmp) {
      count += block.dualAmp->parallelWaitOverBudgetCount();
    }
    if (block.dualRig) {
      count += block.dualRig->parallelWaitOverBudgetCount();
    }
  }
  return count;
}

std::string RuntimeChain::firstNonFiniteBlockId() const
{
  const int index = faults_->firstIndex.load(std::memory_order_relaxed);
  if (index < 0 || static_cast<size_t>(index) >= blocks_.size()) return {};
  return blocks_[static_cast<size_t>(index)].id;
}

std::vector<ClipStageSnapshot> RuntimeChain::takeClipDiagnostics()
{
  std::vector<ClipStageSnapshot> diagnostics;
  diagnostics.reserve(blocks_.size());
  for (auto& block : blocks_) {
    SignalStageKind kind = SignalStageKind::Cab;
    switch (block.kind) {
    case Block::Kind::Nam:
      kind = SignalStageKind::Nam;
      break;
    case Block::Kind::Cab:
      kind = SignalStageKind::Cab;
      break;
    case Block::Kind::IrReverb:
      kind = SignalStageKind::IrReverb;
      break;
    case Block::Kind::Daisy:
      kind = SignalStageKind::Daisy;
      break;
    case Block::Kind::Compressor:
      kind = SignalStageKind::Compressor;
      break;
    case Block::Kind::NoiseGate:
      kind = SignalStageKind::NoiseGate;
      break;
    case Block::Kind::Equalizer:
      kind = SignalStageKind::Equalizer;
      break;
    case Block::Kind::Wah:
      kind = SignalStageKind::Wah;
      break;
    case Block::Kind::DualAmp:
      kind = SignalStageKind::DualAmp;
      break;
    case Block::Kind::DualRig:
      kind = SignalStageKind::DualRig;
      break;
    }
    diagnostics.push_back(takeLevel(*block.meter, kind, block.id));
  }
  return diagnostics;
}

void RuntimeChain::reset()
{
  for (auto& block : blocks_) {
    if (block.nam) {
      block.nam->reset();
    }
    if (block.cab) {
      block.cab->reset();
    }
    if (block.daisy) {
      block.daisy->reset();
    }
    if (block.compressor) {
      block.compressor->reset();
    }
    if (block.noiseGate) {
      block.noiseGate->reset();
    }
    if (block.equalizer) {
      block.equalizer->reset();
    }
    if (block.wah) {
      block.wah->reset();
    }
    if (block.dualAmp) {
      block.dualAmp->reset();
    }
    if (block.dualRig) {
      block.dualRig->reset();
    }
  }
}

size_t RuntimeChain::tailFrames() const noexcept
{
  size_t tail = 0;
  for (const auto& block : blocks_) {
    if (block.cab) {
      tail += block.cab->tailFrames();
    }
    if (block.daisy) {
      tail += block.daisy->tailFrames();
    }
    if (block.dualAmp) {
      tail += block.dualAmp->tailFrames();
    }
    if (block.dualRig) {
      tail += block.dualRig->tailFrames();
    }
  }
  return tail;
}

} // namespace ardor
