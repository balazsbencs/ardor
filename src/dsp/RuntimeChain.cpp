#include "dsp/RuntimeChain.h"

#include "dsp/IrConvolver.h"
#include "dsp/NamProcessor.h"

#include <algorithm>
#include <utility>

namespace ardor {

struct RuntimeChain::Block {
  enum class Kind {
    Nam,
    Cab,
    Daisy,
    Compressor
  };

  Kind kind = Kind::Cab;
  std::unique_ptr<NamProcessor> nam;
  std::unique_ptr<IrConvolver> cab;
  std::unique_ptr<DaisyFxProcessor> daisy;
  std::unique_ptr<CompressorProcessor> compressor;
  float level = 1.0f;
  float mix = 1.0f;
};

RuntimeChain::RuntimeChain() = default;
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
}

bool RuntimeChain::addNam(const std::filesystem::path& modelPath, double sampleRate, int maxBlockSize)
{
  auto nam = std::make_unique<NamProcessor>();
  if (!nam->load(modelPath, sampleRate, maxBlockSize)) {
    return false;
  }
  Block block;
  block.kind = Block::Kind::Nam;
  block.nam = std::move(nam);
  blocks_.push_back(std::move(block));
  return true;
}

void RuntimeChain::addCab(std::vector<float> impulse, float level, float mix)
{
  auto cab = std::make_unique<IrConvolver>();
  cab->loadImpulse(std::move(impulse));
  cab->prepareBlockSize(blockSize_);

  Block block;
  block.kind = Block::Kind::Cab;
  block.cab = std::move(cab);
  block.level = std::max(0.0f, level);
  block.mix = std::clamp(mix, 0.0f, 1.0f);
  blocks_.push_back(std::move(block));
}

void RuntimeChain::addDaisy(DaisyFxProcessor processor)
{
  Block block;
  block.kind = Block::Kind::Daisy;
  block.daisy = std::make_unique<DaisyFxProcessor>(std::move(processor));
  blocks_.push_back(std::move(block));
}

void RuntimeChain::addCompressor(CompressorProcessor processor)
{
  Block block;
  block.kind = Block::Kind::Compressor;
  block.compressor = std::make_unique<CompressorProcessor>(std::move(processor));
  blocks_.push_back(std::move(block));
}

StereoSample RuntimeChain::process(StereoSample input, float cabLevel, float cabMix)
{
  StereoSample current = input;
  for (auto& block : blocks_) {
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
    }
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
    }

    std::swap(currentLeft, nextLeft);
    std::swap(currentRight, nextRight);
  }

  std::copy(currentLeft, currentLeft + frames, left);
  std::copy(currentRight, currentRight + frames, right);
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
  }
}

size_t RuntimeChain::tailFrames() const noexcept
{
  size_t tail = 0;
  for (const auto& block : blocks_) {
    if (block.cab) {
      tail = std::max(tail, block.cab->tailFrames());
    }
  }
  return tail;
}

} // namespace ardor
