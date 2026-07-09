#include "control/ControlEvents.h"

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

  return 0;
}
