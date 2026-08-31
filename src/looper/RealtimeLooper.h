#pragma once

#include "looper/LooperSpscQueue.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ardor {

constexpr std::size_t kLooperTrackCount = 4;

enum class LooperSessionState : uint8_t {
  Inactive,
  EmptyPaused,
  RecordingMaster,
  Running,
  Paused,
  Faulted,
};

enum class LooperTrackState : uint8_t {
  Empty,
  ArmedRecord,
  Recording,
  Playing,
  Muted,
  ArmedOverdub,
  Overdubbing,
};

enum class LooperCommandType : uint8_t {
  OpenEmpty,
  RecordOrOverdub,
  ToggleTrackAudible,
  ToggleUndo,
  ClearTrack,
  Pause,
  Resume,
  ResetSession,
  CloseSession,
  SetTrackLevelDb,
  SetTrackBalance,
};

enum class LooperError : uint8_t {
  None,
  InvalidTrack,
  InvalidState,
  MasterTooShort,
  MaximumLengthReached,
};

struct LooperCommand {
  uint64_t sequence = 0;
  LooperCommandType type = LooperCommandType::OpenEmpty;
  uint8_t track = 0;
  float value = 0.0f;
};

struct LooperTrackTelemetry {
  LooperTrackState state = LooperTrackState::Empty;
  bool audible = false;
  bool undoAvailable = false;
  bool undoApplied = false;
  float levelDb = 0.0f;
  float balance = 0.0f;
};

struct LooperTelemetry {
  uint64_t revision = 0;
  uint64_t contentRevision = 0;
  uint64_t lastAppliedCommandSequence = 0;
  LooperSessionState sessionState = LooperSessionState::Inactive;
  LooperError error = LooperError::None;
  uint64_t masterFrames = 0;
  uint64_t maximumFrames = 0;
  uint64_t playheadFrame = 0;
  std::array<LooperTrackTelemetry, kLooperTrackCount> tracks {};
};

struct LooperPausedTrackView {
  std::span<const float> baseLeft;
  std::span<const float> baseRight;
  std::span<const float> lastTakeLeft;
  std::span<const float> lastTakeRight;
  bool present = false;
  bool lastTakeValid = false;
  bool lastTakeAudible = false;
  bool muted = false;
  float levelDb = 0.0f;
  float balance = 0.0f;
};

struct LooperPausedSessionView {
  float sampleRate = 0.0f;
  uint64_t loopFrames = 0;
  std::array<LooperPausedTrackView, kLooperTrackCount> tracks {};
};

// Four-track, stereo, synchronized looper. prepare() belongs to the control
// thread; enqueue/read belong to the control side; processBlock() belongs to
// the audio thread. Calls made after prepare() are allocation-free.
class RealtimeLooper {
public:
  static constexpr std::size_t kBytesPerMasterFrame =
      kLooperTrackCount * 2 * 2 * sizeof(float); // base + one undo take

  bool prepare(float sampleRate,
               std::size_t maximumBlockFrames,
               std::size_t memoryBudgetBytes,
               std::string& error);

  bool tryEnqueue(const LooperCommand& command) noexcept;
  bool tryReadTelemetry(LooperTelemetry& telemetry) noexcept;
  bool sessionOpen() const noexcept { return sessionOpen_.load(std::memory_order_acquire); }
  // Control-thread only, before this instance begins audio processing. Copies
  // a fully decoded paused session into the already prepared realtime buffers.
  // Any audible last take is flattened into the base, so restored tracks begin
  // with no undo history as required by the persistence contract.
  bool restorePausedSession(const LooperPausedSessionView& session,
                            std::string& error);
  // Control/I/O-thread only, after a Paused acknowledgement. The returned
  // spans remain valid until prepare() is called again; no looper command may
  // be submitted while another thread is consuming them.
  std::optional<LooperPausedSessionView> pausedSessionView() const noexcept;
  void processBlock(float* left, float* right, std::size_t frames) noexcept;

private:
  struct Track {
    std::vector<float> baseLeft;
    std::vector<float> baseRight;
    std::vector<float> lastTakeLeft;
    std::vector<float> lastTakeRight;
    LooperTrackState state = LooperTrackState::Empty;
    bool audible = false;
    bool lastTakeValid = false;
    bool lastTakeAudible = false;
    uint64_t capturedFrames = 0;
    float levelDb = 0.0f;
    float targetGain = 1.0f;
    float currentGain = 1.0f;
    float balance = 0.0f;
    float targetLeftPan = 1.0f;
    float targetRightPan = 1.0f;
    float currentLeftPan = 1.0f;
    float currentRightPan = 1.0f;
    float seamLeft = 0.0f;
    float seamRight = 0.0f;
    uint32_t seamFramesRemaining = 0;
    uint32_t seamLength = 0;
    float lastRawLeft = 0.0f;
    float lastRawRight = 0.0f;
  };

  void consumeCommands() noexcept;
  void applyCommand(const LooperCommand& command) noexcept;
  void recordOrOverdub(std::size_t track) noexcept;
  void clearTrack(std::size_t track) noexcept;
  void clearTrackMetadata(Track& track) noexcept;
  void closeMasterRecording(bool maximumLengthReached = false) noexcept;
  void startArmedTracksAtBoundary() noexcept;
  void finishFollowerCapture(Track& track) noexcept;
  void updateSeamsAtWrap() noexcept;
  void publishTelemetry(bool force) noexcept;
  LooperTelemetry makeTelemetry() const noexcept;
  bool hasPopulatedTracks() const noexcept;
  bool trackIsPopulated(const Track& track) const noexcept;
  void effectiveSample(const Track& track,
                       std::size_t frame,
                       float& left,
                       float& right) const noexcept;

  std::array<Track, kLooperTrackCount> tracks_;
  LooperSpscQueue<LooperCommand, 64> commands_;
  LooperSpscQueue<LooperTelemetry, 64> telemetry_;
  LooperSessionState sessionState_ = LooperSessionState::Inactive;
  LooperError error_ = LooperError::None;
  uint64_t masterFrames_ = 0;
  uint64_t maximumFrames_ = 0;
  uint64_t playheadFrame_ = 0;
  uint64_t lastAppliedCommandSequence_ = 0;
  uint64_t telemetryRevision_ = 0;
  uint64_t contentRevision_ = 0;
  uint64_t framesUntilTelemetry_ = 0;
  uint64_t telemetryIntervalFrames_ = 1600;
  std::size_t maximumBlockFrames_ = 0;
  float sampleRate_ = 0.0f;
  float smoothingCoefficient_ = 1.0f;
  bool prepared_ = false;
  bool stateChanged_ = false;
  bool telemetryPending_ = false;
  std::atomic<bool> sessionOpen_ {false};
  LooperTelemetry pendingTelemetry_ {};
};

} // namespace ardor
