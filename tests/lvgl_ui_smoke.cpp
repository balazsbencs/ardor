#include "ui/LvglUi.h"

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

  const auto tremAsset = std::find_if(state.assets.begin(), state.assets.end(), [](const ardor::UiAsset& asset) {
    return asset.name == "Vintage Trem";
  });
  if (require(tremAsset != state.assets.end(), "Vintage Trem should be available")) return 1;
  ardor::appendAssetBlock(state, static_cast<std::size_t>(std::distance(state.assets.begin(), tremAsset)));
  ardor::selectBlock(state, state.bank.presets[state.activePreset].blocks.size() - 1);

  ardor::setSelectedBlockParam(state, "depth", 2.0f);
  if (require(state.bank.presets[state.activePreset].blocks[state.selectedBlock].params.value("depth", 0.0f) == 1.0f,
              "Daisy setter should enforce descriptor range")) return 1;

  if (require(ardor::parameterPage(state, 0).size() <= 6, "page must contain <= six knobs")) return 1;
  if (require(ardor::parameterPageCount(state) == 2, "seven params require two pages")) return 1;

  lv_init();
  lv_display_t* display = lv_display_create(1280, 720);
  ardor::enterEditMode(state);
  const auto tremControls = ardor::parameterPage(state, 0);
  const auto depth = std::find_if(tremControls.begin(), tremControls.end(),
                                  [](const auto& control) { return control.key == "depth"; });
  if (require(depth != tremControls.end(), "Vintage Trem depth control should be available")) return 1;
  ardor::setSelectedBlockParam(state, "depth", depth->minimum);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());

  const auto& selected = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
  const std::string titleText = selected.label + "  /  " + selected.assetName;
  lv_obj_t* previous = findLabel(lv_screen_active(), "<");
  lv_obj_t* page = findLabel(lv_screen_active(), "PAGE 1/2");
  lv_obj_t* next = findLabel(lv_screen_active(), ">");
  lv_obj_t* title = findLabel(lv_screen_active(), titleText.c_str());
  lv_obj_t* pointer = findKnobPointer(lv_screen_active(), depth->label.c_str());
  if (require(previous && next && title && pointer, "parameter header and knob pointer should render")) return 1;
  if (require(page, "parameter header should show PAGE n/total")) return 1;

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

  ardor::setSelectedBlockParam(state, "depth", depth->maximum);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  pointer = findKnobPointer(lv_screen_active(), depth->label.c_str());
  if (require(pointer && lv_obj_get_style_transform_rotation(pointer, LV_PART_MAIN) == 3150,
              "maximum knob value should point to the end of the arc")) return 1;

  lv_display_delete(display);
  lv_deinit();

  return 0;
}
