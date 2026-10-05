#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

struct SpeexResamplerState_;

namespace ardor::clap_audio {

// Streaming host/engine bridge. prepare() may allocate; reset() and tick() do not.
// The engine always receives 64 mono frames at 48 kHz, regardless of host blocks.
class RateAdapter {
public:
  static constexpr unsigned quantum = 64;
  using Process = void (*)(void*, const float*, float*, float*, unsigned, int);
  struct Output { float left, right, dry; };
  static bool supports(double rate) noexcept;
  void prepare(double rate, unsigned engineLatency);
  void reset(int scene) noexcept;
  bool tick(float input, float dry, int scene, Process process, void* context, Output& output) noexcept;
  unsigned latency() const noexcept { return latency_; }
  unsigned filterTail() const noexcept { return filterTail_; }
private:
  struct Destroy { void operator()(SpeexResamplerState_*) const noexcept; };
  std::unique_ptr<SpeexResamplerState_, Destroy> inputConverter_, outputConverter_;
  std::array<float, quantum> input_{}, left_{}, right_{};
  // At 1 kHz one host frame creates at most 48 engine frames; at 768 kHz
  // one engine quantum creates at most 1024 host frames. Leave endpoint room.
  std::array<float, 50> convertedInput_{};
  std::array<float, 1026> convertedLeft_{}, convertedRight_{};
  std::vector<Output> wetQueue_;
  std::vector<float> dryDelay_;
  std::vector<int> sceneDelay_;
  unsigned latency_ = quantum, schedulingDelay_ = quantum, filterTail_ = 0;
  unsigned position_ = 0;
  std::size_t read_ = 0, write_ = 0, available_ = 0, dryPosition_ = 0, scenePosition_ = 0;
};

} // namespace ardor::clap_audio
