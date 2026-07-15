#include "ui/LvglUi.h"
#include "ui/EqEditorModel.h"
#include "ui/fonts/OpenSansRegular.h"

#include <algorithm>
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

lv_obj_t* findKnobPointer(lv_obj_t* parent)
{
  if (lv_obj_check_type(parent, &lv_line_class) && lv_line_get_point_count(parent) == 2) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* result = findKnobPointer(lv_obj_get_child(parent, static_cast<int32_t>(i)))) {
      return result;
    }
  }
  return nullptr;
}

lv_obj_t* findKnobPointer(lv_obj_t* parent, const char* controlLabel)
{
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    lv_obj_t* child = lv_obj_get_child(parent, static_cast<int32_t>(i));
    if (lv_obj_check_type(child, &lv_label_class) && std::strcmp(lv_label_get_text(child), controlLabel) == 0) {
      return findKnobPointer(parent);
    }
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* result = findKnobPointer(lv_obj_get_child(parent, static_cast<int32_t>(i)), controlLabel)) {
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

  ardor::LvglUi ui;
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
  if (require(depthControl != tremControls.end(), "Daisy depth control should be available")) return 1;
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

  if (require(ardor::parameterPage(state, 0).size() <= 7, "page must contain <= seven knobs")) return 1;
  if (require(ardor::parameterPageCount(state) == 1, "seven params should fit on one page")) return 1;

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
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());

  const auto& selected = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
  const std::string titleText = selected.label + "  /  " + selected.assetName;
  lv_obj_t* previous = findLabel(lv_screen_active(), "<");
  lv_obj_t* page = findLabel(lv_screen_active(), "PAGE 1 / 1");
  lv_obj_t* next = findLabel(lv_screen_active(), ">");
  lv_obj_t* title = findLabel(lv_screen_active(), titleText.c_str());
  lv_obj_t* pointer = findKnobPointer(lv_screen_active(), depth->label.c_str());
  if (require(title && pointer, "parameter header and knob pointer should render")) return 1;
  if (require(page, "parameter header should show PAGE n/total")) return 1;
  if (require(!previous && !next, "seven controls should not require page navigation")) return 1;
  lv_obj_t* pointerLayer = lv_obj_get_parent(pointer);
  lv_obj_t* knobObject = lv_obj_get_parent(pointerLayer);
  lv_obj_t* arc = findObjectOfClass(knobObject, &lv_arc_class);
  lv_obj_t* rim = findObjectWithSizeAndBgColor(knobObject, lv_color_hex(0x000000), 48, 48);
  if (require(arc && rim, "knob arc and dial rim should render")) return 1;
  if (require(lv_obj_get_style_bg_opa(arc, LV_PART_KNOB) == LV_OPA_TRANSP,
              "native arc knob should be transparent")) return 1;
  if (require(!lv_obj_has_flag(rim, LV_OBJ_FLAG_SCROLLABLE),
              "dial rim should not create scrollbars")) return 1;
  if (require(lv_obj_get_width(pointerLayer) == 56 && lv_obj_get_height(pointerLayer) == 56,
              "custom pointer should sit inside a dial-sized layer")) return 1;
  if (require(lv_obj_get_style_transform_rotation(pointerLayer, LV_PART_MAIN) == 0,
              "pointer layer should remain stationary")) return 1;
  const lv_point_precise_t* pointerPoints = lv_line_get_points(pointer);
  if (require(lv_obj_get_style_pad_left(pointerLayer, LV_PART_MAIN) == 0
                && lv_obj_get_style_pad_right(pointerLayer, LV_PART_MAIN) == 0
                && lv_obj_get_style_pad_top(pointerLayer, LV_PART_MAIN) == 0
                && lv_obj_get_style_pad_bottom(pointerLayer, LV_PART_MAIN) == 0,
              "pointer layer should not offset or clip its line with inherited padding")) return 1;
  if (require(pointerPoints && pointerPoints[0].x == 28 && pointerPoints[0].y == 28,
              "custom pointer should start at the dial centre")) return 1;
  lv_area_t arcArea{};
  lv_area_t rimArea{};
  lv_area_t pointerLayerArea{};
  lv_obj_get_coords(arc, &arcArea);
  lv_obj_get_coords(rim, &rimArea);
  lv_obj_get_coords(pointerLayer, &pointerLayerArea);
  if (require(arcArea.x1 + arcArea.x2 == rimArea.x1 + rimArea.x2
                && arcArea.y1 + arcArea.y2 == rimArea.y1 + rimArea.y2,
              "knob arc and dial should share one centre")) return 1;
  if (require(pointerLayerArea.x1 + pointerLayerArea.x2 == rimArea.x1 + rimArea.x2
                && pointerLayerArea.y1 + pointerLayerArea.y2 == rimArea.y1 + rimArea.y2,
              "pointer layer should be laid out at the dial centre")) return 1;
  lv_obj_t* dialCentre = lv_obj_get_child(rim, 0);
  if (require(dialCentre && lv_obj_get_width(dialCentre) == 40
                && lv_obj_get_height(dialCentre) == 40,
              "black rim should be three pixels thick")) return 1;
  if (require((lv_obj_get_width(arc) / 2 - lv_obj_get_style_arc_width(arc, LV_PART_INDICATOR))
                  - lv_obj_get_width(rim) / 2 == 4,
              "gap between black rim and green arc should be four pixels")) return 1;

  lv_obj_t* depthLabel = findLabel(lv_screen_active(), depth->label.c_str());
  lv_obj_t* depthArc = findObjectOfClass(lv_obj_get_parent(depthLabel), &lv_arc_class);
  lv_area_t depthKnobArea{};
  lv_obj_get_coords(lv_obj_get_parent(depthLabel), &depthKnobArea);
  SimulatedPointer simulatedPointer{{(depthKnobArea.x1 + depthKnobArea.x2) / 2,
                                     (depthKnobArea.y1 + depthKnobArea.y2) / 2},
                                    LV_INDEV_STATE_PRESSED};
  lv_indev_t* simulatedInput = lv_indev_create();
  lv_indev_set_type(simulatedInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(simulatedInput, &simulatedPointer);
  lv_indev_set_read_cb(simulatedInput, readSimulatedPointer);
  lv_indev_read(simulatedInput);
  ui.refresh(lv_screen_active(), state);
  simulatedPointer.point.y -= 24;
  lv_indev_read(simulatedInput);
  if (require(state.bank.presets[state.activePreset].blocks[state.selectedBlock].params.value("depth", 0.0f)
                > depth->minimum,
              "simulator knob drag should change the focused parameter")) return 1;
  const auto updatedControls = ardor::parameterPage(state, 0);
  const auto updatedDepth = std::find_if(updatedControls.begin(), updatedControls.end(),
                                         [](const auto& control) { return control.key == "depth"; });
  if (require(lv_arc_get_value(depthArc) > 0,
              "simulator knob drag should update the arc before release")) return 1;
  pointerPoints = lv_line_get_points(pointer);
  if (require(pointerPoints && (pointerPoints[1].x != 14 || pointerPoints[1].y != 42),
              "simulator knob drag should update the needle before release")) return 1;
  if (require(updatedDepth != updatedControls.end()
                && findLabel(lv_obj_get_parent(depthLabel), updatedDepth->formatted.c_str()),
              "simulator knob drag should update the value label before release")) return 1;
  simulatedPointer.state = LV_INDEV_STATE_RELEASED;
  lv_indev_read(simulatedInput);
  ui.refresh(lv_screen_active(), state);
  lv_indev_delete(simulatedInput);
  ardor::setSelectedBlockParam(state, "depth", depth->minimum);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  previous = findLabel(lv_screen_active(), "<");
  page = findLabel(lv_screen_active(), "PAGE 1 / 1");
  next = findLabel(lv_screen_active(), ">");
  title = findLabel(lv_screen_active(), titleText.c_str());
  pointer = findKnobPointer(lv_screen_active(), depth->label.c_str());
  pointerLayer = lv_obj_get_parent(pointer);

  lv_obj_t* chain = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x000000), 1240, 276);
  if (require(chain, "signal chain should be black behind charcoal blocks")) return 1;
  lv_obj_t* firstChainBlock = lv_obj_get_child(chain, 0);
  if (require(firstChainBlock && lv_obj_get_width(firstChainBlock) == 232,
              "single-block chain should use the fixed five-slot tile width")) return 1;
  if (require(!lv_obj_has_flag(chain, LV_OBJ_FLAG_SCROLLABLE),
              "chain should not scroll while blocks are draggable")) return 1;
  if (require(findLabel(lv_screen_active(), "MODULATION"),
              "chain card should render an uppercase category")) return 1;
  if (require(findLabel(lv_screen_active(), "Vintage Trem"),
              "chain card should render its asset name")) return 1;
  if (require(!findLabel(lv_screen_active(), "mod"),
              "chain card should not render its short internal type")) return 1;

  lv_obj_t* parameterPanel = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x242424), 1240, 286);
  lv_obj_t* parameterClose = findLabel(lv_screen_active(), "X");
  lv_obj_t* bypassLabel = findLabel(lv_screen_active(), "Bypass");
  lv_obj_t* bypassSwitch = findObjectOfClass(lv_screen_active(), &lv_switch_class);
  if (require(parameterPanel && parameterClose && bypassLabel && bypassSwitch,
              "parameter panel header controls should render")) return 1;
  lv_area_t parameterPanelArea{};
  lv_area_t parameterCloseArea{};
  lv_area_t bypassLabelArea{};
  lv_area_t bypassSwitchArea{};
  lv_obj_get_coords(parameterPanel, &parameterPanelArea);
  lv_obj_get_coords(lv_obj_get_parent(parameterClose), &parameterCloseArea);
  lv_obj_get_coords(bypassLabel, &bypassLabelArea);
  lv_obj_get_coords(bypassSwitch, &bypassSwitchArea);
  if (require(parameterCloseArea.x2 == parameterPanelArea.x2 - 28,
              "parameter close button should be in the top-right corner")) return 1;
  if (require(bypassSwitchArea.x2 < parameterCloseArea.x1,
              "bypass switch should sit left of the close button")) return 1;
  if (require(bypassLabelArea.x2 < bypassSwitchArea.x1,
              "bypass label should sit left of its switch")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(bypassSwitch, LV_PART_INDICATOR),
                          lv_color_hex(0x43f05a)),
              "checked bypass switch should use acid green")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(parameterClose), LV_PART_MAIN),
                          lv_color_hex(0x000000)),
              "parameter close button should have a black background")) return 1;

  lv_area_t pageArea{};
  lv_area_t titleArea{};
  lv_obj_get_coords(page, &pageArea);
  lv_obj_get_coords(title, &titleArea);
  if (require(pageArea.x2 < titleArea.x1,
              "parameter page status and title should not overlap")) return 1;
  pointerPoints = lv_line_get_points(pointer);
  if (require(pointerPoints && pointerPoints[0].x == 28 && pointerPoints[0].y == 28,
              "knob pointer should start at the dial centre")) return 1;
  if (require(pointerPoints && pointerPoints[1].x == 14 && pointerPoints[1].y == 42,
              "minimum knob value should point to the start of the arc")) return 1;

  ui.focusParameter(depth->key);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* focusedLabel = findLabel(lv_screen_active(), depth->label.c_str());
  if (require(focusedLabel && lv_color_eq(lv_obj_get_style_text_color(focusedLabel, LV_PART_MAIN), lv_color_hex(0x43f05a)),
              "focused knob label should use acid green")) return 1;

  pointer = findKnobPointer(lv_screen_active(), depth->label.c_str());
  pointerPoints = lv_line_get_points(pointer);
  const lv_point_precise_t minimumPointerEnd = pointerPoints[1];
  if (require(ui.applyFocusedParameterDelta(state, 1), "focused encoder adjustment should be consumed")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  pointer = findKnobPointer(lv_screen_active(), depth->label.c_str());
  pointerPoints = pointer ? lv_line_get_points(pointer) : nullptr;
  if (require(pointerPoints && (pointerPoints[1].x != minimumPointerEnd.x
                                || pointerPoints[1].y != minimumPointerEnd.y),
              "focused encoder adjustment should rebuild the knob pointer")) return 1;

  ardor::setSelectedBlockParam(state, "depth", depth->maximum);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  pointer = findKnobPointer(lv_screen_active(), depth->label.c_str());
  pointerPoints = pointer ? lv_line_get_points(pointer) : nullptr;
  if (require(pointerPoints && pointerPoints[0].x == 28 && pointerPoints[0].y == 28
                && pointerPoints[1].x == 42 && pointerPoints[1].y == 42,
              "maximum knob value should point to the end of the arc")) return 1;

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
  if (require(findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x43f05a), 4),
              "active preset should have a thin acid-green indicator")) return 1;

  ardor::enterEditMode(state);
  auto& chainBlocks = state.bank.presets[state.activePreset].blocks;
  while (chainBlocks.size() < 6) {
    chainBlocks.push_back(chainBlocks.front());
  }
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  chain = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x000000), 1240, 276);
  lv_obj_t* sixthChainBlock = chain ? lv_obj_get_child(chain, 5) : nullptr;
  lv_area_t chainArea{};
  lv_area_t sixthChainBlockArea{};
  if (chain) lv_obj_get_coords(chain, &chainArea);
  if (sixthChainBlock) lv_obj_get_coords(sixthChainBlock, &sixthChainBlockArea);
  if (require(sixthChainBlock && sixthChainBlockArea.y1 > chainArea.y1,
              "sixth chain tile should begin on the second row")) return 1;
  lv_obj_t* chainWrapConnector = findLineWithPointCount(lv_screen_active(), 20);
  const lv_point_precise_t* chainWrapPoints = chainWrapConnector ? lv_line_get_points(chainWrapConnector) : nullptr;
  if (require(chainWrapPoints && chainWrapPoints[0].x == 1233 && chainWrapPoints[0].y == 173
                && chainWrapPoints[1].x > chainWrapPoints[0].x && chainWrapPoints[1].y > chainWrapPoints[0].y
                && chainWrapPoints[19].x == 34 && chainWrapPoints[19].y == 313,
              "chain should smoothly wrap from the first row into the second")) return 1;

  constexpr std::size_t blockCount = 10;
  if (require(ardor::LvglUi::chainSlotForPoint(blockCount, {34, 127}) == 0,
              "chain's top-left slot should map to the first block")) return 1;
  if (require(ardor::LvglUi::chainSlotForPoint(blockCount, {34, 277}) == 5,
              "chain's second row should map to the sixth block")) return 1;
  if (require(ardor::LvglUi::chainInsertionSlotForPoint(blockCount, {34 + 242 * 4, 277}) == 9,
              "chain insertion should use the second row's column")) return 1;
  const auto secondRowIndicator = ardor::LvglUi::chainIndicatorPosition(blockCount, 5);
  if (require(secondRowIndicator.x == 34 && secondRowIndicator.y == 267,
              "sixth-slot insertion indicator should be placed on the second row")) return 1;

  ardor::enterEditMode(state);
  ardor::openBlockDrawer(state);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* allFilter = findLabel(lv_screen_active(), "All");
  lv_obj_t* timeFilter = findLabel(lv_screen_active(), "Time");
  lv_obj_t* tremAssetLabel = findLabel(lv_screen_active(), "Vintage Trem");
  lv_obj_t* drawer = findObjectWithBgColor(lv_screen_active(), lv_color_hex(0x000000), 360);
  if (require(drawer && allFilter && timeFilter && tremAssetLabel, "block drawer content should render")) return 1;
  lv_obj_t* allFilterButton = lv_obj_get_parent(allFilter);
  lv_obj_t* timeFilterButton = lv_obj_get_parent(timeFilter);
  lv_obj_t* tremAssetButton = lv_obj_get_parent(tremAssetLabel);
  lv_area_t drawerArea{};
  lv_obj_get_coords(drawer, &drawerArea);
  if (require(lv_obj_get_width(drawer) == 360 && lv_obj_get_height(drawer) == 720 && drawerArea.x2 == 1279,
              "block drawer should fill the right display edge")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(drawer, LV_PART_MAIN), lv_color_hex(0x000000)),
              "block drawer should be black")) return 1;
  if (require(lv_obj_get_y(allFilterButton) == lv_obj_get_y(timeFilterButton),
              "drawer filters should remain in one horizontal row")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(tremAssetButton, LV_PART_MAIN), lv_color_hex(0x242424)),
              "drawer asset tiles should be charcoal")) return 1;

  ardor::closeBlockDrawer(state);
  auto eqRenderAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Five Band EQ";
  });
  if (require(eqRenderAsset != state.assets.end(), "EQ asset should be available to the LVGL editor")) return 1;
  ardor::appendAssetBlock(state, static_cast<std::size_t>(std::distance(state.assets.begin(), eqRenderAsset)));
  ui.selectBlock(state, state.bank.presets[state.activePreset].blocks.size() - 1);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x242424), 1240, 600),
              "EQ should open as the main editor surface")) return 1;
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x111111), 1184, 270),
              "EQ main editor should reserve a tall response graph")) return 1;
  if (require(findLabel(lv_screen_active(), "Parametric EQ"), "EQ should render its dedicated editor title")) return 1;
  if (require(findLabel(lv_screen_active(), "Band 1"), "EQ should render its selected-band strip")) return 1;
  if (require(findLabel(lv_screen_active(), "Reset Band"), "EQ should render a reset-band control")) return 1;
  lv_obj_t* frequencyLabel = findLabel(lv_screen_active(), "Frequency");
  lv_obj_t* qLabel = findLabel(lv_screen_active(), "Q");
  lv_obj_t* gainLabel = findLabel(lv_screen_active(), "Gain");
  if (require(frequencyLabel && qLabel && gainLabel,
              "EQ should render frequency, Q, and gain as dedicated knobs")) return 1;
  lv_obj_t* qKnob = lv_obj_get_parent(qLabel);
  lv_obj_t* qArc = findObjectOfClass(qKnob, &lv_arc_class);
  if (require(qArc && findKnobPointer(qKnob), "EQ controls should use the regular knob visuals")) return 1;
  const float qBeforeDrag = ardor::selectedParametricEqParams(state).bands[0].q;
  lv_area_t qKnobArea{};
  lv_obj_get_coords(qKnob, &qKnobArea);
  SimulatedPointer eqPointer{{(qKnobArea.x1 + qKnobArea.x2) / 2,
                              (qKnobArea.y1 + qKnobArea.y2) / 2},
                             LV_INDEV_STATE_PRESSED};
  lv_indev_t* eqInput = lv_indev_create();
  lv_indev_set_type(eqInput, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(eqInput, &eqPointer);
  lv_indev_set_read_cb(eqInput, readSimulatedPointer);
  lv_indev_read(eqInput);
  eqPointer.point.y -= 24;
  lv_indev_read(eqInput);
  if (require(ardor::selectedParametricEqParams(state).bands[0].q > qBeforeDrag,
              "EQ Q knob drag should update the selected band's Q factor")) return 1;
  if (require(lv_arc_get_value(qArc) > 0, "EQ Q knob drag should update its arc before release")) return 1;
  eqPointer.state = LV_INDEV_STATE_RELEASED;
  lv_indev_read(eqInput);
  ui.refresh(lv_screen_active(), state);
  lv_indev_delete(eqInput);
  lv_obj_t* deleteBlockLabel = findLabel(lv_screen_active(), "Delete Block");
  if (require(deleteBlockLabel, "EQ should render a delete-block control")) return 1;
  if (require(findLineWithPointCount(lv_screen_active(), ardor::kEqCurvePointCount),
              "EQ should render a sampled response curve")) return 1;
  const auto blocksBeforeDelete = state.bank.presets[state.activePreset].blocks.size();
  lv_obj_send_event(lv_obj_get_parent(deleteBlockLabel), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(state.bank.presets[state.activePreset].blocks.size() == blocksBeforeDelete - 1,
              "delete-block control should remove the selected EQ block")) return 1;

  lv_display_delete(display);
  lv_deinit();

  return 0;
}
