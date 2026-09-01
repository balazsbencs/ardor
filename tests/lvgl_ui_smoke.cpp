#include "ui/LvglUi.h"
#include "ui/LvglUiStyle.h"
#include "ui/EqEditorModel.h"
#include "ui/fonts/SairaCondSemibold52.h"
#include "ui/fonts/SairaCondSemibold72.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <vector>

namespace {

void captureFlush(lv_display_t* display, const lv_area_t*, uint8_t*)
{
  lv_display_flush_ready(display);
}

bool saveRgb888Ppm(const char* path, const uint8_t* pixels, uint32_t stride,
                   int width, int height)
{
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) return false;
  output << "P6\n" << width << ' ' << height << "\n255\n";
  for (int y = 0; y < height; ++y) {
    const auto* row = pixels + static_cast<std::size_t>(y) * stride;
    for (int x = 0; x < width; ++x) {
      const std::array<char, 3> rgb = {
        static_cast<char>(row[x * 3 + 2]),
        static_cast<char>(row[x * 3 + 1]),
        static_cast<char>(row[x * 3]),
      };
      output.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    }
  }
  return output.good();
}

int require(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << "\n";
    return 1;
  }
  return 0;
}

void completePreview(ardor::UiState& state)
{
  if (ardor::pendingStructuralPreview(state)) ardor::completeStructuralPreview(state);
}

struct SimulatedPointer {
  lv_point_t point{};
  lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
};

void readSimulatedPointer(lv_indev_t* input, lv_indev_data_t* data)
{
  const auto* pointer = static_cast<const SimulatedPointer*>(lv_indev_get_user_data(input));
  data->point = pointer->point;
  data->state = pointer->state;
  data->continue_reading = false;
}

bool containsKey(const std::vector<ardor::ParameterControl>& controls, const char* key)
{
  return std::any_of(controls.begin(), controls.end(), [key](const auto& control) {
    return control.key == key;
  });
}

lv_obj_t* findLabel(lv_obj_t* parent, const char* text)
{
  if (lv_obj_check_type(parent, &lv_label_class) && std::strcmp(lv_label_get_text(parent), text) == 0) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* result = findLabel(lv_obj_get_child(parent, static_cast<int32_t>(i)), text)) {
      return result;
    }
  }
  return nullptr;
}

lv_obj_t* findNestedLabel(lv_obj_t* parent, const char* text)
{
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    lv_obj_t* child = lv_obj_get_child(parent, static_cast<int32_t>(i));
    if (!lv_obj_check_type(child, &lv_label_class)) {
      if (auto* result = findLabel(child, text)) return result;
    }
  }
  return nullptr;
}

lv_obj_t* findLabelContaining(lv_obj_t* parent, const char* text)
{
  if (lv_obj_check_type(parent, &lv_label_class)
      && std::strstr(lv_label_get_text(parent), text) != nullptr) {
    return parent;
  }
  const auto count = lv_obj_get_child_count(parent);
  for (int32_t index = 0; index < count; ++index) {
    if (auto* found = findLabelContaining(lv_obj_get_child(parent, index), text)) return found;
  }
  return nullptr;
}

lv_obj_t* findObjectWithBgColor(lv_obj_t* parent, lv_color_t color, int width)
{
  if (lv_obj_get_width(parent) == width
      && lv_color_eq(lv_obj_get_style_bg_color(parent, LV_PART_MAIN), color)) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* result = findObjectWithBgColor(lv_obj_get_child(parent, static_cast<int32_t>(i)), color, width)) {
      return result;
    }
  }
  return nullptr;
}

lv_obj_t* findObjectWithBgColor(lv_obj_t* parent, lv_color_t color)
{
  if (lv_color_eq(lv_obj_get_style_bg_color(parent, LV_PART_MAIN), color)
      && lv_obj_get_style_bg_opa(parent, LV_PART_MAIN) != LV_OPA_TRANSP) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* result = findObjectWithBgColor(lv_obj_get_child(parent, static_cast<int32_t>(i)), color)) {
      return result;
    }
  }
  return nullptr;
}

lv_obj_t* findObjectWithSizeAndBgColor(lv_obj_t* parent, lv_color_t color, int width, int height)
{
  if (lv_obj_get_width(parent) == width && lv_obj_get_height(parent) == height
      && lv_color_eq(lv_obj_get_style_bg_color(parent, LV_PART_MAIN), color)) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* result = findObjectWithSizeAndBgColor(lv_obj_get_child(parent, static_cast<int32_t>(i)), color,
                                                     width, height)) {
      return result;
    }
  }
  return nullptr;
}

lv_obj_t* findObjectWithSize(lv_obj_t* parent, int width, int height)
{
  if (lv_obj_get_width(parent) == width && lv_obj_get_height(parent) == height) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* result = findObjectWithSize(
          lv_obj_get_child(parent, static_cast<int32_t>(i)), width, height)) {
      return result;
    }
  }
  return nullptr;
}

// The rail control's fill/handle are identified by their fixed
// geometry rather than colour, since colour now depends on focus state
// (design law 3: lamp only on the selected control).
lv_obj_t* findObjectWithHeight(lv_obj_t* parent, int height)
{
  if (lv_obj_get_height(parent) == height) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* result = findObjectWithHeight(lv_obj_get_child(parent, static_cast<int32_t>(i)), height)) {
      return result;
    }
  }
  return nullptr;
}

std::string upper(const std::string& value)
{
  std::string result = value;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return result;
}

// Mirrors LvglUiParameterRenderer.cpp's splitFormattedValue: the slider's big
// numeral shows only the value, with the unit in a separate muted label.
std::string numericPrefix(const std::string& formatted)
{
  std::size_t i = 0;
  const std::size_t n = formatted.size();
  while (i < n && (std::isdigit(static_cast<unsigned char>(formatted[i]))
                    || formatted[i] == '-' || formatted[i] == '+' || formatted[i] == '.')) {
    ++i;
  }
  if (i == 0 || i == n) {
    return formatted;
  }
  return formatted.substr(0, i);
}

lv_obj_t* findHorizontalRailEndingAt(lv_obj_t* parent, lv_color_t color, int x)
{
  lv_area_t area{};
  lv_obj_get_coords(parent, &area);
  if (lv_obj_get_height(parent) == 3 && area.x2 == x
      && lv_color_eq(lv_obj_get_style_bg_color(parent, LV_PART_MAIN), color)) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* result = findHorizontalRailEndingAt(
          lv_obj_get_child(parent, static_cast<int32_t>(i)), color, x)) {
      return result;
    }
  }
  return nullptr;
}

lv_obj_t* findObjectOfClass(lv_obj_t* parent, const lv_obj_class_t* objectClass)
{
  if (lv_obj_check_type(parent, objectClass)) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* result = findObjectOfClass(lv_obj_get_child(parent, static_cast<int32_t>(i)), objectClass)) {
      return result;
    }
  }
  return nullptr;
}

lv_obj_t* findLineWithPointCount(lv_obj_t* parent, uint32_t pointCount)
{
  if (lv_obj_check_type(parent, &lv_line_class) && lv_line_get_point_count(parent) == pointCount) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* result = findLineWithPointCount(lv_obj_get_child(parent, static_cast<int32_t>(i)), pointCount)) {
      return result;
    }
  }
  return nullptr;
}

} // namespace

int main()
{
  auto state = ardor::makeDemoUiState();
  auto choiceState = ardor::makeDemoUiState();
  ardor::selectPreset(choiceState, 2);
  const auto& choiceBlocks = choiceState.bank.presets[choiceState.activePreset].blocks;
  const auto chorusBlock = std::find_if(choiceBlocks.begin(), choiceBlocks.end(), [](const auto& block) {
    return block.type == "mod" && block.params.value("mode", "") == "chorus";
  });
  if (require(chorusBlock != choiceBlocks.end(), "demo preset should contain Chorus")) return 1;
  ardor::selectBlock(choiceState, static_cast<std::size_t>(std::distance(choiceBlocks.begin(), chorusBlock)));
  const auto chorusControls = ardor::parameterPage(choiceState, 0);
  const auto chorusType = std::find_if(chorusControls.begin(), chorusControls.end(), [](const auto& control) {
    return control.key == "p2";
  });
  if (require(chorusType != chorusControls.end()
                && chorusType->kind == ardor::ParameterControlKind::NormalizedChoice
                && chorusType->choices.size() == 5 && chorusType->step == 1.0f,
              "Chorus type should expose five discrete slider positions")) return 1;
  if (require(ardor::applyParameterDelta(choiceState, *chorusType, 1),
              "one encoder tick should select the next Chorus type")) return 1;
  const float storedChorusType = choiceState.bank.presets[choiceState.activePreset]
    .blocks[choiceState.selectedBlock].params.value("p2", -1.0f);
  if (require(std::fabs(storedChorusType - 0.25f) < 0.0001f,
              "discrete Chorus selection should persist its normalized snap value")) return 1;

  auto precisionState = ardor::makeDemoUiState();
  ardor::selectPreset(precisionState, 2);
  const auto& precisionBlocks = precisionState.bank.presets[precisionState.activePreset].blocks;
  const auto tapeBlock = std::find_if(precisionBlocks.begin(), precisionBlocks.end(), [](const auto& block) {
    return block.type == "delay" && block.params.value("mode", "") == "tape";
  });
  if (require(tapeBlock != precisionBlocks.end(), "demo preset should contain Tape Delay")) return 1;
  ardor::selectBlock(precisionState, static_cast<std::size_t>(std::distance(precisionBlocks.begin(), tapeBlock)));
  const auto tapeControls = ardor::parameterPage(precisionState, 0);
  const auto tapeTime = std::find_if(tapeControls.begin(), tapeControls.end(), [](const auto& control) {
    return control.key == "time";
  });
  if (require(tapeTime != tapeControls.end() && tapeTime->step == 0.001f,
              "Tape Delay time should use fine encoder steps")) return 1;
  auto namState = ardor::makeDemoUiState();
  ardor::selectBlock(namState, 0);
  const auto namControls = ardor::parameterPage(namState, 0);
  const auto namInput = std::find_if(namControls.begin(), namControls.end(), [](const auto& control) {
    return control.key == "inputMode";
  });
  const auto namNano = std::find_if(namControls.begin(), namControls.end(), [](const auto& control) {
    return control.key == "useNano";
  });
  if (require(namControls.size() == 2 && namInput != namControls.end()
                && namInput->kind == ardor::ParameterControlKind::Choice
                && namInput->formatted == "L+R Average",
              "NAM page should expose sum-default input routing")) return 1;
  auto namInputState = namState;
  if (require(ardor::applyParameterDelta(namInputState, *namInput, 1),
              "NAM input routing should apply")) return 1;
  if (require(namInputState.bank.presets[namInputState.activePreset].blocks[0].params.value("inputMode", "") == "left",
              "NAM input routing should persist its string value")) return 1;
  if (require(namNano != namControls.end()
                && namNano->kind == ardor::ParameterControlKind::Toggle
                && namNano->formatted == "Off",
              "NAM page should expose a full-default nano switch")) return 1;
  if (require(ardor::applyParameterDelta(namState, *namNano, 1),
              "NAM nano switch should apply")) return 1;
  if (require(namState.bank.presets[namState.activePreset].blocks[0].params.value("useNano", false),
              "NAM nano switch should persist its boolean value")) return 1;

  ardor::Preset dualRigPreset;
  dualRigPreset.version = 2;
  dualRigPreset.name = "Touch Dual Rig";
  ardor::PresetBlock dualRig{"rig", "dualRig", true, "", {
    {"inputMode", "sum"},
    {"leftLevelDb", 0.0f}, {"leftPolarityInvert", false},
    {"rightLevelDb", -3.0f}, {"rightPolarityInvert", true},
  }};
  dualRig.lanes[0].push_back({"left-nam", "nam", true, "models/clean.nam", nlohmann::json::object()});
  dualRig.lanes[0].push_back({"left-cab", "cab", true, "irs/open-back.wav", nlohmann::json::object()});
  dualRig.lanes[0].push_back({"left-chorus", "mod", true, "", {{"mode", "chorus"}}});
  dualRig.lanes[0].push_back({"left-room", "reverb", true, "", {{"mode", "room"}}});
  dualRig.lanes[1].push_back({"right-nam", "nam", true, "models/crunch.nam", nlohmann::json::object()});
  dualRig.lanes[1].push_back({"right-cab", "cab", true, "irs/vintage.wav", nlohmann::json::object()});
  dualRig.lanes[1].push_back({"right-delay", "delay", true, "", {{"mode", "digital"}}});
  dualRigPreset.blocks.push_back(std::move(dualRig));
  auto dualRigState = ardor::makeDemoUiState();
  ardor::replaceActivePreset(dualRigState, dualRigPreset);
  if (require(dualRigState.bank.presets[dualRigState.activePreset].blocks[0].assetName
                == "Left 4 blocks  /  Right 3 blocks",
              "touchscreen inspector should summarize both Dual Rig lane sizes")) return 1;
  const auto dualRigControls = ardor::parameterPage(dualRigState, 0);
  if (require(dualRigControls.size() == 5
                && dualRigControls[1].key == "leftLevelDb"
                && dualRigControls[3].key == "rightLevelDb",
              "touchscreen should expose Dual Rig routing and lane output controls")) return 1;
  const auto preservedDualRig = ardor::activePresetToPreset(dualRigState);
  if (require(preservedDualRig.version == 2
                && preservedDualRig.blocks[0].lanes[0].size() == 4
                && preservedDualRig.blocks[0].lanes[1][1].asset == "irs/vintage.wav",
              "touchscreen load/save must preserve version-2 child chains")) return 1;
  if (require(ardor::pendingStructuralPreview(namState),
              "changing NAM model tier should queue an engine preview")) return 1;

  ardor::selectBlock(state, 1);
  const auto cabControls = ardor::parameterPage(state, 0);
  if (require(containsKey(cabControls, "levelDb"), "cab page should contain levelDb")) return 1;
  if (require(containsKey(cabControls, "mix"), "cab page should contain mix")) return 1;

  const auto level = std::find_if(cabControls.begin(), cabControls.end(), [](const auto& control) {
    return control.key == "levelDb";
  });
  if (require(level != cabControls.end(), "cab level control should be available")) return 1;
  if (require(ardor::applyParameterDelta(state, *level, 80), "cab level delta should apply")) return 1;
  if (require(state.bank.presets[state.activePreset].blocks[state.selectedBlock].params.value("levelDb", 0.0f) == 12.0f,
              "cab level should clamp high")) return 1;
  if (require(level->formatted == "0 dB", "descriptor formatting should be preserved")) return 1;

  state.dirty = false;
  ardor::setSelectedBlockParam(state, "levelDb", -80.0f);
  ardor::setSelectedBlockParam(state, "mix", 2.0f);
  const auto& cab = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
  if (require(cab.params.value("levelDb", 0.0f) == -60.0f, "cab level setter should enforce its range")) return 1;
  if (require(cab.params.value("mix", 0.0f) == 1.0f, "cab mix setter should enforce its range")) return 1;
  if (require(state.dirty, "cab setter should dirty preset")) return 1;
  const auto lowControls = ardor::parameterPage(state, 0);
  const auto lowLevelControl = std::find_if(lowControls.begin(), lowControls.end(), [](const auto& control) {
    return control.key == "levelDb";
  });
  if (require(lowLevelControl != lowControls.end() && lowLevelControl->formatted == "-60 dB",
              "formatted values should reflect setter-clamped values")) return 1;
  state.dirty = false;
  if (require(!ardor::applyParameterDelta(state, *lowLevelControl, -1), "minimum cab delta should not change value")) return 1;
  if (require(state.dirty, "clamped delta should preserve setter dirty behavior")) return 1;

  int requestedBankDelta = 0;
  int requestedTunerMode = -1;
  int liveBypassUpdates = 0;
  int savedPresetNames = 0;
  std::size_t selectedLooperTrack = 0;
  int looperCommandCount = 0;
  ardor::LooperCommandType lastLooperCommand = ardor::LooperCommandType::OpenEmpty;
  int closeLooperCalls = 0;
  int newLooperCalls = 0;
  int saveLooperCalls = 0;
  int loadLooperCalls = 0;
  std::string loadedLoopId;
  std::string deletedLoopId;
  std::uint32_t savedAudioBlockSize = 0;
  ardor::UiActions uiActions;
  uiActions.savePreset = [&]() {
    ++savedPresetNames;
    state.dirty = false;
    ardor::setUiStatus(state, "Preset saved");
  };
  uiActions.changeBank = [&](int delta) { requestedBankDelta += delta; };
  uiActions.setTunerMode = [&](bool enabled) { requestedTunerMode = enabled ? 1 : 0; };
  uiActions.saveAudioBlockSize = [&](std::uint32_t blockSize, std::string&) {
    savedAudioBlockSize = blockSize;
    return true;
  };
  uiActions.updateBlockEnabled = [&](const std::string&, bool) {
    ++liveBypassUpdates;
    return true;
  };
  uiActions.selectLooperTrack = [&](std::size_t track) { selectedLooperTrack = track; };
  uiActions.looperCommand = [&](ardor::LooperCommandType type, std::size_t, float) {
    lastLooperCommand = type;
    ++looperCommandCount;
  };
  uiActions.closeLooper = [&]() { ++closeLooperCalls; };
  uiActions.newLooper = [&]() { ++newLooperCalls; };
  uiActions.saveLooper = [&]() { ++saveLooperCalls; };
  uiActions.loadLooper = [&]() { ++loadLooperCalls; };
  uiActions.loadLooperSet = [&](const std::string& id) { loadedLoopId = id; };
  uiActions.deleteLooperSet = [&](const std::string& id) { deletedLoopId = id; };
  ardor::LvglUi ui(std::move(uiActions));
  const int masterVolume = state.masterVolume;
  ui.focusParameter("levelDb");
  state.dirty = false;
  if (require(ui.applyFocusedParameterDelta(state, 1), "focused cab control should consume encoder tick")) return 1;
  if (require(state.bank.presets[state.activePreset].blocks[state.selectedBlock].params.value("levelDb", 0.0f) == -59.0f,
              "focused cab control should change by its descriptor step")) return 1;
  if (require(state.dirty, "focused cab control should dirty preset")) return 1;
  if (require(state.masterVolume == masterVolume, "focused cab control should leave master volume unchanged")) return 1;

  ardor::setSelectedBlockParam(state, "levelDb", level->maximum);
  state.dirty = false;
  if (require(ui.applyFocusedParameterDelta(state, 1), "focused cab control should consume a clamped encoder tick")) return 1;
  if (require(state.bank.presets[state.activePreset].blocks[state.selectedBlock].params.value("levelDb", 0.0f) == level->maximum,
              "focused cab control should clamp at its maximum")) return 1;
  if (require(state.dirty, "clamped focused cab control should dirty preset")) return 1;
  if (require(state.masterVolume == masterVolume, "clamped focused cab control should leave master volume unchanged")) return 1;

  ui.setParameterPage(1);
  ui.focusParameter("mix");
  ui.selectBlock(state, 1);
  if (require(ui.parameterPage() == 0, "block selection should reset parameter page")) return 1;
  if (require(!ui.applyFocusedParameterDelta(state, 1), "block selection should clear focused parameter")) return 1;

  state.dirty = false;
  ardor::setSelectedBlockEnabled(state, false);
  if (require(!state.bank.presets[state.activePreset].blocks[state.selectedBlock].enabled, "block should disable")) return 1;
  if (require(state.dirty, "block enable change should dirty preset")) return 1;
  completePreview(state);

  ardor::selectGlobalParams(state);
  const auto globals = ardor::parameterPage(state, 0);
  const auto input = std::find_if(globals.begin(), globals.end(), [](const auto& control) {
    return control.key == "inputGainDb";
  });
  if (require(input != globals.end(), "input gain control should be available")) return 1;
  if (require(input->minimum == -60.0f && input->maximum == 12.0f && input->step == 1.0f,
              "input gain should use the setter range")) return 1;
  if (require(ardor::applyParameterDelta(state, *input, 80), "input gain delta should apply")) return 1;
  if (require(state.bank.presets[state.activePreset].global.inputGainDb == 12.0f, "input gain should clamp high")) return 1;

  ui.setParameterPage(1);
  ui.focusParameter("inputGainDb");
  ui.selectGlobalParams(state);
  if (require(ui.parameterPage() == 0, "global selection should reset parameter page")) return 1;
  if (require(!ui.applyFocusedParameterDelta(state, 1), "global selection should clear focused parameter")) return 1;

  bool customPresetActionCalled = false;
  ardor::LvglUi presetUi({
    [&](std::size_t index) {
      customPresetActionCalled = true;
      ardor::selectPreset(state, index);
    },
    {},
  });
  presetUi.setParameterPage(1);
  presetUi.focusParameter("inputGainDb");
  presetUi.selectPreset(state, 1);
  if (require(customPresetActionCalled, "custom preset action should be called")) return 1;
  if (require(presetUi.parameterPage() == 0, "preset selection should reset parameter page")) return 1;
  if (require(!presetUi.applyFocusedParameterDelta(state, 1), "preset selection should clear focused parameter")) return 1;

  auto eqState = ardor::makeDemoUiState();
  const auto eqAsset = std::find_if(eqState.assets.begin(), eqState.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Five Band EQ";
  });
  if (require(eqAsset != eqState.assets.end(), "EQ asset should be available")) return 1;
  ardor::appendAssetBlock(eqState, static_cast<std::size_t>(std::distance(eqState.assets.begin(), eqAsset)));
  completePreview(eqState);
  bool eqActionCalled = false;
  std::string updatedEqId;
  std::size_t updatedEqBand = ardor::kParametricEqBandCount;
  ardor::EqBandParams updatedEqParams;
  ardor::LvglUi eqUi({
    {}, {},
    [&](const std::string& id, std::size_t band, const ardor::EqBandParams& params) {
      eqActionCalled = true;
      updatedEqId = id;
      updatedEqBand = band;
      updatedEqParams = params;
      return true;
    },
  });
  eqUi.selectBlock(eqState, eqState.bank.presets[eqState.activePreset].blocks.size() - 1);
  const auto eqBefore = ardor::selectedParametricEqParams(eqState).bands[0];
  eqUi.focusEqBandField(ardor::EqBandField::Gain);
  if (require(eqUi.applyFocusedParameterDelta(eqState, 2), "focused EQ gain should consume encoder ticks")) return 1;
  const auto eqAfter = ardor::selectedParametricEqParams(eqState).bands[0];
  if (require(eqAfter.gainDb == eqBefore.gainDb + 1.0f, "EQ gain should move in 0.5 dB ticks")) return 1;
  if (require(eqActionCalled && updatedEqBand == 0 && updatedEqParams == eqAfter,
              "EQ changes should invoke the live update action")) return 1;
  if (require(updatedEqId == eqState.bank.presets[eqState.activePreset].blocks.back().id,
              "EQ live update should retain the stable block id")) return 1;

  auto rejectedEqState = eqState;
  ardor::LvglUi rejectedEqUi({{}, {}, [](const std::string&, std::size_t, const ardor::EqBandParams&) {
    return false;
  }});
  rejectedEqUi.selectBlock(rejectedEqState,
                           rejectedEqState.bank.presets[rejectedEqState.activePreset].blocks.size() - 1);
  rejectedEqState.dirty = false;
  const auto rejectedEqBefore = ardor::selectedParametricEqParams(rejectedEqState).bands[0];
  rejectedEqUi.focusEqBandField(ardor::EqBandField::Gain);
  if (require(rejectedEqUi.applyFocusedParameterDelta(rejectedEqState, 2)
                && ardor::selectedParametricEqParams(rejectedEqState).bands[0] != rejectedEqBefore
                && ardor::pendingStructuralPreview(rejectedEqState),
              "rejected live EQ updates should promote to a full preview")) return 1;
  completePreview(rejectedEqState);

  const auto tremAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Vintage Trem";
  });
  if (require(tremAsset != state.assets.end(), "Vintage Trem should be available")) return 1;
  ardor::appendAssetBlock(state, static_cast<std::size_t>(std::distance(state.assets.begin(), tremAsset)));
  completePreview(state);
  ui.selectBlock(state, state.bank.presets[state.activePreset].blocks.size() - 1);

  const auto tremControls = ardor::parameterPage(state, 0);
  const auto depthControl = std::find_if(tremControls.begin(), tremControls.end(), [](const auto& control) {
    return control.key == "depth";
  });
  const auto speedControl = std::find_if(tremControls.begin(), tremControls.end(), [](const auto& control) {
    return control.key == "speed";
  });
  if (require(depthControl != tremControls.end(), "Daisy depth control should be available")) return 1;
  if (require(speedControl != tremControls.end() && speedControl->formatted.find("Hz") != std::string::npos,
              "Daisy speed should render its physical frequency")) return 1;
  const float depthBefore = depthControl->value;
  state.dirty = false;
  ui.focusParameter("depth");
  if (require(ui.applyFocusedParameterDelta(state, 1), "focused Daisy control should consume encoder tick")) return 1;
  if (require(state.bank.presets[state.activePreset].blocks[state.selectedBlock].params.value("depth", 0.0f)
                == depthBefore + depthControl->step,
              "focused Daisy control should change by its descriptor step")) return 1;
  if (require(state.dirty, "focused Daisy control should dirty preset")) return 1;
  if (require(state.masterVolume == masterVolume, "focused Daisy control should leave master volume unchanged")) return 1;

  auto rejectedDaisyState = state;
  rejectedDaisyState.dirty = false;
  const auto rejectedDaisyParams = rejectedDaisyState.bank.presets[rejectedDaisyState.activePreset]
    .blocks[rejectedDaisyState.selectedBlock].params;
  ardor::LvglUi rejectedDaisyUi({
    {}, {}, {}, [](const std::string&, const std::string&, float) { return false; },
  });
  rejectedDaisyUi.focusParameter("depth");
  if (require(rejectedDaisyUi.applyFocusedParameterDelta(rejectedDaisyState, 1)
                && rejectedDaisyState.pendingPreview.has_value()
                && rejectedDaisyState.pendingPreview->rollback.preset
                     .blocks[rejectedDaisyState.selectedBlock].params == rejectedDaisyParams,
              "rejected live controls should snapshot the pre-edit state for preview rollback")) return 1;
  ardor::failStructuralPreview(rejectedDaisyState, "test rollback");
  if (require(rejectedDaisyState.bank.presets[rejectedDaisyState.activePreset]
                .blocks[rejectedDaisyState.selectedBlock].params == rejectedDaisyParams,
              "a failed promoted preview should restore the original parameters")) return 1;

  ardor::setSelectedBlockParam(state, "depth", 2.0f);
  if (require(state.bank.presets[state.activePreset].blocks[state.selectedBlock].params.value("depth", 0.0f) == 1.0f,
              "Daisy setter should enforce descriptor range")) return 1;

  const std::size_t tremIndex = state.selectedBlock;
  const auto compressorAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Compressor";
  });
  if (require(compressorAsset != state.assets.end(), "compressor asset should be available")) return 1;
  ardor::appendAssetBlock(state, static_cast<std::size_t>(std::distance(state.assets.begin(), compressorAsset)));
  completePreview(state);
  ardor::setSelectedBlockParam(state, "ratio", 4.5f);
  ardor::setSelectedBlockParam(state, "attack_ms", 0.1f);
  const auto compressorControls = ardor::parameterPage(state, 0);
  const auto ratioControl = std::find_if(compressorControls.begin(), compressorControls.end(), [](const auto& control) {
    return control.key == "ratio";
  });
  const auto attackControl = std::find_if(compressorControls.begin(), compressorControls.end(), [](const auto& control) {
    return control.key == "attack_ms";
  });
  if (require(ratioControl != compressorControls.end() && ratioControl->formatted == "4.5:1"
                && attackControl != compressorControls.end() && attackControl->formatted == "0.1 ms",
              "compressor values should preserve meaningful fractional precision")) return 1;

  const auto noiseGateAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Noise Gate";
  });
  if (require(noiseGateAsset != state.assets.end(), "noise gate asset should be available")) return 1;
  ardor::appendAssetBlock(state, static_cast<std::size_t>(std::distance(state.assets.begin(), noiseGateAsset)));
  completePreview(state);
  bool noiseGateActionCalled = false;
  std::string updatedNoiseGateKey;
  ardor::LvglUi noiseGateUi({
    {}, {}, {}, {}, {},
    [&](const std::string&, const std::string& key, float) {
      noiseGateActionCalled = true;
      updatedNoiseGateKey = key;
      return true;
    },
  });
  noiseGateUi.focusParameter("threshold_db");
  if (require(noiseGateUi.applyFocusedParameterDelta(state, 1)
                && noiseGateActionCalled && updatedNoiseGateKey == "threshold_db",
              "noise gate numeric edits should use the live runtime action")) return 1;
  const auto noiseGateControls = ardor::parameterPage(state, 0);
  if (require(noiseGateControls.size() == 6
                && noiseGateControls[0].label == "Threshold"
                && noiseGateControls[1].label == "Reduction",
              "noise gate should expose meaningful first-page controls")) return 1;
  ui.selectBlock(state, tremIndex);

  auto tapeState = ardor::makeDemoUiState();
  const auto tapeAsset = std::find_if(
    tapeState.assets.begin(), tapeState.assets.end(), [](const ardor::UiAsset& asset) {
      return asset.name == "Tape Machine";
    });
  if (require(tapeAsset != tapeState.assets.end(),
              "Tape Machine should be available for the live slider test")) return 1;
  ardor::appendAssetBlock(tapeState, static_cast<std::size_t>(
    std::distance(tapeState.assets.begin(), tapeAsset)));
  completePreview(tapeState);
  int liveTapeUpdates = 0;
  std::string liveTapeKey;
  ardor::UiActions tapeActions;
  tapeActions.updateBlockParameter =
    [&](const std::string&, const std::string& key, float) {
      ++liveTapeUpdates;
      liveTapeKey = key;
      return true;
    };
  ardor::LvglUi tapeUi(std::move(tapeActions));
  tapeUi.selectBlock(
    tapeState, tapeState.bank.presets[tapeState.activePreset].blocks.size() - 1);
  tapeUi.focusParameter("saturation");
  if (require(tapeUi.applyFocusedParameterDelta(tapeState, 1)
                && tapeUi.applyFocusedParameterDelta(tapeState, 1)
                && liveTapeUpdates == 2 && liveTapeKey == "saturation"
                && !ardor::pendingStructuralPreview(tapeState),
              "Tape Machine slider movement should remain on the live block-parameter path")) return 1;

  if (require(ardor::parameterPage(state, 0).size() <= 6, "page must contain <= six sliders")) return 1;
  if (require(ardor::parameterPageCount(state) == 2, "seven params should use two slider pages")) return 1;

  ui.selectGlobalParams(state);
  ardor::setActiveInputGainDb(state, 0.0f);
  state.dirty = false;
  ui.focusParameter("inputGainDb");
  if (require(ui.applyFocusedParameterDelta(state, 1), "focused global control should consume encoder tick")) return 1;
  if (require(state.bank.presets[state.activePreset].global.inputGainDb == 1.0f,
              "focused global control should change by its descriptor step")) return 1;
  if (require(state.dirty, "focused global control should dirty preset")) return 1;
  if (require(state.masterVolume == masterVolume, "focused global control should leave master volume unchanged")) return 1;

  ui.focusParameter("");
  if (require(!ui.applyFocusedParameterDelta(state, 1), "no focused control should leave encoder available to master-volume fallback")) return 1;
  if (require(state.masterVolume == masterVolume, "no-focus UI handling should not change master volume itself")) return 1;

  lv_init();
  ardor::lvgl_ui::setPalette(ardor::PaletteId::Ink);
  if (require(ardor::lvgl_ui::palette().plate == 0x10161f
                && ardor::lvgl_ui::palette().lamp == 0x5fd0e8
                && ardor::lvgl_ui::categoryColor("delay") == 0x8d7fc4,
              "Ink palette should apply plate, LIVE, and family tokens together")) return 1;
  ardor::lvgl_ui::setPalette(ardor::PaletteId::Sodium);
  if (require(ardor::lvgl_ui::palette().plate == 0x0c0b09
                && ardor::lvgl_ui::palette().lamp == 0xffb01f
                && ardor::lvgl_ui::categoryColor("utility") == 0x6f8296,
              "Sodium palette should apply plate, LIVE, and family tokens together")) return 1;
  ardor::lvgl_ui::setPalette(ardor::PaletteId::Nord);
  if (require(ardor::lvgl_ui::palette().plate == 0x2e3440
                && ardor::lvgl_ui::palette().lamp == 0x88c0d0
                && ardor::lvgl_ui::categoryColor("delay") == 0xb48ead,
              "Nord palette should apply plate, LIVE, and family tokens together")) return 1;
  ardor::lvgl_ui::setPalette(ardor::PaletteId::Slate);
  lv_font_glyph_dsc_t glyph{};
  if (require(lv_font_get_glyph_dsc(&ardor_font_saira_cond_medium_18, &glyph, 'A', 0),
              "Open Sans should provide glyph descriptors")) return 1;
  lv_draw_buf_t* glyphBuffer = lv_draw_buf_create(glyph.box_w, glyph.box_h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
  if (require(glyphBuffer, "glyph buffer should allocate")) return 1;
  const void* glyphBitmap = lv_font_get_glyph_bitmap(&glyph, glyphBuffer);
  lv_draw_buf_destroy(glyphBuffer);
  if (require(glyphBitmap, "Open Sans glyphs should render in LVGL")) return 1;

  lv_display_t* display = lv_display_create(1280, 720);
  const char* screenshotPath = std::getenv("ARDOR_UI_SCREENSHOT");
  std::vector<uint8_t> screenshotStorage;
  uint8_t* screenshotPixels = nullptr;
  uint32_t screenshotStride = 0;
  if (screenshotPath != nullptr) {
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    screenshotStride = lv_draw_buf_width_to_stride(1280, LV_COLOR_FORMAT_RGB888);
    const auto screenshotBytes = screenshotStride * 720;
    screenshotStorage.resize(screenshotBytes + LV_DRAW_BUF_ALIGN);
    screenshotPixels = static_cast<uint8_t*>(
      lv_draw_buf_align(screenshotStorage.data(), LV_COLOR_FORMAT_RGB888));
    lv_display_set_buffers(display, screenshotPixels, nullptr, screenshotBytes,
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(display, captureFlush);
  }
  ardor::enterEditMode(dualRigState);
  ui.build(lv_screen_active(), dualRigState);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* splitJunction = findLabel(lv_screen_active(), "SPLIT");
  lv_obj_t* joinJunction = findLabel(lv_screen_active(), "JOIN");
  lv_obj_t* leftRigLane = findLabel(lv_screen_active(), "LEFT");
  lv_obj_t* rightRigLane = findLabel(lv_screen_active(), "RIGHT");
  lv_obj_t* leftOnlyEffect = findLabel(lv_screen_active(), "CHO");
  lv_obj_t* rightOnlyEffect = findLabel(lv_screen_active(), "DLY");
  lv_obj_t* laneDragHandle = findLabel(lv_screen_active(), "DRAG");
  lv_obj_t* splitDragHandle = splitJunction
    ? findLabel(lv_obj_get_parent(splitJunction), "|||") : nullptr;
  lv_area_t splitLabelArea{};
  lv_area_t splitHandleArea{};
  lv_area_t joinArea{};
  lv_area_t leftHeadingArea{};
  lv_area_t rightHeadingArea{};
  lv_area_t leftEffectTitleArea{};
  lv_area_t rightEffectTitleArea{};
  if (splitJunction) lv_obj_get_coords(splitJunction, &splitLabelArea);
  if (splitDragHandle) lv_obj_get_coords(lv_obj_get_parent(splitDragHandle), &splitHandleArea);
  if (joinJunction) lv_obj_get_coords(lv_obj_get_parent(joinJunction), &joinArea);
  if (leftRigLane) lv_obj_get_coords(leftRigLane, &leftHeadingArea);
  if (rightRigLane) lv_obj_get_coords(rightRigLane, &rightHeadingArea);
  if (leftOnlyEffect) lv_obj_get_coords(leftOnlyEffect, &leftEffectTitleArea);
  if (rightOnlyEffect) lv_obj_get_coords(rightOnlyEffect, &rightEffectTitleArea);
  const int joinCenterX = (joinArea.x1 + joinArea.x2) / 2;
  lv_obj_t* leftJoinRail = leftRigLane
    ? findHorizontalRailEndingAt(lv_screen_active(),
        lv_obj_get_style_text_color(leftRigLane, LV_PART_MAIN), joinCenterX) : nullptr;
  lv_obj_t* rightJoinRail = rightRigLane
    ? findHorizontalRailEndingAt(lv_screen_active(),
        lv_obj_get_style_text_color(rightRigLane, LV_PART_MAIN), joinCenterX) : nullptr;
  if (require(splitJunction && joinJunction && leftRigLane && rightRigLane
                && leftOnlyEffect && rightOnlyEffect && laneDragHandle && splitDragHandle,
              "Dual Rig should render Split/Join, both lanes, effects, and drag handles")) return 1;
  if (require(leftJoinRail && rightJoinRail,
              "both Dual Rig lane rails should reach the centered Join stem")) return 1;
  if (require(!lv_color_eq(lv_obj_get_style_text_color(leftRigLane, LV_PART_MAIN),
                           lv_obj_get_style_text_color(rightRigLane, LV_PART_MAIN))
                && lv_obj_get_y(leftRigLane) < lv_obj_get_y(rightRigLane)
                && lv_obj_get_x(splitJunction) < lv_obj_get_x(joinJunction),
              "Dual Rig should distinguish and order its two lanes")) return 1;
  if (require(splitLabelArea.x2 < splitHandleArea.x1,
              "Split title should not sit underneath its drag handle")) return 1;
  if (require(leftHeadingArea.x1 < leftEffectTitleArea.x1
                && rightHeadingArea.x1 < rightEffectTitleArea.x1,
              "Dual Rig lane headings should sit back at the split bend")) return 1;
  lv_obj_t* leftOnlyEffectTile = lv_obj_get_parent(lv_obj_get_parent(leftOnlyEffect));
  if (require(lv_obj_get_width(leftOnlyEffectTile) == 200
                && lv_obj_get_height(leftOnlyEffectTile) == 92,
              "Dual Rig effects should leave room for readable titles")) return 1;
  if (require(lv_obj_get_width(lv_obj_get_parent(laneDragHandle)) == 200
                && lv_obj_get_height(lv_obj_get_parent(laneDragHandle)) == 52,
              "Dual Rig effects should use their full title bar as a touch drag target")) return 1;
  lv_obj_t* dualRigChain = findObjectWithSizeAndBgColor(
    lv_screen_active(), lv_color_hex(0x212528), 1240, 492);
  lv_obj_t* laneDragSurface = lv_obj_get_parent(laneDragHandle);
  lv_obj_send_event(laneDragSurface, LV_EVENT_PRESSED, nullptr);
  if (require(dualRigChain && !lv_obj_has_flag(dualRigChain, LV_OBJ_FLAG_SCROLLABLE),
              "pressing a lane title bar should give drag ownership over chain scrolling")) return 1;
  lv_obj_send_event(laneDragSurface, LV_EVENT_RELEASED, nullptr);
  if (require(lv_obj_has_flag(dualRigChain, LV_OBJ_FLAG_SCROLLABLE),
              "releasing a lane title bar should restore chain scrolling")) return 1;

  lv_obj_send_event(lv_obj_get_parent(lv_obj_get_parent(rightOnlyEffect)), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), dualRigState);
  lv_obj_update_layout(lv_screen_active());
  const auto* selectedLaneEffect = ardor::selectedUiBlock(dualRigState);
  const auto selectedLaneControls = ardor::parameterPage(dualRigState, 0);
  if (require(selectedLaneEffect && selectedLaneEffect->id == "right-delay"
                && selectedLaneEffect->type == "delay"
                && dualRigState.paramDrawerOpen
                && !selectedLaneControls.empty()
                && !containsKey(selectedLaneControls, "leftLevelDb")
                && findLabel(lv_screen_active(), "Delay  /  Digital Delay"),
              "clicking a Dual Rig lane effect should open that effect's parameter drawer")) return 1;

  ui.selectBlock(state, state.selectedBlock);
  ardor::enterEditMode(state);
  const auto renderControls = ardor::parameterPage(state, 0);
  const auto depth = std::find_if(renderControls.begin(), renderControls.end(),
                                  [](const auto& control) { return control.key == "depth"; });
  if (require(depth != renderControls.end(), "Vintage Trem depth control should be available")) return 1;
  ardor::setSelectedBlockParam(state, "depth", depth->minimum);
  ardor::setSelectedBlockParam(state, "p2", 1.0f);
  state.bank.presets[state.activePreset].blocks.front().enabled = false;
  ardor::setUiStatus(state, "Preset saved");
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());

  const auto& selected = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
  const std::string titleText = selected.label + "  /  " + selected.assetName;
  lv_obj_t* previous = findLabel(lv_screen_active(), "<");
  lv_obj_t* page = findLabel(lv_screen_active(), "PAGE 1 / 2");
  lv_obj_t* next = findLabel(lv_screen_active(), ">");
  lv_obj_t* title = findLabel(lv_screen_active(), titleText.c_str());
  lv_obj_t* status = findLabel(lv_screen_active(), "Preset saved");
  lv_obj_t* undoLabel = findLabel(lv_screen_active(), "UNDO");
  lv_obj_t* depthLabel = findLabel(lv_screen_active(), upper(depth->label).c_str());
  lv_obj_t* depthSlider = depthLabel ? lv_obj_get_parent(depthLabel) : nullptr;
  lv_obj_t* depthFill = depthSlider ? findObjectWithHeight(depthSlider, 16) : nullptr;
  if (require(title && depthSlider && depthFill, "parameter header and slider should render")) return 1;
  if (require(status && lv_color_eq(lv_obj_get_style_text_color(status, LV_PART_MAIN), lv_color_hex(0xe2e4e3)),
              "success status should render in engraved text")) return 1;
  lv_area_t statusToastArea{};
  lv_obj_get_coords(status, &statusToastArea);
  if (require(statusToastArea.y1 >= 80 && statusToastArea.y2 < 180,
              "transient status should render as a top toast instead of footer text")) return 1;
  if (require(lv_anim_get(status, nullptr) != nullptr,
              "status toast should run the Panel slide-in/hold/dismiss animation")) return 1;
  lv_obj_t* retainedCanvas = ui.canvas();
  lv_obj_t* retainedStatus = status;
  for (int i = 0; i < 256; ++i) {
    auto telemetry = state.telemetry;
    telemetry.overBudget = static_cast<uint64_t>(i);
    telemetry.maxMs = static_cast<double>(i) / 10.0;
    ardor::updateRealtimeTelemetry(state, telemetry);
    ardor::setUiStatus(state, "tick " + std::to_string(i));
    ui.refresh(lv_screen_active(), state);
  }
  if (require(ui.canvas() == retainedCanvas && findLabel(lv_screen_active(), "tick 255") == retainedStatus,
              "telemetry and status churn should update retained objects in place")) return 1;
  ardor::setUiStatus(state, "Preset saved");
  ui.refresh(lv_screen_active(), state);
  ardor::updateClipDebugTelemetry(state, {true, true, "ir:cab", 0.5f, 10, 12});
  ui.refresh(lv_screen_active(), state);
  lv_obj_t* clipLabel = findLabel(lv_screen_active(), "CLIP  ir:cab  +0.5dB  10f");
  if (require(clipLabel
                && lv_color_eq(lv_obj_get_style_text_color(clipLabel, LV_PART_MAIN), lv_color_hex(0xf97373)),
              "touchscreen clip diagnostic should render in red")) return 1;
  ardor::updateClipDebugTelemetry(state, {});
  ui.refresh(lv_screen_active(), state);
  if (require(undoLabel && lv_obj_get_width(lv_obj_get_parent(undoLabel)) == 96
                && lv_obj_get_height(lv_obj_get_parent(undoLabel)) == 52,
              "reversible block edits should expose a large Undo action")) return 1;
  if (require(page, "parameter header should show PAGE n/total")) return 1;
  if (require(previous && next, "a seventh control should enable page navigation")) return 1;
  const auto depthIndex = static_cast<int>(std::distance(renderControls.begin(), depth));
  lv_area_t sliderArea{};
  lv_area_t sliderPanelArea{};
  lv_obj_get_coords(depthSlider, &sliderArea);
  lv_obj_get_coords(lv_obj_get_parent(depthSlider), &sliderPanelArea);
  // The Panel plate's visible one-pixel rule forms part of the inner origin.
  const int expectedDepthX = 29 + (depthIndex % 3) * (385 + 14);
  const int expectedDepthY = 79 + (depthIndex / 3) * (132 + 16);
  if (require(sliderArea.x1 - sliderPanelArea.x1 == expectedDepthX
                && sliderArea.y1 - sliderPanelArea.y1 == expectedDepthY,
              "parameter sliders should use a three-column, two-row grid")) return 1;
  if (require(lv_obj_get_width(depthSlider) == 385 && lv_obj_get_height(depthSlider) == 132,
              "parameter slider should provide a large vertical touch target")) return 1;
  if (require(findObjectWithSize(depthSlider, 44, 54),
              "parameter slider should expose a wide, finger-readable thumb")) return 1;
  if (require(lv_obj_get_style_radius(depthSlider, LV_PART_MAIN) == 0
                && lv_obj_get_style_radius(depthFill, LV_PART_MAIN) == 0,
              "parameter slider should use flat, unrounded plate geometry")) return 1;
  if (require(lv_obj_get_width(depthFill) == 0,
              "minimum parameter value should leave the active fill empty")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_text_color(depthLabel, LV_PART_MAIN), lv_color_hex(0x8d9499)),
              "an unfocused slider's key label should read as muted engraving")) return 1;
  lv_obj_t* typeLabel = findLabel(lv_screen_active(), "TYPE");
  lv_obj_t* typeSlider = typeLabel ? lv_obj_get_parent(typeLabel) : nullptr;
  // A discrete card renders the selected value once as its readout and again
  // inside the segmented option row. Target the nested copy so these checks
  // exercise the option button rather than the card itself.
  lv_obj_t* photoresistorValue = typeSlider ? findNestedLabel(typeSlider, "Photoresistor") : nullptr;
  lv_area_t typeSliderArea{};
  lv_area_t photoresistorArea{};
  if (typeSlider) lv_obj_get_coords(typeSlider, &typeSliderArea);
  if (photoresistorValue) lv_obj_get_coords(lv_obj_get_parent(photoresistorValue), &photoresistorArea);
  if (require(photoresistorValue
                && photoresistorArea.x1 >= typeSliderArea.x1 && photoresistorArea.x2 <= typeSliderArea.x2,
              "discrete modulation choices should render as segmented options inside the slider")) return 1;
  if (require(photoresistorValue
                && lv_obj_get_height(lv_obj_get_parent(photoresistorValue)) == 60
                && lv_obj_get_width(lv_obj_get_parent(photoresistorValue)) >= 44,
              "every discrete option should provide a finger-sized touch target")) return 1;

  const auto& defaultMappingControl = renderControls.front();
  lv_obj_t* mappingSelection = findLabel(lv_screen_active(),
    ("Selected  /  " + defaultMappingControl.label).c_str());
  lv_obj_t* mappingToolbar = mappingSelection ? lv_obj_get_parent(mappingSelection) : nullptr;
  lv_obj_t* assignExpression = mappingToolbar ? findLabel(mappingToolbar, "Assign EXP") : nullptr;
  lv_obj_t* learnMidi = mappingToolbar ? findLabel(mappingToolbar, "MIDI Learn") : nullptr;
  if (require(mappingToolbar && assignExpression && learnMidi
                && !findLabel(depthSlider, "EXP") && !findLabel(depthSlider, "MIDI"),
              "parameter mapping actions should live in one contextual toolbar")) return 1;
  lv_obj_send_event(lv_obj_get_parent(learnMidi), LV_EVENT_CLICKED, nullptr);
  if (require(state.midiLearn.stage == ardor::UiMidiLearnStage::Waiting,
              "contextual MIDI Learn should target the selected parameter")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_t* listeningLabel = findLabel(lv_screen_active(), "Listening...");
  lv_obj_t* midiLearnCard = listeningLabel ? lv_obj_get_parent(listeningLabel) : nullptr;
  lv_obj_t* midiLearnTitle = midiLearnCard ? findLabel(midiLearnCard, "MIDI Learn") : nullptr;
  if (require(midiLearnTitle
                && listeningLabel,
              "MIDI Learn should show a blocking listening sheet")) return 1;
  ardor::observeMidiLearnControlChange(state, 0, 11, 64);
  ui.refresh(lv_screen_active(), state);
  lv_obj_t* advancedLearn = midiLearnCard ? findLabel(midiLearnCard, "Advanced") : nullptr;
  if (require(findLabel(lv_screen_active(), "CC 11  ·  Channel 1")
                && advancedLearn && findLabel(lv_screen_active(), "Save"),
              "captured CC should expose Save and Advanced actions")) return 1;
  lv_obj_send_event(lv_obj_get_parent(advancedLearn), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(findLabel(lv_screen_active(), "Continuous")
                && findLabel(lv_screen_active(), "1")
                && findLabel(lv_screen_active(), "2"),
              "advanced MIDI Learn should show mode and two endpoint sliders")) return 1;
  lv_obj_t* cancelLearn = midiLearnCard ? findLabel(midiLearnCard, "Cancel") : nullptr;
  lv_obj_send_event(lv_obj_get_parent(cancelLearn), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(state.midiLearn.stage == ardor::UiMidiLearnStage::None,
              "cancelling MIDI Learn should close it without a mapping")) return 1;

  SimulatedPointer simulatedPointer{{sliderArea.x1 + 1,
                                     (sliderArea.y1 + sliderArea.y2) / 2},
                                    LV_INDEV_STATE_PRESSED};
  lv_indev_t* simulatedInput = lv_indev_create();
  lv_indev_set_type(simulatedInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(simulatedInput, &simulatedPointer);
  lv_indev_set_read_cb(simulatedInput, readSimulatedPointer);
  lv_indev_read(simulatedInput);
  ui.refresh(lv_screen_active(), state);
  simulatedPointer.point.x = (sliderArea.x1 + sliderArea.x2) / 2;
  lv_indev_read(simulatedInput);
  lv_obj_update_layout(depthSlider);
  const float draggedDepth = state.bank.presets[state.activePreset].blocks[state.selectedBlock]
    .params.value("depth", 0.0f);
  if (require(draggedDepth > depth->minimum && draggedDepth < depth->maximum,
              "a horizontal slider drag should set an intermediate parameter value")) return 1;
  const auto updatedControls = ardor::parameterPage(state, 0);
  const auto updatedDepth = std::find_if(updatedControls.begin(), updatedControls.end(),
                                         [](const auto& control) { return control.key == "depth"; });
  if (require(lv_obj_get_width(depthFill) > 0,
              "slider drag should update the active fill before release")) return 1;
  if (require(updatedDepth != updatedControls.end()
                && findLabel(depthSlider, numericPrefix(updatedDepth->formatted).c_str()),
              "slider drag should update the value label before release")) return 1;
  if (require(updatedDepth != updatedControls.end()
                && findLabel(mappingToolbar, ("Selected  /  " + updatedDepth->label).c_str())
                && findLabel(mappingToolbar, updatedDepth->formatted.c_str()),
              "contextual mapping toolbar should follow the touched parameter and live value")) return 1;
  simulatedPointer.state = LV_INDEV_STATE_RELEASED;
  lv_indev_read(simulatedInput);
  ui.refresh(lv_screen_active(), state);
  lv_indev_delete(simulatedInput);

  lv_obj_send_event(lv_obj_get_parent(next), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* pageTwo = findLabel(lv_screen_active(), "PAGE 2 / 2");
  lv_obj_t* pageTwoLayer = pageTwo ? lv_obj_get_parent(lv_obj_get_parent(pageTwo)) : nullptr;
  if (require(ui.parameterPage() == 1 && pageTwoLayer
                && !lv_obj_has_flag(pageTwoLayer, LV_OBJ_FLAG_HIDDEN),
              "page navigation should remain usable after dragging a parameter slider")) return 1;
  lv_obj_t* pageTwoPrevious = pageTwo ? findLabel(lv_obj_get_parent(pageTwo), "<") : nullptr;
  if (require(pageTwoPrevious, "the second parameter page should provide a back action")) return 1;
  lv_obj_send_event(lv_obj_get_parent(pageTwoPrevious), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* returnedPageOne = findLabel(lv_screen_active(), "PAGE 1 / 2");
  lv_obj_t* returnedPageOneLayer = returnedPageOne
    ? lv_obj_get_parent(lv_obj_get_parent(returnedPageOne)) : nullptr;
  if (require(ui.parameterPage() == 0 && returnedPageOneLayer
                && !lv_obj_has_flag(returnedPageOneLayer, LV_OBJ_FLAG_HIDDEN),
              "the first parameter page should remain reachable after a slider drag")) return 1;

  ardor::setSelectedBlockParam(state, "depth", depth->minimum);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  previous = findLabel(lv_screen_active(), "<");
  page = findLabel(lv_screen_active(), "PAGE 1 / 2");
  next = findLabel(lv_screen_active(), ">");
  title = findLabel(lv_screen_active(), titleText.c_str());
  depthLabel = findLabel(lv_screen_active(), upper(depth->label).c_str());
  depthSlider = depthLabel ? lv_obj_get_parent(depthLabel) : nullptr;
  depthFill = depthSlider ? findObjectWithHeight(depthSlider, 16) : nullptr;

  lv_obj_t* chain = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x212528), 1240, 492);
  if (require(chain, "signal chain should use the Panel plate ground")) return 1;
  std::string firstCategory = state.bank.presets[state.activePreset].blocks.front().label;
  std::transform(firstCategory.begin(), firstCategory.end(), firstCategory.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  lv_obj_t* firstCategoryLabel = findLabel(chain, firstCategory.c_str());
  lv_obj_t* firstChainBlock = firstCategoryLabel
    ? lv_obj_get_parent(lv_obj_get_parent(firstCategoryLabel)) : nullptr;
  if (require(firstChainBlock && lv_obj_get_width(firstChainBlock) == 168
                && lv_obj_get_height(firstChainBlock) == 326,
              "the chain should use tall engraved module cards, per the mockup")) return 1;
  int retainedChainCardMarker = 0;
  lv_obj_set_user_data(firstChainBlock, &retainedChainCardMarker);
  ui.selectBlock(state, state.selectedBlock);
  ui.refresh(lv_screen_active(), state);
  firstCategoryLabel = findLabel(chain, firstCategory.c_str());
  firstChainBlock = firstCategoryLabel
    ? lv_obj_get_parent(lv_obj_get_parent(firstCategoryLabel)) : nullptr;
  if (require(firstChainBlock
                && lv_obj_get_user_data(firstChainBlock) == &retainedChainCardMarker,
              "selection-only chain updates should retain existing card objects")) return 1;
  if (require(lv_obj_has_flag(chain, LV_OBJ_FLAG_SCROLLABLE),
              "the signal canvas should scroll independently of dedicated drag handles")) return 1;
  lv_obj_t* dragHandleLabel = findLabel(chain, "DRAG");
  if (require(dragHandleLabel
                && lv_obj_get_width(lv_obj_get_parent(dragHandleLabel)) == 168
                && lv_obj_get_height(lv_obj_get_parent(dragHandleLabel)) == 64,
              "chain blocks should use the full title bar as a touch drag target")) return 1;
  lv_obj_t* dragHandle = lv_obj_get_parent(dragHandleLabel);
  lv_obj_send_event(dragHandle, LV_EVENT_PRESSED, nullptr);
  if (require(!lv_obj_has_flag(chain, LV_OBJ_FLAG_SCROLLABLE)
                && lv_obj_get_scrollbar_mode(chain) == LV_SCROLLBAR_MODE_OFF,
              "pressing an effect drag handle should lock the competing chain scrollbar")) return 1;
  lv_obj_send_event(dragHandle, LV_EVENT_RELEASED, nullptr);
  if (require(lv_obj_has_flag(chain, LV_OBJ_FLAG_SCROLLABLE)
                && lv_obj_get_scrollbar_mode(chain) == LV_SCROLLBAR_MODE_AUTO,
              "releasing an effect drag handle should restore ordinary chain scrolling")) return 1;
  if (require(findLabel(lv_screen_active(), "BYPASSED"),
              "disabled blocks should show an explicit bypass state")) return 1;
  lv_obj_t* firstCardAssetLabel = findLabel(firstChainBlock,
      upper(state.bank.presets[state.activePreset].blocks.front().assetName).c_str());
  lv_obj_t* firstBypassedLabel = findLabel(firstChainBlock, "BYPASSED");
  lv_obj_t* firstDragHandle = findLabel(firstChainBlock, "DRAG");
  if (require(firstCategoryLabel && firstCardAssetLabel && firstBypassedLabel && firstDragHandle,
              "disabled chain card should render all text rows and its drag handle")) return 1;
  lv_area_t firstCategoryArea{};
  lv_area_t firstAssetArea{};
  lv_area_t firstBypassedArea{};
  lv_area_t firstDragHandleArea{};
  lv_obj_get_coords(firstCategoryLabel, &firstCategoryArea);
  lv_obj_get_coords(firstCardAssetLabel, &firstAssetArea);
  lv_obj_get_coords(firstBypassedLabel, &firstBypassedArea);
  lv_obj_get_coords(firstDragHandle, &firstDragHandleArea);
  if (require(firstCategoryArea.y2 < firstAssetArea.y1
                && firstAssetArea.y2 < firstBypassedArea.y1,
              "chain-card category, asset, and bypass labels should occupy separate rows")) return 1;
  // The large drag surface stacks its category and action into separate rows;
  // the asset name and bypass status continue in the card body below it.
  if (require(firstCategoryArea.y2 < firstDragHandleArea.y1,
              "the chain-card header should separate its category and drag instruction")) return 1;
  // The instructional hint text was retired: the bottom rail's Input/Output
  // jump controls now sit in a tighter band, and the circular patch points
  // plus drag handles are self-evident per the redesign's lettering-first,
  // no-borrowed-icon philosophy (docs/lvgl-ui-redesign-spec.md §8.11).
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 1280, 48),
              "runtime and action feedback should use a dedicated status bar")) return 1;
  if (require(findLabel(lv_screen_active(), "MODULATION"),
              "chain card should render an uppercase category")) return 1;
  if (require(findLabel(lv_screen_active(), "VINTAGE TREM"),
              "chain card should render its asset name")) return 1;
  if (require(!findLabel(lv_screen_active(), "mod"),
              "chain card should not render its short internal type")) return 1;
  const auto retainedFirstId = state.bank.presets[state.activePreset].blocks.front().id;
  const auto retainedFirstAsset = state.bank.presets[state.activePreset].blocks.front().assetName;
  const auto selectedBeforeReorder = state.selectedBlock;
  ardor::moveBlock(state, 0, 1);
  completePreview(state);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), upper(retainedFirstAsset).c_str()),
              "horizontal chain reordering should preserve the moved block content")) return 1;
  const auto retainedPosition = std::find_if(
    state.bank.presets[state.activePreset].blocks.begin(),
    state.bank.presets[state.activePreset].blocks.end(),
    [&](const auto& block) { return block.id == retainedFirstId; });
  ardor::moveBlock(state,
    static_cast<std::size_t>(std::distance(state.bank.presets[state.activePreset].blocks.begin(), retainedPosition)), 0);
  completePreview(state);
  ui.selectBlock(state, selectedBeforeReorder);
  ui.refresh(lv_screen_active(), state);
  title = findLabel(lv_screen_active(), titleText.c_str());
  page = findLabel(lv_screen_active(), "PAGE 1 / 2");
  depthLabel = findLabel(lv_screen_active(), upper(depth->label).c_str());
  depthSlider = depthLabel ? lv_obj_get_parent(depthLabel) : nullptr;
  depthFill = depthSlider ? findObjectWithHeight(depthSlider, 16) : nullptr;

  lv_obj_t* parameterPanel = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 1240, 452);
  lv_obj_t* parameterClose = findLabel(lv_screen_active(), "Close");
  lv_obj_t* deleteBlock = findLabel(lv_screen_active(), "Delete Block");
  lv_obj_t* bypassLabel = parameterPanel ? findLabel(parameterPanel, "Bypass") : nullptr;
  lv_obj_t* bypassControl = bypassLabel ? lv_obj_get_parent(bypassLabel) : nullptr;
  lv_obj_t* bypassValue = bypassControl ? findLabel(bypassControl, "Off") : nullptr;
  lv_obj_t* bypassFill = bypassControl
    ? findObjectWithBgColor(bypassControl, lv_color_hex(0xd8422f)) : nullptr;
  if (require(parameterPanel && parameterClose && deleteBlock && bypassLabel && bypassControl
                && bypassValue && bypassFill,
              "parameter panel header controls should render")) return 1;
  lv_area_t parameterPanelArea{};
  lv_area_t parameterCloseArea{};
  lv_area_t deleteBlockArea{};
  lv_area_t bypassControlArea{};
  lv_area_t titleArea{};
  lv_obj_get_coords(parameterPanel, &parameterPanelArea);
  lv_obj_get_coords(lv_obj_get_parent(parameterClose), &parameterCloseArea);
  lv_obj_get_coords(lv_obj_get_parent(deleteBlock), &deleteBlockArea);
  lv_obj_get_coords(bypassControl, &bypassControlArea);
  lv_obj_get_coords(title, &titleArea);
  if (require(parameterCloseArea.x2 > parameterPanelArea.x1 + (lv_obj_get_width(parameterPanel) * 9) / 10,
              "parameter close button should stay in the top-right corner")) return 1;
  if (require(lv_obj_get_width(lv_obj_get_parent(parameterClose)) == 88
                && lv_obj_get_height(lv_obj_get_parent(parameterClose)) == 52,
              "parameter close button should be a large dedicated target")) return 1;
  if (require(bypassControlArea.x2 + 32 < parameterCloseArea.x1,
              "bypass control should leave a generous gap before the close button")) return 1;
  if (require(titleArea.x2 + 20 < deleteBlockArea.x1
                && deleteBlockArea.x2 + 20 < bypassControlArea.x1,
              "parameter title, delete, and bypass control should have fixed gaps")) return 1;
  if (require(!lv_obj_has_state(bypassControl, LV_STATE_CHECKED),
              "enabled block should show Bypass as Off")) return 1;
  if (require(lv_obj_get_width(bypassControl) == 160 && lv_obj_get_height(bypassControl) == 52,
              "bypass control should provide a large rectangular touch target")) return 1;
  if (require(lv_obj_get_style_radius(bypassControl, LV_PART_MAIN) == 0,
              "bypass control should use a square Panel plate")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(bypassControl, LV_PART_MAIN),
                          lv_color_hex(0x2a2f33))
                && lv_obj_get_width(bypassFill) == 0,
              "Bypass Off should use the dark inactive surface with no active fill")) return 1;
  if (require(!findObjectOfClass(bypassControl, &lv_switch_class),
              "bypass control should not retain a native switch or circular thumb")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(parameterClose), LV_PART_MAIN),
                          lv_color_hex(0x2a2f33)),
              "parameter close button should use the raised Panel plate")) return 1;

  const auto bypassedBlock = state.selectedBlock;
  lv_obj_send_event(bypassControl, LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(!state.bank.presets[state.activePreset].blocks[bypassedBlock].enabled
                && liveBypassUpdates == 1
                && ardor::previewIsSynchronized(state)
                && lv_obj_has_state(bypassControl, LV_STATE_CHECKED)
                && findLabel(bypassControl, "On")
                && lv_obj_get_width(bypassFill) == 160,
              "tapping Bypass should disable the block and fill the control green")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_send_event(bypassControl, LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.bank.presets[state.activePreset].blocks[bypassedBlock].enabled
                && liveBypassUpdates == 2
                && ardor::previewIsSynchronized(state)
                && !lv_obj_has_state(bypassControl, LV_STATE_CHECKED)
                && findLabel(bypassControl, "Off")
                && lv_obj_get_width(bypassFill) == 0,
              "tapping Bypass again should restore the inactive Off state")) return 1;
  lv_area_t pageArea{};
  lv_obj_get_coords(page, &pageArea);
  if (require(pageArea.x2 < titleArea.x1,
              "parameter page status and title should not overlap")) return 1;
  if (require(depthFill && lv_obj_get_width(depthFill) == 0,
              "minimum slider value should start with no active fill")) return 1;

  ui.focusParameter(depth->key);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* focusedLabel = findLabel(lv_screen_active(), upper(depth->label).c_str());
  lv_obj_t* focusedSlider = focusedLabel ? lv_obj_get_parent(focusedLabel) : nullptr;
  if (require(focusedSlider && lv_obj_get_style_outline_width(focusedSlider, LV_PART_MAIN) == 1
                && lv_color_eq(lv_obj_get_style_outline_color(focusedSlider, LV_PART_MAIN), lv_color_hex(0xd8422f)),
              "focused slider should use the LIVE outline")) return 1;

  lv_obj_t* focusedFill = findObjectWithBgColor(focusedSlider, lv_color_hex(0xd8422f));
  const int minimumFillWidth = focusedFill ? lv_obj_get_width(focusedFill) : -1;
  if (require(ui.applyFocusedParameterDelta(state, 1), "focused encoder adjustment should be consumed")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  focusedLabel = findLabel(lv_screen_active(), upper(depth->label).c_str());
  focusedSlider = focusedLabel ? lv_obj_get_parent(focusedLabel) : nullptr;
  focusedFill = focusedSlider ? findObjectWithBgColor(focusedSlider, lv_color_hex(0xd8422f)) : nullptr;
  if (require(focusedFill && lv_obj_get_width(focusedFill) > minimumFillWidth,
              "focused encoder adjustment should increase the slider fill")) return 1;

  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* stableDepthLabel = findLabel(lv_screen_active(), upper(depth->label).c_str());
  lv_obj_t* stableDepthSlider = stableDepthLabel ? lv_obj_get_parent(stableDepthLabel) : nullptr;
  lv_obj_t* stableFill = stableDepthSlider
    ? findObjectWithBgColor(stableDepthSlider, lv_color_hex(0xd8422f)) : nullptr;
  if (require(stableDepthSlider && stableFill, "focused slider should expose stable visual handles")) return 1;
  const int stableFillWidth = lv_obj_get_width(stableFill);
  ui.setFocusedWidgets(stableDepthSlider);
  ui.focusParameter(depth->key);
  if (require(ui.applyFocusedParameterDelta(state, 5), "targeted encoder adjustment should be consumed")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(stableDepthSlider);
  lv_obj_t* retainedDepthLabel = findLabel(lv_screen_active(), upper(depth->label).c_str());
  if (require(retainedDepthLabel && lv_obj_get_parent(retainedDepthLabel) == stableDepthSlider,
              "targeted encoder adjustment should retain the slider object")) return 1;
  if (require(lv_obj_get_width(stableFill) > stableFillWidth,
              "targeted encoder adjustment should update the retained fill")) return 1;
  ui.focusParameter("");
  ardor::setSelectedBlockParam(state, "depth", depth->minimum);
  ui.refresh(lv_screen_active(), state);
  retainedDepthLabel = findLabel(lv_screen_active(), upper(depth->label).c_str());
  if (require(retainedDepthLabel && lv_obj_get_parent(retainedDepthLabel) == stableDepthSlider,
              "model-driven parameter updates should retain the slider object")) return 1;
  const auto retainedBlockIndex = state.selectedBlock;
  ui.selectBlock(state, 1);
  ui.refresh(lv_screen_active(), state);
  ui.selectBlock(state, retainedBlockIndex);
  ui.refresh(lv_screen_active(), state);
  retainedDepthLabel = findLabel(lv_screen_active(), upper(depth->label).c_str());
  if (require(retainedDepthLabel && lv_obj_get_parent(retainedDepthLabel) == stableDepthSlider,
              "switching parameter targets should reactivate the cached retained panel")) return 1;

  ardor::setSelectedBlockParam(state, "depth", depth->maximum);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  depthLabel = findLabel(lv_screen_active(), upper(depth->label).c_str());
  depthSlider = depthLabel ? lv_obj_get_parent(depthLabel) : nullptr;
  depthFill = depthSlider ? findObjectWithHeight(depthSlider, 16) : nullptr;
  lv_obj_t* depthRail = depthSlider ? findObjectWithHeight(depthSlider, 18) : nullptr;
  if (require(depthFill && depthRail
                && lv_obj_get_width(depthFill) == lv_obj_get_width(depthRail) - 2,
              "maximum slider value should fill the rail interior")) return 1;

  ui.selectGlobalParams(state);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "PAGE 1 / 1"),
              "single-page global controls should show page status")) return 1;

  ui.selectBlock(state, 1);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "PAGE 1 / 1"),
              "single-page cab controls should show page status")) return 1;

  ardor::enterPresetMode(state);
  ardor::updateRealtimeTelemetry(
    state, ardor::makeRuntimeTelemetry(100, 0, 0, 3.0, 2.0, 10.0, false,
                                      0, 0, 0, 2.5));
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(!findLabel(lv_screen_active(), state.bank.name.c_str()),
              "the minimal top rail should omit the bank title")) return 1;
  lv_obj_t* presetName = findLabel(lv_screen_active(), upper(state.bank.presets[state.activePreset].name).c_str());
  if (require(presetName && lv_obj_get_style_text_font(presetName, LV_PART_MAIN) == &ardor_font_saira_cond_semibold_72,
              "preset-card names should render in the distance-readable 72 px Panel face")) return 1;
  if (require(lv_obj_get_height(presetName) == 160 &&
              lv_label_get_long_mode(presetName) == LV_LABEL_LONG_MODE_DOTS,
              "preset-card names should reserve a bounded two-line title area")) return 1;
  const std::size_t activeSlot = state.activePreset;
  const std::size_t inactiveSlot = (activeSlot + 1) % state.bank.presets.size();
  const std::string activeFsText = "FS " + std::to_string(activeSlot + 1) + "  \xC2\xB7  LIVE";
  const std::string inactiveFsText = "FS " + std::to_string(inactiveSlot + 1);
  lv_obj_t* activeFsLabel = findLabel(lv_screen_active(), activeFsText.c_str());
  lv_obj_t* inactiveFsLabel = findLabel(lv_screen_active(), inactiveFsText.c_str());
  if (require(activeFsLabel && inactiveFsLabel,
              "preset tiles should identify the physical footswitch and explicit live state")) return 1;
  lv_obj_t* activeHeader = activeFsLabel ? lv_obj_get_parent(activeFsLabel) : nullptr;
  if (require(activeHeader && lv_obj_get_height(activeHeader) == 44
                && lv_color_eq(lv_obj_get_style_bg_color(activeHeader, LV_PART_MAIN),
                               lv_color_hex(0xd8422f)),
              "active preset should use a distance-visible full-width LIVE header")) return 1;
  lv_obj_t* retainedPresetCard = lv_obj_get_parent(presetName);
  lv_obj_t* inactivePresetName = findLabel(
    lv_screen_active(), upper(state.bank.presets[inactiveSlot].name).c_str());
  lv_obj_t* inactivePresetCard = inactivePresetName ? lv_obj_get_parent(inactivePresetName) : nullptr;
  if (require(lv_obj_get_style_border_width(retainedPresetCard, LV_PART_MAIN) == 3
                && inactivePresetCard
                && lv_obj_get_style_border_width(inactivePresetCard, LV_PART_MAIN) == 1,
              "live preset should have a stronger perimeter than inactive presets")) return 1;
  ardor::synchronizePresetSelection(state, inactiveSlot);
  ui.refresh(lv_screen_active(), state);
  const std::string movedLiveText = "FS " + std::to_string(inactiveSlot + 1) + "  \xC2\xB7  LIVE";
  const std::string previousFsText = "FS " + std::to_string(activeSlot + 1);
  if (require(findLabel(lv_screen_active(), movedLiveText.c_str())
                && findLabel(lv_screen_active(), previousFsText.c_str())
                && lv_obj_get_style_border_width(inactivePresetCard, LV_PART_MAIN) == 3
                && lv_obj_get_style_border_width(retainedPresetCard, LV_PART_MAIN) == 1,
              "retained preset cards should move the complete LIVE treatment together")) return 1;
  ardor::synchronizePresetSelection(state, activeSlot);
  ui.refresh(lv_screen_active(), state);
  const auto installedAssetPath = state.bank.presets[state.activePreset].blocks[0].assetPath;
  const auto installedBlockType = state.bank.presets[state.activePreset].blocks[0].type;
  const bool installedBlockEnabled = state.bank.presets[state.activePreset].blocks[0].enabled;
  state.bank.presets[state.activePreset].blocks[0].type = "nam";
  state.bank.presets[state.activePreset].blocks[0].enabled = true;
  state.bank.presets[state.activePreset].blocks[0].assetPath = "models/not-installed.nam";
  if (require(ardor::presetHasUnavailableAssets(state, state.activePreset),
              "test preset should be unavailable after removing its model")) return 1;
  ardor::markUiChanged(state, ardor::UiChange::Presets);
  ui.refresh(lv_screen_active(), state);
  lv_obj_t* unavailableLabel = findLabel(retainedPresetCard, "ASSET NOT FOUND");
  if (require(unavailableLabel && !lv_obj_has_flag(unavailableLabel, LV_OBJ_FLAG_HIDDEN),
              "preset cards should identify presets with unavailable assets")) return 1;
  state.bank.presets[state.activePreset].blocks[0].assetPath = installedAssetPath;
  state.bank.presets[state.activePreset].blocks[0].type = installedBlockType;
  state.bank.presets[state.activePreset].blocks[0].enabled = installedBlockEnabled;
  ardor::markUiChanged(state, ardor::UiChange::Presets);
  ui.refresh(lv_screen_active(), state);
  if (require(lv_obj_has_flag(unavailableLabel, LV_OBJ_FLAG_HIDDEN),
              "the unavailable indicator should clear after the asset is repaired")) return 1;
  const auto originalPresetName = state.bank.presets[state.activePreset].name;
  state.bank.presets[state.activePreset].name = "Retained preset";
  ardor::markUiChanged(state, ardor::UiChange::Presets);
  ui.refresh(lv_screen_active(), state);
  lv_obj_t* renamedPresetLabel = findLabel(lv_screen_active(), "RETAINED PRESET");
  if (require(renamedPresetLabel && lv_obj_get_parent(renamedPresetLabel) == retainedPresetCard,
              "preset metadata changes should update the retained card")) return 1;
  state.bank.presets[state.activePreset].name = originalPresetName;
  ardor::markUiChanged(state, ardor::UiChange::Presets);
  ui.refresh(lv_screen_active(), state);

  ardor::enterEditMode(state);
  auto& chainBlocks = state.bank.presets[state.activePreset].blocks;
  while (chainBlocks.size() < 6) {
    chainBlocks.push_back(chainBlocks.front());
  }
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* renameLabel = findLabel(lv_screen_active(), "RENAME");
  if (require(renameLabel && lv_obj_get_width(lv_obj_get_parent(renameLabel)) == 96,
              "the edit rail should expose a clear preset rename target")) return 1;
  const std::string nameBeforeRename = state.bank.presets[state.activePreset].name;
  lv_obj_send_event(lv_obj_get_parent(renameLabel), LV_EVENT_CLICKED, nullptr);
  lv_obj_t* renameTitle = findLabel(lv_screen_active(), "Rename preset");
  lv_obj_t* renameSheet = renameTitle ? lv_obj_get_parent(renameTitle) : nullptr;
  lv_obj_t* renameOverlay = renameSheet ? lv_obj_get_parent(renameSheet) : nullptr;
  lv_obj_t* renameField = renameSheet
    ? findObjectOfClass(renameSheet, &lv_textarea_class) : nullptr;
  lv_obj_t* renameKeyboard = renameSheet
    ? findObjectOfClass(renameSheet, &lv_keyboard_class) : nullptr;
  if (require(renameOverlay && renameField && renameKeyboard
                && !lv_obj_has_flag(renameOverlay, LV_OBJ_FLAG_HIDDEN),
              "preset rename should open a blocking sheet with a text field and keyboard")) return 1;
  lv_textarea_set_text(renameField, "   ");
  lv_obj_send_event(lv_obj_get_parent(findLabel(renameSheet, "SAVE")), LV_EVENT_CLICKED, nullptr);
  if (require(findLabel(renameSheet, "Enter a preset name")
                && state.bank.presets[state.activePreset].name == nameBeforeRename
                && !lv_obj_has_flag(renameOverlay, LV_OBJ_FLAG_HIDDEN),
              "preset rename should reject an empty name without closing")) return 1;
  lv_textarea_set_text(renameField, "Discarded name");
  lv_obj_send_event(lv_obj_get_parent(findLabel(renameSheet, "CANCEL")), LV_EVENT_CLICKED, nullptr);
  if (require(state.bank.presets[state.activePreset].name == nameBeforeRename
                && lv_obj_has_flag(renameOverlay, LV_OBJ_FLAG_HIDDEN),
              "cancelling preset rename should preserve the original name")) return 1;
  lv_obj_send_event(lv_obj_get_parent(renameLabel), LV_EVENT_CLICKED, nullptr);
  lv_textarea_set_text(renameField, "  Stage Lead  ");
  lv_obj_send_event(lv_obj_get_parent(findLabel(renameSheet, "SAVE")), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(savedPresetNames == 1
                && state.bank.presets[state.activePreset].name == "Stage Lead"
                && findLabel(lv_screen_active(), "Stage Lead")
                && lv_obj_has_flag(renameOverlay, LV_OBJ_FLAG_HIDDEN),
              "saving preset rename should trim, persist, and refresh the active preset name")) return 1;
  chain = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x212528), 1240, 492);
  if (require(chain && lv_obj_has_flag(chain, LV_OBJ_FLAG_SCROLLABLE)
                && lv_obj_get_scroll_right(chain) > 0,
              "long chains should remain on one horizontally scrollable rail")) return 1;
  if (require(findLabel(lv_screen_active(), "<  INPUT")
                && findLabel(lv_screen_active(), "OUTPUT  >"),
              "the fixed chain footer should expose Input and Output jump controls")) return 1;
  ui.scrollChainToEnd(state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.chainScrollOffsets[state.activePreset] > 0,
              "jumping to Output should remember a positive per-preset scroll offset")) return 1;
  ui.scrollChainToStart(state);
  if (require(state.chainScrollOffsets[state.activePreset] == 0,
              "jumping to Input should reset the current preset scroll offset")) return 1;
  lv_obj_scroll_to_x(chain, 0, LV_ANIM_OFF);
  ui.setChainDragActive(true);
  ui.autoScrollChainForDrag(state, {1259, 324});
  if (require(!lv_obj_has_flag(chain, LV_OBJ_FLAG_SCROLLABLE)
                && state.chainScrollOffsets[state.activePreset] > 0,
              "drag locking should preserve deliberate edge auto-scroll")) return 1;
  ui.setChainDragActive(false);
  ui.scrollChainToStart(state);

  constexpr std::size_t blockCount = 10;
  if (require(ardor::LvglUi::chainSlotForPoint(blockCount, {283, 324}) == 0,
              "the first horizontal tile should map to the first block")) return 1;
  if (require(ardor::LvglUi::chainSlotForPoint(blockCount, {1633, 324}) == 5,
              "the sixth horizontal tile should map to the sixth block")) return 1;
  if (require(ardor::LvglUi::chainInsertionSlotForPoint(blockCount, {1538, 324}) == 5,
              "horizontal insertion should use the nearest signal boundary")) return 1;
  const auto sixthIndicator = ardor::LvglUi::chainIndicatorPosition(blockCount, 5);
  if (require(sixthIndicator.x == 1428 && sixthIndicator.y == 161,
              "sixth-slot insertion indicator should stay on the single horizontal rail")) return 1;
  const auto forwardIndicator = ardor::LvglUi::chainReorderIndicatorPosition(blockCount, 0, 1);
  if (require(forwardIndicator.x == 684 && forwardIndicator.y == 161,
              "forward reorder indicator should appear after the hovered block")) return 1;
  const auto backwardIndicator = ardor::LvglUi::chainReorderIndicatorPosition(blockCount, 4, 1);
  if (require(backwardIndicator.x == 436 && backwardIndicator.y == 161,
              "backward reorder indicator should appear before the hovered block")) return 1;

  // Preset now carries its own top legend rail and bottom control rail, per
  // docs/lvgl-ui-redesign-spec.md §4f — replacing the old free-floating
  // header buttons and the shared status-bar footer for this screen.
  ardor::enterPresetMode(state);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* telemetryLegend = findLabel(
    lv_screen_active(), "LATENCY 1.33 MS  \xC2\xB7  BUFFER 25% USED");
  lv_obj_t* presetTopRail = telemetryLegend ? lv_obj_get_parent(telemetryLegend) : nullptr;
  lv_obj_t* editButtonLabel = findLabel(lv_screen_active(), "Edit");
  lv_obj_t* editButton = editButtonLabel ? lv_obj_get_parent(editButtonLabel) : nullptr;
  lv_obj_t* setupButtonLabel = findLabel(lv_screen_active(), "Setup");
  lv_obj_t* setupButton = setupButtonLabel ? lv_obj_get_parent(setupButtonLabel) : nullptr;
  lv_obj_t* tunerButtonLabel = findLabel(lv_screen_active(), "Tuner");
  lv_obj_t* tunerButton = tunerButtonLabel ? lv_obj_get_parent(tunerButtonLabel) : nullptr;
  lv_obj_t* bankDownLabel = findLabel(lv_screen_active(), "Bank -");
  lv_obj_t* bankUpLabel = findLabel(lv_screen_active(), "Bank +");
  lv_obj_t* masterLegend = findLabel(lv_screen_active(), "MASTER");
  lv_obj_t* masterValue = findLabel(lv_screen_active(), std::to_string(state.masterVolume).c_str());
  lv_obj_t* bankDownButton = bankDownLabel ? lv_obj_get_parent(bankDownLabel) : nullptr;
  lv_obj_t* bankUpButton = bankUpLabel ? lv_obj_get_parent(bankUpLabel) : nullptr;
  if (require(presetTopRail
                && !findLabel(presetTopRail, "ARDOR")
                && !findLabel(presetTopRail, "MIDI")
                && !findLabel(presetTopRail, "48 KHZ"),
              "preset top rail should contain only latency and live buffer use")) return 1;
  ardor::updateRealtimeTelemetry(
    state, ardor::makeRuntimeTelemetry(120, 0, 0, 7.0, 3.0, 10.0, false,
                                      0, 0, 0, 6.0));
  ui.refresh(lv_screen_active(), state);
  if (require(findLabel(lv_screen_active(),
                        "LATENCY 1.33 MS  \xC2\xB7  BUFFER 60% USED") == telemetryLegend,
              "the retained top-rail buffer percentage should follow one-second telemetry updates")) return 1;
  if (require(editButton && lv_obj_get_width(editButton) == 112 && lv_obj_get_height(editButton) == 52,
              "Edit should have a large, finger-friendly hit target")) return 1;
  if (require(setupButton && lv_obj_get_width(setupButton) == 96 && lv_obj_get_height(setupButton) == 52,
              "bottom rail should expose a Setup control")) return 1;
  if (require(bankDownButton && bankUpButton && lv_obj_get_width(bankUpButton) == 96
                && lv_obj_get_height(bankUpButton) == 52
                && lv_obj_get_height(bankDownButton) == 52,
              "preset screen should render dedicated bank up and down buttons")) return 1;
  if (require(masterLegend && masterValue && tunerButton
                && lv_obj_get_width(tunerButton) == 112
                && lv_obj_get_height(tunerButton) == 52,
              "preset screen should provide a Tuner button and a master travel scale")) return 1;
  lv_obj_t* masterScaleGroup = lv_obj_get_parent(masterLegend);
  lv_obj_t* masterRail = findObjectWithSizeAndBgColor(
    masterScaleGroup, lv_color_hex(0x191c1f), 250, 18);
  lv_obj_t* masterHandle = findObjectWithSizeAndBgColor(
    masterScaleGroup, lv_color_hex(0xd8422f), 44, 54);
  if (require(masterRail && masterHandle,
              "master should use the same recessed rail and wide thumb as parameter controls")) return 1;
  // Live control telemetry must not add extra content to this intentionally
  // minimal top rail.
  ardor::updateControlInputTelemetry(state, {true, true, true, true, 0.5f, true, 12000});
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(!findLabel(presetTopRail, "MIDI ON"),
              "live control status should not add extra top-rail content")) return 1;
  lv_area_t bankDownArea{};
  lv_area_t bankUpArea{};
  lv_area_t tunerButtonArea{};
  lv_area_t setupButtonArea{};
  lv_area_t editButtonArea{};
  lv_obj_get_coords(bankDownButton, &bankDownArea);
  lv_obj_get_coords(bankUpButton, &bankUpArea);
  lv_obj_get_coords(tunerButton, &tunerButtonArea);
  lv_obj_get_coords(setupButton, &setupButtonArea);
  lv_obj_get_coords(editButton, &editButtonArea);
  if (require(editButtonArea.x1 < tunerButtonArea.x1 && tunerButtonArea.x1 < bankDownArea.x1
                && bankDownArea.x1 < bankUpArea.x1 && bankUpArea.x1 < setupButtonArea.x1,
              "bottom rail should order Edit, Tuner, Bank-, Bank+, Setup left to right")) return 1;
  if (require(editButtonArea.y1 == tunerButtonArea.y1
                && tunerButtonArea.y1 == bankDownArea.y1
                && bankDownArea.y1 == bankUpArea.y1
                && bankUpArea.y1 == setupButtonArea.y1,
              "bottom-rail controls should share one row")) return 1;
  if (require(masterValue && masterLegend, "master travel scale should render a legend and value")) return 1;
  ardor::setMasterVolume(state, 50);
  ui.refresh(lv_screen_active(), state);
  if (require(findLabel(lv_screen_active(), "50"),
              "master volume should update the retained travel-scale value")) return 1;
  if (require(lv_obj_has_state(bankDownButton, LV_STATE_DISABLED),
              "bank down should be disabled at the first bank")) return 1;
  if (require(lv_obj_get_style_text_font(bankUpLabel, LV_PART_MAIN) == &ardor_font_saira_cond_semibold_22,
              "buttons should use the larger, more legible font")) return 1;

  lv_obj_send_event(setupButton, LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "Settings")
                && findLabel(lv_screen_active(), "Appearance")
                && findLabel(lv_screen_active(), "Panel palette"),
              "settings gear should open the touchscreen appearance screen")) return 1;
  lv_obj_t* inkPalette = findLabel(lv_screen_active(), "Ink");
  if (require(inkPalette, "Appearance should offer the Ink Panel palette")) return 1;
  lv_obj_send_event(lv_obj_get_parent(inkPalette), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.settings.paletteId == ardor::PaletteId::Ink
                && ardor::lvgl_ui::palette().plate == 0x10161f,
              "selecting Ink should rebuild the UI with Ink tokens")) return 1;
  lv_obj_t* slatePalette = findLabel(lv_screen_active(), "Slate");
  if (require(slatePalette, "Appearance should keep Slate available after a palette rebuild")) return 1;
  lv_obj_send_event(lv_obj_get_parent(slatePalette), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.settings.paletteId == ardor::PaletteId::Slate
                && ardor::lvgl_ui::palette().plate == 0x212528,
              "returning to Slate should restore the default palette cleanly")) return 1;
  lv_obj_t* nordPalette = findLabel(lv_screen_active(), "Nord");
  if (require(nordPalette, "Appearance should offer the Nord palette")) return 1;
  lv_obj_send_event(lv_obj_get_parent(nordPalette), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.settings.paletteId == ardor::PaletteId::Nord
                && ardor::lvgl_ui::palette().plate == 0x2e3440,
              "selecting Nord should rebuild the UI with Nord tokens")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "Slate")), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.settings.paletteId == ardor::PaletteId::Slate,
              "returning to Slate after Nord should restore the default palette")) return 1;
  lv_obj_t* wifiSectionLabel = findLabel(lv_screen_active(), "Wi-Fi");
  if (require(wifiSectionLabel, "settings should expose a Wi-Fi section")) return 1;
  lv_obj_send_event(lv_obj_get_parent(wifiSectionLabel), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* wifiKeyboard = findObjectOfClass(lv_screen_active(), &lv_keyboard_class);
  if (require(findLabel(lv_screen_active(), "Network name")
                && findLabel(lv_screen_active(), "Password")
                && wifiKeyboard,
              "touchscreen Wi-Fi settings should render fields and an on-screen keyboard")) return 1;
  lv_area_t keyboardArea{};
  lv_area_t wifiContentArea{};
  lv_obj_get_coords(wifiKeyboard, &keyboardArea);
  lv_obj_get_coords(lv_obj_get_parent(wifiKeyboard), &wifiContentArea);
  if (require(lv_obj_get_width(wifiKeyboard) == 944
                && lv_obj_get_height(wifiKeyboard) == 360
                && keyboardArea.x1 == wifiContentArea.x1 + 29
                && keyboardArea.x2 == wifiContentArea.x2 - 27
                && keyboardArea.y1 == wifiContentArea.y1 + 225
                && keyboardArea.y2 < wifiContentArea.y2,
              "Wi-Fi keyboard should be a fully contained, proportionate bottom panel")) return 1;
  lv_obj_t* passwordEye = findLabel(lv_screen_active(), LV_SYMBOL_EYE_OPEN);
  lv_obj_t* passwordEyeButton = passwordEye ? lv_obj_get_parent(passwordEye) : nullptr;
  lv_obj_t* passwordLabel = findLabel(lv_screen_active(), "Password");
  lv_obj_t* passwordField = passwordLabel
    ? lv_obj_get_child(lv_obj_get_parent(passwordLabel), lv_obj_get_index(passwordLabel) + 1)
    : nullptr;
  lv_area_t passwordEyeArea{};
  lv_area_t passwordFieldArea{};
  if (passwordEyeButton) lv_obj_get_coords(passwordEyeButton, &passwordEyeArea);
  if (passwordField) lv_obj_get_coords(passwordField, &passwordFieldArea);
  if (require(passwordEyeButton && passwordField
                && lv_obj_check_type(passwordField, &lv_textarea_class)
                && lv_obj_get_height(passwordField) == 62
                && lv_obj_get_width(passwordEyeButton) == 54
                && lv_obj_get_height(passwordEyeButton) == 58
                && passwordEyeArea.x1 >= passwordFieldArea.x1
                && passwordEyeArea.x2 <= passwordFieldArea.x2
                && passwordEyeArea.y1 >= passwordFieldArea.y1
                && passwordEyeArea.y2 <= passwordFieldArea.y2,
              "password visibility should use a compact eye control inside the input")) return 1;
  lv_obj_t* countryLabel = findLabel(lv_screen_active(), "Country code");
  lv_obj_t* countryField = countryLabel
    ? lv_obj_get_child(lv_obj_get_parent(countryLabel), lv_obj_get_index(countryLabel) + 1)
    : nullptr;
  lv_obj_t* countryText = countryField && lv_obj_check_type(countryField, &lv_textarea_class)
    ? lv_textarea_get_label(countryField) : nullptr;
  lv_area_t countryFieldArea{};
  lv_area_t countryTextArea{};
  if (countryField) lv_obj_get_coords(countryField, &countryFieldArea);
  if (countryText) lv_obj_get_coords(countryText, &countryTextArea);
  // Tolerance is 4 px, not 2: Saira Condensed's vertical metrics differ
  // slightly from Open Sans at the same nominal pixel size.
  if (require(countryText
                && std::abs((countryFieldArea.y1 + countryFieldArea.y2)
                            - (countryTextArea.y1 + countryTextArea.y2)) <= 4,
              "single-line Wi-Fi input text should be vertically centered")) return 1;
  lv_obj_send_event(passwordEyeButton, LV_EVENT_CLICKED, nullptr);
  if (require(std::strcmp(lv_label_get_text(passwordEye), LV_SYMBOL_EYE_CLOSE) == 0
                && !lv_textarea_get_password_mode(passwordField),
              "password eye should clearly reflect and toggle the visible state")) return 1;
  lv_obj_send_event(passwordEyeButton, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* audioSectionLabel = findLabel(lv_screen_active(), "Audio");
  if (require(audioSectionLabel, "settings should expose a dedicated Audio section")) return 1;
  lv_obj_send_event(lv_obj_get_parent(audioSectionLabel), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "32 samples")
                && findLabel(lv_screen_active(), "64 samples")
                && findLabel(lv_screen_active(), "128 samples")
                && findLabel(lv_screen_active(), "Apply & restart audio"),
              "Audio settings should offer the supported buffers and an explicit restart action")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "32 samples")),
                    LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* audioChoice = lv_obj_get_parent(findLabel(lv_screen_active(), "32 samples"));
  lv_obj_t* audioExplanation = lv_obj_get_parent(findLabel(lv_screen_active(), "Latency and stability"));
  lv_obj_t* audioApply = lv_obj_get_parent(findLabel(lv_screen_active(), "Apply & restart audio"));
  lv_area_t audioChoiceArea{}, audioExplanationArea{}, audioApplyArea{}, audioContentArea{};
  lv_obj_get_coords(audioChoice, &audioChoiceArea);
  lv_obj_get_coords(audioExplanation, &audioExplanationArea);
  lv_obj_get_coords(audioApply, &audioApplyArea);
  lv_obj_get_coords(lv_obj_get_parent(audioChoice), &audioContentArea);
  if (require(audioChoiceArea.x1 >= audioContentArea.x1
                && audioChoiceArea.x2 <= audioContentArea.x2
                && audioChoiceArea.y2 < audioExplanationArea.y1
                && audioExplanationArea.y2 < audioApplyArea.y1
                && audioApplyArea.y2 <= audioContentArea.y2,
              "Audio settings should keep choices, guidance, and Apply action in a clear vertical flow")) return 1;
  lv_obj_send_event(audioApply, LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* audioMessage = findLabel(lv_screen_active(), "Buffer saved - restarting audio");
  lv_obj_t* disabledAudioApply = lv_obj_get_parent(findLabel(lv_screen_active(), "Apply & restart audio"));
  lv_area_t audioMessageArea{}, disabledAudioApplyArea{};
  if (audioMessage) lv_obj_get_coords(audioMessage, &audioMessageArea);
  if (disabledAudioApply) lv_obj_get_coords(disabledAudioApply, &disabledAudioApplyArea);
  if (require(savedAudioBlockSize == 32 && state.settings.audioBlockSize == 32
                && audioMessage && disabledAudioApply
                && lv_obj_has_state(disabledAudioApply, LV_STATE_DISABLED)
                && disabledAudioApplyArea.y2 < audioMessageArea.y1,
              "applying an audio buffer should persist it and announce the restart")) return 1;
  lv_obj_t* controlSectionLabel = findLabel(lv_screen_active(), "Control I/O");
  if (require(controlSectionLabel, "settings should expose a Control I/O section")) return 1;
  lv_obj_send_event(lv_obj_get_parent(controlSectionLabel), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "MIDI receive channel")
                && findLabel(lv_screen_active(), "Tuner on/off CC")
                && findLabel(lv_screen_active(), "Expression pedal")
                && findLabel(lv_screen_active(), "Capture heel:  0")
                && findLabel(lv_screen_active(), "Capture toe:  26400"),
              "Control I/O should integrate MIDI and expression calibration")) return 1;
  lv_obj_t* midiChannelTitle = findLabel(lv_screen_active(), "MIDI receive channel");
  lv_obj_t* midiChannelCard = midiChannelTitle ? lv_obj_get_parent(midiChannelTitle) : nullptr;
  lv_obj_t* midiChannelMinus = midiChannelCard ? lv_obj_get_child(midiChannelCard, 1) : nullptr;
  lv_area_t midiChannelTitleArea{};
  lv_area_t midiChannelMinusArea{};
  if (midiChannelTitle) lv_obj_get_coords(midiChannelTitle, &midiChannelTitleArea);
  if (midiChannelMinus) lv_obj_get_coords(midiChannelMinus, &midiChannelMinusArea);
  if (require(midiChannelTitle && midiChannelMinus
                && midiChannelTitleArea.y2 < midiChannelMinusArea.y1,
              "MIDI receive channel title should not be obscured by its controls")) return 1;
  const auto stepperIsAligned = [](lv_obj_t* title) {
    if (!title) return false;
    lv_obj_t* card = lv_obj_get_parent(title);
    if (!card || lv_obj_get_child_count(card) < 4) return false;
    lv_obj_t* minus = lv_obj_get_child(card, 1);
    lv_obj_t* value = lv_obj_get_child(card, 2);
    lv_obj_t* plus = lv_obj_get_child(card, 3);
    lv_obj_t* minusLabel = lv_obj_get_child(minus, 0);
    lv_obj_t* plusLabel = lv_obj_get_child(plus, 0);
    lv_area_t cardArea{}, titleArea{}, minusArea{}, valueArea{}, plusArea{};
    lv_area_t minusLabelArea{}, plusLabelArea{};
    lv_obj_get_coords(card, &cardArea);
    lv_obj_get_coords(title, &titleArea);
    lv_obj_get_coords(minus, &minusArea);
    lv_obj_get_coords(value, &valueArea);
    lv_obj_get_coords(plus, &plusArea);
    lv_obj_get_coords(minusLabel, &minusLabelArea);
    lv_obj_get_coords(plusLabel, &plusLabelArea);
    const auto centerX = [](const lv_area_t& area) { return area.x1 + area.x2; };
    const auto centerY = [](const lv_area_t& area) { return area.y1 + area.y2; };
    return std::abs((titleArea.x1 - cardArea.x1) - (cardArea.x2 - titleArea.x2)) <= 2
      && std::abs((minusArea.x1 - cardArea.x1) - (cardArea.x2 - plusArea.x2)) <= 2
      && std::abs(centerX(valueArea) - centerX(cardArea)) <= 2
      && centerY(minusArea) == centerY(valueArea)
      && centerY(valueArea) == centerY(plusArea)
      && std::abs(centerX(minusLabelArea) - centerX(minusArea)) <= 1
      && std::abs(centerY(minusLabelArea) - centerY(minusArea)) <= 1
      && std::abs(centerX(plusLabelArea) - centerX(plusArea)) <= 1
      && std::abs(centerY(plusLabelArea) - centerY(plusArea)) <= 1;
  };
  if (require(stepperIsAligned(midiChannelTitle)
                && stepperIsAligned(findLabel(lv_screen_active(), "Tuner on/off CC")),
              "MIDI and tuner steppers should share symmetric padding and centered controls")) return 1;
  ui.closeSettings(state);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(),
                        "LATENCY 0.67 MS  \xC2\xB7  BUFFER 60% USED"),
              "the top rail should reflect the active latency setting and buffer use")) return 1;
  bankUpLabel = findLabel(lv_screen_active(), "Bank +");
  bankUpButton = bankUpLabel ? lv_obj_get_parent(bankUpLabel) : nullptr;
  tunerButtonLabel = findLabel(lv_screen_active(), "Tuner");
  tunerButton = tunerButtonLabel ? lv_obj_get_parent(tunerButtonLabel) : nullptr;
  editButtonLabel = findLabel(lv_screen_active(), "Edit");
  editButton = editButtonLabel ? lv_obj_get_parent(editButtonLabel) : nullptr;

  lv_obj_send_event(bankUpButton, LV_EVENT_CLICKED, nullptr);
  if (require(requestedBankDelta == 1, "bank up should request the next bank")) return 1;
  lv_obj_send_event(tunerButton, LV_EVENT_PRESSED, nullptr);
  if (require(requestedTunerMode == 1 && state.mode == ardor::UiMode::Preset,
              "the Tuner button should request a host-level tuner transition")) return 1;
  requestedTunerMode = -1;
  ardor::LooperTelemetry loopTelemetry;
  loopTelemetry.revision = 1;
  loopTelemetry.contentRevision = 1;
  loopTelemetry.sessionState = ardor::LooperSessionState::Running;
  loopTelemetry.masterFrames = 480000;
  loopTelemetry.maximumFrames = 1920000;
  loopTelemetry.playheadFrame = 240000;
  loopTelemetry.tracks[0].state = ardor::LooperTrackState::Playing;
  loopTelemetry.tracks[0].audible = true;
  loopTelemetry.tracks[0].undoAvailable = true;
  loopTelemetry.tracks[1].state = ardor::LooperTrackState::ArmedOverdub;
  loopTelemetry.tracks[1].audible = true;
  loopTelemetry.tracks[2].state = ardor::LooperTrackState::Recording;
  loopTelemetry.tracks[2].audible = true;
  loopTelemetry.tracks[3].state = ardor::LooperTrackState::Muted;
  ardor::enterLooperMode(state, "Ambient Lead", 128ULL * 1024ULL * 1024ULL);
  ardor::updateLooperUi(state, loopTelemetry, 0);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "LOOPER")
                && findLabel(lv_screen_active(), "LOCKED · AMBIENT LEAD")
                && findLabel(lv_screen_active(), "PLAY")
                && findLabel(lv_screen_active(), "DUB ARMED · NEXT LOOP")
                && findLabel(lv_screen_active(), "REC")
                && findLabel(lv_screen_active(), "MUTED")
                && findLabel(lv_screen_active(), "STOP ALL")
                && findLabel(lv_screen_active(), "NEW")
                && findLabel(lv_screen_active(), "SAVE")
                && findLabel(lv_screen_active(), "LOAD")
                && findLabel(lv_screen_active(), "EXIT")
                && findLabel(lv_screen_active(), "CLOSE"),
              "looper mode should expose locked preset, four track states, and transport")) return 1;
  auto* looperTitle = findLabel(lv_screen_active(), "LOOPER");
  auto* looperRoot = looperTitle ? lv_obj_get_parent(lv_obj_get_parent(looperTitle)) : nullptr;
  if (screenshotPath != nullptr) {
    lv_refr_now(display);
    if (require(saveRgb888Ppm(screenshotPath, screenshotPixels, screenshotStride, 1280, 720),
                "looper screenshot should be writable")) return 1;
  }
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "REC")), LV_EVENT_CLICKED, nullptr);
  if (require(selectedLooperTrack == 2 && state.looper.selectedTrack == 2,
              "touching a track plate should select the physical track")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "STOP ALL")), LV_EVENT_PRESSED, nullptr);
  if (require(looperCommandCount == 1 && lastLooperCommand == ardor::LooperCommandType::Pause,
              "running looper Stop All should request an audio-thread pause")) return 1;

  loopTelemetry.revision = 2;
  loopTelemetry.error = ardor::LooperError::MaximumLengthReached;
  ardor::updateLooperUi(state, loopTelemetry, 2, 0.5f);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "HOLD TO CLEAR TRACK 3 · 50% · RELEASE CANCELS")
                && findLabel(lv_screen_active(), "CLEAR 50% · RELEASE CANCELS"),
              "looper screen should expose the destructive hold countdown")) return 1;

  loopTelemetry.revision = 3;
  ardor::updateLooperUi(state, loopTelemetry, 2);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "MAX LENGTH · LOOP CLOSED AND PLAYING"),
              "looper screen should explain an automatic maximum-length close")) return 1;

  loopTelemetry.revision = 4;
  loopTelemetry.error = ardor::LooperError::None;
  loopTelemetry.sessionState = ardor::LooperSessionState::Paused;
  ardor::updateLooperUi(state, loopTelemetry, 2);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "RESUME"),
              "paused transport should relabel Stop All as Resume")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(looperRoot, "PLAY")), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(looperRoot, "TRACK 1 MIX")
                && findLabel(looperRoot, "+0 DB")
                && findLabel(looperRoot, "CENTER")
                && findLabel(looperRoot, "CLEAR TRACK"),
              "touching a populated track should expose its large mix controls")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(looperRoot, "+1 DB")), LV_EVENT_PRESSED, nullptr);
  if (require(lastLooperCommand == ardor::LooperCommandType::SetTrackLevelDb,
              "track mix overlay should issue realtime level commands")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(looperRoot, "CLEAR TRACK")), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(looperRoot, "CLEAR SELECTED TRACK?"),
              "touch Clear Track should require confirmation")) return 1;
  auto* clearTrackTitle = findLabel(looperRoot, "CLEAR SELECTED TRACK?");
  auto* clearTrackPanel = clearTrackTitle ? lv_obj_get_parent(clearTrackTitle) : nullptr;
  lv_obj_send_event(lv_obj_get_parent(findLabel(clearTrackPanel, "CANCEL")), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  state.looper.mixerOpen = false;
  ardor::markUiChanged(state, ardor::UiChange::Looper);
  ui.refresh(lv_screen_active(), state);
  lv_obj_send_event(lv_obj_get_parent(findLabel(looperRoot, "NEW")), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(looperRoot, "START A NEW LOOP?"),
              "New should protect a modified loop with Save, Discard, and Cancel")) return 1;
  auto* newLoopTitle = findLabel(looperRoot, "START A NEW LOOP?");
  auto* newLoopPanel = newLoopTitle ? lv_obj_get_parent(newLoopTitle) : nullptr;
  lv_obj_send_event(lv_obj_get_parent(findLabel(newLoopPanel, "DISCARD")), LV_EVENT_CLICKED, nullptr);
  lv_obj_send_event(lv_obj_get_parent(findLabel(looperRoot, "SAVE")), LV_EVENT_PRESSED, nullptr);
  lv_obj_send_event(lv_obj_get_parent(findLabel(looperRoot, "LOAD")), LV_EVENT_PRESSED, nullptr);
  if (require(newLooperCalls == 1 && saveLooperCalls == 1 && loadLooperCalls == 1,
              "paused looper management buttons should invoke their host actions")) return 1;
  ardor::UiLooperState::LibraryEntry libraryEntry;
  libraryEntry.id = std::string(32, 'a');
  libraryEntry.name = "Night Sketch";
  libraryEntry.sourcePresetName = "Ambient Lead";
  libraryEntry.savedAt = "2026-08-31T21:00:00Z";
  libraryEntry.loopFrames = 480000;
  libraryEntry.populatedTracks = 3;
  libraryEntry.available = true;
  ardor::openLooperLibrary(state, {libraryEntry});
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(looperRoot, "SAVED LOOPS")
                && findLabel(looperRoot, "NIGHT SKETCH")
                && findLabelContaining(looperRoot, "3 TRACKS"),
              "saved-loop library should expose name, source, duration, and track count")) return 1;
  auto* libraryTitle = findLabel(looperRoot, "SAVED LOOPS");
  auto* libraryPanel = libraryTitle ? lv_obj_get_parent(libraryTitle) : nullptr;
  lv_obj_send_event(lv_obj_get_parent(findLabel(libraryPanel, "LOAD")), LV_EVENT_CLICKED, nullptr);
  if (require(loadedLoopId == libraryEntry.id && !state.looper.libraryOpen,
              "library Load should select the exact saved-loop id and close the overlay")) return 1;
  ardor::openLooperLibrary(state, {libraryEntry});
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  libraryTitle = findLabel(looperRoot, "SAVED LOOPS");
  libraryPanel = libraryTitle ? lv_obj_get_parent(libraryTitle) : nullptr;
  lv_obj_send_event(lv_obj_get_parent(findLabel(libraryPanel, "DELETE")), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(looperRoot, "DELETE SAVED LOOP?") != nullptr,
              "library Delete should require a destructive confirmation")) return 1;
  auto* deleteTitle = findLabel(looperRoot, "DELETE SAVED LOOP?");
  auto* deletePanel = deleteTitle ? lv_obj_get_parent(deleteTitle) : nullptr;
  lv_obj_send_event(lv_obj_get_parent(findLabel(deletePanel, "DELETE")), LV_EVENT_CLICKED, nullptr);
  if (require(deletedLoopId == libraryEntry.id,
              "confirmed library deletion should target the exact saved-loop id")) return 1;
  ardor::closeLooperLibrary(state);
  ardor::markLooperUnsaved(state);
  ui.refresh(lv_screen_active(), state);
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "CLOSE")), LV_EVENT_PRESSED, nullptr);
  auto* discardLoopLabel = findLabel(lv_screen_active(), "DISCARD UNSAVED LOOP?");
  auto* closeOverlay = discardLoopLabel
    ? lv_obj_get_parent(lv_obj_get_parent(discardLoopLabel)) : nullptr;
  if (require(closeOverlay && !lv_obj_has_flag(closeOverlay, LV_OBJ_FLAG_HIDDEN),
              "closing a modified loop should require explicit discard confirmation")) return 1;
  auto* closeConfirmation = discardLoopLabel ? lv_obj_get_parent(discardLoopLabel) : nullptr;
  lv_obj_send_event(lv_obj_get_parent(findLabel(closeConfirmation, "DISCARD")),
                    LV_EVENT_CLICKED, nullptr);
  if (require(closeLooperCalls == 1,
              "discard confirmation should invoke the host close action exactly once")) return 1;

  ardor::enterTunerMode(state);
  ui.refresh(lv_screen_active(), state);
  lv_obj_t* tunerTitle = findLabel(lv_screen_active(), "TUNER");
  if (require(tunerTitle && !lv_obj_has_flag(lv_obj_get_parent(tunerTitle), LV_OBJ_FLAG_HIDDEN),
              "the Tuner button should reveal the existing tuner screen")) return 1;
  ardor::enterPresetMode(state);
  ui.refresh(lv_screen_active(), state);
  lv_obj_send_event(editButton, LV_EVENT_PRESSED, nullptr);
  if (require(state.mode == ardor::UiMode::Edit,
              "pressing Edit should enter the editor before opening Blocks")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* blocksButtonLabel = findLabel(lv_screen_active(), "MODULES");
  lv_obj_t* blocksButton = blocksButtonLabel ? lv_obj_get_parent(blocksButtonLabel) : nullptr;
  if (require(blocksButton && lv_obj_get_width(blocksButton) == 112 && lv_obj_get_height(blocksButton) == 52,
              "Modules should have a large, finger-friendly hit target")) return 1;
  lv_obj_send_event(blocksButton, LV_EVENT_PRESSED, nullptr);
  if (require(state.mode == ardor::UiMode::Edit && state.blockDrawerOpen,
              "pressing Modules should reliably keep the edit screen open and show the drawer")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 480);
  lv_obj_t* allFilter = drawer ? findLabel(drawer, "All") : nullptr;
  lv_obj_t* utilityFilter = drawer ? findLabel(drawer, "Utility") : nullptr;
  lv_obj_t* delayFilter = drawer ? findLabel(drawer, "Delays") : nullptr;
  lv_obj_t* reverbFilter = drawer ? findLabel(drawer, "Reverbs") : nullptr;
  lv_obj_t* tremAssetLabel = drawer ? findLabel(drawer, "VINTAGE TREM") : nullptr;
  lv_obj_t* compressorAssetLabel = drawer ? findLabel(drawer, "COMPRESSOR") : nullptr;
  lv_obj_t* noiseGateAssetLabel = drawer ? findLabel(drawer, "NOISE GATE") : nullptr;
  lv_obj_t* eqAssetLabel = drawer ? findLabel(drawer, "FIVE BAND EQ") : nullptr;
  lv_obj_t* splitAssetLabel = drawer ? findLabel(drawer, "SPLIT LEFT / RIGHT") : nullptr;
  lv_obj_t* splitAssetButton = splitAssetLabel ? lv_obj_get_parent(splitAssetLabel) : nullptr;
  lv_obj_t* splitUnavailableReason = splitAssetButton
    ? findLabel(splitAssetButton, "Remove standalone NAM / IR first") : nullptr;
  if (require(drawer && allFilter && utilityFilter && delayFilter && reverbFilter
                && tremAssetLabel && compressorAssetLabel && noiseGateAssetLabel && eqAssetLabel
                && splitAssetLabel && splitUnavailableReason,
              "block drawer should group compressor, noise gate, and EQ under Utility")) return 1;
  lv_area_t splitTitleArea{};
  lv_area_t splitReasonArea{};
  lv_area_t splitButtonArea{};
  lv_obj_get_coords(splitAssetLabel, &splitTitleArea);
  lv_obj_get_coords(splitUnavailableReason, &splitReasonArea);
  lv_obj_get_coords(splitAssetButton, &splitButtonArea);
  if (require(splitTitleArea.y2 < splitReasonArea.y1
                && std::abs((splitTitleArea.x1 + splitTitleArea.x2)
                              - (splitButtonArea.x1 + splitButtonArea.x2)) <= 1
                && std::abs((splitReasonArea.x1 + splitReasonArea.x2)
                              - (splitButtonArea.x1 + splitButtonArea.x2)) <= 1
                && std::abs((splitTitleArea.y1 + splitReasonArea.y2)
                              - (splitButtonArea.y1 + splitButtonArea.y2)) <= 1,
              "disabled Split text should form a centered, non-overlapping two-line group")) return 1;
  lv_obj_t* allFilterButton = lv_obj_get_parent(allFilter);
  lv_obj_t* utilityFilterButton = lv_obj_get_parent(utilityFilter);
  lv_obj_t* delayFilterButton = lv_obj_get_parent(delayFilter);
  lv_obj_t* reverbFilterButton = lv_obj_get_parent(reverbFilter);
  lv_obj_t* tremAssetButton = lv_obj_get_parent(tremAssetLabel);
  lv_obj_t* compressorAssetButton = lv_obj_get_parent(compressorAssetLabel);
  lv_obj_t* noiseGateAssetButton = lv_obj_get_parent(noiseGateAssetLabel);
  lv_obj_t* eqAssetButton = lv_obj_get_parent(eqAssetLabel);
  lv_obj_t* retainedDrawer = drawer;
  lv_obj_t* retainedAssetList = lv_obj_get_parent(tremAssetButton);
  lv_area_t drawerArea{};
  lv_obj_get_coords(drawer, &drawerArea);
  if (require(lv_obj_get_width(drawer) == 480 && lv_obj_get_height(drawer) == 720
                && drawerArea.x2 == 1279 && drawerArea.y2 == 719,
              "block drawer should fill the full right edge")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(drawer, LV_PART_MAIN), lv_color_hex(0x191c1f)),
              "block drawer should use the recessed Panel plate")) return 1;
  if (require(lv_obj_get_y(delayFilterButton) > lv_obj_get_y(allFilterButton)
              && lv_obj_get_y(reverbFilterButton) == lv_obj_get_y(delayFilterButton)
                && lv_obj_get_width(allFilterButton) == 105 && lv_obj_get_height(allFilterButton) == 58,
              "all seven drawer filters should fill a fixed two-row touch grid")) return 1;
  lv_obj_t* categorySlider = findObjectOfClass(drawer, &lv_slider_class);
  lv_obj_t* filterRow = lv_obj_get_parent(allFilterButton);
  if (require(!categorySlider && !lv_obj_has_flag(filterRow, LV_OBJ_FLAG_SCROLLABLE),
              "category grid should have no competing slider or native scrolling")) return 1;
  lv_obj_t* drawerInstruction = findLabel(drawer, "ALL MODULES");
  lv_obj_t* drawerSeparator = findObjectWithSizeAndBgColor(drawer, lv_color_hex(0x3b4247), 444, 1);
  lv_area_t filterArea{};
  lv_area_t separatorArea{};
  lv_area_t instructionArea{};
  lv_area_t retainedListArea{};
  if (require(drawerSeparator && drawerInstruction,
              "drawer should render its list separator and helper text")) return 1;
  lv_obj_get_coords(filterRow, &filterArea);
  lv_obj_get_coords(drawerSeparator, &separatorArea);
  lv_obj_get_coords(drawerInstruction, &instructionArea);
  lv_obj_get_coords(retainedAssetList, &retainedListArea);
  if (require(filterArea.y2 < separatorArea.y1
                && separatorArea.y2 < instructionArea.y1
                && instructionArea.y2 < retainedListArea.y1,
              "drawer categories, separator, helper text, and asset list should not overlap")) return 1;
  if (require(filterArea.x1 == separatorArea.x1
                && separatorArea.x1 == instructionArea.x1
                && instructionArea.x1 == retainedListArea.x1,
              "drawer section labels and content should share one left edge")) return 1;
  lv_obj_send_event(utilityFilterButton, LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(state.categoryFilter == "utility"
                && !lv_obj_has_flag(compressorAssetButton, LV_OBJ_FLAG_HIDDEN)
                && !lv_obj_has_flag(noiseGateAssetButton, LV_OBJ_FLAG_HIDDEN)
                && !lv_obj_has_flag(eqAssetButton, LV_OBJ_FLAG_HIDDEN)
                && lv_obj_has_flag(tremAssetButton, LV_OBJ_FLAG_HIDDEN),
              "Utility should show compressor, noise gate, and EQ while hiding modulation effects")) return 1;
  lv_obj_send_event(delayFilterButton, LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 480);
  categorySlider = findObjectOfClass(drawer, &lv_slider_class);
  allFilter = drawer ? findLabel(drawer, "All") : nullptr;
  tremAssetLabel = drawer ? findLabel(drawer, "TAPE DELAY") : nullptr;
  tremAssetButton = tremAssetLabel ? lv_obj_get_parent(tremAssetLabel) : nullptr;
  lv_obj_t* roomReverbLabel = drawer ? findLabel(drawer, "ROOM REVERB") : nullptr;
  lv_obj_t* roomReverbButton = roomReverbLabel ? lv_obj_get_parent(roomReverbLabel) : nullptr;
  filterRow = lv_obj_get_parent(lv_obj_get_parent(allFilter));
  if (require(drawer == retainedDrawer && tremAssetButton && roomReverbButton
                && lv_obj_get_parent(tremAssetButton) == retainedAssetList,
              "category changes should retain the drawer and its asset list")) return 1;
  if (require(state.categoryFilter == "delay", "Delays should select the delay filter")) return 1;
  if (require(!lv_obj_has_flag(tremAssetButton, LV_OBJ_FLAG_HIDDEN),
              "Delays should show delay assets")) return 1;
  if (require(lv_obj_has_flag(roomReverbButton, LV_OBJ_FLAG_HIDDEN),
              "Delays should hide reverb assets")) return 1;
  if (require(!categorySlider && !lv_obj_has_flag(filterRow, LV_OBJ_FLAG_SCROLLABLE),
              "choosing a category should keep the fixed category grid stable")) return 1;
  if (require(tremAssetButton && lv_color_eq(lv_obj_get_style_bg_color(tremAssetButton, LV_PART_MAIN), lv_color_hex(0x2a2f33)),
              "drawer asset tiles should be charcoal")) return 1;
  if (require(lv_obj_get_height(tremAssetButton) == 72,
              "drawer asset tiles should have large vertical touch targets")) return 1;
  lv_obj_t* assetList = lv_obj_get_parent(tremAssetButton);
  if (require(lv_obj_has_flag(assetList, LV_OBJ_FLAG_SCROLLABLE)
                && lv_obj_get_scroll_dir(assetList) == LV_DIR_VER,
              "drawer asset list should own ordinary vertical swipe gestures")) return 1;
  lv_obj_send_event(tremAssetButton, LV_EVENT_LONG_PRESSED, nullptr);
  if (require(!lv_obj_has_flag(assetList, LV_OBJ_FLAG_SCROLLABLE),
              "a deliberate long press should transfer gesture ownership to drag and drop")) return 1;
  lv_obj_send_event(tremAssetButton, LV_EVENT_RELEASED, nullptr);
  if (require(lv_obj_has_flag(assetList, LV_OBJ_FLAG_SCROLLABLE),
              "asset list scrolling should be restored after a drag finishes")) return 1;

  lv_obj_send_event(reverbFilterButton, LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 480);
  tremAssetLabel = drawer ? findLabel(drawer, "TAPE DELAY") : nullptr;
  tremAssetButton = tremAssetLabel ? lv_obj_get_parent(tremAssetLabel) : nullptr;
  roomReverbLabel = drawer ? findLabel(drawer, "ROOM REVERB") : nullptr;
  roomReverbButton = roomReverbLabel ? lv_obj_get_parent(roomReverbLabel) : nullptr;
  if (require(state.categoryFilter == "reverb" && tremAssetButton && roomReverbButton
                && lv_obj_has_flag(tremAssetButton, LV_OBJ_FLAG_HIDDEN)
                && !lv_obj_has_flag(roomReverbButton, LV_OBJ_FLAG_HIDDEN),
              "Reverbs should show reverb assets and hide delay assets")) return 1;

  allFilter = drawer ? findLabel(drawer, "All") : nullptr;
  lv_obj_send_event(lv_obj_get_parent(allFilter), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 480);
  lv_obj_t* firstAssetLabel = drawer ? findLabel(drawer, upper(state.assets.front().name).c_str()) : nullptr;
  assetList = firstAssetLabel ? lv_obj_get_parent(lv_obj_get_parent(firstAssetLabel)) : nullptr;
  if (require(assetList, "all-assets drawer list should render")) return 1;
  lv_obj_scroll_to_y(assetList, 100, LV_ANIM_OFF);
  lv_obj_send_event(assetList, LV_EVENT_SCROLL, nullptr);
  const int savedScrollOffset = lv_obj_get_scroll_y(assetList);
  if (require(savedScrollOffset > 0 && state.assetScrollOffset == savedScrollOffset,
              "asset list should persist its vertical scroll offset")) return 1;
  lv_obj_send_event(assetList, LV_EVENT_SCROLL_BEGIN, nullptr);
  ui.invalidate(ardor::UiChange::Drawers);
  ui.refresh(lv_screen_active(), state);
  if (require(findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 480) == drawer,
              "pending refresh should not delete the drawer during scrolling")) return 1;
  lv_obj_send_event(assetList, LV_EVENT_SCROLL_END, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 480);
  firstAssetLabel = drawer ? findLabel(drawer, upper(state.assets.front().name).c_str()) : nullptr;
  assetList = firstAssetLabel ? lv_obj_get_parent(lv_obj_get_parent(firstAssetLabel)) : nullptr;
  if (require(assetList && lv_obj_get_scroll_y(assetList) == savedScrollOffset,
              "retained drawer refresh should preserve the user's scroll position")) return 1;
  lv_obj_t* retainedFirstAssetButton = lv_obj_get_parent(firstAssetLabel);
  state.assets.push_back({"Temporary Asset", "", "delay"});
  ardor::markUiChanged(state, ardor::UiChange::Assets);
  ui.refresh(lv_screen_active(), state);
  firstAssetLabel = findLabel(lv_screen_active(), upper(state.assets.front().name).c_str());
  if (require(firstAssetLabel && lv_obj_get_parent(firstAssetLabel) == retainedFirstAssetButton,
              "asset reloads should reconcile rows without replacing unchanged assets")) return 1;
  state.assets.pop_back();
  ardor::markUiChanged(state, ardor::UiChange::Assets);
  ui.refresh(lv_screen_active(), state);

  auto fullState = state;
  auto& fullBlocks = fullState.bank.presets[fullState.activePreset].blocks;
  while (fullBlocks.size() < ardor::kMaxEffectBlocks) {
    fullBlocks.push_back(fullBlocks.front());
  }
  fullState.paramDrawerOpen = false;
  fullState.blockDrawerOpen = true;
  ui.build(lv_screen_active(), fullState);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 480);
  firstAssetLabel = drawer ? findLabel(drawer, upper(fullState.assets.front().name).c_str()) : nullptr;
  if (require(drawer && findLabel(drawer, "CHAIN FULL - DELETE A BLOCK TO ADD")
                && firstAssetLabel
                && lv_obj_has_state(lv_obj_get_parent(firstAssetLabel), LV_STATE_DISABLED),
              "full chain should explain why drawer items are disabled")) return 1;
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());

  // The scrim dims the chain (Panel plate at ~55% opacity) rather than
  // covering it outright, so the chosen insertion point stays visible.
  lv_obj_t* scrim = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 800, 720);
  if (require(scrim && lv_obj_has_flag(scrim, LV_OBJ_FLAG_CLICKABLE)
                && lv_obj_get_style_bg_opa(scrim, LV_PART_MAIN) == 140,
              "block drawer should dim the chain with a tappable modal scrim")) return 1;
  lv_obj_send_event(scrim, LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(!state.blockDrawerOpen, "tapping outside the block drawer should close it")) return 1;
  ardor::openBlockDrawer(state);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 480);
  lv_obj_t* digitalDelayLabel = drawer ? findLabel(drawer, "DIGITAL DELAY") : nullptr;
  if (require(digitalDelayLabel, "drawer should reopen after modal dismissal")) return 1;
  const auto blocksBeforeQuickAdd = state.bank.presets[state.activePreset].blocks.size();
  lv_obj_send_event(lv_obj_get_parent(digitalDelayLabel), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(!state.blockDrawerOpen && state.paramDrawerOpen
                && state.bank.presets[state.activePreset].blocks.size() == blocksBeforeQuickAdd + 1,
              "tapping an asset should close Blocks and open the new block editor")) return 1;
  lv_obj_t* addedDelayLabel = findLabel(lv_screen_active(), "DIGITAL DELAY");
  lv_obj_t* addedDelayCard = addedDelayLabel ? lv_obj_get_parent(addedDelayLabel) : nullptr;
  if (require(addedDelayCard
                && lv_obj_get_style_border_width(addedDelayCard, LV_PART_MAIN) == 3
                && lv_color_eq(lv_obj_get_style_border_color(addedDelayCard, LV_PART_MAIN),
                               lv_color_hex(0xe2e4e3)),
              "newly added block should receive a clear highlight")) return 1;
  completePreview(state);

  ardor::closeBlockDrawer(state);
  auto eqRenderAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Five Band EQ";
  });
  if (require(eqRenderAsset != state.assets.end(), "EQ asset should be available to the LVGL editor")) return 1;
  ardor::appendAssetBlock(state, static_cast<std::size_t>(std::distance(state.assets.begin(), eqRenderAsset)));
  completePreview(state);
  ui.selectBlock(state, state.bank.presets[state.activePreset].blocks.size() - 1);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 1240, 578),
              "EQ should open as the main editor surface")) return 1;
  lv_obj_t* eqPanel = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 1240, 578);
  lv_obj_t* eqGraph = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 1184, 236);
  if (require(eqGraph,
              "EQ main editor should reserve a tall response graph")) return 1;
  if (require(findLabel(lv_screen_active(), "Parametric EQ"), "EQ should render its dedicated editor title")) return 1;
  if (require(findLabelContaining(lv_screen_active(), "Band 1"), "EQ should render its selected-band strip")) return 1;
  if (require(findLabel(lv_screen_active(), "Reset Band"), "EQ should render a reset-band control")) return 1;
  lv_obj_t* eqHeaderDelete = lv_obj_get_parent(findLabel(lv_screen_active(), "Delete Block"));
  lv_obj_t* eqBypassLabel = findLabel(eqPanel, "Bypass");
  lv_obj_t* eqHeaderBypass = eqBypassLabel ? lv_obj_get_parent(eqBypassLabel) : nullptr;
  lv_obj_t* eqHeaderClose = lv_obj_get_parent(findLabel(lv_screen_active(), "Close"));
  if (require(eqHeaderDelete && eqHeaderBypass && eqHeaderClose,
              "EQ should render the shared rectangular header actions")) return 1;
  lv_area_t eqPanelArea{};
  lv_area_t eqGraphArea{};
  lv_area_t eqDeleteArea{};
  lv_area_t eqBypassArea{};
  lv_area_t eqCloseArea{};
  lv_obj_get_coords(eqPanel, &eqPanelArea);
  lv_obj_get_coords(eqGraph, &eqGraphArea);
  lv_obj_get_coords(eqHeaderDelete, &eqDeleteArea);
  lv_obj_get_coords(eqHeaderBypass, &eqBypassArea);
  lv_obj_get_coords(eqHeaderClose, &eqCloseArea);
  // The Panel plate's visible one-pixel rule forms part of the inner origin
  // on one side only (see the parameter-grid tolerance above), so the two
  // insets are within a pixel of each other rather than exactly equal.
  if (require(std::abs((eqGraphArea.x1 - eqPanelArea.x1) - (eqPanelArea.x2 - eqGraphArea.x2)) <= 2,
              "EQ response graph should have equal left and right insets")) return 1;
  if (require(eqDeleteArea.y1 + eqDeleteArea.y2 == eqBypassArea.y1 + eqBypassArea.y2
                && eqBypassArea.y1 + eqBypassArea.y2 == eqCloseArea.y1 + eqCloseArea.y2,
              "EQ Delete, Bypass, and Close controls should share one centre line")) return 1;
  if (require(eqGraphArea.y1 > std::max({eqDeleteArea.y2, eqBypassArea.y2, eqCloseArea.y2}) + 10,
              "EQ response graph should not overlap the header actions")) return 1;
  lv_obj_t* eqNodeLabel = findLabel(eqGraph, "B1");
  if (require(eqNodeLabel && lv_obj_get_width(lv_obj_get_parent(eqNodeLabel)) == 44
                && lv_obj_get_height(lv_obj_get_parent(eqNodeLabel)) == 44,
              "EQ graph nodes should be finger-sized targets")) return 1;
  lv_obj_t* eqNode = lv_obj_get_parent(eqNodeLabel);
  lv_obj_t* eqNodeMark = lv_obj_get_child(eqNode, 0);
  if (require(eqNodeMark && lv_obj_get_width(eqNodeMark) == 19 && lv_obj_get_height(eqNodeMark) == 19
                && lv_color_eq(lv_obj_get_style_bg_color(eqNodeMark, LV_PART_MAIN), lv_color_hex(0xd8422f)),
              "the selected band's node mark should be sized and coloured on the very first render, "
              "not only after the next drag or slider tweak repaints the graph")) return 1;
  lv_obj_t* frequencyLabel = findLabel(lv_screen_active(), "FREQUENCY");
  lv_obj_t* qLabel = findLabel(lv_screen_active(), "Q");
  lv_obj_t* gainLabel = findLabel(lv_screen_active(), "GAIN");
  if (require(frequencyLabel && qLabel && gainLabel,
              "EQ should render frequency, Q, and gain as dedicated sliders")) return 1;
  lv_obj_t* frequencySlider = lv_obj_get_parent(frequencyLabel);
  const int freqFillWidthBeforeNodeDrag = [&] {
    lv_obj_t* fill = findObjectWithHeight(frequencySlider, 16);
    return fill ? lv_obj_get_width(fill) : -1;
  }();
  lv_area_t eqNodeArea{};
  lv_obj_get_coords(eqNode, &eqNodeArea);
  SimulatedPointer nodePointer{{(eqNodeArea.x1 + eqNodeArea.x2) / 2, (eqNodeArea.y1 + eqNodeArea.y2) / 2},
                               LV_INDEV_STATE_PRESSED};
  lv_indev_t* nodeInput = lv_indev_create();
  lv_indev_set_type(nodeInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(nodeInput, &nodePointer);
  lv_indev_set_read_cb(nodeInput, readSimulatedPointer);
  lv_indev_read(nodeInput);
  nodePointer.point.x += 200;
  lv_indev_read(nodeInput);
  lv_obj_update_layout(frequencySlider);
  lv_obj_t* freqFillDuringNodeDrag = findObjectWithHeight(frequencySlider, 16);
  if (require(freqFillDuringNodeDrag
                && lv_obj_get_width(freqFillDuringNodeDrag) != freqFillWidthBeforeNodeDrag,
              "dragging an EQ graph node should live-update the Frequency slider before release, "
              "not only once the drag ends")) return 1;
  nodePointer.state = LV_INDEV_STATE_RELEASED;
  lv_indev_read(nodeInput);
  ui.refresh(lv_screen_active(), state);
  lv_indev_delete(nodeInput);
  lv_obj_t* qSlider = lv_obj_get_parent(qLabel);
  // Q is the band editor's default encoder target (spec 9.1), so its slider
  // starts focused/lamp-coloured as soon as the EQ editor opens.
  lv_obj_t* qFill = findObjectWithHeight(qSlider, 16);
  if (require(qFill && !findObjectOfClass(qSlider, &lv_arc_class)
                && lv_obj_get_width(qSlider) == 385 && lv_obj_get_height(qSlider) == 132
                && lv_obj_get_style_radius(qSlider, LV_PART_MAIN) == 0,
              "EQ controls should use the same engraved-scale slider visuals")) return 1;
  const float qBeforeDrag = ardor::selectedParametricEqParams(state).bands[0].q;
  lv_area_t qSliderArea{};
  lv_obj_get_coords(qSlider, &qSliderArea);
  SimulatedPointer eqPointer{{qSliderArea.x1 + lv_obj_get_width(qSlider) / 4,
                              (qSliderArea.y1 + qSliderArea.y2) / 2},
                             LV_INDEV_STATE_PRESSED};
  lv_indev_t* eqInput = lv_indev_create();
  lv_indev_set_type(eqInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(eqInput, &eqPointer);
  lv_indev_set_read_cb(eqInput, readSimulatedPointer);
  lv_indev_read(eqInput);
  eqPointer.point.x = qSliderArea.x1 + (lv_obj_get_width(qSlider) * 3) / 4;
  lv_indev_read(eqInput);
  lv_obj_update_layout(qSlider);
  if (require(ardor::selectedParametricEqParams(state).bands[0].q > qBeforeDrag * 1.5f,
              "EQ Q slider drag should map across its logarithmic range")) return 1;
  if (require(lv_obj_get_width(qFill) > 0,
              "EQ Q slider drag should update its active fill before release")) return 1;
  eqPointer.state = LV_INDEV_STATE_RELEASED;
  lv_indev_read(eqInput);
  ui.refresh(lv_screen_active(), state);
  lv_indev_delete(eqInput);
  lv_obj_t* graph = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 1184, 236);
  lv_obj_t* responseLine = findLineWithPointCount(graph, ardor::kEqCurvePointCount);
  lv_obj_t* gainSlider = lv_obj_get_parent(findLabel(lv_screen_active(), "GAIN"));
  const int responsePoint = 51;  // Band 1 is centred at 80 Hz, roughly 20% into the log graph.
  const int32_t responseYBefore = lv_line_get_points(responseLine)[responsePoint].y;
  lv_area_t gainSliderArea{};
  lv_obj_get_coords(gainSlider, &gainSliderArea);
  SimulatedPointer gainPointer{{(gainSliderArea.x1 + gainSliderArea.x2) / 2,
                                (gainSliderArea.y1 + gainSliderArea.y2) / 2},
                               LV_INDEV_STATE_PRESSED};
  lv_indev_t* gainInput = lv_indev_create();
  lv_indev_set_type(gainInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(gainInput, &gainPointer);
  lv_indev_set_read_cb(gainInput, readSimulatedPointer);
  lv_indev_read(gainInput);
  gainPointer.point.x = gainSliderArea.x1 + (lv_obj_get_width(gainSlider) * 3) / 4;
  lv_indev_read(gainInput);
  if (require(lv_line_get_points(responseLine)[responsePoint].y != responseYBefore,
              "EQ response graph should redraw during a slider drag")) return 1;
  gainPointer.state = LV_INDEV_STATE_RELEASED;
  lv_indev_read(gainInput);
  ui.refresh(lv_screen_active(), state);
  lv_indev_delete(gainInput);
  lv_obj_t* retainedEqGraph = graph;
  lv_obj_t* retainedQSlider = qSlider;
  lv_obj_t* bandTwoLabel = findLabelContaining(lv_screen_active(), "B2");
  lv_obj_send_event(lv_obj_get_parent(bandTwoLabel), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 1184, 236)
                == retainedEqGraph
                && lv_obj_get_parent(findLabel(lv_screen_active(), "Q")) == retainedQSlider,
              "EQ band selection should retain the response graph and slider objects")) return 1;
  lv_obj_t* highPassLabel = findLabelContaining(lv_screen_active(), "HP  ");
  lv_obj_send_event(lv_obj_get_parent(highPassLabel), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(findLabelContaining(lv_screen_active(), "High-pass filter") != nullptr,
              "EQ should expose a selectable high-pass stage")) return 1;
  lv_obj_t* filterOffLabel = findLabel(lv_screen_active(), "Filter Off");
  lv_obj_send_event(lv_obj_get_parent(filterOffLabel), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(ardor::selectedParametricEqParams(state).highPass.enabled,
              "EQ high-pass control should update preset parameters")) return 1;
  lv_obj_t* slopeLabel = findLabel(lv_screen_active(), "SLOPE");
  if (require(slopeLabel
                && !lv_obj_has_flag(lv_obj_get_parent(slopeLabel), LV_OBJ_FLAG_HIDDEN),
              "pass-filter editor should expose slope beside cutoff and resonance")) return 1;
  const int slopeBefore = ardor::selectedParametricEqParams(state).highPass.slopeDbPerOctave;
  ui.focusEqBandField(ardor::EqBandField::Slope);
  if (require(ui.applyFocusedParameterDelta(state, 1)
                && ardor::selectedParametricEqParams(state).highPass.slopeDbPerOctave
                  > slopeBefore,
              "hardware encoder should move pass-filter slope between supported choices")) return 1;
  lv_obj_t* deleteBlockLabel = findLabel(lv_screen_active(), "Delete Block");
  if (require(deleteBlockLabel && lv_obj_get_width(lv_obj_get_parent(deleteBlockLabel)) >= 156
                && lv_obj_get_height(lv_obj_get_parent(deleteBlockLabel)) >= 48,
              "EQ should render a finger-sized delete-block control")) return 1;
  if (require(findLineWithPointCount(lv_screen_active(), ardor::kEqCurvePointCount),
              "EQ should render a sampled response curve")) return 1;
  lv_obj_t* eqCloseLabel = findLabel(lv_screen_active(), "Close");
  lv_obj_t* eqCloseButton = eqCloseLabel ? lv_obj_get_parent(eqCloseLabel) : nullptr;
  lv_obj_send_event(eqCloseButton, LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(!state.paramDrawerOpen, "EQ close should act on touch-down")) return 1;
  if (require(!ui.applyFocusedParameterDelta(state, 1),
              "closing the editor should clear stale hardware-encoder focus")) return 1;
  ui.selectBlock(state, state.bank.presets[state.activePreset].blocks.size() - 1);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x191c1f), 1184, 236)
                == retainedEqGraph,
              "closing and reopening EQ should reactivate the retained editor")) return 1;
  deleteBlockLabel = findLabel(lv_screen_active(), "Delete Block");
  const auto blocksBeforeDelete = state.bank.presets[state.activePreset].blocks.size();
  lv_obj_send_event(lv_obj_get_parent(deleteBlockLabel), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(state.bank.presets[state.activePreset].blocks.size() == blocksBeforeDelete - 1,
              "delete-block control should remove the selected EQ block")) return 1;

  ardor::enterTunerMode(state);
  ardor::updateTunerTelemetry(state, {true, 82.4f, -2.0f, 0.96f, "E", 2});
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* tunerExitLabel = findLabel(lv_screen_active(), "Exit");
  lv_obj_t* tunerExitButton = tunerExitLabel ? lv_obj_get_parent(tunerExitLabel) : nullptr;
  if (require(findLabel(lv_screen_active(), "TUNER")
                && findLabel(lv_screen_active(), "OUTPUT MUTED")
                && findLabel(lv_screen_active(), "E2")
                && findLabel(lv_screen_active(), "FLAT")
                && findLabel(lv_screen_active(), "IN TUNE")
                && findLabel(lv_screen_active(), "SHARP")
                && findObjectWithBgColor(lv_screen_active(), lv_color_hex(0xd8422f), 11)
                && findLabel(lv_screen_active(), "Press any footswitch to exit")
                && tunerExitButton && lv_obj_get_width(tunerExitButton) == 120
                && lv_obj_get_height(tunerExitButton) == 60,
              "tuner mode should render live note, mute state, and three-lamp guidance")) return 1;
  lv_obj_send_event(tunerExitButton, LV_EVENT_PRESSED, nullptr);
  if (require(requestedTunerMode == 0 && state.mode == ardor::UiMode::Tuner,
              "the tuner Exit button should request a host-level audio restore")) return 1;
  requestedTunerMode = -1;
  ardor::enterPresetMode(state);
  ui.refresh(lv_screen_active(), state);
  if (require(findLabel(lv_screen_active(), "Edit"),
              "exiting tuner should restore the preset screen")) return 1;

  auto overlayState = ardor::makeDemoUiState();
  auto overlayAsset = std::find_if(overlayState.assets.begin(), overlayState.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Vintage Trem";
  });
  ardor::LvglUi overlayUi;
  overlayUi.build(lv_screen_active(), overlayState);
  if (require(overlayAsset != overlayState.assets.end(), "overlay test needs a structural asset")) return 1;
  ardor::appendAssetBlock(overlayState,
                          static_cast<std::size_t>(std::distance(overlayState.assets.begin(), overlayAsset)));
  overlayUi.refresh(lv_screen_active(), overlayState);
  lv_obj_t* applyingLabel = findLabelContaining(lv_screen_active(), "Applying effect chain...");
  if (require(ardor::pendingStructuralPreview(overlayState) && !applyingLabel,
              "queued preview should not cover the editor with a loading overlay")) return 1;
  completePreview(overlayState);
  overlayUi.refresh(lv_screen_active(), overlayState);
  if (require(ardor::previewIsSynchronized(overlayState),
              "completed preview should synchronize without a loading overlay")) return 1;

  auto navigationState = ardor::makeDemoUiState();
  navigationState.dirty = true;
  std::optional<ardor::UiNavigationDecision> navigationDecision;
  ardor::UiActions navigationActions;
  navigationActions.selectPreset = [&](std::size_t index) {
    ardor::requestPresetNavigation(navigationState, {0, index});
  };
  navigationActions.resolveNavigation = [&](ardor::UiNavigationDecision decision) {
    navigationDecision = decision;
    ardor::confirmNavigation(navigationState, decision);
  };
  ardor::LvglUi navigationUi(std::move(navigationActions));
  navigationUi.build(lv_screen_active(), navigationState);
  navigationUi.selectPreset(navigationState, 1);
  navigationUi.refresh(lv_screen_active(), navigationState);
  if (require(navigationState.activePreset == 0 && navigationState.navigationPrompt.has_value()
                && findLabel(lv_screen_active(), "Unsaved changes")
                && findLabel(lv_screen_active(), "Save") && findLabel(lv_screen_active(), "Discard")
                && findLabel(lv_screen_active(), "Cancel"),
              "dirty navigation should retain the draft and present Save/Discard/Cancel")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "Cancel")), LV_EVENT_CLICKED, nullptr);
  if (require(navigationDecision == ardor::UiNavigationDecision::Cancel
                && !navigationState.navigationPrompt.has_value() && navigationState.activePreset == 0,
              "Cancel should retain the current draft and selection")) return 1;
  navigationUi.selectPreset(navigationState, 1);
  navigationUi.refresh(lv_screen_active(), navigationState);
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "Discard")), LV_EVENT_CLICKED, nullptr);
  if (require(navigationDecision == ardor::UiNavigationDecision::Discard
                && !navigationState.navigationPrompt.has_value(),
              "Discard should release the selected destination for activation")) return 1;

  // Gain-reduction meter: renders for a selected compressor block, and stays
  // live even while a slider drag holds an input device -- guarding against
  // the interaction-gate bug class already fixed once for the EQ graph (see
  // LvglUi::syncCompressorGainMeter).
  auto gainMeterState = ardor::makeDemoUiState();
  ardor::enterEditMode(gainMeterState);
  const auto gainMeterCompressorAsset = std::find_if(
    gainMeterState.assets.begin(), gainMeterState.assets.end(), [](const ardor::UiAsset& asset) {
      return asset.name == "Compressor";
    });
  if (require(gainMeterCompressorAsset != gainMeterState.assets.end(),
              "compressor asset should be available for the gain-meter test")) return 1;
  ardor::appendAssetBlock(gainMeterState, static_cast<std::size_t>(
    std::distance(gainMeterState.assets.begin(), gainMeterCompressorAsset)));
  completePreview(gainMeterState);
  ui.selectBlock(gainMeterState,
                 gainMeterState.bank.presets[gainMeterState.activePreset].blocks.size() - 1);
  ui.build(lv_screen_active(), gainMeterState);
  lv_obj_update_layout(lv_screen_active());

  if (require(findLabel(lv_screen_active(), "THRESHOLD"),
              "compressor panel should render a Threshold slider")) return 1;
  lv_obj_t* gainMeterLabel = findLabel(lv_screen_active(), "0.0 dB");
  if (require(gainMeterLabel, "compressor panel should render the gain-reduction meter at 0 dB "
                              "before any reduction has been reported")) return 1;
  lv_obj_t* gainMeterPill = lv_obj_get_parent(gainMeterLabel);
  lv_obj_t* gainMeterDeleteButton = lv_obj_get_parent(findLabel(lv_screen_active(), "Delete Block"));
  lv_area_t gainMeterArea{};
  lv_area_t gainMeterDeleteArea{};
  lv_obj_get_coords(gainMeterPill, &gainMeterArea);
  lv_obj_get_coords(gainMeterDeleteButton, &gainMeterDeleteArea);
  if (require(lv_obj_get_width(gainMeterPill) == 120 && lv_obj_get_height(gainMeterPill) == 52
                && gainMeterArea.x2 < gainMeterDeleteArea.x1,
              "the gain-reduction meter should sit as a fixed-size pill just before Delete Block"))
    return 1;

  ardor::updateCompressorGainReduction(gainMeterState, -6.0f);
  ui.beginParameterInteraction();
  ui.refresh(lv_screen_active(), gainMeterState);
  if (require(findLabel(lv_screen_active(), "-6.0 dB") == gainMeterLabel,
              "the gain-reduction meter should update even while a slider interaction is active, "
              "since it is sampled telemetry rather than a discrete UI event")) return 1;
  ui.endParameterInteraction();

  auto nonCompressorState = ardor::makeDemoUiState();
  ardor::enterEditMode(nonCompressorState);
  ui.selectBlock(nonCompressorState, 0);  // block 0 in the demo bank is a "nam" block, not a compressor
  ui.build(lv_screen_active(), nonCompressorState);
  lv_obj_update_layout(lv_screen_active());
  if (require(!findLabel(lv_screen_active(), "0.0 dB"),
              "the gain-reduction meter should only render for compressor-mode dynamics blocks"))
    return 1;

  lv_display_delete(display);
  lv_deinit();

  return 0;
}
