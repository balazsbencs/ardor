#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ardor {

enum class SceneTransitionLaw : std::uint8_t {
  Linear,
  Decibels,
  LogFrequency,
  Stepped,
};

enum class SceneRuntimeTargetKind : std::uint8_t {
  InputGainDb,
  BlockEnabled,
  DaisyParameter,
  CabParameter,
  IrReverbParameter,
  StereoParameter,
  CompressorParameter,
  NoiseGateParameter,
  TransientShaperParameter,
  DistortionParameter,
  WahParameter,
  WdwLaneParameter,
};

enum class SceneBlockContainer : std::uint8_t {
  Serial,
  WdwDry,
  WdwWet,
  DualRigLeft,
  DualRigRight,
  None,
};

enum class SceneWdwLane : std::uint8_t { None, Dry, Wet };

enum SceneRuntimeParameter : std::uint16_t {
  Mix,
  LevelDb,
  PreDelayMs,
  LowCutHz,
  HighCutHz,
  Width,
  DelayMs,
  BassMonoHz,
  Position,
  ThresholdDb,
  Ratio,
  AttackMs,
  ReleaseMs,
  KneeDb,
  MakeupDb,
  InputGainDb,
  SidechainHpfHz,
  ReductionDb,
  HoldMs,
  HysteresisDb,
  Attack,
  Sustain,
  OutputDb,
  Distortion,
  Filter,
  Volume,
  Fuzz,
  Tone,
  Drive,
  HissDb,
  Flutter,
  Saturation,
  Bias,
  HeadBump,
  Pan,
  Enabled,
};

struct SceneRuntimeAddress {
  SceneRuntimeTargetKind kind = SceneRuntimeTargetKind::InputGainDb;
  SceneBlockContainer container = SceneBlockContainer::None;
  std::uint16_t topIndex = 0;
  std::uint16_t childIndex = 0;
  std::uint16_t parameterIndex = 0;
  SceneWdwLane lane = SceneWdwLane::None;
  bool child = false;
};

struct SceneTransitionTarget {
  SceneTransitionLaw law = SceneTransitionLaw::Linear;
  std::array<float, 4> values{};
  SceneRuntimeAddress address{};
};

struct SceneTransitionProgram {
  std::vector<SceneTransitionTarget> targets;
  std::array<float, 4> outputTrimDb{};
  std::uint8_t defaultSceneIndex = 0;
  std::uint64_t presetGeneration = 0;
};

struct SceneTransitionRequest {
  std::uint64_t presetGeneration = 0;
  std::uint64_t requestId = 0;
  std::uint32_t durationFrames = 0;
  std::uint8_t sceneIndex = 0;
};

struct SceneTransitionTelemetry {
  std::uint64_t lastAppliedRequestId = 0;
  std::uint8_t currentSceneIndex = 0;
  std::uint8_t destinationSceneIndex = 0;
  std::uint32_t elapsedFrames = 0;
  std::uint32_t totalFrames = 0;
  bool transitioning = false;
};

// Single-producer, single-consumer latest-value mailbox. Publishing never
// blocks and supersedes an unread request. The audio side takes one bounded
// snapshot at a block boundary and retries on the next block if publication
// overlaps that snapshot.
class SceneRequestMailbox {
public:
  void reset() noexcept;
  void publish(const SceneTransitionRequest& request) noexcept;
  bool readLatest(std::uint64_t& lastSerial, SceneTransitionRequest& request) const noexcept;

private:
  std::atomic<std::uint64_t> serial_{0};
  std::atomic<std::uint64_t> presetGeneration_{0};
  std::atomic<std::uint64_t> requestId_{0};
  std::atomic<std::uint32_t> durationFrames_{0};
  std::atomic<std::uint8_t> sceneIndex_{0};
};

// Prepared on the control thread, then owned by one audio thread. beginBlock()
// consumes at most one latest request. advanceFrame() performs bounded scalar
// work with no locks, allocation, JSON, or string lookup.
class SceneTransitionController {
public:
  static constexpr std::size_t kMaximumTargets = 512;

  bool prepare(SceneTransitionProgram program);
  bool prepared() const noexcept { return prepared_; }
  bool request(const SceneTransitionRequest& request) noexcept;
  bool requestOverride(std::size_t targetIndex, float value) noexcept;

  void beginBlock() noexcept;
  void advanceFrame() noexcept;

  std::span<const float> currentValues() const noexcept { return currentValues_; }
  std::span<const SceneTransitionTarget> targets() const noexcept { return program_.targets; }
  float currentOutputTrimDb() const noexcept { return currentOutputTrimDb_; }
  std::uint64_t presetGeneration() const noexcept { return program_.presetGeneration; }
  SceneTransitionTelemetry telemetry() const noexcept;

private:
  static float interpolate(SceneTransitionLaw law, float start, float end,
                           float position) noexcept;
  void startRequest(const SceneTransitionRequest& request) noexcept;
  void consumeOverrides() noexcept;
  void publishTelemetry() noexcept;

  SceneTransitionProgram program_;
  SceneRequestMailbox mailbox_;
  std::vector<float> currentValues_;
  std::vector<float> startValues_;
  std::vector<float> destinationValues_;
  std::vector<bool> overridden_;
  std::array<std::atomic<float>, kMaximumTargets> overrideValues_{};
  std::array<std::atomic<std::uint64_t>, (kMaximumTargets + 63) / 64> overrideBits_{};
  float currentOutputTrimDb_ = 0.0f;
  float startOutputTrimDb_ = 0.0f;
  float destinationOutputTrimDb_ = 0.0f;
  std::uint64_t consumedSerial_ = 0;
  std::uint64_t lastAppliedRequestId_ = 0;
  std::uint32_t elapsedFrames_ = 0;
  std::uint32_t totalFrames_ = 0;
  std::uint8_t currentSceneIndex_ = 0;
  std::uint8_t destinationSceneIndex_ = 0;
  bool transitioning_ = false;
  bool prepared_ = false;
  // Audio publishes a coherent, bounded snapshot for control-thread readers.
  std::atomic<std::uint64_t> telemetrySerial_{0};
  std::atomic<std::uint64_t> telemetryRequestId_{0};
  std::atomic<std::uint8_t> telemetryCurrentScene_{0};
  std::atomic<std::uint8_t> telemetryDestinationScene_{0};
  std::atomic<std::uint32_t> telemetryElapsedFrames_{0};
  std::atomic<std::uint32_t> telemetryTotalFrames_{0};
  std::atomic<bool> telemetryTransitioning_{false};
};

} // namespace ardor
