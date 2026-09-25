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

  gesture.configureScenes(true, true);
  if (require(!gesture.handle({ardor::ControlEventType::FootswitchPressed, 2, 0}, start),
              "scene switch should wait for the 60 ms chord window")) return 1;
  if (require(!gesture.poll(start + 59ms), "scene switch should not fire before 60 ms")) return 1;
  const auto sceneAtThreshold = gesture.poll(start + 60ms);
  if (require(sceneAtThreshold
                && sceneAtThreshold->type == ardor::FootswitchActionType::SelectScene
                && sceneAtThreshold->index == 2,
              "scene switch should fire at the 60 ms threshold")) return 1;
  gesture.handle({ardor::ControlEventType::FootswitchReleased, 2, 0}, start + 70ms);

  gesture.configureScenes(true, true);
  gesture.handle({ardor::ControlEventType::FootswitchPressed, 2, 0}, start);
  gesture.handle({ardor::ControlEventType::FootswitchPressed, 3, 0}, start + 60ms);
  if (require(!gesture.poll(start + 659ms), "scene-layer chord should not fire early")) return 1;
  const auto layerAction = gesture.poll(start + 660ms);
  if (require(layerAction
                && layerAction->type == ardor::FootswitchActionType::ToggleSceneLayer,
              "right chord should toggle the scene layer after 600 ms")) return 1;
  if (require(!gesture.poll(start + 2s), "held scene-layer chord should fire once")) return 1;
  gesture.handle({ardor::ControlEventType::FootswitchReleased, 2, 0}, start + 2s);
  gesture.handle({ardor::ControlEventType::FootswitchReleased, 3, 0}, start + 2s);

  gesture.configureScenes(true, false);
  gesture.handle({ardor::ControlEventType::FootswitchPressed, 0, 0}, start);
  gesture.handle({ardor::ControlEventType::FootswitchPressed, 1, 0}, start + 60ms);
  const auto sceneModeTuner = gesture.poll(start + 1060ms);
  if (require(sceneModeTuner
                && sceneModeTuner->type == ardor::FootswitchActionType::ToggleTuner,
              "left chord should retain tuner in the enhanced parser")) return 1;

  gesture.configureScenes(true, true);
  gesture.handle({ardor::ControlEventType::FootswitchPressed, 2, 0}, start);
  gesture.handle({ardor::ControlEventType::FootswitchPressed, 3, 0}, start + 20ms);
  gesture.handle({ardor::ControlEventType::FootswitchPressed, 0, 0}, start + 30ms);
  if (require(!gesture.poll(start + 2s), "third overlapping switch should cancel pending chords")) return 1;
  gesture.handle({ardor::ControlEventType::FootswitchReleased, 0, 0}, start + 2s);
  gesture.handle({ardor::ControlEventType::FootswitchReleased, 2, 0}, start + 2s);
  gesture.handle({ardor::ControlEventType::FootswitchReleased, 3, 0}, start + 2s);

  gesture.configureScenes(true, true, false);
  const auto chordDisabledRight = gesture.handle(
    {ardor::ControlEventType::FootswitchPressed, 3, 0}, start);
  if (require(chordDisabledRight
                && chordDisabledRight->type == ardor::FootswitchActionType::SelectScene,
              "disabled layer chord should restore immediate right switches")) return 1;

  gesture.configureScenes(false, true);
  const auto noScenesRight = gesture.handle(
    {ardor::ControlEventType::FootswitchPressed, 3, 0}, start);
  if (require(noScenesRight
                && noScenesRight->type == ardor::FootswitchActionType::SelectPreset,
              "presets without scenes should retain legacy preset switching")) return 1;

  gesture.configureScenes(false, false);
  gesture.setLooperEntrySlot(2);
  if (require(!gesture.handle({ardor::ControlEventType::FootswitchPressed, 2, 0}, start),
              "active right preset must wait for its looper hold")) return 1;
  if (require(!gesture.poll(start + 999ms), "looper entry must wait one second")) return 1;
  const auto openLooper = gesture.poll(start + 1000ms);
  if (require(openLooper && openLooper->type == ardor::FootswitchActionType::OpenLooper
                && openLooper->index == 2,
              "holding active preset must open Looper")) return 1;
  if (require(!gesture.poll(start + 1500ms)
                && !gesture.handle({ardor::ControlEventType::FootswitchReleased, 2, 0},
                                   start + 1510ms),
              "looper entry must fire once and suppress release selection")) return 1;
  gesture.handle({ardor::ControlEventType::FootswitchPressed, 2, 0}, start + 2s);
  const auto activeTap = gesture.handle(
    {ardor::ControlEventType::FootswitchReleased, 2, 0}, start + 2050ms);
  if (require(activeTap && activeTap->type == ardor::FootswitchActionType::SelectPreset,
              "a short active-preset tap must retain preset selection")) return 1;
  const auto inactiveTap = gesture.handle(
    {ardor::ControlEventType::FootswitchPressed, 3, 0}, start + 3s);
  if (require(inactiveTap && inactiveTap->type == ardor::FootswitchActionType::SelectPreset
                && inactiveTap->index == 3,
              "an inactive preset must still select immediately")) return 1;
  gesture.handle({ardor::ControlEventType::FootswitchReleased, 3, 0}, start + 3050ms);

  gesture.configureScenes(false, false);
  gesture.setLooperEntrySlot(0);
  gesture.handle({ardor::ControlEventType::FootswitchPressed, 0, 0}, start);
  gesture.handle({ardor::ControlEventType::FootswitchPressed, 1, 0}, start + 50ms);
  const auto chordBeforeLooper = gesture.poll(start + 1050ms);
  if (require(chordBeforeLooper
                && chordBeforeLooper->type == ardor::FootswitchActionType::ToggleTuner,
              "the tuner chord must outrank an active-preset looper hold")) return 1;
  gesture.handle({ardor::ControlEventType::FootswitchReleased, 0, 0}, start + 1100ms);
  gesture.handle({ardor::ControlEventType::FootswitchReleased, 1, 0}, start + 1100ms);
  gesture.setLooperEntrySlot(-1);
  const auto noLooperEntry = gesture.handle(
    {ardor::ControlEventType::FootswitchPressed, 2, 0}, start + 2s);
  if (require(noLooperEntry && noLooperEntry->type == ardor::FootswitchActionType::SelectPreset,
              "looper entry must be disabled away from the preset screen")) return 1;
  gesture.handle({ardor::ControlEventType::FootswitchReleased, 2, 0}, start + 2050ms);

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

  ardor::PresetSceneSet midiScenes;
  for (std::size_t index = 0; index < midiScenes.scenes.size(); ++index) {
    midiScenes.scenes[index].id = "scene-" + std::to_string(index + 1);
  }
  ardor::SceneMidiMapper sceneMidi;
  sceneMidi.load({
    {2, 70, ardor::PresetSceneMidiActionType::SelectScene, "scene-3"},
    {2, 71, ardor::PresetSceneMidiActionType::SceneNumber, ""},
    {2, 72, ardor::PresetSceneMidiActionType::ShowPresets, ""},
    {2, 73, ardor::PresetSceneMidiActionType::ShowScenes, ""},
  }, midiScenes);
  const auto directScene = sceneMidi.map(
    {ardor::MidiMessageType::ControlChange, 2, 70, 127});
  if (require(directScene && directScene->type == ardor::SceneMidiActionType::SelectScene
                && directScene->sceneIndex == 2,
              "named MIDI scene should resolve its stable ID")) return 1;
  if (require(!sceneMidi.map({ardor::MidiMessageType::ControlChange, 2, 70, 127}),
              "held named scene switch should not retrigger")) return 1;
  sceneMidi.map({ardor::MidiMessageType::ControlChange, 2, 70, 0});
  if (require(sceneMidi.map({ardor::MidiMessageType::ControlChange, 2, 70, 127}).has_value(),
              "named scene switch should rearm after release")) return 1;
  const auto numberedScene = sceneMidi.map(
    {ardor::MidiMessageType::ControlChange, 2, 71, 3});
  if (require(numberedScene && numberedScene->sceneIndex == 3,
              "scene-number MIDI should map exact slot values")) return 1;
  if (require(!sceneMidi.map({ardor::MidiMessageType::ControlChange, 2, 71, 3}),
              "repeated scene-number values should not restart a transition")) return 1;
  if (require(!sceneMidi.map({ardor::MidiMessageType::ControlChange, 2, 71, 4}),
              "scene-number MIDI should ignore values above three")) return 1;
  const auto showPresets = sceneMidi.map(
    {ardor::MidiMessageType::ControlChange, 2, 72, 127});
  const auto showScenes = sceneMidi.map(
    {ardor::MidiMessageType::ControlChange, 2, 73, 127});
  if (require(showPresets && showPresets->type == ardor::SceneMidiActionType::ShowPresets
                && showScenes && showScenes->type == ardor::SceneMidiActionType::ShowScenes,
              "named MIDI actions should set either control layer")) return 1;

  ardor::ControllerPickup pickup;
  if (require(pickup.observe(0.1f), "unarmed controller should pass through")) return 1;
  pickup.arm(0.8f, 0.02f, 0.02f);
  if (require(!pickup.observe(0.1f) && !pickup.observe(0.11f),
              "stationary or insignificant movement should not pick up")) return 1;
  if (require(!pickup.observe(0.5f), "fresh movement short of target should remain gated")) return 1;
  if (require(pickup.observe(0.81f), "crossing the recalled target should pick up")) return 1;
  pickup.arm(0.0f, 0.02f, 0.02f, false);
  if (require(!pickup.observe(0.82f) && pickup.observe(0.85f),
              "equal endpoints should require fresh meaningful movement")) return 1;

  ardor::PresetScene pickupScene;
  pickupScene.targets.push_back({
    ardor::PresetSceneTargetType::Parameter, "wah", "position", "", 0.8f,
  });
  ardor::PresetMidiMapper pickupMidi;
  pickupMidi.load({
    {2, 11, ardor::PresetMidiBindingMode::Continuous,
      {{ardor::PresetMidiTargetType::Parameter, "wah", "position", 0.2f, 0.8f}}},
  });
  if (require(pickupMidi.map({ardor::MidiMessageType::ControlChange, 2, 11, 20}).size() == 1,
              "continuous MIDI should pass before scene recall")) return 1;
  pickupMidi.rearmSceneOwned(pickupScene);
  if (require(pickupMidi.map({ardor::MidiMessageType::ControlChange, 2, 11, 20}).empty()
                && pickupMidi.map({ardor::MidiMessageType::ControlChange, 2, 11, 21}).empty()
                && pickupMidi.map({ardor::MidiMessageType::ControlChange, 2, 11, 80}).empty(),
              "MIDI pickup should require movement and target crossing")) return 1;
  if (require(pickupMidi.map({ardor::MidiMessageType::ControlChange, 2, 11, 127}).size() == 1,
              "MIDI pickup should release at the recalled destination")) return 1;

  ardor::PresetScene mixedScene;
  mixedScene.targets.push_back({
    ardor::PresetSceneTargetType::Parameter, "scene-block", "mix", "", 0.5f,
  });
  ardor::PresetMidiMapper mixedToggle;
  mixedToggle.load({
    {2, 74, ardor::PresetMidiBindingMode::Toggle, {
      {ardor::PresetMidiTargetType::Parameter, "scene-block", "mix", 0.1f, 0.9f},
      {ardor::PresetMidiTargetType::Parameter, "shared-block", "gain", 0.2f, 0.8f},
    }},
  });
  mixedToggle.reset();
  const auto firstMixedHigh = mixedToggle.map(
    {ardor::MidiMessageType::ControlChange, 2, 74, 127});
  if (require(firstMixedHigh.size() == 2 && firstMixedHigh[0].value == 0.9f
                && firstMixedHigh[1].value == 0.8f,
              "mixed toggle should enter both second endpoints")) return 1;
  mixedToggle.rearmSceneOwned(mixedScene);
  if (require(mixedToggle.map({ardor::MidiMessageType::ControlChange, 2, 74, 127}).empty(),
              "scene recall should preserve a held toggle high state")) return 1;
  mixedToggle.map({ardor::MidiMessageType::ControlChange, 2, 74, 0});
  const auto mixedAfterRecall = mixedToggle.map(
    {ardor::MidiMessageType::ControlChange, 2, 74, 127});
  if (require(mixedAfterRecall.size() == 2 && mixedAfterRecall[0].value == 0.9f
                && mixedAfterRecall[1].value == 0.2f,
              "scene recall should reset only the scene-owned toggle latch")) return 1;

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
