#include "ui/LvglUi.h"
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
  if (lv_obj_get_width(parent) == 3 && lv_obj_get_height(parent) == 20) {
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

  if (require(ardor::parameterPage(state, 0).size() <= 6, "page must contain <= six knobs")) return 1;
  if (require(ardor::parameterPageCount(state) == 2, "seven params require two pages")) return 1;

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
  lv_obj_t* page = findLabel(lv_screen_active(), "PAGE 1 / 2");
  lv_obj_t* next = findLabel(lv_screen_active(), ">");
  lv_obj_t* title = findLabel(lv_screen_active(), titleText.c_str());
  lv_obj_t* pointer = findKnobPointer(lv_screen_active(), depth->label.c_str());
  if (require(previous && next && title && pointer, "parameter header and knob pointer should render")) return 1;
  if (require(page, "parameter header should show PAGE n/total")) return 1;

  lv_obj_t* chain = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x000000), 1240, 126);
  if (require(chain, "signal chain should be black behind charcoal blocks")) return 1;

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

  lv_area_t previousArea{};
  lv_area_t pageArea{};
  lv_area_t nextArea{};
  lv_area_t titleArea{};
  lv_obj_get_coords(lv_obj_get_parent(previous), &previousArea);
  lv_obj_get_coords(page, &pageArea);
  lv_obj_get_coords(lv_obj_get_parent(next), &nextArea);
  lv_obj_get_coords(title, &titleArea);
  if (require(previousArea.x2 < pageArea.x1 && pageArea.x2 < nextArea.x1 && nextArea.x2 < titleArea.x1,
              "parameter header controls and title should not overlap")) return 1;
  if (require(lv_obj_get_style_transform_pivot_x(pointer, LV_PART_MAIN) == 1
                && lv_obj_get_style_transform_pivot_y(pointer, LV_PART_MAIN) == 21,
              "knob pointer should rotate from its radial base")) return 1;
  if (require(lv_obj_get_x(pointer) + 1 == lv_obj_get_width(lv_obj_get_parent(pointer)) / 2
                && lv_obj_get_y(pointer) + 21 == lv_obj_get_height(lv_obj_get_parent(pointer)) / 2,
              "knob pointer base should be at the rim centre")) return 1;
  if (require(lv_obj_get_style_transform_rotation(pointer, LV_PART_MAIN) == 450,
              "minimum knob value should point to the start of the arc")) return 1;

  ui.focusParameter(depth->key);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* focusedLabel = findLabel(lv_screen_active(), depth->label.c_str());
  if (require(focusedLabel && lv_color_eq(lv_obj_get_style_text_color(focusedLabel, LV_PART_MAIN), lv_color_hex(0x43f05a)),
              "focused knob label should use acid green")) return 1;

  pointer = findKnobPointer(lv_screen_active(), depth->label.c_str());
  const auto minimumRotation = lv_obj_get_style_transform_rotation(pointer, LV_PART_MAIN);
  if (require(ui.applyFocusedParameterDelta(state, 1), "focused encoder adjustment should be consumed")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  pointer = findKnobPointer(lv_screen_active(), depth->label.c_str());
  if (require(pointer && lv_obj_get_style_transform_rotation(pointer, LV_PART_MAIN) > minimumRotation,
              "focused encoder adjustment should rebuild the knob pointer")) return 1;

  ardor::setSelectedBlockParam(state, "depth", depth->maximum);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  pointer = findKnobPointer(lv_screen_active(), depth->label.c_str());
  if (require(pointer && lv_obj_get_style_transform_rotation(pointer, LV_PART_MAIN) == 3150,
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

  constexpr std::size_t largeBlockCount = 10000;
  if (require(ardor::LvglUi::chainSlotForX(largeBlockCount, 34) == 0,
              "large chains should map their left edge to the first block")) return 1;
  if (require(ardor::LvglUi::chainSlotForX(largeBlockCount, 1260) == largeBlockCount - 1,
              "large chains should map their right edge to the final block")) return 1;
  if (require(ardor::LvglUi::chainInsertionSlotForX(largeBlockCount, 1260) == largeBlockCount,
              "large chains should allow insertion after the final block")) return 1;
  if (require(ardor::LvglUi::chainIndicatorX(largeBlockCount, largeBlockCount) <= 1246,
              "large chain indicator should remain within the chain")) return 1;

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

  lv_display_delete(display);
  lv_deinit();

  return 0;
}
