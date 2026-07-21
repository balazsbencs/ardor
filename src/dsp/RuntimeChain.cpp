#include "dsp/RuntimeChain.h"

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
    Daisy,
    Compressor,
    Equalizer
  };

  Kind kind = Kind::Cab;
  std::string id;
  std::unique_ptr<NamProcessor> nam;
  std::unique_ptr<IrConvolver> cab;
  std::unique_ptr<DaisyFxProcessor> daisy;
  std::unique_ptr<CompressorProcessor> compressor;
  std::unique_ptr<ParametricEqProcessor> equalizer;
  std::unique_ptr<LevelState> meter = std::make_unique<LevelState>();
  float level = 1.0f;
  float mix = 1.0f;
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
  }
}

void RuntimeChain::clear()
{
  blocks_.clear();
  faults_ = std::make_shared<FaultState>();
}

bool RuntimeChain::addNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize,
                          std::string id, float slimmableSize)
{
  auto nam = std::make_unique<NamProcessor>();
  if (!nam->load(modelPath, sampleRate, maxBlockSize, slimmableSize)) {
    return false;
  }
  Block block;
  block.kind = Block::Kind::Nam;
  block.id = std::move(id);
  block.nam = std::move(nam);
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
  }
  return false;
}

StereoSample RuntimeChain::process(StereoSample input, float cabLevel, float cabMix)
{
  StereoSample current = input;
  for (size_t index = 0; index < blocks_.size(); ++index) {
    auto& block = blocks_[index];
    switch (block.kind) {
    case Block::Kind::Nam: {
      const float mono = block.nam->process(current.left);
      current = {mono, mono};
      break;
    }
    case Block::Kind::Cab: {
      const float dry = current.left;
      const float wet = block.cab->processSample(dry) * cabLevel;
      const float mixed = (wet * cabMix) + (dry * (1.0f - cabMix));
      current = {mixed, mixed};
      break;
    }
    case Block::Kind::Daisy:
      current = block.daisy->process(current);
      break;
    case Block::Kind::Compressor:
      current = block.compressor->process(current);
      break;
    case Block::Kind::Equalizer:
      block.equalizer->process(current.left, current.right);
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
    switch (block.kind) {
    case Block::Kind::Nam: {
      const float* namInput = currentLeft;
      if (currentIsStereo) {
        for (size_t i = 0; i < frames; ++i) {
          monoScratch_[i] = (currentLeft[i] + currentRight[i]) * 0.5f;
        }
        namInput = monoScratch_.data();
      }
      block.nam->processBlock(namInput, nextLeft, frames);
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
        const float wet = nextLeft[i] * cabLevels[i];
        nextLeft[i] = wet * cabMixes[i] + cabInput[i] * (1.0f - cabMixes[i]);
      }
      std::copy(nextLeft, nextLeft + frames, nextRight);
      currentIsStereo = false;
      break;
    }
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
    case Block::Kind::Equalizer:
      block.equalizer->processBlock(currentLeft, currentRight, nextLeft, nextRight, frames);
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
    case Block::Kind::Daisy:
      kind = SignalStageKind::Daisy;
      break;
    case Block::Kind::Compressor:
      kind = SignalStageKind::Compressor;
      break;
    case Block::Kind::Equalizer:
      kind = SignalStageKind::Equalizer;
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
    if (block.equalizer) {
      block.equalizer->reset();
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
  }
  return tail;
}

} // namespace ardor
