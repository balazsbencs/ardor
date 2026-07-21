#include "ui/LvglUi.h"
#include "ui/EqEditorModel.h"
#include "ui/fonts/OpenSansRegular.h"
#include "ui/fonts/OpenSansSemibold.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>

namespace {

int require(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << "\n";
    return 1;
  }
  return 0;
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
  if (require(namControls.size() == 1 && namControls.front().key == "useNano"
                && namControls.front().kind == ardor::ParameterControlKind::Toggle
                && namControls.front().formatted == "Off",
              "NAM page should expose a full-default nano switch")) return 1;
  if (require(ardor::applyParameterDelta(namState, namControls.front(), 1),
              "NAM nano switch should apply")) return 1;
  if (require(namState.bank.presets[namState.activePreset].blocks[0].params.value("useNano", false),
              "NAM nano switch should persist its boolean value")) return 1;
  if (require(namState.requiresEngineReload, "changing NAM model tier should require an engine reload")) return 1;

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
  ardor::LvglUi ui({
    {}, {}, {}, {}, {}, {}, {},
    [&](int delta) { requestedBankDelta += delta; },
  });
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
  if (require(!rejectedEqUi.applyFocusedParameterDelta(rejectedEqState, 2)
                && ardor::selectedParametricEqParams(rejectedEqState).bands[0] == rejectedEqBefore
                && !rejectedEqState.dirty,
              "rejected live EQ updates should roll back UI state")) return 1;

  const auto tremAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Vintage Trem";
  });
  if (require(tremAsset != state.assets.end(), "Vintage Trem should be available")) return 1;
  ardor::appendAssetBlock(state, static_cast<std::size_t>(std::distance(state.assets.begin(), tremAsset)));
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

  ardor::setSelectedBlockParam(state, "depth", 2.0f);
  if (require(state.bank.presets[state.activePreset].blocks[state.selectedBlock].params.value("depth", 0.0f) == 1.0f,
              "Daisy setter should enforce descriptor range")) return 1;

  const std::size_t tremIndex = state.selectedBlock;
  const auto compressorAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Compressor";
  });
  if (require(compressorAsset != state.assets.end(), "compressor asset should be available")) return 1;
  ardor::appendAssetBlock(state, static_cast<std::size_t>(std::distance(state.assets.begin(), compressorAsset)));
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
  ui.selectBlock(state, tremIndex);

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
  lv_font_glyph_dsc_t glyph{};
  if (require(lv_font_get_glyph_dsc(&ardor_font_open_sans_regular_18, &glyph, 'A', 0),
              "Open Sans should provide glyph descriptors")) return 1;
  lv_draw_buf_t* glyphBuffer = lv_draw_buf_create(glyph.box_w, glyph.box_h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
  if (require(glyphBuffer, "glyph buffer should allocate")) return 1;
  const void* glyphBitmap = lv_font_get_glyph_bitmap(&glyph, glyphBuffer);
  lv_draw_buf_destroy(glyphBuffer);
  if (require(glyphBitmap, "Open Sans glyphs should render in LVGL")) return 1;

  lv_display_t* display = lv_display_create(1280, 720);
  ui.selectBlock(state, state.selectedBlock);
  ardor::enterEditMode(state);
  const auto renderControls = ardor::parameterPage(state, 0);
  const auto depth = std::find_if(renderControls.begin(), renderControls.end(),
                                  [](const auto& control) { return control.key == "depth"; });
  if (require(depth != renderControls.end(), "Vintage Trem depth control should be available")) return 1;
  ardor::setSelectedBlockParam(state, "depth", depth->minimum);
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
  lv_obj_t* undoLabel = findLabel(lv_screen_active(), "Undo");
  lv_obj_t* depthLabel = findLabel(lv_screen_active(), depth->label.c_str());
  lv_obj_t* depthSlider = depthLabel ? lv_obj_get_parent(depthLabel) : nullptr;
  lv_obj_t* depthFill = depthSlider ? findObjectWithBgColor(depthSlider, lv_color_hex(0x43f05a)) : nullptr;
  if (require(title && depthSlider && depthFill, "parameter header and slider should render")) return 1;
  if (require(status && lv_color_eq(lv_obj_get_style_text_color(status, LV_PART_MAIN), lv_color_hex(0x43f05a)),
              "success status should render in accent green")) return 1;
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
  if (require(undoLabel && lv_obj_get_width(lv_obj_get_parent(undoLabel)) == 108
                && lv_obj_get_height(lv_obj_get_parent(undoLabel)) == 40,
              "reversible block edits should expose a large Undo action")) return 1;
  if (require(page, "parameter header should show PAGE n/total")) return 1;
  if (require(previous && next, "a seventh control should enable page navigation")) return 1;
  const auto depthIndex = static_cast<int>(std::distance(renderControls.begin(), depth));
  lv_area_t sliderArea{};
  lv_area_t sliderPanelArea{};
  lv_obj_get_coords(depthSlider, &sliderArea);
  lv_obj_get_coords(lv_obj_get_parent(depthSlider), &sliderPanelArea);
  const int expectedDepthX = 28 + (depthIndex % 3) * (385 + 14);
  const int expectedDepthY = 82 + (depthIndex / 3) * (76 + 14);
  if (require(sliderArea.x1 - sliderPanelArea.x1 == expectedDepthX
                && sliderArea.y1 - sliderPanelArea.y1 == expectedDepthY,
              "parameter sliders should use a three-column, two-row grid")) return 1;
  if (require(lv_obj_get_width(depthSlider) == 385 && lv_obj_get_height(depthSlider) == 76,
              "parameter slider should provide a large horizontal touch target")) return 1;
  if (require(lv_obj_get_style_radius(depthSlider, LV_PART_MAIN) == 5
                && lv_obj_get_style_radius(depthFill, LV_PART_MAIN) == 5,
              "parameter slider corners should use a subtle five-pixel radius")) return 1;
  if (require(lv_obj_get_width(depthFill) == 0,
              "minimum parameter value should leave the active fill empty")) return 1;
  lv_obj_t* activeTextLayer = lv_obj_get_child(depthFill, 0);
  lv_obj_t* activeDepthLabel = activeTextLayer ? findLabel(activeTextLayer, depth->label.c_str()) : nullptr;
  if (require(activeDepthLabel
                && lv_color_eq(lv_obj_get_style_text_color(depthLabel, LV_PART_MAIN), lv_color_hex(0xf5f5f5))
                && lv_color_eq(lv_obj_get_style_text_color(activeDepthLabel, LV_PART_MAIN), lv_color_hex(0x102014)),
              "slider text should switch contrast across the fill boundary")) return 1;

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
                && findLabel(depthSlider, updatedDepth->formatted.c_str()),
              "slider drag should update the value label before release")) return 1;
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
  depthLabel = findLabel(lv_screen_active(), depth->label.c_str());
  depthSlider = depthLabel ? lv_obj_get_parent(depthLabel) : nullptr;
  depthFill = depthSlider ? findObjectWithBgColor(depthSlider, lv_color_hex(0x43f05a)) : nullptr;

  lv_obj_t* chain = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x000000), 1240, 276);
  if (require(chain, "signal chain should be black behind charcoal blocks")) return 1;
  lv_obj_t* firstChainBlock = lv_obj_get_child(chain, 0);
  if (require(firstChainBlock && lv_obj_get_width(firstChainBlock) == 232,
              "single-block chain should use the fixed five-slot tile width")) return 1;
  if (require(!lv_obj_has_flag(chain, LV_OBJ_FLAG_SCROLLABLE),
              "chain should not scroll while blocks are draggable")) return 1;
  lv_obj_t* dragHandleLabel = findLabel(chain, "|||");
  if (require(dragHandleLabel
                && lv_obj_get_width(lv_obj_get_parent(dragHandleLabel)) == 60
                && lv_obj_get_height(lv_obj_get_parent(dragHandleLabel)) == 56,
              "chain blocks should expose a dedicated drag handle")) return 1;
  if (require(findLabel(lv_screen_active(), "BYPASSED"),
              "disabled blocks should show an explicit bypass state")) return 1;
  std::string firstCategory = state.bank.presets[state.activePreset].blocks.front().label;
  std::transform(firstCategory.begin(), firstCategory.end(), firstCategory.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  lv_obj_t* firstCategoryLabel = findLabel(firstChainBlock, firstCategory.c_str());
  lv_obj_t* firstCardAssetLabel = findLabel(firstChainBlock,
                                             state.bank.presets[state.activePreset].blocks.front().assetName.c_str());
  lv_obj_t* firstBypassedLabel = findLabel(firstChainBlock, "BYPASSED");
  lv_obj_t* firstDragHandle = findLabel(firstChainBlock, "|||");
  if (require(firstCategoryLabel && firstCardAssetLabel && firstBypassedLabel && firstDragHandle,
              "disabled chain card should render all text rows and its drag handle")) return 1;
  lv_area_t firstCategoryArea{};
  lv_area_t firstAssetArea{};
  lv_area_t firstBypassedArea{};
  lv_area_t firstDragHandleArea{};
  lv_obj_get_coords(firstCategoryLabel, &firstCategoryArea);
  lv_obj_get_coords(firstCardAssetLabel, &firstAssetArea);
  lv_obj_get_coords(firstBypassedLabel, &firstBypassedArea);
  lv_obj_get_coords(lv_obj_get_parent(firstDragHandle), &firstDragHandleArea);
  if (require(firstCategoryArea.y2 < firstAssetArea.y1
                && firstAssetArea.y2 < firstBypassedArea.y1,
              "chain-card category, asset, and bypass labels should occupy separate rows")) return 1;
  if (require(firstCategoryArea.x2 < firstDragHandleArea.x1
                && firstAssetArea.x2 < firstDragHandleArea.x1
                && firstBypassedArea.x2 < firstDragHandleArea.x1,
              "chain-card labels should stay inside the text column beside the drag handle")) return 1;
  if (require(findLabel(lv_screen_active(), "Tap a block to edit  |  Drag the handle to reorder"),
              "chain should explain its tap and drag interactions")) return 1;
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x111111), 1280, 48),
              "runtime and action feedback should use a dedicated status bar")) return 1;
  if (require(findLabel(lv_screen_active(), "MODULATION"),
              "chain card should render an uppercase category")) return 1;
  if (require(findLabel(lv_screen_active(), "Vintage Trem"),
              "chain card should render its asset name")) return 1;
  if (require(!findLabel(lv_screen_active(), "mod"),
              "chain card should not render its short internal type")) return 1;
  const auto retainedFirstId = state.bank.presets[state.activePreset].blocks.front().id;
  const auto retainedFirstAsset = state.bank.presets[state.activePreset].blocks.front().assetName;
  const auto selectedBeforeReorder = state.selectedBlock;
  ardor::moveBlock(state, 0, 1);
  ui.refresh(lv_screen_active(), state);
  if (require(findLabel(firstChainBlock, retainedFirstAsset.c_str()),
              "keyed chain reordering should move the existing card instead of recreating it")) return 1;
  const auto retainedPosition = std::find_if(
    state.bank.presets[state.activePreset].blocks.begin(),
    state.bank.presets[state.activePreset].blocks.end(),
    [&](const auto& block) { return block.id == retainedFirstId; });
  ardor::moveBlock(state,
    static_cast<std::size_t>(std::distance(state.bank.presets[state.activePreset].blocks.begin(), retainedPosition)), 0);
  ui.selectBlock(state, selectedBeforeReorder);
  ui.refresh(lv_screen_active(), state);
  title = findLabel(lv_screen_active(), titleText.c_str());
  page = findLabel(lv_screen_active(), "PAGE 1 / 2");
  depthLabel = findLabel(lv_screen_active(), depth->label.c_str());
  depthSlider = depthLabel ? lv_obj_get_parent(depthLabel) : nullptr;
  depthFill = depthSlider ? findObjectWithBgColor(depthSlider, lv_color_hex(0x43f05a)) : nullptr;

  lv_obj_t* parameterPanel = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x242424), 1240, 286);
  lv_obj_t* parameterClose = findLabel(lv_screen_active(), "Close");
  lv_obj_t* deleteBlock = findLabel(lv_screen_active(), "Delete Block");
  lv_obj_t* bypassLabel = parameterPanel ? findLabel(parameterPanel, "Bypass") : nullptr;
  lv_obj_t* bypassControl = bypassLabel ? lv_obj_get_parent(bypassLabel) : nullptr;
  lv_obj_t* bypassValue = bypassControl ? findLabel(bypassControl, "Off") : nullptr;
  lv_obj_t* bypassFill = bypassControl
    ? findObjectWithBgColor(bypassControl, lv_color_hex(0x43f05a)) : nullptr;
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
  if (require(lv_obj_get_style_radius(bypassControl, LV_PART_MAIN) == 5,
              "bypass control should use the same subtle five-pixel rounding as parameter sliders")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(bypassControl, LV_PART_MAIN),
                          lv_color_hex(0x343434))
                && lv_obj_get_width(bypassFill) == 0,
              "Bypass Off should use the dark inactive surface with no active fill")) return 1;
  if (require(!findObjectOfClass(bypassControl, &lv_switch_class),
              "bypass control should not retain a native switch or circular thumb")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(parameterClose), LV_PART_MAIN),
                          lv_color_hex(0x000000)),
              "parameter close button should have a black background")) return 1;

  const auto bypassedBlock = state.selectedBlock;
  lv_obj_send_event(bypassControl, LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(!state.bank.presets[state.activePreset].blocks[bypassedBlock].enabled
                && lv_obj_has_state(bypassControl, LV_STATE_CHECKED)
                && findLabel(bypassControl, "On")
                && lv_obj_get_width(bypassFill) == 160,
              "tapping Bypass should disable the block and fill the control green")) return 1;
  lv_obj_send_event(bypassControl, LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.bank.presets[state.activePreset].blocks[bypassedBlock].enabled
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
  lv_obj_t* focusedLabel = findLabel(lv_screen_active(), depth->label.c_str());
  lv_obj_t* focusedSlider = focusedLabel ? lv_obj_get_parent(focusedLabel) : nullptr;
  if (require(focusedSlider && lv_obj_get_style_outline_width(focusedSlider, LV_PART_MAIN) == 2
                && lv_color_eq(lv_obj_get_style_outline_color(focusedSlider, LV_PART_MAIN), lv_color_hex(0x43f05a)),
              "focused slider should use an acid-green outline")) return 1;

  lv_obj_t* focusedFill = findObjectWithBgColor(focusedSlider, lv_color_hex(0x43f05a));
  const int minimumFillWidth = focusedFill ? lv_obj_get_width(focusedFill) : -1;
  if (require(ui.applyFocusedParameterDelta(state, 1), "focused encoder adjustment should be consumed")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  focusedLabel = findLabel(lv_screen_active(), depth->label.c_str());
  focusedSlider = focusedLabel ? lv_obj_get_parent(focusedLabel) : nullptr;
  focusedFill = focusedSlider ? findObjectWithBgColor(focusedSlider, lv_color_hex(0x43f05a)) : nullptr;
  if (require(focusedFill && lv_obj_get_width(focusedFill) > minimumFillWidth,
              "focused encoder adjustment should increase the slider fill")) return 1;

  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* stableDepthLabel = findLabel(lv_screen_active(), depth->label.c_str());
  lv_obj_t* stableDepthSlider = stableDepthLabel ? lv_obj_get_parent(stableDepthLabel) : nullptr;
  lv_obj_t* stableFill = stableDepthSlider
    ? findObjectWithBgColor(stableDepthSlider, lv_color_hex(0x43f05a)) : nullptr;
  if (require(stableDepthSlider && stableFill, "focused slider should expose stable visual handles")) return 1;
  const int stableFillWidth = lv_obj_get_width(stableFill);
  ui.setFocusedWidgets(stableDepthSlider);
  ui.focusParameter(depth->key);
  if (require(ui.applyFocusedParameterDelta(state, 5), "targeted encoder adjustment should be consumed")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(stableDepthSlider);
  lv_obj_t* retainedDepthLabel = findLabel(lv_screen_active(), depth->label.c_str());
  if (require(retainedDepthLabel && lv_obj_get_parent(retainedDepthLabel) == stableDepthSlider,
              "targeted encoder adjustment should retain the slider object")) return 1;
  if (require(lv_obj_get_width(stableFill) > stableFillWidth,
              "targeted encoder adjustment should update the retained fill")) return 1;
  ui.focusParameter("");
  ardor::setSelectedBlockParam(state, "depth", depth->minimum);
  ui.refresh(lv_screen_active(), state);
  retainedDepthLabel = findLabel(lv_screen_active(), depth->label.c_str());
  if (require(retainedDepthLabel && lv_obj_get_parent(retainedDepthLabel) == stableDepthSlider,
              "model-driven parameter updates should retain the slider object")) return 1;
  const auto retainedBlockIndex = state.selectedBlock;
  ui.selectBlock(state, 1);
  ui.refresh(lv_screen_active(), state);
  ui.selectBlock(state, retainedBlockIndex);
  ui.refresh(lv_screen_active(), state);
  retainedDepthLabel = findLabel(lv_screen_active(), depth->label.c_str());
  if (require(retainedDepthLabel && lv_obj_get_parent(retainedDepthLabel) == stableDepthSlider,
              "switching parameter targets should reactivate the cached retained panel")) return 1;

  ardor::setSelectedBlockParam(state, "depth", depth->maximum);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  depthLabel = findLabel(lv_screen_active(), depth->label.c_str());
  depthSlider = depthLabel ? lv_obj_get_parent(depthLabel) : nullptr;
  depthFill = depthSlider ? findObjectWithBgColor(depthSlider, lv_color_hex(0x43f05a)) : nullptr;
  if (require(depthFill && lv_obj_get_width(depthFill) == 385,
              "maximum slider value should fill the complete control")) return 1;

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
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* presetTitle = findLabel(lv_screen_active(), state.bank.name.c_str());
  if (require(presetTitle && lv_obj_get_style_transform_scale_x(presetTitle, LV_PART_MAIN) == LV_SCALE_NONE,
              "bank title should keep its standard size")) return 1;
  lv_obj_t* presetName = findLabel(lv_screen_active(), state.bank.presets[state.activePreset].name.c_str());
  if (require(presetName && lv_obj_get_style_text_font(presetName, LV_PART_MAIN) == &ardor_font_open_sans_semibold_28
                && lv_obj_get_style_transform_scale_x(presetName, LV_PART_MAIN) >= 2 * LV_SCALE_NONE,
              "preset-card names should be at least three times standard button text size")) return 1;
  if (require(findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x43f05a), 4),
              "active preset should have a thin acid-green indicator")) return 1;
  lv_obj_t* retainedPresetCard = lv_obj_get_parent(presetName);
  const auto originalPresetName = state.bank.presets[state.activePreset].name;
  state.bank.presets[state.activePreset].name = "Retained preset";
  ardor::markUiChanged(state, ardor::UiChange::Presets);
  ui.refresh(lv_screen_active(), state);
  lv_obj_t* renamedPresetLabel = findLabel(lv_screen_active(), "Retained preset");
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
  chain = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x000000), 1240, 276);
  lv_obj_t* fifthChainBlock = chain ? lv_obj_get_child(chain, 4) : nullptr;
  lv_obj_t* sixthChainBlock = chain ? lv_obj_get_child(chain, 5) : nullptr;
  lv_area_t chainArea{};
  lv_area_t fifthChainBlockArea{};
  lv_area_t sixthChainBlockArea{};
  if (chain) lv_obj_get_coords(chain, &chainArea);
  if (fifthChainBlock) lv_obj_get_coords(fifthChainBlock, &fifthChainBlockArea);
  if (sixthChainBlock) lv_obj_get_coords(sixthChainBlock, &sixthChainBlockArea);
  if (require(sixthChainBlock && sixthChainBlockArea.y1 > chainArea.y1,
              "sixth chain tile should begin on the second row")) return 1;
  lv_obj_t* chainWrapConnector = findLineWithPointCount(lv_screen_active(), 20);
  const lv_point_precise_t* chainWrapPoints = chainWrapConnector ? lv_line_get_points(chainWrapConnector) : nullptr;
  if (require(chainWrapPoints && chainWrapPoints[0].x == 1233 && chainWrapPoints[0].y == 173
                && chainWrapPoints[1].x > chainWrapPoints[0].x && chainWrapPoints[1].y > chainWrapPoints[0].y
                && chainWrapPoints[19].x == 34 && chainWrapPoints[19].y == 313,
              "chain should smoothly wrap from the first row into the second")) return 1;
  if (require(fifthChainBlock
                && chainWrapPoints[0].x == fifthChainBlockArea.x2
                && chainWrapPoints[0].y == (fifthChainBlockArea.y1 + fifthChainBlockArea.y2 + 1) / 2
                && chainWrapPoints[19].x == sixthChainBlockArea.x1
                && chainWrapPoints[19].y == (sixthChainBlockArea.y1 + sixthChainBlockArea.y2 + 1) / 2,
              "chain wrap endpoints should align with both effect rows")) return 1;

  constexpr std::size_t blockCount = 10;
  if (require(ardor::LvglUi::chainSlotForPoint(blockCount, {34, 127}) == 0,
              "chain's top-left slot should map to the first block")) return 1;
  if (require(ardor::LvglUi::chainSlotForPoint(blockCount, {34, 277}) == 5,
              "chain's second row should map to the sixth block")) return 1;
  if (require(ardor::LvglUi::chainInsertionSlotForPoint(blockCount, {34 + 242 * 4, 277}) == 9,
              "chain insertion should use the second row's column")) return 1;
  if (require(ardor::LvglUi::chainInsertionSlotForPoint(blockCount, {34 + 122, 127}) == 1,
              "chain insertion should switch at the nearest slot boundary")) return 1;
  const auto secondRowIndicator = ardor::LvglUi::chainIndicatorPosition(blockCount, 5);
  if (require(secondRowIndicator.x == 34 && secondRowIndicator.y == 267,
              "sixth-slot insertion indicator should be placed on the second row")) return 1;
  const auto forwardIndicator = ardor::LvglUi::chainReorderIndicatorPosition(blockCount, 0, 1);
  if (require(forwardIndicator.x == 518 && forwardIndicator.y == 127,
              "forward reorder indicator should appear after the hovered block")) return 1;
  const auto rowEndIndicator = ardor::LvglUi::chainReorderIndicatorPosition(blockCount, 0, 4);
  if (require(rowEndIndicator.x == 1244 && rowEndIndicator.y == 127,
              "forward reorder indicator should stay beside the hovered row")) return 1;
  const auto backwardIndicator = ardor::LvglUi::chainReorderIndicatorPosition(blockCount, 4, 1);
  if (require(backwardIndicator.x == 276 && backwardIndicator.y == 127,
              "backward reorder indicator should appear before the hovered block")) return 1;

  ardor::enterPresetMode(state);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* editButtonLabel = findLabel(lv_screen_active(), "Edit");
  lv_obj_t* editButton = editButtonLabel ? lv_obj_get_parent(editButtonLabel) : nullptr;
  lv_obj_t* tunerButtonLabel = findLabel(lv_screen_active(), "Tuner");
  lv_obj_t* tunerButton = tunerButtonLabel ? lv_obj_get_parent(tunerButtonLabel) : nullptr;
  lv_obj_t* bankDownLabel = findLabel(lv_screen_active(), "Bank -");
  lv_obj_t* bankUpLabel = findLabel(lv_screen_active(), "Bank +");
  lv_obj_t* bankNameLabel = findLabel(lv_screen_active(), state.bank.name.c_str());
  lv_obj_t* masterLabel = findLabel(
    lv_screen_active(), ("Master " + std::to_string(state.masterVolume) + "%").c_str());
  lv_obj_t* bankDownButton = bankDownLabel ? lv_obj_get_parent(bankDownLabel) : nullptr;
  lv_obj_t* bankUpButton = bankUpLabel ? lv_obj_get_parent(bankUpLabel) : nullptr;
  if (require(editButton && lv_obj_get_width(editButton) == 164 && lv_obj_get_height(editButton) == 60,
              "Edit should have a large, finger-friendly hit target")) return 1;
  if (require(bankDownButton && bankUpButton && lv_obj_get_width(bankUpButton) == 144
                && lv_obj_get_height(bankUpButton) == 52,
              "preset screen should render dedicated bank up and down buttons")) return 1;
  if (require(masterLabel && tunerButton && lv_obj_get_width(tunerButton) == 120
                && lv_obj_get_height(tunerButton) == 60,
              "preset screen should provide a finger-sized Tuner button")) return 1;
  lv_area_t bankDownArea{};
  lv_area_t bankUpArea{};
  lv_area_t bankNameArea{};
  lv_area_t masterArea{};
  lv_area_t tunerButtonArea{};
  lv_obj_get_coords(bankDownButton, &bankDownArea);
  lv_obj_get_coords(bankUpButton, &bankUpArea);
  lv_obj_get_coords(bankNameLabel, &bankNameArea);
  lv_obj_get_coords(masterLabel, &masterArea);
  lv_obj_get_coords(tunerButton, &tunerButtonArea);
  if (require(bankDownArea.x2 < bankNameArea.x1 && bankUpArea.x1 > bankNameArea.x2
                && bankDownArea.y1 <= bankNameArea.y1 && bankDownArea.y2 >= bankNameArea.y2,
              "bank controls should flank the bank name in the header")) return 1;
  if (require(masterArea.x2 < tunerButtonArea.x1 && tunerButtonArea.x2 < bankDownArea.x1,
              "Master, Tuner, and Bank controls should not overlap")) return 1;
  if (require(lv_obj_has_state(bankDownButton, LV_STATE_DISABLED),
              "bank down should be disabled at the first bank")) return 1;
  if (require(lv_obj_get_style_text_font(bankUpLabel, LV_PART_MAIN) == &ardor_font_open_sans_semibold_22,
              "buttons should use the larger, more legible font")) return 1;
  lv_obj_send_event(bankUpButton, LV_EVENT_CLICKED, nullptr);
  if (require(requestedBankDelta == 1, "bank up should request the next bank")) return 1;
  lv_obj_send_event(tunerButton, LV_EVENT_PRESSED, nullptr);
  if (require(state.mode == ardor::UiMode::Tuner,
              "pressing the preset-screen Tuner button should enter tuner mode")) return 1;
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
  lv_obj_t* blocksButtonLabel = findLabel(lv_screen_active(), "Blocks");
  lv_obj_t* blocksButton = blocksButtonLabel ? lv_obj_get_parent(blocksButtonLabel) : nullptr;
  if (require(blocksButton && lv_obj_get_width(blocksButton) == 164 && lv_obj_get_height(blocksButton) == 60,
              "Blocks should have a large, finger-friendly hit target")) return 1;
  lv_obj_send_event(blocksButton, LV_EVENT_PRESSED, nullptr);
  if (require(state.mode == ardor::UiMode::Edit && state.blockDrawerOpen,
              "pressing Blocks should reliably keep the edit screen open and show the drawer")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x000000), 480);
  lv_obj_t* allFilter = drawer ? findLabel(drawer, "All") : nullptr;
  lv_obj_t* delayFilter = drawer ? findLabel(drawer, "Delays") : nullptr;
  lv_obj_t* reverbFilter = drawer ? findLabel(drawer, "Reverbs") : nullptr;
  lv_obj_t* tremAssetLabel = drawer ? findLabel(drawer, "Vintage Trem") : nullptr;
  if (require(drawer && allFilter && delayFilter && reverbFilter && tremAssetLabel,
              "block drawer should render separate delay and reverb categories")) return 1;
  lv_obj_t* allFilterButton = lv_obj_get_parent(allFilter);
  lv_obj_t* delayFilterButton = lv_obj_get_parent(delayFilter);
  lv_obj_t* reverbFilterButton = lv_obj_get_parent(reverbFilter);
  lv_obj_t* tremAssetButton = lv_obj_get_parent(tremAssetLabel);
  lv_obj_t* retainedDrawer = drawer;
  lv_obj_t* retainedAssetList = lv_obj_get_parent(tremAssetButton);
  lv_area_t drawerArea{};
  lv_obj_get_coords(drawer, &drawerArea);
  if (require(lv_obj_get_width(drawer) == 480 && lv_obj_get_height(drawer) == 672 && drawerArea.x2 == 1279,
              "block drawer should fill the right edge above the global status bar")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(drawer, LV_PART_MAIN), lv_color_hex(0x000000)),
              "block drawer should be black")) return 1;
  if (require(lv_obj_get_y(delayFilterButton) > lv_obj_get_y(allFilterButton)
                && lv_obj_get_y(reverbFilterButton) == lv_obj_get_y(delayFilterButton)
                && lv_obj_get_width(allFilterButton) == 105 && lv_obj_get_height(allFilterButton) == 58,
              "all eight drawer filters should fill a fixed two-row touch grid")) return 1;
  lv_obj_t* categorySlider = findObjectOfClass(drawer, &lv_slider_class);
  lv_obj_t* filterRow = lv_obj_get_parent(allFilterButton);
  if (require(!categorySlider && !lv_obj_has_flag(filterRow, LV_OBJ_FLAG_SCROLLABLE),
              "category grid should have no competing slider or native scrolling")) return 1;
  lv_obj_t* drawerInstruction = findLabel(drawer, "Tap to add - hold to drag");
  lv_obj_t* drawerSeparator = findObjectWithSizeAndBgColor(drawer, lv_color_hex(0x3a3a3a), 444, 1);
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
  lv_obj_send_event(delayFilterButton, LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x000000), 480);
  categorySlider = findObjectOfClass(drawer, &lv_slider_class);
  allFilter = drawer ? findLabel(drawer, "All") : nullptr;
  tremAssetLabel = drawer ? findLabel(drawer, "Tape Delay") : nullptr;
  tremAssetButton = tremAssetLabel ? lv_obj_get_parent(tremAssetLabel) : nullptr;
  lv_obj_t* roomReverbLabel = drawer ? findLabel(drawer, "Room Reverb") : nullptr;
  lv_obj_t* roomReverbButton = roomReverbLabel ? lv_obj_get_parent(roomReverbLabel) : nullptr;
  filterRow = lv_obj_get_parent(lv_obj_get_parent(allFilter));
  if (require(drawer == retainedDrawer && tremAssetButton && roomReverbButton
                && lv_obj_get_parent(tremAssetButton) == retainedAssetList,
              "category changes should retain the drawer and its asset list")) return 1;
  if (require(state.categoryFilter == "delay"
                && !lv_obj_has_flag(tremAssetButton, LV_OBJ_FLAG_HIDDEN)
                && lv_obj_has_flag(roomReverbButton, LV_OBJ_FLAG_HIDDEN),
              "Delays should show delay assets and hide reverb assets")) return 1;
  if (require(!categorySlider && !lv_obj_has_flag(filterRow, LV_OBJ_FLAG_SCROLLABLE),
              "choosing a category should keep the fixed category grid stable")) return 1;
  if (require(tremAssetButton && lv_color_eq(lv_obj_get_style_bg_color(tremAssetButton, LV_PART_MAIN), lv_color_hex(0x242424)),
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
  drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x000000), 480);
  tremAssetLabel = drawer ? findLabel(drawer, "Tape Delay") : nullptr;
  tremAssetButton = tremAssetLabel ? lv_obj_get_parent(tremAssetLabel) : nullptr;
  roomReverbLabel = drawer ? findLabel(drawer, "Room Reverb") : nullptr;
  roomReverbButton = roomReverbLabel ? lv_obj_get_parent(roomReverbLabel) : nullptr;
  if (require(state.categoryFilter == "reverb" && tremAssetButton && roomReverbButton
                && lv_obj_has_flag(tremAssetButton, LV_OBJ_FLAG_HIDDEN)
                && !lv_obj_has_flag(roomReverbButton, LV_OBJ_FLAG_HIDDEN),
              "Reverbs should show reverb assets and hide delay assets")) return 1;

  allFilter = drawer ? findLabel(drawer, "All") : nullptr;
  lv_obj_send_event(lv_obj_get_parent(allFilter), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x000000), 480);
  lv_obj_t* firstAssetLabel = drawer ? findLabel(drawer, state.assets.front().name.c_str()) : nullptr;
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
  if (require(findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x000000), 480) == drawer,
              "pending refresh should not delete the drawer during scrolling")) return 1;
  lv_obj_send_event(assetList, LV_EVENT_SCROLL_END, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x000000), 480);
  firstAssetLabel = drawer ? findLabel(drawer, state.assets.front().name.c_str()) : nullptr;
  assetList = firstAssetLabel ? lv_obj_get_parent(lv_obj_get_parent(firstAssetLabel)) : nullptr;
  if (require(assetList && lv_obj_get_scroll_y(assetList) == savedScrollOffset,
              "retained drawer refresh should preserve the user's scroll position")) return 1;
  lv_obj_t* retainedFirstAssetButton = lv_obj_get_parent(firstAssetLabel);
  state.assets.push_back({"Temporary Asset", "", "delay"});
  ardor::markUiChanged(state, ardor::UiChange::Assets);
  ui.refresh(lv_screen_active(), state);
  firstAssetLabel = findLabel(lv_screen_active(), state.assets.front().name.c_str());
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
  drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x000000), 480);
  firstAssetLabel = drawer ? findLabel(drawer, fullState.assets.front().name.c_str()) : nullptr;
  if (require(drawer && findLabel(drawer, "Chain full - delete a block to add")
                && firstAssetLabel
                && lv_obj_has_state(lv_obj_get_parent(firstAssetLabel), LV_STATE_DISABLED),
              "full chain should explain why drawer items are disabled")) return 1;
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());

  lv_obj_t* scrim = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x000000), 800, 672);
  if (require(scrim && lv_obj_has_flag(scrim, LV_OBJ_FLAG_CLICKABLE),
              "block drawer should dim the chain with a tappable modal scrim")) return 1;
  lv_obj_send_event(scrim, LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(!state.blockDrawerOpen, "tapping outside the block drawer should close it")) return 1;
  ardor::openBlockDrawer(state);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x000000), 480);
  lv_obj_t* digitalDelayLabel = drawer ? findLabel(drawer, "Digital Delay") : nullptr;
  if (require(digitalDelayLabel, "drawer should reopen after modal dismissal")) return 1;
  const auto blocksBeforeQuickAdd = state.bank.presets[state.activePreset].blocks.size();
  lv_obj_send_event(lv_obj_get_parent(digitalDelayLabel), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(!state.blockDrawerOpen && state.paramDrawerOpen
                && state.bank.presets[state.activePreset].blocks.size() == blocksBeforeQuickAdd + 1,
              "tapping an asset should close Blocks and open the new block editor")) return 1;
  lv_obj_t* addedDelayLabel = findLabel(lv_screen_active(), "Digital Delay");
  lv_obj_t* addedDelayCard = addedDelayLabel ? lv_obj_get_parent(addedDelayLabel) : nullptr;
  if (require(addedDelayCard
                && lv_obj_get_style_border_width(addedDelayCard, LV_PART_MAIN) == 3
                && lv_color_eq(lv_obj_get_style_border_color(addedDelayCard, LV_PART_MAIN),
                               lv_color_hex(0x43f05a)),
              "newly added block should receive a clear green highlight")) return 1;

  ardor::closeBlockDrawer(state);
  auto eqRenderAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Five Band EQ";
  });
  if (require(eqRenderAsset != state.assets.end(), "EQ asset should be available to the LVGL editor")) return 1;
  ardor::appendAssetBlock(state, static_cast<std::size_t>(std::distance(state.assets.begin(), eqRenderAsset)));
  ui.selectBlock(state, state.bank.presets[state.activePreset].blocks.size() - 1);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x242424), 1240, 578),
              "EQ should open as the main editor surface")) return 1;
  lv_obj_t* eqPanel = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x242424), 1240, 578);
  lv_obj_t* eqGraph = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x111111), 1184, 236);
  if (require(eqGraph,
              "EQ main editor should reserve a tall response graph")) return 1;
  if (require(findLabel(lv_screen_active(), "Parametric EQ"), "EQ should render its dedicated editor title")) return 1;
  if (require(findLabel(lv_screen_active(), "Band 1"), "EQ should render its selected-band strip")) return 1;
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
  if (require(eqGraphArea.x1 - eqPanelArea.x1 == eqPanelArea.x2 - eqGraphArea.x2,
              "EQ response graph should have equal left and right insets")) return 1;
  if (require(eqDeleteArea.y1 + eqDeleteArea.y2 == eqBypassArea.y1 + eqBypassArea.y2
                && eqBypassArea.y1 + eqBypassArea.y2 == eqCloseArea.y1 + eqCloseArea.y2,
              "EQ Delete, Bypass, and Close controls should share one centre line")) return 1;
  if (require(eqGraphArea.y1 > std::max({eqDeleteArea.y2, eqBypassArea.y2, eqCloseArea.y2}) + 10,
              "EQ response graph should not overlap the header actions")) return 1;
  lv_obj_t* eqNodeLabel = findLabel(
    eqGraph, "1");
  if (require(eqNodeLabel && lv_obj_get_width(lv_obj_get_parent(eqNodeLabel)) == 48
                && lv_obj_get_height(lv_obj_get_parent(eqNodeLabel)) == 48,
              "EQ graph nodes should be finger-sized targets")) return 1;
  lv_obj_t* frequencyLabel = findLabel(lv_screen_active(), "Frequency");
  lv_obj_t* qLabel = findLabel(lv_screen_active(), "Q");
  lv_obj_t* gainLabel = findLabel(lv_screen_active(), "Gain");
  if (require(frequencyLabel && qLabel && gainLabel,
              "EQ should render frequency, Q, and gain as dedicated sliders")) return 1;
  lv_obj_t* qSlider = lv_obj_get_parent(qLabel);
  lv_obj_t* qFill = findObjectWithBgColor(qSlider, lv_color_hex(0x43f05a));
  if (require(qFill && !findObjectOfClass(qSlider, &lv_arc_class)
                && lv_obj_get_width(qSlider) == 385 && lv_obj_get_height(qSlider) == 76
                && lv_obj_get_style_radius(qSlider, LV_PART_MAIN) == 5,
              "EQ controls should use the same five-pixel slider visuals")) return 1;
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
  lv_obj_t* graph = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x111111), 1184, 236);
  lv_obj_t* responseLine = findLineWithPointCount(graph, ardor::kEqCurvePointCount);
  lv_obj_t* gainSlider = lv_obj_get_parent(findLabel(lv_screen_active(), "Gain"));
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
  lv_obj_t* bandTwoLabel = findLabel(lv_screen_active(), "Band 2");
  lv_obj_send_event(lv_obj_get_parent(bandTwoLabel), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x111111), 1184, 236)
                == retainedEqGraph
                && lv_obj_get_parent(findLabel(lv_screen_active(), "Q")) == retainedQSlider,
              "EQ band selection should retain the response graph and slider objects")) return 1;
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
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x111111), 1184, 236)
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
                && findLabel(lv_screen_active(), "IN TUNE")
                && findLabel(lv_screen_active(), "Press any footswitch to exit")
                && tunerExitButton && lv_obj_get_width(tunerExitButton) == 120
                && lv_obj_get_height(tunerExitButton) == 60,
              "tuner mode should render live note, mute state, and guidance")) return 1;
  lv_obj_send_event(tunerExitButton, LV_EVENT_PRESSED, nullptr);
  if (require(state.mode == ardor::UiMode::Preset,
              "pressing the tuner Exit button should return to preset mode")) return 1;
  ui.refresh(lv_screen_active(), state);
  if (require(findLabel(lv_screen_active(), "Edit"),
              "exiting tuner should restore the preset screen")) return 1;

  lv_display_delete(display);
  lv_deinit();

  return 0;
}
