#include "control/ControlEvents.h"
#include "control/Expression.h"
#include "control/Midi.h"

#include <chrono>
#include <cmath>
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

  ardor::MidiStreamParser midi;
  ardor::MidiControlMapper midiControls;
  if (require(!midi.push(0xc2), "program status should wait for data")) return 1;
  const auto program = midi.push(3);
  if (require(program && program->type == ardor::MidiMessageType::ProgramChange
                && program->channel == 2 && program->data1 == 3,
              "program change should parse")) return 1;
  const auto presetAction = midiControls.map(*program);
  if (require(presetAction && presetAction->type == ardor::MidiActionType::SelectPreset
                && presetAction->value == 3,
              "program change should select one of four preset slots")) return 1;

  // Running status remains active and realtime clock bytes may be interleaved.
  if (require(!midi.push(0xf8), "realtime byte should be ignored")) return 1;
  const auto runningProgram = midi.push(1);
  if (require(runningProgram && runningProgram->data1 == 1,
              "program running status should parse")) return 1;

  midi.push(0xb2);
  midi.push(32);
  const auto bank = midi.push(42);
  const auto bankAction = bank ? midiControls.map(*bank) : std::nullopt;
  if (require(bankAction && bankAction->type == ardor::MidiActionType::SelectBank
                && bankAction->value == 42,
              "bank select should map to bank 42")) return 1;

  midi.push(20); // CC running status
  const auto tunerOn = midi.push(127);
  const auto tunerOnAction = tunerOn ? midiControls.map(*tunerOn) : std::nullopt;
  if (require(tunerOnAction && tunerOnAction->type == ardor::MidiActionType::SetTuner
                && tunerOnAction->value == 1,
              "tuner CC high value should enable tuner")) return 1;
  midi.push(20);
  const auto tunerOff = midi.push(0);
  const auto tunerOffAction = tunerOff ? midiControls.map(*tunerOff) : std::nullopt;
  if (require(tunerOffAction && tunerOffAction->type == ardor::MidiActionType::SetTuner
                && tunerOffAction->value == 0,
              "tuner CC low value should disable tuner")) return 1;

  ardor::MidiControlMapper channelOne{{0, 20}};
  if (require(!channelOne.map(*program), "channel filter should reject another channel")) return 1;

  ardor::PresetMidiMapper presetMidi;
  presetMidi.load({
    {2, 11, ardor::PresetMidiBindingMode::Continuous,
      {{ardor::PresetMidiTargetType::Parameter, "wah", "position", 0.2f, 0.8f}}},
    {2, 64, ardor::PresetMidiBindingMode::Toggle, {
      {ardor::PresetMidiTargetType::BlockEnabled, "boost", "", 0.0f, 1.0f},
      {ardor::PresetMidiTargetType::BlockEnabled, "chorus", "", 1.0f, 0.0f},
      {ardor::PresetMidiTargetType::Parameter, "amp", "gain", 0.5f, 0.7f},
    }},
  });
  const auto sceneOne = presetMidi.reset();
  if (require(sceneOne.size() == 3 && sceneOne[0].value == 0.0f
                && sceneOne[1].value == 1.0f && sceneOne[2].value == 0.5f,
              "preset load should apply toggle scene 1")) return 1;
  const auto wahMid = presetMidi.map({ardor::MidiMessageType::ControlChange, 2, 11, 64});
  if (require(wahMid.size() == 1 && std::fabs(wahMid[0].value - 0.5023622f) < 0.0001f,
              "continuous learned CC should scale across its range")) return 1;
  const auto sceneTwo = presetMidi.map({ardor::MidiMessageType::ControlChange, 2, 64, 127});
  if (require(sceneTwo.size() == 3 && sceneTwo[0].value == 1.0f
                && sceneTwo[1].value == 0.0f && sceneTwo[2].value == 0.7f,
              "footswitch high edge should apply all scene 2 actions")) return 1;
  if (require(presetMidi.map({ardor::MidiMessageType::ControlChange, 2, 64, 127}).empty(),
              "held footswitch should not retrigger a scene")) return 1;
  if (require(presetMidi.map({ardor::MidiMessageType::ControlChange, 2, 64, 0}).empty(),
              "footswitch release should leave the scene latched")) return 1;
  const auto backToOne = presetMidi.map({ardor::MidiMessageType::ControlChange, 2, 64, 127});
  if (require(backToOne.size() == 3 && backToOne[2].value == 0.5f,
              "next footswitch press should return to scene 1")) return 1;
  if (require(!presetMidi.handles({ardor::MidiMessageType::ControlChange, 1, 11, 64}),
              "learned mapping should respect its channel")) return 1;

  ardor::ExpressionFilter expression{{100, 1100, 1.0f, 0.01f}};
  if (require(expression.valid(), "expression calibration should be valid")) return 1;
  const auto expressionMinimum = expression.update(100);
  if (require(expressionMinimum && *expressionMinimum == 0.0f,
              "expression minimum should map to zero")) return 1;
  const auto expressionMid = expression.update(600);
  if (require(expressionMid && std::fabs(*expressionMid - 0.5f) < 1.0e-6f,
              "expression midpoint should map to one half")) return 1;
  if (require(!expression.update(600),
              "expression deadband should suppress insignificant repeats")) return 1;
  expression.reset();
  const auto expressionMaximum = expression.update(1200);
  if (require(expressionMaximum && *expressionMaximum == 1.0f,
              "expression input should clamp above calibrated maximum")) return 1;
  ardor::ExpressionFilter invalidExpression{{100, 100, 0.5f, 0.01f}};
  if (require(!invalidExpression.valid() && !invalidExpression.update(100),
              "invalid expression calibration should reject samples")) return 1;

  return 0;
}
