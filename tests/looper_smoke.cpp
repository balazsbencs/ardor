#include "looper/RealtimeLooper.h"
#include "looper/LooperController.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {
std::atomic<uint64_t> allocationCount {0};
}

void* operator new(std::size_t size)
{
  allocationCount.fetch_add(1, std::memory_order_relaxed);
  if (void* memory = std::malloc(size)) return memory;
  throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
  return ::operator new(size);
}

void operator delete(void* memory) noexcept
{
  std::free(memory);
}

void operator delete[](void* memory) noexcept
{
  std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
  std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept
{
  std::free(memory);
}

namespace {

constexpr std::size_t kTestBlockFrames = 4;

void require(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
}

void requireNear(float actual, float expected, const char* message)
{
  if (std::fabs(actual - expected) > 1.0e-4f) {
    throw std::runtime_error(std::string(message) + " (actual=" + std::to_string(actual)
                             + ", expected=" + std::to_string(expected) + ")");
  }
}

ardor::LooperTelemetry latest(ardor::RealtimeLooper& looper)
{
  ardor::LooperTelemetry result;
  ardor::LooperTelemetry next;
  bool found = false;
  while (looper.tryReadTelemetry(next)) {
    result = next;
    found = true;
  }
  require(found, "expected looper telemetry");
  return result;
}

void processConstant(ardor::RealtimeLooper& looper,
                     float input,
                     std::array<float, kTestBlockFrames>* output = nullptr)
{
  std::array<float, kTestBlockFrames> left;
  std::array<float, kTestBlockFrames> right;
  left.fill(input);
  right.fill(input);
  looper.processBlock(left.data(), right.data(), kTestBlockFrames);
  if (output != nullptr) *output = left;
  for (std::size_t i = 0; i < kTestBlockFrames; ++i) {
    requireNear(left[i], right[i], "stereo paths differ");
  }
}

void processStereo(ardor::RealtimeLooper& looper, float inputLeft, float inputRight,
                   std::array<float, kTestBlockFrames>* outputLeft = nullptr,
                   std::array<float, kTestBlockFrames>* outputRight = nullptr)
{
  std::array<float, kTestBlockFrames> left;
  std::array<float, kTestBlockFrames> right;
  left.fill(inputLeft);
  right.fill(inputRight);
  looper.processBlock(left.data(), right.data(), kTestBlockFrames);
  if (outputLeft) *outputLeft = left;
  if (outputRight) *outputRight = right;
}

void enqueue(ardor::RealtimeLooper& looper,
             uint64_t sequence,
             ardor::LooperCommandType type,
             uint8_t track = 0,
             float value = 0.0f)
{
  require(looper.tryEnqueue({sequence, type, track, value}), "command queue unexpectedly full");
}

void testMasterRecordPlaybackAndPause()
{
  ardor::RealtimeLooper looper;
  std::string error;
  constexpr std::size_t blockFrames = kTestBlockFrames;
  require(looper.prepare(48000.0f,
                         blockFrames,
                         64 * ardor::RealtimeLooper::kBytesPerMasterFrame,
                         error),
          "prepare failed");

  std::array<float, blockFrames> output;
  enqueue(looper, 1, ardor::LooperCommandType::OpenEmpty);
  processConstant(looper, 0.25f, &output);
  requireNear(output[0], 0.25f, "empty looper must be transparent");

  enqueue(looper, 2, ardor::LooperCommandType::RecordOrOverdub, 0);
  processConstant(looper, 1.0f);
  processConstant(looper, 1.0f);
  enqueue(looper, 3, ardor::LooperCommandType::RecordOrOverdub, 0);
  processConstant(looper, 0.0f, &output);
  for (float sample : output) requireNear(sample, 1.0f, "master loop playback failed");

  auto state = latest(looper);
  require(state.sessionState == ardor::LooperSessionState::Running, "master should run");
  require(state.masterFrames == 8, "master length should be quantized to command block");
  require(state.tracks[0].state == ardor::LooperTrackState::Playing, "master track should play");

  enqueue(looper, 4, ardor::LooperCommandType::Pause);
  processConstant(looper, 0.25f, &output);
  for (float sample : output) requireNear(sample, 0.25f, "pause should leave live program transparent");
  const auto paused = latest(looper);
  require(paused.sessionState == ardor::LooperSessionState::Paused, "pause state missing");
  const auto frozenPlayhead = paused.playheadFrame;

  processConstant(looper, 0.5f);
  enqueue(looper, 5, ardor::LooperCommandType::Resume);
  processConstant(looper, 0.0f, &output);
  requireNear(output[0], 1.0f, "resume should restore loop playback");
  require(frozenPlayhead == 4, "pause should occur at the command boundary");
}

void testOverdubUndoMuteAndClear()
{
  ardor::RealtimeLooper looper;
  std::string error;
  constexpr std::size_t blockFrames = kTestBlockFrames;
  require(looper.prepare(48000.0f,
                         blockFrames,
                         64 * ardor::RealtimeLooper::kBytesPerMasterFrame,
                         error),
          "prepare failed");
  enqueue(looper, 1, ardor::LooperCommandType::OpenEmpty);
  processConstant(looper, 0.0f);
  enqueue(looper, 2, ardor::LooperCommandType::RecordOrOverdub, 0);
  processConstant(looper, 1.0f);
  processConstant(looper, 1.0f);
  enqueue(looper, 3, ardor::LooperCommandType::RecordOrOverdub, 0);
  processConstant(looper, 0.0f); // playback positions 0..3

  enqueue(looper, 4, ardor::LooperCommandType::RecordOrOverdub, 0);
  processConstant(looper, 2.0f); // armed until wrap
  processConstant(looper, 2.0f); // overdub positions 0..3
  processConstant(looper, 2.0f); // overdub positions 4..7

  std::array<float, blockFrames> output;
  processConstant(looper, 0.0f); // bounded seam correction after committing the take
  processConstant(looper, 0.0f, &output);
  for (float sample : output) requireNear(sample, 3.0f, "overdub should add one independent take");
  auto state = latest(looper);
  require(state.tracks[0].undoAvailable, "completed overdub should be undoable");

  enqueue(looper, 5, ardor::LooperCommandType::ToggleUndo, 0);
  processConstant(looper, 0.0f, &output);
  for (float sample : output) requireNear(sample, 1.0f, "undo should remove only the last take");
  require(latest(looper).tracks[0].undoApplied, "undo telemetry missing");

  enqueue(looper, 6, ardor::LooperCommandType::ToggleUndo, 0);
  processConstant(looper, 0.0f, &output);
  for (float sample : output) requireNear(sample, 3.0f, "redo should restore the last take");

  enqueue(looper, 7, ardor::LooperCommandType::ToggleTrackAudible, 0);
  processConstant(looper, 0.25f, &output);
  for (float sample : output) requireNear(sample, 0.25f, "muting a track must preserve live monitoring");

  enqueue(looper, 8, ardor::LooperCommandType::ClearTrack, 0);
  processConstant(looper, 0.25f, &output);
  state = latest(looper);
  require(state.sessionState == ardor::LooperSessionState::EmptyPaused,
          "clearing the last track should return to empty paused");
  require(state.masterFrames == 0, "clearing the last track should remove the master length");
  enqueue(looper, 9, ardor::LooperCommandType::CloseSession);
  processConstant(looper, 0.5f, &output);
  for (float sample : output) requireNear(sample, 0.5f, "closed looper must be transparent");
  require(!looper.sessionOpen(), "close should publish an unlocked session state");
  require(latest(looper).sessionState == ardor::LooperSessionState::Inactive,
          "close should return the realtime state to inactive");
}

void testFollowerTrackAndMinimumLength()
{
  ardor::RealtimeLooper looper;
  std::string error;
  constexpr std::size_t blockFrames = kTestBlockFrames;
  require(looper.prepare(48000.0f,
                         blockFrames,
                         64 * ardor::RealtimeLooper::kBytesPerMasterFrame,
                         error),
          "prepare failed");
  enqueue(looper, 1, ardor::LooperCommandType::OpenEmpty);
  processConstant(looper, 0.0f);
  enqueue(looper, 2, ardor::LooperCommandType::RecordOrOverdub, 0);
  processConstant(looper, 1.0f);
  enqueue(looper, 3, ardor::LooperCommandType::RecordOrOverdub, 0);
  processConstant(looper, 0.0f);
  auto state = latest(looper);
  require(state.sessionState == ardor::LooperSessionState::EmptyPaused,
          "a master shorter than two blocks must be rejected");
  require(state.error == ardor::LooperError::MasterTooShort, "short-loop reason missing");

  enqueue(looper, 4, ardor::LooperCommandType::RecordOrOverdub, 0);
  processConstant(looper, 1.0f);
  processConstant(looper, 1.0f);
  enqueue(looper, 5, ardor::LooperCommandType::RecordOrOverdub, 0);
  processConstant(looper, 0.0f); // playhead 4
  enqueue(looper, 6, ardor::LooperCommandType::RecordOrOverdub, 1);
  processConstant(looper, 2.0f); // waits for boundary
  processConstant(looper, 2.0f);
  processConstant(looper, 2.0f);
  std::array<float, blockFrames> output;
  processConstant(looper, 0.0f); // bounded seam correction after committing the track
  processConstant(looper, 0.0f, &output);
  for (float sample : output) requireNear(sample, 3.0f, "follower track should be phase-aligned");
  state = latest(looper);
  require(state.tracks[1].state == ardor::LooperTrackState::Playing,
          "follower track should finish after exactly one master cycle");
}

void testMaximumLengthClosesMasterWithoutDroppingPlayback()
{
  ardor::RealtimeLooper looper;
  std::string error;
  constexpr std::size_t maximumFrames = kTestBlockFrames * 2;
  require(looper.prepare(48000.0f,
                         kTestBlockFrames,
                         maximumFrames * ardor::RealtimeLooper::kBytesPerMasterFrame,
                         error),
          "prepare failed");
  enqueue(looper, 1, ardor::LooperCommandType::OpenEmpty);
  processConstant(looper, 0.0f);
  enqueue(looper, 2, ardor::LooperCommandType::RecordOrOverdub, 0);
  processConstant(looper, 0.75f);
  processConstant(looper, 0.75f);

  const auto state = latest(looper);
  require(state.sessionState == ardor::LooperSessionState::Running,
          "maximum-length capture should transition directly to playback");
  require(state.masterFrames == maximumFrames,
          "automatic close should retain every available master frame");
  require(state.error == ardor::LooperError::MaximumLengthReached,
          "automatic close should report the maximum-length reason");

  std::array<float, kTestBlockFrames> output;
  processConstant(looper, 0.0f, &output);
  for (float sample : output) requireNear(sample, 0.75f, "maximum-length loop should play back");
}

void testLevelAndBalancePreserveStereoHeadroom()
{
  ardor::RealtimeLooper looper;
  std::string error;
  require(looper.prepare(48000.0f,
                         kTestBlockFrames,
                         64 * ardor::RealtimeLooper::kBytesPerMasterFrame,
                         error),
          "prepare failed");
  enqueue(looper, 1, ardor::LooperCommandType::OpenEmpty);
  processStereo(looper, 0.0f, 0.0f);
  enqueue(looper, 2, ardor::LooperCommandType::RecordOrOverdub, 0);
  processStereo(looper, 0.25f, 0.5f);
  processStereo(looper, 0.25f, 0.5f);
  enqueue(looper, 3, ardor::LooperCommandType::RecordOrOverdub, 0);
  processStereo(looper, 0.0f, 0.0f);

  enqueue(looper, 4, ardor::LooperCommandType::SetTrackBalance, 0, -1.0f);
  for (int block = 0; block < 800; ++block) processStereo(looper, 0.0f, 0.0f);
  std::array<float, kTestBlockFrames> left;
  std::array<float, kTestBlockFrames> right;
  processStereo(looper, 0.0f, 0.0f, &left, &right);
  requireNear(left.back(), 0.25f, "left balance must not boost the favored channel");
  require(std::fabs(right.back()) < 0.001f, "left balance should attenuate the opposite channel");

  enqueue(looper, 5, ardor::LooperCommandType::SetTrackBalance, 0, 0.0f);
  enqueue(looper, 6, ardor::LooperCommandType::SetTrackLevelDb, 0, -6.0206f);
  for (int block = 0; block < 800; ++block) processStereo(looper, 0.0f, 0.0f);
  processStereo(looper, 0.0f, 0.0f, &left, &right);
  requireNear(left.back(), 0.125f, "track level should attenuate the left channel");
  requireNear(right.back(), 0.25f, "track level should attenuate the right channel equally");

  enqueue(looper, 7, ardor::LooperCommandType::SetTrackBalance, 0,
          std::numeric_limits<float>::quiet_NaN());
  processStereo(looper, 0.0f, 0.0f, &left, &right);
  for (std::size_t frame = 0; frame < kTestBlockFrames; ++frame) {
    require(std::isfinite(left[frame]) && std::isfinite(right[frame]),
            "non-finite control payload must not reach looper output");
  }
  require(latest(looper).error == ardor::LooperError::InvalidState,
          "non-finite control payload should be rejected explicitly");
}

void testRestorePausedSession()
{
  ardor::RealtimeLooper looper;
  std::string error;
  require(looper.prepare(48000.0f,
                         kTestBlockFrames,
                         64 * ardor::RealtimeLooper::kBytesPerMasterFrame,
                         error),
          "prepare failed");

  std::array<float, 8> baseLeft;
  std::array<float, 8> baseRight;
  std::array<float, 8> takeLeft;
  std::array<float, 8> takeRight;
  std::array<float, 8> mutedLeft;
  std::array<float, 8> mutedRight;
  baseLeft.fill(0.25f);
  baseRight.fill(0.5f);
  takeLeft.fill(0.1f);
  takeRight.fill(0.2f);
  mutedLeft.fill(4.0f);
  mutedRight.fill(4.0f);

  ardor::LooperPausedSessionView session;
  session.sampleRate = 48000.0f;
  session.loopFrames = baseLeft.size();
  session.tracks[0] = {
    baseLeft, baseRight, takeLeft, takeRight,
    true, true, true, false, 0.0f, 0.0f,
  };
  session.tracks[1] = {
    mutedLeft, mutedRight, {}, {},
    true, false, false, true, 0.0f, 0.0f,
  };

  baseLeft[2] = std::numeric_limits<float>::quiet_NaN();
  require(!looper.restorePausedSession(session, error) && !looper.sessionOpen(),
          "invalid decoded audio must not partially open a realtime session");
  baseLeft[2] = 0.25f;
  require(looper.restorePausedSession(session, error),
          "valid decoded audio should restore into prepared realtime buffers");
  require(looper.sessionOpen(), "restored loop should acquire the preset lock");

  std::array<float, kTestBlockFrames> left;
  std::array<float, kTestBlockFrames> right;
  processStereo(looper, 0.05f, 0.05f, &left, &right);
  for (std::size_t frame = 0; frame < kTestBlockFrames; ++frame) {
    requireNear(left[frame], 0.05f, "restored session must initially remain paused");
    requireNear(right[frame], 0.05f, "paused restored session must preserve live stereo audio");
  }
  auto state = latest(looper);
  require(state.sessionState == ardor::LooperSessionState::Paused
            && state.masterFrames == baseLeft.size()
            && state.tracks[0].state == ardor::LooperTrackState::Playing
            && state.tracks[1].state == ardor::LooperTrackState::Muted
            && !state.tracks[0].undoAvailable,
          "restored telemetry should retain track mix state but clear undo history");

  enqueue(looper, 1, ardor::LooperCommandType::Resume);
  processStereo(looper, 0.0f, 0.0f, &left, &right);
  for (std::size_t frame = 0; frame < kTestBlockFrames; ++frame) {
    requireNear(left[frame], 0.35f, "restore should flatten the audible last take into the base");
    requireNear(right[frame], 0.7f, "restored stereo loop playback is incorrect");
  }
}

void testQueueCapacityAndOrdering()
{
  ardor::LooperSpscQueue<uint32_t, 4> queue;
  require(queue.tryPush(10) && queue.tryPush(11) && queue.tryPush(12) && queue.tryPush(13),
          "queue should accept its declared capacity");
  require(!queue.tryPush(14), "queue overflow must be reported");
  uint32_t value = 0;
  require(queue.tryPop(value) && value == 10, "queue ordering failed");
  require(queue.tryPush(14), "queue should wrap after a pop");
  for (uint32_t expected = 11; expected <= 14; ++expected) {
    require(queue.tryPop(value) && value == expected, "queue wrap ordering failed");
  }
  require(!queue.tryPop(value), "empty queue should report no value");
}

void testAudioPathDoesNotAllocate()
{
  ardor::RealtimeLooper looper;
  std::string error;
  require(looper.prepare(48000.0f,
                         kTestBlockFrames,
                         64 * ardor::RealtimeLooper::kBytesPerMasterFrame,
                         error),
          "prepare failed");
  enqueue(looper, 1, ardor::LooperCommandType::OpenEmpty);
  processConstant(looper, 0.0f);
  enqueue(looper, 2, ardor::LooperCommandType::RecordOrOverdub, 0);
  const auto before = allocationCount.load(std::memory_order_relaxed);
  processConstant(looper, 1.0f);
  processConstant(looper, 1.0f);
  enqueue(looper, 3, ardor::LooperCommandType::RecordOrOverdub, 0);
  processConstant(looper, 0.0f);
  const auto after = allocationCount.load(std::memory_order_relaxed);
  require(after == before, "audio processing must not allocate after prepare");
}

void testFootswitchController()
{
  using namespace std::chrono_literals;
  ardor::LooperController controller;
  const auto start = ardor::LooperController::Clock::time_point{};
  const auto open = controller.openSession();
  require(open && open->command.type == ardor::LooperCommandType::OpenEmpty,
          "opening should issue an audio command");
  require(controller.sessionLocked(), "opening should acquire the preset lock immediately");

  ardor::LooperTelemetry telemetry;
  telemetry.sessionState = ardor::LooperSessionState::EmptyPaused;
  telemetry.lastAppliedCommandSequence = open->command.sequence;
  controller.updateTelemetry(telemetry);

  const auto record = controller.handleFootswitch(
      {ardor::ControlEventType::FootswitchPressed, 2, 0}, start);
  require(record && record->command.type == ardor::LooperCommandType::RecordOrOverdub
            && record->command.track == 0,
          "FS3 downstroke should record the selected track immediately");
  const auto mute = controller.handleFootswitch(
      {ardor::ControlEventType::FootswitchPressed, 3, 0}, start);
  require(mute && mute->command.type == ardor::LooperCommandType::ToggleTrackAudible,
          "FS4 downstroke should toggle selected-track audibility immediately");
  controller.handleFootswitch({ardor::ControlEventType::FootswitchReleased, 2, 0}, start + 10ms);
  controller.handleFootswitch({ardor::ControlEventType::FootswitchReleased, 3, 0}, start + 10ms);

  controller.handleFootswitch({ardor::ControlEventType::FootswitchPressed, 1, 0}, start + 20ms);
  require(controller.clearHoldProgress(start + 770ms) > 0.49f
            && controller.clearHoldProgress(start + 770ms) < 0.51f,
          "FS2 hold progress should be available to the UI");
  controller.handleFootswitch({ardor::ControlEventType::FootswitchReleased, 1, 0}, start + 800ms);
  require(controller.selectedTrack() == 1, "short FS2 release should select the next track");

  controller.handleFootswitch({ardor::ControlEventType::FootswitchPressed, 0, 0}, start + 900ms);
  const auto undo = controller.handleFootswitch(
      {ardor::ControlEventType::FootswitchReleased, 0, 0}, start + 950ms);
  require(undo && undo->command.type == ardor::LooperCommandType::ToggleUndo
            && undo->command.track == 1,
          "short FS1 release should toggle undo on the selected track");

  controller.handleFootswitch({ardor::ControlEventType::FootswitchPressed, 1, 0}, start + 1s);
  require(!controller.poll(start + 2499ms), "FS2 clear must not trigger before its hold threshold");
  const auto clear = controller.poll(start + 2500ms);
  require(clear && clear->command.type == ardor::LooperCommandType::ClearTrack
            && clear->command.track == 1,
          "FS2 hold should clear exactly the selected track");
  controller.handleFootswitch({ardor::ControlEventType::FootswitchReleased, 1, 0}, start + 2600ms);
  require(controller.selectedTrack() == 1, "clear hold should suppress FS2 short selection");

  telemetry.sessionState = ardor::LooperSessionState::Running;
  telemetry.lastAppliedCommandSequence = clear->command.sequence;
  controller.updateTelemetry(telemetry);
  controller.handleFootswitch({ardor::ControlEventType::FootswitchPressed, 0, 0}, start + 3s);
  controller.handleFootswitch({ardor::ControlEventType::FootswitchPressed, 1, 0}, start + 3050ms);
  require(!controller.poll(start + 4049ms), "tuner chord must not trigger early");
  const auto pauseForTuner = controller.poll(start + 4050ms);
  require(pauseForTuner && pauseForTuner->command.type == ardor::LooperCommandType::Pause,
          "running looper should pause before tuner entry");
  telemetry.sessionState = ardor::LooperSessionState::Paused;
  telemetry.lastAppliedCommandSequence = pauseForTuner->command.sequence;
  controller.updateTelemetry(telemetry);
  const auto enterTuner = controller.poll(start + 4060ms);
  require(enterTuner && enterTuner->type == ardor::LooperControllerActionType::EnterTuner
            && controller.tunerActive(),
          "tuner entry should wait for the audio-thread pause acknowledgement");
  const auto resumeAfterTuner = controller.handleFootswitch(
      {ardor::ControlEventType::FootswitchPressed, 3, 0}, start + 4100ms);
  require(resumeAfterTuner && resumeAfterTuner->command.type == ardor::LooperCommandType::Resume
            && !controller.tunerActive(),
          "leaving tuner should restore a previously running loop");

  telemetry.sessionState = ardor::LooperSessionState::Paused;
  telemetry.lastAppliedCommandSequence = resumeAfterTuner->command.sequence;
  controller.updateTelemetry(telemetry);
  const auto close = controller.closeSession();
  require(close && close->command.type == ardor::LooperCommandType::CloseSession,
          "paused session should be closable");
  telemetry.sessionState = ardor::LooperSessionState::Inactive;
  telemetry.lastAppliedCommandSequence = close->command.sequence;
  controller.updateTelemetry(telemetry);
  require(!controller.sessionLocked(), "close acknowledgement should release the preset lock");

  ardor::LooperController captureController;
  const auto captureOpen = captureController.openSession();
  require(captureOpen.has_value(), "capture controller should open");
  ardor::LooperTelemetry captureTelemetry;
  captureTelemetry.sessionState = ardor::LooperSessionState::RecordingMaster;
  captureTelemetry.tracks[0].state = ardor::LooperTrackState::Recording;
  captureController.updateTelemetry(captureTelemetry);
  const auto rejectedTuner = captureController.requestTunerMode();
  require(rejectedTuner && rejectedTuner->command.type == ardor::LooperCommandType::Pause,
          "tuner request during capture should ask realtime state to reject pause visibly");
  captureTelemetry.lastAppliedCommandSequence = rejectedTuner->command.sequence;
  captureTelemetry.error = ardor::LooperError::InvalidState;
  captureController.updateTelemetry(captureTelemetry);
  require(captureController.requestTunerMode().has_value(),
          "rejected tuner pause must not leave the controller permanently pending");

  ardor::LooperController queueFailureController;
  const auto failedOpen = queueFailureController.openSession();
  require(failedOpen && queueFailureController.sessionLocked(),
          "session open should reserve the preset lock before publication");
  queueFailureController.submissionFailed(*failedOpen);
  require(!queueFailureController.sessionLocked() && queueFailureController.openSession().has_value(),
          "failed open publication should release the phantom preset lock");
}

} // namespace

int main()
{
  try {
    testQueueCapacityAndOrdering();
    testAudioPathDoesNotAllocate();
    testFootswitchController();
    testMasterRecordPlaybackAndPause();
    testOverdubUndoMuteAndClear();
    testFollowerTrackAndMinimumLength();
    testMaximumLengthClosesMasterWithoutDroppingPlayback();
    testLevelAndBalancePreserveStereoHeadroom();
    testRestorePausedSession();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "looper_smoke failed: " << error.what() << '\n';
    return 1;
  }
}
