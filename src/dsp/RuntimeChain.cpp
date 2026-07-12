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
    Daisy
  };

  Kind kind = Kind::Cab;
  std::unique_ptr<NamProcessor> nam;
  std::unique_ptr<IrConvolver> cab;
  std::unique_ptr<DaisyFxProcessor> daisy;
  float level = 1.0f;
  float mix = 1.0f;
};

RuntimeChain::RuntimeChain() = default;
RuntimeChain::~RuntimeChain() = default;

void RuntimeChain::prepareBlockSize(size_t frames)
{
  if (frames == 0 || blockSize_ == frames) {
    return;
  }
  blockSize_ = frames;
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

void RuntimeChain::setCabParams(float level, float mix)
{
  const float clampedLevel = std::max(0.0f, level);
  const float clampedMix = std::clamp(mix, 0.0f, 1.0f);
  for (auto& block : blocks_) {
    if (block.kind == Block::Kind::Cab) {
      block.level = clampedLevel;
      block.mix = clampedMix;
    }
  }
}

StereoSample RuntimeChain::process(StereoSample input)
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
      const float wet = block.cab->processSample(dry) * block.level;
      const float mixed = (wet * block.mix) + (dry * (1.0f - block.mix));
      current = {mixed, mixed};
      break;
    }
    case Block::Kind::Daisy:
      current = block.daisy->process(current);
      break;
    }
  }
  return current;
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
  }
}

} // namespace ardor
