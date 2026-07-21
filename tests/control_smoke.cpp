#include "control/ControlEvents.h"

#include <chrono>
#include <iostream>

namespace {

int require(bool ok, const char* message)
{
  if (!ok) {
    std::cerr << message << "\n";
    return 1;
  }
  return 0;
}

} // namespace

int main()
{
  ardor::ControlState state;
  state.activeSlot = 0;
  state.masterVolume = 82;

  if (require(ardor::applyControlEvent(state, {ardor::ControlEventType::FootswitchPressed, 2, 0}),
              "footswitch should apply")) return 1;
  if (require(state.activeSlot == 2, "footswitch should select slot")) return 1;

  if (require(!ardor::applyControlEvent(state, {ardor::ControlEventType::FootswitchPressed, 7, 0}),
              "invalid footswitch should be ignored")) return 1;
  if (require(state.activeSlot == 2, "invalid footswitch should not change slot")) return 1;

  if (require(ardor::applyControlEvent(state, {ardor::ControlEventType::EncoderTurned, 0, 4}),
              "encoder should apply")) return 1;
  if (require(state.masterVolume == 86, "encoder should raise volume")) return 1;

  if (require(ardor::applyControlEvent(state, {ardor::ControlEventType::EncoderTurned, 0, -200}),
              "encoder low clamp should apply")) return 1;
  if (require(state.masterVolume == 0, "encoder should clamp low")) return 1;

  if (require(ardor::applyControlEvent(state, {ardor::ControlEventType::EncoderTurned, 0, 500}),
              "encoder high clamp should apply")) return 1;
  if (require(state.masterVolume == 100, "encoder should clamp high")) return 1;

  using namespace std::chrono_literals;
  ardor::FootswitchGesture gesture;
  const auto start = ardor::FootswitchGesture::Clock::time_point{};
  if (require(!gesture.handle({ardor::ControlEventType::FootswitchPressed, 0, 0}, start),
              "first chord switch should wait")) return 1;
  if (require(!gesture.handle({ardor::ControlEventType::FootswitchPressed, 1, 0}, start + 50ms),
              "second chord switch should suppress preset selection")) return 1;
  if (require(!gesture.poll(start + 1049ms), "chord should not trigger early")) return 1;
  const auto tunerAction = gesture.poll(start + 1050ms);
  if (require(tunerAction && tunerAction->type == ardor::FootswitchActionType::ToggleTuner,
              "one-second left chord should toggle tuner")) return 1;
  if (require(!gesture.poll(start + 2s), "held chord should trigger only once")) return 1;
  gesture.handle({ardor::ControlEventType::FootswitchReleased, 0, 0}, start + 2s);
  gesture.handle({ardor::ControlEventType::FootswitchReleased, 1, 0}, start + 2s);

  gesture.reset();
  gesture.handle({ardor::ControlEventType::FootswitchPressed, 0, 0}, start);
  const auto shortPress = gesture.handle(
    {ardor::ControlEventType::FootswitchReleased, 0, 0}, start + 50ms);
  if (require(shortPress && shortPress->type == ardor::FootswitchActionType::SelectPreset
                && shortPress->index == 0,
              "short left press should retain preset selection")) return 1;

  const auto immediate = gesture.handle(
    {ardor::ControlEventType::FootswitchPressed, 3, 0}, start + 100ms);
  if (require(immediate && immediate->type == ardor::FootswitchActionType::SelectPreset
                && immediate->index == 3,
              "right switches should remain immediate")) return 1;

  return 0;
}
