#include "ui/LvglUi.h"
#include "ui/LvglUiStyle.h"
#include "ui/fonts/LampBlackFonts.h"
#include "ui/PresetChainStrip.h"
#include "ui/EqEditorModel.h"
#include "ui/fonts/SairaCondSemibold52.h"
#include "ui/fonts/SairaCondSemibold72.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
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

// A slider-card legend: the label sits on a 385 px parameter card. The chain
// cards and the context rail can repeat the same legend.
// A control card's travel scale is its only 30 px tall child; the fill sits
// after the 11 ticks and the track.
lv_obj_t* findTravelFill(lv_obj_t* card)
{
  for (uint32_t i = 0; i < lv_obj_get_child_count(card); ++i) {
    lv_obj_t* child = lv_obj_get_child(card, static_cast<int32_t>(i));
    if (lv_obj_get_height(child) == 30 && lv_obj_get_child_count(child) == 14) {
      return lv_obj_get_child(child, 12);
    }
  }
  return nullptr;
}

lv_obj_t* findSliderLabel(lv_obj_t* parent, const char* text)
{
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    lv_obj_t* child = lv_obj_get_child(parent, static_cast<int32_t>(i));
    if (lv_obj_check_type(child, &lv_label_class) && std::strcmp(lv_label_get_text(child), text) == 0
        && lv_obj_get_width(parent) == 403 && !lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_t* layer = parent;
      bool visible = true;
      while ((layer = lv_obj_get_parent(layer))) visible = visible && !lv_obj_has_flag(layer, LV_OBJ_FLAG_HIDDEN);
      if (visible) return child;
    }
    if (lv_obj_t* found = findSliderLabel(child, text)) return found;
  }
  return nullptr;
}

// A full-height chain card (296 px) whose asset label reads `asset`. The
// module drawer and the parameter title can repeat the same name.
// A lane card's token sits in its 52 px drag header. Preset tiles print the
// same codes in their chain strips, so match on the header too.
lv_obj_t* findLaneToken(lv_obj_t* parent, const char* token)
{
  if (lv_obj_check_type(parent, &lv_label_class) && std::strcmp(lv_label_get_text(parent), token) == 0
      && lv_obj_get_height(lv_obj_get_parent(parent)) == 52) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* found = findLaneToken(lv_obj_get_child(parent, static_cast<int32_t>(i)), token)) {
      return found;
    }
  }
  return nullptr;
}

lv_obj_t* findChainCard(lv_obj_t* parent, const std::string& asset)
{
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    lv_obj_t* child = lv_obj_get_child(parent, static_cast<int32_t>(i));
    if (lv_obj_check_type(child, &lv_label_class) && asset == lv_label_get_text(child)
        && lv_obj_get_height(parent) == 296) {
      return parent;
    }
    if (lv_obj_t* found = findChainCard(child, asset)) return found;
  }
  return nullptr;
}

// Last match in tree order. Parameter drawers are created after the chain,
// so this finds a drawer control even when a chain-card summary repeats its
// legend ("DEPTH" on both).
lv_obj_t* findLastLabel(lv_obj_t* parent, const char* text)
{
  for (uint32_t i = lv_obj_get_child_count(parent); i > 0; --i) {
    if (auto* result = findLastLabel(lv_obj_get_child(parent, static_cast<int32_t>(i - 1)), text)) {
      return result;
    }
  }
  if (lv_obj_check_type(parent, &lv_label_class) && std::strcmp(lv_label_get_text(parent), text) == 0) {
    return parent;
  }
  return nullptr;
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

// A control card's scope tag is a 24 px ruled tag holding its legend.
lv_obj_t* findLabelWithParentHeight(lv_obj_t* parent, const char* text, int height)
{
  if (lv_obj_check_type(parent, &lv_label_class) && std::strcmp(lv_label_get_text(parent), text) == 0
      && lv_obj_get_height(lv_obj_get_parent(parent)) == height) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* found = findLabelWithParentHeight(lv_obj_get_child(parent, static_cast<int32_t>(i)),
                                                text, height)) {
      return found;
    }
  }
  return nullptr;
}

lv_obj_t* findLabelWithParentWidth(lv_obj_t* parent, const char* text, int width)
{
  if (lv_obj_check_type(parent, &lv_label_class)
      && std::strcmp(lv_label_get_text(parent), text) == 0
      && lv_obj_get_parent(parent) && lv_obj_get_width(lv_obj_get_parent(parent)) == width)
    return parent;
  for (uint32_t index = 0; index < lv_obj_get_child_count(parent); ++index) {
    if (auto* found = findLabelWithParentWidth(lv_obj_get_child(parent, index), text, width))
      return found;
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
  if (lv_obj_get_height(parent) == 4 && area.x2 == x
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

  ardor::Preset wdwPreset;
  wdwPreset.version = 3;
  wdwPreset.routing = "wdw";
  wdwPreset.name = "Touch Wet Dry Wet";
  wdwPreset.wdw = ardor::WdwRouting{};
  wdwPreset.wdw->dry.levelDb = -2.0f;
  wdwPreset.wdw->dry.pan = -0.35f;
  wdwPreset.wdw->wet.levelDb = -4.0f;
  wdwPreset.wdw->wet.width = 0.8f;
  wdwPreset.wdw->dry.blocks.push_back({"wdw-dry-nam", "nam", true,
                                       "models/clean.nam", nlohmann::json::object()});
  wdwPreset.wdw->dry.blocks.push_back({"wdw-dry-cab", "cab", true,
                                       "irs/open-back.wav", nlohmann::json::object()});
  wdwPreset.wdw->wet.blocks.push_back({"wdw-wet-nam", "nam", true,
                                       "models/crunch.nam", nlohmann::json::object()});
  wdwPreset.wdw->wet.blocks.push_back({"wdw-wet-cab", "cab", true,
                                       "irs/vintage.wav", nlohmann::json::object()});
  wdwPreset.wdw->wet.blocks.push_back({"wdw-wet-delay", "delay", true, "",
                                       {{"mode", "digital"}}});
  auto wdwState = ardor::makeDemoUiState();
  ardor::replaceActivePreset(wdwState, wdwPreset);
  if (require(wdwState.bank.presets[wdwState.activePreset].routing == "wdw"
                && wdwState.bank.presets[wdwState.activePreset].blocks[0].assetName
                  == "Dry 2 blocks  /  Wet 3 blocks",
              "touchscreen should retain the WDW route and lane summary")) return 1;
  const auto wdwControls = ardor::parameterPage(wdwState, 0);
  if (require(wdwControls.size() == 6 && wdwControls[2].key == "dryPan"
                && wdwControls[5].key == "wetWidth",
              "touchscreen should expose WDW lane mix controls")) return 1;
  ardor::setSelectedBlockParam(wdwState, "dryPan", 1.4f);
  if (ardor::pendingStructuralPreview(wdwState)) ardor::completeStructuralPreview(wdwState);
  ardor::setSelectedBlockParamValue(wdwState, "wetEnabled", false);
  const auto preservedWdw = ardor::activePresetToPreset(wdwState);
  if (require(preservedWdw.version == 3 && preservedWdw.routing == "wdw" && preservedWdw.wdw
                && preservedWdw.wdw->dry.blocks[1].id == "wdw-dry-cab"
                && preservedWdw.wdw->dry.pan == 1.0f
                && !preservedWdw.wdw->wet.enabled,
              "touchscreen load/save must preserve and edit WDW lane state")) return 1;
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
  std::size_t requestedScene = 99;
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
  uiActions.saveControlInputSettings = [](const ardor::DeviceSettings&, std::string&) {
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
  uiActions.readUpdateStatus = [](ardor::DeviceUpdateStatus& status, std::string&) {
    status.enabled = true;
    status.installedVersion = "1.2.3";
    status.baseVersion = "1.0.0";
    return true;
  };
  uiActions.checkForUpdate = [](ardor::DeviceUpdateStatus& status, std::string&) {
    status.state = "available";
    status.availableVersion = "1.3.0";
    return true;
  };
  uiActions.installUpdate = [](const std::string&, ardor::DeviceUpdateStatus& status, std::string&) {
    status.state = "staged";
    return true;
  };
  uiActions.selectScene = [&](std::size_t scene) { requestedScene = scene; };
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
  if (require(ardor::lvgl_ui::palette().plate == 0x0b0c0d
                && ardor::lvgl_ui::palette().plate2 == 0x16181a
                && ardor::lvgl_ui::palette().plate3 == 0x121416
                && ardor::lvgl_ui::palette().lamp == 0xe8472f
                && ardor::lvgl_ui::categoryColor("delay") == 0x9a82d6,
              "Slate should carry the Lamp Black ground, plates, lamp, and family tokens")) return 1;
  if (require(ardor::lvgl_ui::categoryColor("irreverb") == ardor::lvgl_ui::categoryColor("reverb")
                && ardor::lvgl_ui::categoryColor("wah") == ardor::lvgl_ui::categoryColor("modulation")
                && ardor::lvgl_ui::categoryColor("dualAmp") == ardor::lvgl_ui::categoryColor("amp")
                && ardor::lvgl_ui::categoryColor("time") == ardor::lvgl_ui::categoryColor("delay")
                && ardor::lvgl_ui::categoryColor("stereo") == ardor::lvgl_ui::categoryColor("utility"),
              "every block type should map to a family colour for the chain strip")) return 1;
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
  const char* editScreenshotPath = std::getenv("ARDOR_UI_EDIT_SCREENSHOT");
  const char* scenesScreenshotPath = std::getenv("ARDOR_UI_SCENES_SCREENSHOT");
  const char* sceneEditorScreenshotPath = std::getenv("ARDOR_UI_SCENE_EDITOR_SCREENSHOT");
  std::vector<uint8_t> screenshotStorage;
  uint8_t* screenshotPixels = nullptr;
  uint32_t screenshotStride = 0;
  if (screenshotPath != nullptr || editScreenshotPath != nullptr
      || scenesScreenshotPath != nullptr || sceneEditorScreenshotPath != nullptr) {
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
  lv_obj_t* leftOnlyEffect = findLaneToken(lv_screen_active(), "CHO");
  lv_obj_t* rightOnlyEffect = findLaneToken(lv_screen_active(), "DLY");
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
    lv_screen_active(), lv_color_hex(ardor::lvgl_ui::bg), 1280, 548);
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
                && findLastLabel(lv_screen_active(), "DIGITAL DELAY")
                && findLastLabel(lv_screen_active(), "DELAY"),
              "clicking a Dual Rig lane effect should open that effect's parameter drawer")) return 1;

  ardor::enterEditMode(wdwState);
  ui.build(lv_screen_active(), wdwState);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* dryWdwHeading = findLabel(lv_screen_active(), "DRY");
  lv_obj_t* wetWdwHeading = findLabel(lv_screen_active(), "WET");
  lv_obj_t* dryWdwAsset = findLabel(lv_screen_active(), "CLEAN TWIN");
  lv_obj_t* wetWdwEffect = findLaneToken(lv_screen_active(), "DLY");
  lv_area_t dryWdwHeadingArea{};
  lv_area_t wetWdwHeadingArea{};
  lv_area_t dryWdwTileArea{};
  lv_area_t wetWdwTileArea{};
  if (dryWdwHeading) lv_obj_get_coords(dryWdwHeading, &dryWdwHeadingArea);
  if (wetWdwHeading) lv_obj_get_coords(wetWdwHeading, &wetWdwHeadingArea);
  if (dryWdwAsset) {
    lv_obj_get_coords(lv_obj_get_parent(dryWdwAsset), &dryWdwTileArea);
  }
  if (wetWdwEffect) {
    lv_obj_get_coords(lv_obj_get_parent(lv_obj_get_parent(wetWdwEffect)), &wetWdwTileArea);
  }
  if (require(findLabel(lv_screen_active(), "WDW")
                && findLabel(lv_screen_active(), "NO DIRECT INPUT")
                && dryWdwHeading && wetWdwHeading
                && findLabelContaining(lv_screen_active(), "LEVEL -2 DB")
                && findLabelContaining(lv_screen_active(), "WIDTH 80%"),
              "WDW should render explicit dry/wet lanes, mix summaries, and no direct path")) return 1;
  if (require(dryWdwAsset && wetWdwEffect
                && dryWdwHeadingArea.y2 < dryWdwTileArea.y1
                && wetWdwHeadingArea.y1 > wetWdwTileArea.y2,
              "WDW lane captions should flank their effect chains without covering the junction")) return 1;

  auto wdwDeleteUiState = ardor::makeDemoUiState();
  ardor::replaceActivePreset(wdwDeleteUiState, wdwPreset);
  ardor::enterEditMode(wdwDeleteUiState);
  ui.build(lv_screen_active(), wdwDeleteUiState);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* dryNamAsset = findLabel(lv_screen_active(), "CLEAN TWIN");
  if (require(dryNamAsset, "WDW dry NAM should be selectable in the edit chain")) return 1;
  lv_obj_send_event(lv_obj_get_parent(dryNamAsset), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), wdwDeleteUiState);
  lv_obj_t* wdwDeleteLabel = findLastLabel(lv_screen_active(), "DELETE");
  if (require(wdwDeleteLabel && ardor::selectedBlockIsLaneChild(wdwDeleteUiState),
              "selecting a WDW lane NAM should open its block editor")) return 1;
  lv_obj_send_event(lv_obj_get_parent(wdwDeleteLabel), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), wdwDeleteUiState);
  if (require(wdwDeleteUiState.bank.presets[wdwDeleteUiState.activePreset]
                  .blocks[0].lanes[0].size() == 1
                && wdwDeleteUiState.bank.presets[wdwDeleteUiState.activePreset]
                  .blocks[0].lanes[0][0].type == "cab",
              "WDW Delete Block should remove a selected NAM from its lane")) return 1;

  state.bank.presets[state.activePreset].sceneSet = ardor::PresetSceneSet{};
  state.bank.presets[state.activePreset].sceneSet->defaultSceneId = "scene-1";
  const std::array sceneNames = {"Verse", "Chorus", "Solo", "Outro"};
  for (std::size_t index = 0; index < sceneNames.size(); ++index) {
    auto& scene = state.bank.presets[state.activePreset].sceneSet->scenes[index];
    scene.id = "scene-" + std::to_string(index + 1);
    scene.name = sceneNames[index];
    scene.enterTimeMs = index == 1 ? 500 : 0;
    scene.outputTrimDb = index == 2 ? 2.0f : 0.0f;
  }
  state.dirty = true;
  ardor::enterScenesMode(state);
  ardor::updateSceneTelemetry(state, {0, 2, 0.5f, false, true, false, false, {}});
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "SCENES")
                && findLabel(lv_screen_active(), "VERSE")
                && findLabel(lv_screen_active(), "FS 3  ·  GOING TO")
                && findLabel(lv_screen_active(), "+2.0 DB  ·  INSTANT")
                && findLabel(lv_screen_active(), "UNSAVED")
                && findLabel(lv_screen_active(), "PRESETS"),
              "Scenes mode should render four physical-map plates and its control rail")) return 1;
  if (scenesScreenshotPath != nullptr) {
    lv_refr_now(display);
    if (require(saveRgb888Ppm(scenesScreenshotPath, screenshotPixels, screenshotStride, 1280, 720),
                "Scenes screenshot should be writable")) return 1;
  }
  ardor::updateSceneTelemetry(state, {2, 2, 1.0f, false, false, true, true, {}});
  ui.refresh(lv_screen_active(), state);
  if (require(findLabel(lv_screen_active(), "FS 3  ·  LIVE  ·  PEDAL"),
              "Scenes mode should distinguish a pedal-altered live scene")) return 1;

  ardor::enterEditMode(state);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* sceneTwoTab = findLabel(lv_screen_active(), "FS2  CHORUS");
  if (require(findLabel(lv_screen_active(), "EDIT") && sceneTwoTab
                && findLabel(lv_screen_active(), "SCENE SETTINGS"),
              "scene-enabled presets should add an explicit scene strip to the editor")) return 1;
  lv_obj_send_event(lv_obj_get_parent(sceneTwoTab), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(state.editingScene == 1 && requestedScene == 1,
              "choosing an edit tab should select and recall that draft scene")) return 1;
  lv_obj_update_layout(lv_screen_active());
  {
    int checkedCards = 0;
    bool sceneValuesShown = true;
    for (const auto& block : state.bank.presets[state.activePreset].blocks) {
      const auto summary = ardor::blockSummaryControls(state, block, 2);
      if (!block.enabled || block.type == "dualRig" || summary.empty()) continue;
      lv_obj_t* card = findChainCard(lv_screen_active(), upper(block.assetName));
      sceneValuesShown = sceneValuesShown && card
        && findLabel(card, summary[0].formatted.c_str());
      ++checkedCards;
    }
    if (require(checkedCards > 0 && sceneValuesShown,
                "chain cards should show the values of the scene being edited")) return 1;
  }
  if (require(findLabelContaining(lv_screen_active(), "SCENE 2"),
              "the edit header should identify the selected scene")) return 1;
  lv_obj_t* sceneSettingsLabel = findLabel(lv_screen_active(), "SCENE SETTINGS");
  lv_obj_send_event(lv_obj_get_parent(sceneSettingsLabel), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.sceneSettingsOpen
                && findLabel(lv_screen_active(), "ENTER TIME")
                && findLabel(lv_screen_active(), "SCENE TRIM")
                && findLabel(lv_screen_active(), "DEFAULT ON PRESET LOAD")
                && findLabel(lv_screen_active(), "COPY TO")
                && findLabel(lv_screen_active(), "MORE  ·  CAPTURE")
                && findLabel(lv_screen_active(), "MORE  ·  DISABLE")
                && findLabel(lv_screen_active(), "PRESET SETTINGS  ·  OPEN IN"),
              "scene settings should expose authored timing, trim, default, copy, swap, and Open in")) return 1;
  if (sceneEditorScreenshotPath != nullptr) {
    lv_refr_now(display);
    if (require(saveRgb888Ppm(sceneEditorScreenshotPath, screenshotPixels, screenshotStride,
                              1280, 720),
                "scene editor screenshot should be writable")) return 1;
  }
  lv_obj_t* instantButton = findLastLabel(lv_screen_active(), "INSTANT");
  if (require(instantButton, "scene settings should offer instant transitions")) return 1;
  lv_obj_send_event(lv_obj_get_parent(instantButton), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(state.bank.presets[state.activePreset].sceneSet->scenes[1].enterTimeMs == 0,
              "scene timing controls should update the authored draft")) return 1;
  lv_obj_t* copyVerse = findLabel(lv_screen_active(), "1 VERSE");
  if (require(copyVerse, "scene settings should offer named copy destinations")) return 1;
  lv_obj_send_event(lv_obj_get_parent(copyVerse), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(findLabel(lv_screen_active(), "REPLACE SCENE SOUND?")
                && findLabel(lv_screen_active(), "REPLACE")
                && findLabel(lv_screen_active(), "CANCEL"),
              "copying a scene should show an explicit overwrite confirmation")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "CANCEL")),
                    LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  ardor::closeSceneSettings(state);
  ui.selectBlock(state, state.selectedBlock);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* sharedScope = findLabelWithParentHeight(lv_screen_active(), "SHARED", 24);
  if (require(sharedScope,
              "scene-enabled parameter cards should label shared ownership")) return 1;
  if (!lv_obj_has_state(lv_obj_get_parent(sharedScope), LV_STATE_DISABLED)) {
    lv_obj_send_event(lv_obj_get_parent(sharedScope), LV_EVENT_CLICKED, nullptr);
    ui.refresh(lv_screen_active(), state);
    if (require(state.pendingPreview.has_value(),
                "the scope action should queue a prepared scene-bank update")) return 1;
    if (require(findLabel(lv_screen_active(), "THIS SCENE"),
                "the scope action should promote an eligible shared control to This scene")) return 1;
    ardor::completeStructuralPreview(state);
  }
  state.bank.presets[state.activePreset].sceneSet.reset();
  state.sceneSettingsOpen = false;
  state.dirty = false;


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
  if (editScreenshotPath != nullptr) {
    auto chainCaptureState = state;
    chainCaptureState.paramDrawerOpen = false;
    ui.build(lv_screen_active(), chainCaptureState);
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(display);
    if (require(saveRgb888Ppm(editScreenshotPath, screenshotPixels, screenshotStride, 1280, 720),
                "edit-chain screenshot should be writable")) return 1;
    ui.build(lv_screen_active(), state);
    lv_obj_update_layout(lv_screen_active());
  }

  const auto& selected = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
  const std::string titleText = upper(selected.assetName);
  lv_obj_t* previous = findLabel(lv_screen_active(), "<");
  lv_obj_t* page = findLabel(lv_screen_active(), "PAGE 1 / 2");
  lv_obj_t* next = findLabel(lv_screen_active(), ">");
  lv_obj_t* title = findLastLabel(lv_screen_active(), titleText.c_str());
  // The looper's notice line repeats the status; the toast is drawn last.
  lv_obj_t* status = findLastLabel(lv_screen_active(), "PRESET SAVED");
  lv_obj_t* undoLabel = findLabel(lv_screen_active(), "UNDO");
  lv_obj_t* depthLabel = findSliderLabel(lv_screen_active(), upper(depth->label).c_str());
  lv_obj_t* depthSlider = depthLabel ? lv_obj_get_parent(depthLabel) : nullptr;
  lv_obj_t* depthFill = depthSlider ? findTravelFill(depthSlider) : nullptr;
  if (require(title && depthSlider && depthFill, "parameter header and slider should render")) return 1;
  if (require(status && lv_color_eq(lv_obj_get_style_text_color(status, LV_PART_MAIN), lv_color_hex(ardor::lvgl_ui::text)),
              "success status should render in engraved text")) return 1;
  lv_area_t statusToastArea{};
  lv_obj_get_coords(status, &statusToastArea);
  if (require(statusToastArea.y1 >= 500 && statusToastArea.y2 < 612,
              "transient status should render as a toast just above the rail")) return 1;
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
  if (require(ui.canvas() == retainedCanvas && findLastLabel(lv_screen_active(), "TICK 255") == retainedStatus,
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
  if (require(undoLabel && lv_obj_get_width(lv_obj_get_parent(undoLabel)) == 124
                && lv_obj_get_height(lv_obj_get_parent(undoLabel)) == 60,
              "reversible block edits should expose a large Undo action")) return 1;
  if (require(page, "parameter header should show PAGE n/total")) return 1;
  if (require(previous && next, "a seventh control should enable page navigation")) return 1;
  const auto depthIndex = static_cast<int>(std::distance(renderControls.begin(), depth));
  lv_area_t sliderArea{};
  lv_area_t sliderPanelArea{};
  lv_obj_get_coords(depthSlider, &sliderArea);
  lv_obj_get_coords(lv_obj_get_parent(depthSlider), &sliderPanelArea);
  // CSS grid columns of 402.67 px land the cards on 24, 439 and 853.
  const std::array<int, 3> columnX = {24, 439, 853};
  const int expectedDepthX = columnX[static_cast<std::size_t>(depthIndex % 3)];
  const int expectedDepthY = 100 + (depthIndex / 3) * (166 + 12);
  if (require(sliderArea.x1 - sliderPanelArea.x1 == expectedDepthX
                && sliderArea.y1 - sliderPanelArea.y1 == expectedDepthY,
              "parameter sliders should use a three-column, two-row grid")) return 1;
  if (require(lv_obj_get_width(depthSlider) == 403 && lv_obj_get_height(depthSlider) == 166,
              "parameter slider should provide a large vertical touch target")) return 1;
  if (require(findObjectWithSize(depthSlider, 8, 32),
              "parameter slider should expose the travel scale's bone handle")) return 1;
  if (require(lv_obj_get_style_radius(depthSlider, LV_PART_MAIN) == 0
                && lv_obj_get_style_radius(depthFill, LV_PART_MAIN) == 0,
              "parameter slider should use flat, unrounded plate geometry")) return 1;
  if (require(lv_obj_get_width(depthFill) == 0,
              "minimum parameter value should leave the active fill empty")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_text_color(depthLabel, LV_PART_MAIN), lv_color_hex(ardor::lvgl_ui::muted)),
              "an unfocused slider's key label should read as muted engraving")) return 1;
  lv_obj_t* typeLabel = findLabel(lv_screen_active(), "TYPE");
  lv_obj_t* typeSlider = typeLabel ? lv_obj_get_parent(typeLabel) : nullptr;
  // A discrete card renders the selected value once as its readout and again
  // inside the segmented option row. Target the nested copy so these checks
  // exercise the option button rather than the card itself.
  lv_obj_t* photoresistorValue = typeSlider ? findNestedLabel(typeSlider, "PHOTORESISTOR") : nullptr;
  lv_area_t typeSliderArea{};
  lv_area_t photoresistorArea{};
  if (typeSlider) lv_obj_get_coords(typeSlider, &typeSliderArea);
  if (photoresistorValue) lv_obj_get_coords(lv_obj_get_parent(photoresistorValue), &photoresistorArea);
  if (require(photoresistorValue
                && photoresistorArea.x1 >= typeSliderArea.x1 && photoresistorArea.x2 <= typeSliderArea.x2,
              "discrete modulation choices should render as segmented options inside the slider")) return 1;
  if (require(photoresistorValue
                && lv_obj_get_height(lv_obj_get_parent(photoresistorValue)) == 56
                && lv_obj_get_width(lv_obj_get_parent(photoresistorValue)) >= 44,
              "every discrete option should provide a finger-sized touch target")) return 1;

  const auto& defaultMappingControl = renderControls.front();
  // The context rail is found through its EXP action: the plain control name
  // also appears on the sliders and the chain cards.
  lv_obj_t* railExpression = findLastLabel(lv_screen_active(), "ASSIGN EXP");
  lv_obj_t* mappingToolbar = railExpression
    ? lv_obj_get_parent(lv_obj_get_parent(railExpression)) : nullptr;
  lv_obj_t* assignExpression = mappingToolbar ? findLabel(mappingToolbar, "ASSIGN EXP") : nullptr;
  lv_obj_t* learnMidi = mappingToolbar ? findLabel(mappingToolbar, "MIDI LEARN") : nullptr;
  if (require(mappingToolbar && findLabel(mappingToolbar, upper(defaultMappingControl.label).c_str()),
              "the context rail should name the selected control")) return 1;
  if (require(mappingToolbar && assignExpression && learnMidi
                && !findLabel(depthSlider, "EXP") && !findLabel(depthSlider, "MIDI"),
              "parameter mapping actions should live in one contextual toolbar")) return 1;
  lv_obj_send_event(lv_obj_get_parent(learnMidi), LV_EVENT_CLICKED, nullptr);
  if (require(state.midiLearn.stage == ardor::UiMidiLearnStage::Waiting,
              "contextual MIDI Learn should target the selected parameter")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_t* listeningLabel = findLabel(lv_screen_active(), "LISTENING...");
  lv_obj_t* midiLearnCard = listeningLabel ? lv_obj_get_parent(listeningLabel) : nullptr;
  lv_obj_t* midiLearnTitle = midiLearnCard ? findLabel(midiLearnCard, "MIDI LEARN") : nullptr;
  if (require(midiLearnTitle
                && listeningLabel,
              "MIDI Learn should show a blocking listening sheet")) return 1;
  ardor::observeMidiLearnControlChange(state, 0, 11, 64);
  ui.refresh(lv_screen_active(), state);
  lv_obj_t* advancedLearn = midiLearnCard ? findLabel(midiLearnCard, "ADVANCED") : nullptr;
  if (require(findLabel(lv_screen_active(), "CC 11  ·  CHANNEL 1")
                && advancedLearn && findLabel(midiLearnCard, "SAVE"),
              "captured CC should expose Save and Advanced actions")) return 1;
  lv_obj_send_event(lv_obj_get_parent(advancedLearn), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(findLabel(lv_screen_active(), "CONTINUOUS")
                && findLabel(lv_screen_active(), "1")
                && findLabel(lv_screen_active(), "2"),
              "advanced MIDI Learn should show mode and two endpoint sliders")) return 1;
  lv_obj_t* cancelLearn = midiLearnCard ? findLabel(midiLearnCard, "CANCEL") : nullptr;
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
                && findLabel(mappingToolbar, upper(updatedDepth->label).c_str())
                && findLabel(mappingToolbar, updatedDepth->formatted.c_str()),
              "contextual mapping toolbar should follow the touched parameter and live value")) return 1;
  {
    // Fine steps on the context rail move the selected control by one step.
    lv_obj_t* stepUp = findLabel(mappingToolbar, "+");
    lv_obj_t* stepDown = findLabel(mappingToolbar, "-");
    const float before = ardor::selectedUiBlock(state)->params.value("depth", 0.0f);
    if (require(stepUp && stepDown, "the context rail should offer - and + steps")) return 1;
    lv_obj_send_event(lv_obj_get_parent(stepUp), LV_EVENT_CLICKED, nullptr);
    ui.refresh(lv_screen_active(), state);
    const float after = ardor::selectedUiBlock(state)->params.value("depth", 0.0f);
    if (require(after > before, "the + step should raise the selected control")) return 1;
    lv_obj_send_event(lv_obj_get_parent(stepDown), LV_EVENT_CLICKED, nullptr);
    ui.refresh(lv_screen_active(), state);
    if (require(ardor::selectedUiBlock(state)->params.value("depth", 0.0f) < after,
                "the - step should lower the selected control")) return 1;
  }
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
  // PAGE n / m sits in its recessed legend box between the two steps.
  lv_obj_t* pageTwoPrevious = pageTwo ? findLabel(lv_obj_get_parent(lv_obj_get_parent(pageTwo)), "<")
                                      : nullptr;
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
  title = findLastLabel(lv_screen_active(), titleText.c_str());
  depthLabel = findSliderLabel(lv_screen_active(), upper(depth->label).c_str());
  depthSlider = depthLabel ? lv_obj_get_parent(depthLabel) : nullptr;
  depthFill = depthSlider ? findTravelFill(depthSlider) : nullptr;

  lv_obj_t* chain = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::bg), 1280, 548);
  if (require(chain, "signal chain should use the Panel plate ground")) return 1;
  std::string firstCategory = state.bank.presets[state.activePreset].blocks.front().label;
  std::transform(firstCategory.begin(), firstCategory.end(), firstCategory.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  lv_obj_t* firstCategoryLabel = findLabel(chain, firstCategory.c_str());
  lv_obj_t* firstChainBlock = firstCategoryLabel
    ? lv_obj_get_parent(lv_obj_get_parent(firstCategoryLabel)) : nullptr;
  if (require(firstChainBlock && lv_obj_get_width(firstChainBlock) == 172
                && lv_obj_get_height(firstChainBlock) == 296,
              "the chain should use tall engraved module cards, per the mockup")) return 1;
  {
    // The selected card lifts on one hard offset plate behind it: no blur,
    // so it cannot band on the RGB565 panel.
    lv_obj_t* selectedCard = nullptr;
    for (uint32_t child = 0; child < lv_obj_get_child_count(lv_obj_get_parent(firstChainBlock)); ++child) {
      lv_obj_t* candidate = lv_obj_get_child(lv_obj_get_parent(firstChainBlock), static_cast<int32_t>(child));
      if (lv_obj_get_width(candidate) == 172 && lv_obj_get_height(candidate) == 296
          && lv_obj_get_style_border_width(candidate, LV_PART_MAIN) == 3) {
        selectedCard = candidate;
      }
    }
    lv_obj_t* liftPlate = selectedCard ? findObjectWithSizeAndBgColor(
      lv_obj_get_parent(selectedCard), lv_color_hex(ardor::lvgl_ui::liftShadow), 172, 296) : nullptr;
    if (require(selectedCard && liftPlate && !lv_obj_has_flag(liftPlate, LV_OBJ_FLAG_HIDDEN)
                  && lv_obj_get_x(liftPlate) == lv_obj_get_x(selectedCard) + 8
                  && lv_obj_get_y(liftPlate) == lv_obj_get_y(selectedCard) + 8,
                "the selected chain card should lift on a hard offset plate")) return 1;
  }
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
  // The title bar is still the drag surface; a flat grip replaces the old
  // DRAG legend so the header can carry only the block type.
  lv_obj_t* dragHandle = lv_obj_get_parent(firstCategoryLabel);
  if (require(dragHandle && lv_obj_get_width(dragHandle) == 170
                && lv_obj_get_height(dragHandle) == 64
                && !findLabel(dragHandle, "DRAG"),
              "chain blocks should use the full title bar as a touch drag target")) return 1;
  lv_obj_send_event(dragHandle, LV_EVENT_PRESSED, nullptr);
  if (require(!lv_obj_has_flag(chain, LV_OBJ_FLAG_SCROLLABLE)
                && lv_obj_get_scrollbar_mode(chain) == LV_SCROLLBAR_MODE_OFF,
              "pressing an effect drag handle should lock the competing chain scrollbar")) return 1;
  lv_obj_send_event(dragHandle, LV_EVENT_RELEASED, nullptr);
  if (require(lv_obj_has_flag(chain, LV_OBJ_FLAG_SCROLLABLE)
                && lv_obj_get_scrollbar_mode(chain) == LV_SCROLLBAR_MODE_ACTIVE,
              "releasing an effect drag handle should restore ordinary chain scrolling")) return 1;
  lv_obj_t* firstCardAssetLabel = findLabel(firstChainBlock,
      upper(state.bank.presets[state.activePreset].blocks.front().assetName).c_str());
  lv_obj_t* firstOffLabel = findLabel(firstChainBlock, "OFF");
  if (require(firstCategoryLabel && firstCardAssetLabel && firstOffLabel,
              "disabled chain card should render its name and OFF state")) return 1;
  if (require(!findLabel(firstChainBlock, "BYPASSED")
                && lv_obj_has_state(firstChainBlock, LV_STATE_USER_1)
                && !lv_obj_has_flag(lv_obj_get_child(firstChainBlock, 0), LV_OBJ_FLAG_HIDDEN),
              "disabled chain card should show the bypass hatch")) return 1;
  lv_area_t firstCategoryArea{};
  lv_area_t firstAssetArea{};
  lv_area_t firstOffArea{};
  lv_obj_get_coords(firstCategoryLabel, &firstCategoryArea);
  lv_obj_get_coords(firstCardAssetLabel, &firstAssetArea);
  lv_obj_get_coords(firstOffLabel, &firstOffArea);
  // Lamp Black puts the OFF tag on the name's row, against the right edge.
  if (require(firstCategoryArea.y2 < firstAssetArea.y1
                && firstOffArea.x1 > firstAssetArea.x1
                && firstOffArea.y1 < firstAssetArea.y2,
              "chain-card OFF tag should sit on the name row, right-aligned")) return 1;
  state.bank.presets[state.activePreset].blocks.front().enabled = true;
  ardor::markUiChanged(state, ardor::UiChange::Chain);
  ui.refresh(lv_screen_active(), state);
  if (require(!lv_obj_has_state(firstChainBlock, LV_STATE_USER_1)
                && lv_obj_has_flag(lv_obj_get_child(firstChainBlock, 0), LV_OBJ_FLAG_HIDDEN)
                && lv_obj_has_flag(lv_obj_get_parent(firstOffLabel), LV_OBJ_FLAG_HIDDEN),
              "enabling a retained chain card should clear its outline state")) return 1;
  {
    // Enabled cards summarise the block with its main values instead of the
    // old family ticks; the summary follows parameter edits on retained cards.
    const auto& chainBlocks = state.bank.presets[state.activePreset].blocks;
    const auto summaryBlock = std::find_if(chainBlocks.begin(), chainBlocks.end(),
      [](const ardor::UiBlock& block) {
        return block.enabled && block.type != "dualRig"
          && !ardor::blockSummaryControls(block, 2).empty();
      });
    if (require(summaryBlock != chainBlocks.end(),
                "the demo chain should include a block with continuous controls")) return 1;
    const auto summary = ardor::blockSummaryControls(*summaryBlock, 2);
    lv_obj_t* summaryAsset = findLabel(chain, upper(summaryBlock->assetName).c_str());
    lv_obj_t* summaryCard = summaryAsset ? lv_obj_get_parent(summaryAsset) : nullptr;
    if (require(summaryCard
                  && findLabel(summaryCard, summary[0].label.c_str())
                  && findLabel(summaryCard, summary[0].formatted.c_str()),
                "enabled chain cards should show their main parameter values")) return 1;
  }
  state.bank.presets[state.activePreset].blocks.front().enabled = false;
  ardor::markUiChanged(state, ardor::UiChange::Chain);
  ui.refresh(lv_screen_active(), state);
  if (require(lv_obj_has_state(firstChainBlock, LV_STATE_USER_1)
                && !lv_obj_has_flag(lv_obj_get_parent(firstOffLabel), LV_OBJ_FLAG_HIDDEN),
              "bypassing a retained chain card should restore its hatch and OFF tag")) return 1;
  // The instructional hint text was retired: the bottom rail's Input/Output
  // jump controls now sit in a tighter band, and the circular patch points
  // plus drag handles are self-evident per the redesign's lettering-first,
  // no-borrowed-icon philosophy (docs/lvgl-ui-redesign-spec.md §8.11).
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::panelAlt), 1280, 48),
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
  title = findLastLabel(lv_screen_active(), titleText.c_str());
  page = findLabel(lv_screen_active(), "PAGE 1 / 2");
  depthLabel = findSliderLabel(lv_screen_active(), upper(depth->label).c_str());
  depthSlider = depthLabel ? lv_obj_get_parent(depthLabel) : nullptr;
  depthFill = depthSlider ? findTravelFill(depthSlider) : nullptr;

  lv_obj_t* parameterPanel = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::panel), 1280, 464);
  // Scope the title to the drawer: the module drawer and chain cards can
  // repeat the same asset name.
  title = parameterPanel ? findLabel(parameterPanel, titleText.c_str()) : nullptr;
  {
    // The drawer takes the block's family colour along its top edge and in a
    // type tag; travel fills use the family colour, the lamp frames the
    // focused control only.
    const auto& panelBlock = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
    const auto family = lv_color_hex(ardor::lvgl_ui::categoryColor(panelBlock.type));
    lv_obj_t* familyBar = parameterPanel
      ? findObjectWithSizeAndBgColor(parameterPanel, family, 1280, 4) : nullptr;
    lv_obj_t* typeTag = parameterPanel ? findLabel(parameterPanel, upper(panelBlock.label).c_str()) : nullptr;
    if (require(familyBar && typeTag
                  && lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(typeTag), LV_PART_MAIN), family)
                  && lv_color_eq(lv_obj_get_style_text_color(typeTag, LV_PART_MAIN),
                                 lv_color_hex(ardor::lvgl_ui::bg)),
                "the parameter drawer should carry a family edge and a family type tag")) return 1;
    int fills = 0;
    bool fillsFamily = true;
    for (uint32_t child = 0; parameterPanel && child < lv_obj_get_child_count(parameterPanel); ++child) {
      lv_obj_t* card = lv_obj_get_child(parameterPanel, static_cast<int32_t>(child));
      if (lv_obj_get_width(card) != 403) continue;
      lv_obj_t* fill = findTravelFill(card);
      if (!fill) continue;
      ++fills;
      fillsFamily = fillsFamily && lv_color_eq(lv_obj_get_style_bg_color(fill, LV_PART_MAIN), family);
    }
    if (require(fills > 0 && fillsFamily,
                "travel fills should use the block's family colour")) return 1;
  }
  {
    // Chip strip: the chain shrinks to one chip per block above the drawer,
    // so another block is one tap away without closing the drawer.
    const auto& chipBlocks = state.bank.presets[state.activePreset].blocks;
    const auto findChip = [&](const std::string& asset) -> lv_obj_t* {
      std::function<lv_obj_t*(lv_obj_t*)> walk = [&](lv_obj_t* parent) -> lv_obj_t* {
        for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
          lv_obj_t* child = lv_obj_get_child(parent, static_cast<int32_t>(i));
          if (lv_obj_check_type(child, &lv_label_class) && upper(asset) == lv_label_get_text(child)
              && lv_obj_get_height(parent) == 56 && !lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN)) {
            return parent;
          }
          if (lv_obj_t* found = walk(child)) return found;
        }
        return nullptr;
      };
      return walk(lv_screen_active());
    };
    lv_obj_update_layout(lv_screen_active());
    lv_obj_t* selectedChip = findChip(chipBlocks[state.selectedBlock].assetName);
    const std::size_t otherIndex = state.selectedBlock == 0 ? 1 : 0;
    lv_obj_t* otherChip = findChip(chipBlocks[otherIndex].assetName);
    if (require(selectedChip && otherChip
                  && lv_obj_get_style_border_width(selectedChip, LV_PART_MAIN) == 3
                  && lv_obj_get_style_border_width(otherChip, LV_PART_MAIN) == 1,
                "the parameter drawer should show a chip per block, the selected one framed")) return 1;
    const std::size_t returnIndex = state.selectedBlock;
    lv_obj_send_event(otherChip, LV_EVENT_CLICKED, nullptr);
    ui.refresh(lv_screen_active(), state);
    lv_obj_update_layout(lv_screen_active());
    if (require(state.selectedBlock == otherIndex && state.paramDrawerOpen,
                "tapping a chip should select that block and keep the drawer open")) return 1;
    lv_obj_send_event(findChip(chipBlocks[returnIndex].assetName), LV_EVENT_CLICKED, nullptr);
    ui.refresh(lv_screen_active(), state);
    lv_obj_update_layout(lv_screen_active());
    if (require(state.selectedBlock == returnIndex,
                "tapping the first chip again should return to that block")) return 1;
  }
  // The context rail's Done closes the drawer; the drawer's header carries
  // BLOCK [ON], MIDI and DELETE right-aligned after the title and pager.
  lv_obj_t* parameterClose = findLastLabel(lv_screen_active(), "DONE");
  lv_obj_t* deleteBlock = parameterPanel ? findLabel(parameterPanel, "DELETE") : nullptr;
  lv_obj_t* bypassLabel = parameterPanel ? findLabel(parameterPanel, "BLOCK") : nullptr;
  lv_obj_t* bypassControl = bypassLabel ? lv_obj_get_parent(bypassLabel) : nullptr;
  lv_obj_t* bypassValue = bypassControl ? findLabel(bypassControl, "ON") : nullptr;
  lv_obj_t* bypassBadge = bypassValue ? lv_obj_get_parent(bypassValue) : nullptr;
  if (require(parameterPanel && parameterClose && deleteBlock && bypassLabel && bypassControl
                && bypassValue && bypassBadge,
              "parameter panel header controls should render")) return 1;
  lv_area_t parameterCloseArea{};
  lv_area_t deleteBlockArea{};
  lv_area_t bypassControlArea{};
  lv_area_t titleArea{};
  lv_obj_get_coords(lv_obj_get_parent(parameterClose), &parameterCloseArea);
  lv_obj_get_coords(lv_obj_get_parent(deleteBlock), &deleteBlockArea);
  lv_obj_get_coords(bypassControl, &bypassControlArea);
  lv_obj_get_coords(title, &titleArea);
  if (require(parameterCloseArea.y1 >= 612 && parameterCloseArea.x2 == 1255
                && lv_obj_get_width(lv_obj_get_parent(parameterClose)) == 124
                && lv_obj_get_height(lv_obj_get_parent(parameterClose)) == 60,
              "Done should close the drawer from the right end of the context rail")) return 1;
  if (require(titleArea.x2 < bypassControlArea.x1 && bypassControlArea.x2 < deleteBlockArea.x1
                && deleteBlockArea.x2 == 1255,
              "title, Block, MIDI and Delete should read left to right")) return 1;
  if (require(!lv_obj_has_state(bypassControl, LV_STATE_CHECKED),
              "an enabled block should show Block ON")) return 1;
  if (require(lv_obj_get_width(bypassControl) == 167 && lv_obj_get_height(bypassControl) == 60,
              "the Block control should provide a large rectangular touch target")) return 1;
  if (require(lv_obj_get_style_radius(bypassControl, LV_PART_MAIN) == 0,
              "the Block control should use a square Panel plate")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(bypassBadge, LV_PART_MAIN),
                          lv_color_hex(ardor::lvgl_ui::text)),
              "Block ON should print ground on a bone badge")) return 1;
  if (require(!findObjectOfClass(bypassControl, &lv_switch_class),
              "the Block control should not retain a native switch or circular thumb")) return 1;

  const auto bypassedBlock = state.selectedBlock;
  lv_obj_send_event(bypassControl, LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(!state.bank.presets[state.activePreset].blocks[bypassedBlock].enabled
                && liveBypassUpdates == 1
                && ardor::previewIsSynchronized(state)
                && lv_obj_has_state(bypassControl, LV_STATE_CHECKED)
                && findLabel(bypassControl, "OFF")
                && lv_color_eq(lv_obj_get_style_bg_color(bypassBadge, LV_PART_MAIN),
                               lv_color_hex(ardor::lvgl_ui::bg)),
              "tapping Block should bypass the block and drop the badge to OFF")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_send_event(bypassControl, LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.bank.presets[state.activePreset].blocks[bypassedBlock].enabled
                && liveBypassUpdates == 2
                && ardor::previewIsSynchronized(state)
                && !lv_obj_has_state(bypassControl, LV_STATE_CHECKED)
                && findLabel(bypassControl, "ON"),
              "tapping Block again should restore Block ON")) return 1;
  lv_area_t pageArea{};
  lv_obj_get_coords(page, &pageArea);
  if (require(pageArea.x1 > titleArea.x2,
              "the pager should follow the title without overlapping it")) return 1;
  if (require(depthFill && lv_obj_get_width(depthFill) == 0,
              "minimum slider value should start with no active fill")) return 1;

  ui.focusParameter(depth->key);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* focusedLabel = findSliderLabel(lv_screen_active(), upper(depth->label).c_str());
  lv_obj_t* focusedSlider = focusedLabel ? lv_obj_get_parent(focusedLabel) : nullptr;
  if (require(focusedSlider && lv_obj_get_style_border_width(focusedSlider, LV_PART_MAIN) == 3
                && lv_color_eq(lv_obj_get_style_border_color(focusedSlider, LV_PART_MAIN), lv_color_hex(ardor::lvgl_ui::lamp)),
              "the focused control should take the 3 px lamp frame")) return 1;

  lv_obj_t* focusedFill = focusedSlider ? findTravelFill(focusedSlider) : nullptr;
  const int minimumFillWidth = focusedFill ? lv_obj_get_width(focusedFill) : -1;
  if (require(ui.applyFocusedParameterDelta(state, 1), "focused encoder adjustment should be consumed")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  focusedLabel = findSliderLabel(lv_screen_active(), upper(depth->label).c_str());
  focusedSlider = focusedLabel ? lv_obj_get_parent(focusedLabel) : nullptr;
  focusedFill = focusedSlider ? findTravelFill(focusedSlider) : nullptr;
  if (require(focusedFill && lv_obj_get_width(focusedFill) > minimumFillWidth,
              "focused encoder adjustment should increase the slider fill")) return 1;
  {
    // The selected card's summary follows live parameter edits.
    const auto& editedBlock = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
    const auto summary = ardor::blockSummaryControls(state, editedBlock, 2);
    const auto edited = std::find_if(summary.begin(), summary.end(),
      [&](const auto& item) { return item.key == depth->key; });
    lv_obj_t* editedCard = findChainCard(lv_screen_active(), upper(editedBlock.assetName));
    if (require(edited != summary.end() && editedCard
                  && findLabel(editedCard, edited->formatted.c_str()),
                "the selected chain card should show the edited value")) return 1;
  }

  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* stableDepthLabel = findSliderLabel(lv_screen_active(), upper(depth->label).c_str());
  lv_obj_t* stableDepthSlider = stableDepthLabel ? lv_obj_get_parent(stableDepthLabel) : nullptr;
  lv_obj_t* stableFill = stableDepthSlider ? findTravelFill(stableDepthSlider) : nullptr;
  if (require(stableDepthSlider && stableFill, "focused slider should expose stable visual handles")) return 1;
  const int stableFillWidth = lv_obj_get_width(stableFill);
  ui.setFocusedWidgets(stableDepthSlider);
  ui.focusParameter(depth->key);
  if (require(ui.applyFocusedParameterDelta(state, 5), "targeted encoder adjustment should be consumed")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(stableDepthSlider);
  lv_obj_t* retainedDepthLabel = findSliderLabel(lv_screen_active(), upper(depth->label).c_str());
  if (require(retainedDepthLabel && lv_obj_get_parent(retainedDepthLabel) == stableDepthSlider,
              "targeted encoder adjustment should retain the slider object")) return 1;
  if (require(lv_obj_get_width(stableFill) > stableFillWidth,
              "targeted encoder adjustment should update the retained fill")) return 1;
  ui.focusParameter("");
  ardor::setSelectedBlockParam(state, "depth", depth->minimum);
  ui.refresh(lv_screen_active(), state);
  retainedDepthLabel = findSliderLabel(lv_screen_active(), upper(depth->label).c_str());
  if (require(retainedDepthLabel && lv_obj_get_parent(retainedDepthLabel) == stableDepthSlider,
              "model-driven parameter updates should retain the slider object")) return 1;
  const auto retainedBlockIndex = state.selectedBlock;
  ui.selectBlock(state, 1);
  ui.refresh(lv_screen_active(), state);
  ui.selectBlock(state, retainedBlockIndex);
  ui.refresh(lv_screen_active(), state);
  retainedDepthLabel = findSliderLabel(lv_screen_active(), upper(depth->label).c_str());
  if (require(retainedDepthLabel && lv_obj_get_parent(retainedDepthLabel) == stableDepthSlider,
              "switching parameter targets should reactivate the cached retained panel")) return 1;

  ardor::setSelectedBlockParam(state, "depth", depth->maximum);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  depthLabel = findSliderLabel(lv_screen_active(), upper(depth->label).c_str());
  depthSlider = depthLabel ? lv_obj_get_parent(depthLabel) : nullptr;
  depthFill = depthSlider ? findTravelFill(depthSlider) : nullptr;
  lv_obj_t* depthRail = depthFill ? lv_obj_get_child(lv_obj_get_parent(depthFill), 11) : nullptr;
  if (require(depthFill && depthRail
                && lv_obj_get_width(depthFill) == lv_obj_get_width(depthRail),
              "maximum slider value should fill the whole track")) return 1;

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
  if (require(findLabel(lv_screen_active(), "BANK 00") && findLabel(lv_screen_active(), "CORE SOUNDS"),
              "the header should name the active bank by number and name")) return 1;
  lv_obj_t* presetName = findLabel(lv_screen_active(), upper(state.bank.presets[state.activePreset].name).c_str());
  if (require(presetName && lv_obj_get_style_text_font(presetName, LV_PART_MAIN) == &ardor_lb_cond800_96,
              "the live preset name should print in the 96 px extra-bold face")) return 1;
  if (require(lv_obj_get_width(presetName) == 556
                && lv_label_get_long_mode(presetName) == LV_LABEL_LONG_MODE_DOTS,
              "preset-card names should stay inside the tile's content width")) return 1;
  const std::size_t activeSlot = state.activePreset;
  const std::size_t inactiveSlot = (activeSlot + 1) % state.bank.presets.size();
  const std::string activeFsText = "FS " + std::to_string(activeSlot + 1);
  const std::string inactiveFsText = "FS " + std::to_string(inactiveSlot + 1);
  lv_obj_t* retainedPresetCard = lv_obj_get_parent(presetName);
  lv_obj_t* inactivePresetName = findLabel(
    lv_screen_active(), upper(state.bank.presets[inactiveSlot].name).c_str());
  lv_obj_t* inactivePresetCard = inactivePresetName ? lv_obj_get_parent(inactivePresetName) : nullptr;
  lv_obj_t* activeFsLabel = findLabel(retainedPresetCard, activeFsText.c_str());
  lv_obj_t* inactiveFsLabel = inactivePresetCard ? findLabel(inactivePresetCard, inactiveFsText.c_str())
                                                 : nullptr;
  if (require(activeFsLabel && inactiveFsLabel,
              "preset tiles should identify the physical footswitch")) return 1;
  lv_obj_t* activeLiveTag = findLabel(retainedPresetCard, "LIVE");
  lv_obj_t* inactiveLiveTag = inactivePresetCard ? findLabel(inactivePresetCard, "LIVE") : nullptr;
  if (require(activeLiveTag && inactiveLiveTag
                && !lv_obj_has_flag(lv_obj_get_parent(activeLiveTag), LV_OBJ_FLAG_HIDDEN)
                && lv_obj_has_flag(lv_obj_get_parent(inactiveLiveTag), LV_OBJ_FLAG_HIDDEN)
                && lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(activeLiveTag), LV_PART_MAIN),
                               lv_color_hex(ardor::lvgl_ui::lampInk)),
              "only the active preset should show the lamp-ink LIVE tag")) return 1;
  if (require(lv_obj_get_style_border_width(retainedPresetCard, LV_PART_MAIN) == 1
                && inactivePresetCard
                && lv_obj_get_style_border_width(inactivePresetCard, LV_PART_MAIN) == 1,
              "every preset tile should keep a 1 px frame; the flood marks LIVE")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(retainedPresetCard, LV_PART_MAIN),
                          lv_color_hex(ardor::lvgl_ui::lamp))
                && lv_color_eq(lv_obj_get_style_text_color(presetName, LV_PART_MAIN),
                               lv_color_hex(ardor::lvgl_ui::lampInk))
                && lv_color_eq(lv_obj_get_style_bg_color(inactivePresetCard, LV_PART_MAIN),
                               lv_color_hex(ardor::lvgl_ui::panel))
                && lv_color_eq(lv_obj_get_style_text_color(inactivePresetName, LV_PART_MAIN),
                               lv_color_hex(ardor::lvgl_ui::text)),
              "the live preset should flood its whole tile with the lamp and use lamp-ink lettering")) return 1;
  {
    const auto& liveBlocks = state.bank.presets[activeSlot].blocks;
    const auto& idleBlocks = state.bank.presets[inactiveSlot].blocks;
    // Search only the tile's child containers, so a preset named like its
    // first block's code ("Crunch" / "CRUNCH") cannot match the name label.
    const auto findStripCode = [](lv_obj_t* card, const std::string& code) -> lv_obj_t* {
      for (uint32_t child = 0; child < lv_obj_get_child_count(card); ++child) {
        lv_obj_t* container = lv_obj_get_child(card, static_cast<int32_t>(child));
        if (lv_obj_check_type(container, &lv_label_class)) continue;
        if (lv_obj_t* found = findLabel(container, code.c_str())) return found;
      }
      return nullptr;
    };
    lv_obj_t* liveCode = liveBlocks.empty() ? nullptr
      : findStripCode(retainedPresetCard, ardor::chainStripCode(liveBlocks[0]));
    lv_obj_t* idleCode = idleBlocks.empty() ? nullptr
      : findStripCode(inactivePresetCard, ardor::chainStripCode(idleBlocks[0]));
    if (require(liveCode && idleCode,
                "each preset tile should show its chain as a strip of block codes")) return 1;
    if (require(lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(liveCode), LV_PART_MAIN),
                            lv_color_hex(ardor::lvgl_ui::lampInk))
                  && lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(idleCode), LV_PART_MAIN),
                                 lv_color_hex(idleBlocks[0].enabled
                                   ? ardor::lvgl_ui::categoryColor(idleBlocks[0].type)
                                   : ardor::lvgl_ui::rule)),
                "chain segments should be dark on the lamp and family-coloured elsewhere")) return 1;
  }
  ardor::synchronizePresetSelection(state, inactiveSlot);
  ui.refresh(lv_screen_active(), state);
  if (require(!lv_obj_has_flag(lv_obj_get_parent(inactiveLiveTag), LV_OBJ_FLAG_HIDDEN)
                && lv_obj_has_flag(lv_obj_get_parent(activeLiveTag), LV_OBJ_FLAG_HIDDEN)
                && lv_obj_get_style_text_font(inactivePresetName, LV_PART_MAIN) == &ardor_lb_cond800_96
                && lv_obj_get_style_text_font(presetName, LV_PART_MAIN) == &ardor_lb_cond700_76,
              "retained preset cards should move the complete LIVE treatment together")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_bg_color(inactivePresetCard, LV_PART_MAIN),
                          lv_color_hex(ardor::lvgl_ui::lamp))
                && lv_color_eq(lv_obj_get_style_bg_color(retainedPresetCard, LV_PART_MAIN),
                               lv_color_hex(ardor::lvgl_ui::panel)),
              "the lamp flood should move with the LIVE preset")) return 1;
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
  if (require(unavailableLabel && !lv_obj_has_flag(lv_obj_get_parent(unavailableLabel), LV_OBJ_FLAG_HIDDEN),
              "preset cards should identify presets with unavailable assets")) return 1;
  state.bank.presets[state.activePreset].blocks[0].assetPath = installedAssetPath;
  state.bank.presets[state.activePreset].blocks[0].type = installedBlockType;
  state.bank.presets[state.activePreset].blocks[0].enabled = installedBlockEnabled;
  ardor::markUiChanged(state, ardor::UiChange::Presets);
  ui.refresh(lv_screen_active(), state);
  if (require(lv_obj_has_flag(lv_obj_get_parent(unavailableLabel), LV_OBJ_FLAG_HIDDEN),
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
  if (require(renameLabel && lv_obj_get_width(lv_obj_get_parent(renameLabel)) >= 124
                && lv_obj_get_height(lv_obj_get_parent(renameLabel)) == 60,
              "the edit rail should expose a clear preset rename target")) return 1;
  const std::string nameBeforeRename = state.bank.presets[state.activePreset].name;
  lv_obj_send_event(lv_obj_get_parent(renameLabel), LV_EVENT_CLICKED, nullptr);
  // The rename sheet is drawn last; its title repeats the rail legend.
  lv_obj_t* renameTitle = findLastLabel(lv_screen_active(), "RENAME");
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
                && findLabelContaining(lv_screen_active(), "STAGE LEAD")
                && lv_obj_has_flag(renameOverlay, LV_OBJ_FLAG_HIDDEN),
              "saving preset rename should trim, persist, and refresh the active preset name")) return 1;
  chain = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::bg), 1280, 548);
  if (require(chain && lv_obj_has_flag(chain, LV_OBJ_FLAG_SCROLLABLE)
                && lv_obj_get_scroll_right(chain) > 0,
              "long chains should remain on one horizontally scrollable rail")) return 1;
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
  // Lamp Black chain: block i spans x = 164 + 228 i, 172 px wide, and the
  // insert circle before it centres on x = 136 + 228 i.
  if (require(ardor::LvglUi::chainSlotForPoint(blockCount, {250, 328}) == 0,
              "the first horizontal tile should map to the first block")) return 1;
  if (require(ardor::LvglUi::chainSlotForPoint(blockCount, {1390, 328}) == 5,
              "the sixth horizontal tile should map to the sixth block")) return 1;
  if (require(ardor::LvglUi::chainInsertionSlotForPoint(blockCount, {1276, 328}) == 5,
              "horizontal insertion should use the nearest signal boundary")) return 1;
  const auto sixthIndicator = ardor::LvglUi::chainIndicatorPosition(blockCount, 5);
  if (require(sixthIndicator.x == 1284 && sixthIndicator.y == 180,
              "sixth-slot insertion indicator should stay on the single horizontal rail")) return 1;
  const auto forwardIndicator = ardor::LvglUi::chainReorderIndicatorPosition(blockCount, 0, 1);
  if (require(forwardIndicator.x == 600 && forwardIndicator.y == 180,
              "forward reorder indicator should appear after the hovered block")) return 1;
  const auto backwardIndicator = ardor::LvglUi::chainReorderIndicatorPosition(blockCount, 4, 1);
  if (require(backwardIndicator.x == 372 && backwardIndicator.y == 180,
              "backward reorder indicator should appear before the hovered block")) return 1;

  // Preset now carries its own top legend rail and bottom control rail, per
  // docs/lvgl-ui-redesign-spec.md §4f — replacing the old free-floating
  // header buttons and the shared status-bar footer for this screen.
  ardor::enterPresetMode(state);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* telemetryLegend = findLabel(lv_screen_active(), "1.33 MS  \xC2\xB7  BUFFER 25%");
  lv_obj_t* presetTopRail = telemetryLegend ? lv_obj_get_parent(telemetryLegend) : nullptr;
  lv_obj_t* editButtonLabel = findLabel(lv_screen_active(), "EDIT");
  lv_obj_t* editButton = editButtonLabel ? lv_obj_get_parent(editButtonLabel) : nullptr;
  lv_obj_t* setupButtonLabel = findLabel(lv_screen_active(), "SETUP");
  lv_obj_t* setupButton = setupButtonLabel ? lv_obj_get_parent(setupButtonLabel) : nullptr;
  lv_obj_t* tunerButtonLabel = findLabel(lv_screen_active(), "TUNER");
  lv_obj_t* tunerButton = tunerButtonLabel ? lv_obj_get_parent(tunerButtonLabel) : nullptr;
  // The bank pair: two 76 px steps around a recessed BANK legend.
  lv_obj_t* bankLegend = presetTopRail ? findLabel(presetTopRail, "BANK") : nullptr;
  lv_obj_t* bankDownLabel = presetTopRail ? findLabel(presetTopRail, "-") : nullptr;
  lv_obj_t* bankUpLabel = presetTopRail ? findLabel(presetTopRail, "+") : nullptr;
  lv_obj_t* masterLegend = findLabel(lv_screen_active(), "MASTER");
  lv_obj_t* masterValue = findLabel(lv_screen_active(), std::to_string(state.masterVolume).c_str());
  lv_obj_t* bankDownButton = bankDownLabel ? lv_obj_get_parent(bankDownLabel) : nullptr;
  lv_obj_t* bankUpButton = bankUpLabel ? lv_obj_get_parent(bankUpLabel) : nullptr;
  if (require(presetTopRail
                && !findLabel(presetTopRail, "ARDOR")
                && !findLabel(presetTopRail, "48 KHZ")
                && findLabel(presetTopRail, "BANK 00"),
              "preset header should carry the bank, latency, and live buffer use")) return 1;
  ardor::updateRealtimeTelemetry(
    state, ardor::makeRuntimeTelemetry(120, 0, 0, 7.0, 3.0, 10.0, false,
                                      0, 0, 0, 6.0));
  ui.refresh(lv_screen_active(), state);
  if (require(findLabel(lv_screen_active(), "1.33 MS  \xC2\xB7  BUFFER 60%") == telemetryLegend,
              "the retained header buffer percentage should follow one-second telemetry updates")) return 1;
  if (require(editButton && lv_obj_get_width(editButton) == 124 && lv_obj_get_height(editButton) == 60,
              "Edit should have a large, finger-friendly hit target")) return 1;
  if (require(setupButton && lv_obj_get_width(setupButton) >= 124 && lv_obj_get_height(setupButton) == 60,
              "bottom rail should expose a Setup control")) return 1;
  if (require(bankLegend && bankDownButton && bankUpButton && lv_obj_get_width(bankUpButton) == 76
                && lv_obj_get_height(bankUpButton) == 60
                && lv_obj_get_height(bankDownButton) == 60,
              "preset screen should render the - BANK + pair")) return 1;
  if (require(masterLegend && masterValue && tunerButton
                && lv_obj_get_width(tunerButton) >= 124
                && lv_obj_get_height(tunerButton) == 60,
              "preset screen should provide a Tuner button and a master readout")) return 1;
  const auto masterMeter = [&]() -> lv_obj_t* {
    lv_obj_t* parent = lv_obj_get_parent(masterValue);
    return lv_obj_get_child(parent, static_cast<int32_t>(lv_obj_get_index(masterValue)) + 1);
  };
  const auto litMasterSegments = [&]() {
    lv_obj_t* meter = masterMeter();
    int lit = 0;
    for (uint32_t segment = 0; meter && segment < lv_obj_get_child_count(meter); ++segment) {
      lit += lv_color_eq(lv_obj_get_style_bg_color(
        lv_obj_get_child(meter, static_cast<int32_t>(segment)), LV_PART_MAIN),
        lv_color_hex(ardor::lvgl_ui::text)) ? 1 : 0;
    }
    return lit;
  };
  if (require(masterMeter() && lv_obj_get_child_count(masterMeter()) == 16
                && litMasterSegments() == (state.masterVolume * 16 + 99) / 100,
              "master should render a 16-segment bone meter lit to the volume")) return 1;
  if (require(lv_obj_get_style_text_font(masterValue, LV_PART_MAIN) == &ardor_lb_cond700_52,
              "the master value should use the bold condensed rail face")) return 1;
  if (require(lv_color_eq(lv_obj_get_style_text_color(tunerButtonLabel, LV_PART_MAIN),
                          lv_color_hex(ardor::lvgl_ui::text)),
              "the Tuner button should not spend the LIVE lamp colour")) return 1;
  lv_area_t masterMeterArea{};
  lv_area_t masterValueArea{};
  lv_area_t masterLabelArea{};
  lv_obj_get_coords(masterMeter(), &masterMeterArea);
  lv_obj_get_coords(masterValue, &masterValueArea);
  lv_obj_get_coords(masterLegend, &masterLabelArea);
  if (require(masterMeterArea.x2 == 1255 && masterMeterArea.y2 == 688
                && masterLabelArea.x2 < masterValueArea.x1
                && masterValueArea.x2 < masterMeterArea.x1,
              "master legend, value, and meter should read left to right to the gutter")) return 1;
  // Live control telemetry must not add extra content to this intentionally
  // minimal header.
  ardor::updateControlInputTelemetry(state, {true, true, true, true, 0.5f, true, 12000});
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(!findLabel(presetTopRail, "MIDI ON"),
              "live control status should not add extra header content")) return 1;
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
  if (require(editButtonArea.x1 == 24 && editButtonArea.x1 < tunerButtonArea.x1
                && tunerButtonArea.x1 < bankDownArea.x1
                && bankDownArea.x1 < bankUpArea.x1 && bankUpArea.x1 < setupButtonArea.x1,
              "bottom rail should order Edit, Tuner, -, +, Setup left to right")) return 1;
  if (require(editButtonArea.y1 == 637
                && editButtonArea.y1 == tunerButtonArea.y1
                && tunerButtonArea.y1 == bankDownArea.y1
                && bankDownArea.y1 == bankUpArea.y1
                && bankUpArea.y1 == setupButtonArea.y1,
              "bottom-rail controls should share one row at y = 637")) return 1;
  if (require(masterValue && masterLegend, "master travel scale should render a legend and value")) return 1;
  ardor::setMasterVolume(state, 50);
  ui.refresh(lv_screen_active(), state);
  if (require(findLabel(lv_screen_active(), "50") && litMasterSegments() == 8,
              "master volume should update the retained value and meter")) return 1;
  if (require(lv_obj_has_state(bankDownButton, LV_STATE_DISABLED),
              "bank down should be disabled at the first bank")) return 1;
  if (require(lv_obj_get_style_text_font(bankUpLabel, LV_PART_MAIN) == &ardor_lb_cond600_22,
              "buttons should use the larger, more legible font")) return 1;

  lv_obj_send_event(setupButton, LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "SETUP")
                && findLabel(lv_screen_active(), "APPEARANCE")
                && findLabel(lv_screen_active(), "PANEL PALETTE"),
              "settings gear should open the touchscreen appearance screen")) return 1;
  lv_obj_t* inkPalette = findLabel(lv_screen_active(), "INK");
  if (require(inkPalette, "Appearance should offer the Ink Panel palette")) return 1;
  lv_obj_send_event(lv_obj_get_parent(inkPalette), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.settings.paletteId == ardor::PaletteId::Ink
                && ardor::lvgl_ui::palette().plate == 0x10161f,
              "selecting Ink should rebuild the UI with Ink tokens")) return 1;
  lv_obj_t* slatePalette = findLabel(lv_screen_active(), "SLATE");
  if (require(slatePalette, "Appearance should keep Slate available after a palette rebuild")) return 1;
  lv_obj_send_event(lv_obj_get_parent(slatePalette), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.settings.paletteId == ardor::PaletteId::Slate
                && ardor::lvgl_ui::palette().plate == 0x0b0c0d,
              "returning to Slate should restore the default palette cleanly")) return 1;
  lv_obj_t* nordPalette = findLabel(lv_screen_active(), "NORD");
  if (require(nordPalette, "Appearance should offer the Nord palette")) return 1;
  lv_obj_send_event(lv_obj_get_parent(nordPalette), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.settings.paletteId == ardor::PaletteId::Nord
                && ardor::lvgl_ui::palette().plate == 0x2e3440,
              "selecting Nord should rebuild the UI with Nord tokens")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "SLATE")), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(state.settings.paletteId == ardor::PaletteId::Slate,
              "returning to Slate after Nord should restore the default palette")) return 1;
  lv_obj_t* wifiSectionLabel = findLabel(lv_screen_active(), "WI-FI");
  if (require(wifiSectionLabel, "settings should expose a Wi-Fi section")) return 1;
  lv_obj_send_event(lv_obj_get_parent(wifiSectionLabel), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* wifiKeyboard = findObjectOfClass(lv_screen_active(), &lv_keyboard_class);
  if (require(findLabel(lv_screen_active(), "NETWORK NAME")
                && findLabel(lv_screen_active(), "PASSWORD")
                && wifiKeyboard,
              "touchscreen Wi-Fi settings should render fields and an on-screen keyboard")) return 1;
  lv_area_t keyboardArea{};
  lv_area_t wifiContentArea{};
  lv_obj_get_coords(wifiKeyboard, &keyboardArea);
  lv_obj_get_coords(lv_obj_get_parent(wifiKeyboard), &wifiContentArea);
  if (require(lv_obj_get_width(wifiKeyboard) == 944
                && keyboardArea.x1 == wifiContentArea.x1 + 28
                && keyboardArea.x2 == wifiContentArea.x2 - 28
                && keyboardArea.y1 == wifiContentArea.y1 + 209
                && keyboardArea.y2 < wifiContentArea.y2,
              "Wi-Fi keyboard should be a fully contained, proportionate bottom panel")) return 1;
  lv_obj_t* passwordEye = findLabel(lv_screen_active(), LV_SYMBOL_EYE_OPEN);
  lv_obj_t* passwordEyeButton = passwordEye ? lv_obj_get_parent(passwordEye) : nullptr;
  lv_obj_t* passwordLabel = findLabel(lv_screen_active(), "PASSWORD");
  lv_obj_t* passwordField = passwordLabel
    ? lv_obj_get_child(lv_obj_get_parent(passwordLabel), lv_obj_get_index(passwordLabel) + 1)
    : nullptr;
  lv_area_t passwordEyeArea{};
  lv_area_t passwordFieldArea{};
  if (passwordEyeButton) lv_obj_get_coords(passwordEyeButton, &passwordEyeArea);
  if (passwordField) lv_obj_get_coords(passwordField, &passwordFieldArea);
  if (require(passwordEyeButton && passwordField
                && lv_obj_check_type(passwordField, &lv_textarea_class)
                && lv_obj_get_height(passwordField) == 60
                && lv_obj_get_width(passwordEyeButton) == 52
                && lv_obj_get_height(passwordEyeButton) == 52
                && passwordEyeArea.x1 >= passwordFieldArea.x1
                && passwordEyeArea.x2 <= passwordFieldArea.x2
                && passwordEyeArea.y1 >= passwordFieldArea.y1
                && passwordEyeArea.y2 <= passwordFieldArea.y2,
              "password visibility should use a compact eye control inside the input")) return 1;
  lv_obj_t* countryLabel = findLabel(lv_screen_active(), "COUNTRY");
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
  lv_obj_t* audioSectionLabel = findLabel(lv_screen_active(), "AUDIO");
  if (require(audioSectionLabel, "settings should expose a dedicated Audio section")) return 1;
  lv_obj_send_event(lv_obj_get_parent(audioSectionLabel), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "0.67 MS BLOCK TIME")
                && findLabel(lv_screen_active(), "1.33 MS BLOCK TIME")
                && findLabel(lv_screen_active(), "2.67 MS BLOCK TIME")
                && findLabel(lv_screen_active(), "APPLY & RESTART AUDIO"),
              "Audio settings should offer the supported buffers and an explicit restart action")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "0.67 MS BLOCK TIME")),
                    LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* audioChoice = lv_obj_get_parent(findLabel(lv_screen_active(), "0.67 MS BLOCK TIME"));
  lv_obj_t* audioExplanation = lv_obj_get_parent(findLabel(lv_screen_active(), "LATENCY AND STABILITY"));
  lv_obj_t* audioApply = lv_obj_get_parent(findLabel(lv_screen_active(), "APPLY & RESTART AUDIO"));
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
  lv_obj_t* audioMessage = findLabel(lv_screen_active(), "BUFFER SAVED - RESTARTING AUDIO");
  lv_obj_t* disabledAudioApply = lv_obj_get_parent(findLabel(lv_screen_active(), "APPLY & RESTART AUDIO"));
  lv_area_t audioMessageArea{}, disabledAudioApplyArea{};
  if (audioMessage) lv_obj_get_coords(audioMessage, &audioMessageArea);
  if (disabledAudioApply) lv_obj_get_coords(disabledAudioApply, &disabledAudioApplyArea);
  if (require(savedAudioBlockSize == 32 && state.settings.audioBlockSize == 32
                && audioMessage && disabledAudioApply
                && lv_obj_has_state(disabledAudioApply, LV_STATE_DISABLED)
                && disabledAudioApplyArea.y2 < audioMessageArea.y1,
              "applying an audio buffer should persist it and announce the restart")) return 1;
  lv_obj_t* controlSectionLabel = findLabel(lv_screen_active(), "CONTROL I/O");
  if (require(controlSectionLabel, "settings should expose a Control I/O section")) return 1;
  lv_obj_send_event(lv_obj_get_parent(controlSectionLabel), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "MIDI RECEIVE CHANNEL")
                && findLabel(lv_screen_active(), "TUNER ON/OFF CC")
                && findLabel(lv_screen_active(), "EXPRESSION PEDAL")
                && findLabel(lv_screen_active(), "SCENE LAYER CHORD:  ON")
                && findLabel(lv_screen_active(), "CAPTURE HEEL:  0")
                && findLabel(lv_screen_active(), "CAPTURE TOE:  26400"),
              "Control I/O should integrate MIDI and expression calibration")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "SCENE LAYER CHORD:  ON")),
                    LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(!state.settings.sceneLayerChordEnabled
                && findLabel(lv_screen_active(), "SCENE LAYER CHORD:  OFF"),
              "Control I/O should persist the scene-layer chord preference")) return 1;
  lv_obj_t* midiChannelTitle = findLabel(lv_screen_active(), "MIDI RECEIVE CHANNEL");
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
    // The legend shares the steps' left inset; the steps mirror each other.
    return titleArea.x1 == minusArea.x1
      && std::abs((minusArea.x1 - cardArea.x1) - (cardArea.x2 - plusArea.x2)) <= 2
      // Legends centre on their CSS baseline, not their glyph box, so the
      // label boxes may sit a few pixels off the geometric centre.
      && std::abs(centerX(valueArea) - centerX(cardArea)) <= 4
      && centerY(minusArea) == centerY(plusArea)
      && std::abs(centerY(minusArea) - centerY(valueArea)) <= 12
      && std::abs(centerX(minusLabelArea) - centerX(minusArea)) <= 4
      && std::abs(centerY(minusLabelArea) - centerY(minusArea)) <= 12
      && std::abs(centerX(plusLabelArea) - centerX(plusArea)) <= 4
      && std::abs(centerY(plusLabelArea) - centerY(plusArea)) <= 12;
  };
  if (require(stepperIsAligned(midiChannelTitle)
                && stepperIsAligned(findLabel(lv_screen_active(), "TUNER ON/OFF CC")),
              "MIDI and tuner steppers should share symmetric padding and centered controls")) return 1;
  lv_obj_t* updatesSectionLabel = findLabel(lv_screen_active(), "UPDATES");
  if (require(updatesSectionLabel, "settings should expose an Updates section")) return 1;
  lv_obj_send_event(lv_obj_get_parent(updatesSectionLabel), LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "INSTALLED VERSION")
                && findLabel(lv_screen_active(), "1.2.3")
                && findLabel(lv_screen_active(), "CHECK FOR UPDATES"),
              "Updates should show the installed version and a manual check action")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "CHECK FOR UPDATES")),
                    LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "ARDOR 1.3.0")
                && findLabel(lv_screen_active(), "INSTALL & RESTART"),
              "a compatible release should expose an install action")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "INSTALL & RESTART")),
                    LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(), "CONFIRM INSTALL"),
              "installing should require a second explicit confirmation")) return 1;
  ui.closeSettings(state);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findLabel(lv_screen_active(),
                        "0.67 MS  \xC2\xB7  BUFFER 60%"),
              "the top rail should reflect the active latency setting and buffer use")) return 1;
  bankUpLabel = findLabel(lv_screen_active(), "+");
  bankUpButton = bankUpLabel ? lv_obj_get_parent(bankUpLabel) : nullptr;
  tunerButtonLabel = findLabel(lv_screen_active(), "TUNER");
  tunerButton = tunerButtonLabel ? lv_obj_get_parent(tunerButtonLabel) : nullptr;
  editButtonLabel = findLabel(lv_screen_active(), "EDIT");
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
  // The scenes rail also has a LOOPER button; the lock legend is unique to
  // the looper's own header, which sits directly on the looper layer.
  auto* looperLock = findLabel(lv_screen_active(), "LOCKED · AMBIENT LEAD");
  auto* looperRoot = looperLock ? lv_obj_get_parent(looperLock) : nullptr;
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
  if (require(blocksButton && lv_obj_get_width(blocksButton) >= 124 && lv_obj_get_height(blocksButton) == 60,
              "Modules should have a large, finger-friendly hit target")) return 1;
  lv_obj_send_event(blocksButton, LV_EVENT_PRESSED, nullptr);
  if (require(state.mode == ardor::UiMode::Edit && state.blockDrawerOpen,
              "pressing Modules should reliably keep the edit screen open and show the drawer")) return 1;
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  lv_obj_t* drawer = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::panel), 480, 720);
  lv_obj_t* allFilter = drawer ? findLabel(drawer, "ALL") : nullptr;
  lv_obj_t* utilityFilter = drawer ? findLabel(drawer, "UTILITY") : nullptr;
  lv_obj_t* delayFilter = drawer ? findLabel(drawer, "DELAYS") : nullptr;
  lv_obj_t* reverbFilter = drawer ? findLabel(drawer, "REVERBS") : nullptr;
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
  if (require(splitTitleArea.y2 <= splitReasonArea.y1
                && splitTitleArea.x1 == splitReasonArea.x1
                && splitReasonArea.y2 < splitButtonArea.y2,
              "a disabled Split row should give its reason on the subtitle line")) return 1;
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
  if (require(lv_color_eq(lv_obj_get_style_bg_color(drawer, LV_PART_MAIN), lv_color_hex(ardor::lvgl_ui::panel)),
              "block drawer should use the Lamp Black plate")) return 1;
  if (require(lv_obj_get_y(delayFilterButton) > lv_obj_get_y(allFilterButton)
              && lv_obj_get_y(reverbFilterButton) == lv_obj_get_y(delayFilterButton)
                && lv_obj_get_width(allFilterButton) == 102 && lv_obj_get_height(allFilterButton) == 52,
              "all seven drawer filters should fill a fixed two-row touch grid")) return 1;
  lv_obj_t* categorySlider = findObjectOfClass(drawer, &lv_slider_class);
  lv_obj_t* filterRow = lv_obj_get_parent(allFilterButton);
  if (require(!categorySlider && !lv_obj_has_flag(filterRow, LV_OBJ_FLAG_SCROLLABLE),
              "category grid should have no competing slider or native scrolling")) return 1;
  lv_obj_t* drawerInstruction = findLabel(drawer, "INSERT AFTER ");
  lv_area_t filterArea{};
  lv_area_t instructionArea{};
  lv_area_t retainedListArea{};
  if (require(drawerInstruction, "drawer should name the insert point")) return 1;
  lv_obj_get_coords(filterRow, &filterArea);
  lv_obj_get_coords(drawerInstruction, &instructionArea);
  lv_obj_get_coords(retainedAssetList, &retainedListArea);
  if (require(instructionArea.y2 < filterArea.y1 && filterArea.y2 < retainedListArea.y1,
              "the insert line, filters, and asset list should not overlap")) return 1;
  // Rows bleed 12 px past the content column for their pressed highlight.
  if (require(filterArea.x1 == instructionArea.x1
                && retainedListArea.x1 == filterArea.x1 - 12,
              "drawer sections should share one left edge")) return 1;
  {
    // Each row carries a family-coloured type-code square instead of a thin tick.
    const auto tremAsset = std::find_if(state.assets.begin(), state.assets.end(),
      [](const ardor::UiAsset& asset) { return asset.name == "Vintage Trem"; });
    lv_obj_t* tremCode = tremAsset == state.assets.end() ? nullptr
      : findLabel(tremAssetButton, ardor::assetCode(tremAsset->name, tremAsset->blockType).c_str());
    if (require(tremCode && lv_color_eq(lv_obj_get_style_bg_color(lv_obj_get_parent(tremCode), LV_PART_MAIN),
                                        lv_color_hex(ardor::lvgl_ui::categoryColor(tremAsset->type)))
                  && lv_obj_get_width(lv_obj_get_parent(tremCode)) == 52,
                "drawer rows should show a family-coloured type-code square")) return 1;
  }
  lv_obj_send_event(utilityFilterButton, LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(lv_color_eq(lv_obj_get_style_bg_color(utilityFilterButton, LV_PART_MAIN),
                          lv_color_hex(ardor::lvgl_ui::text))
                && lv_color_eq(lv_obj_get_style_text_color(utilityFilter, LV_PART_MAIN),
                               lv_color_hex(ardor::lvgl_ui::bg))
                && lv_color_eq(lv_obj_get_style_bg_color(allFilterButton, LV_PART_MAIN),
                               lv_color_hex(ardor::lvgl_ui::bg))
                && lv_color_eq(lv_obj_get_style_text_color(allFilter, LV_PART_MAIN),
                               lv_color_hex(ardor::lvgl_ui::text)),
              "the chosen filter should invert to bone with dark lettering, using palette tokens")) return 1;
  if (require(state.categoryFilter == "utility"
                && !lv_obj_has_flag(compressorAssetButton, LV_OBJ_FLAG_HIDDEN)
                && !lv_obj_has_flag(noiseGateAssetButton, LV_OBJ_FLAG_HIDDEN)
                && !lv_obj_has_flag(eqAssetButton, LV_OBJ_FLAG_HIDDEN)
                && lv_obj_has_flag(tremAssetButton, LV_OBJ_FLAG_HIDDEN),
              "Utility should show compressor, noise gate, and EQ while hiding modulation effects")) return 1;
  lv_obj_send_event(delayFilterButton, LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::panel), 480, 720);
  categorySlider = findObjectOfClass(drawer, &lv_slider_class);
  allFilter = drawer ? findLabel(drawer, "ALL") : nullptr;
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
  if (require(tremAssetButton && lv_color_eq(lv_obj_get_style_bg_color(tremAssetButton, LV_PART_MAIN), lv_color_hex(ardor::lvgl_ui::panel)),
              "drawer asset tiles should be charcoal")) return 1;
  if (require(lv_obj_get_height(tremAssetButton) == 72,
              "drawer asset tiles should have large vertical touch targets")) return 1;
  {
    // Child order is fixed: 0 title, 1 code square, 2 subtitle, 3 the + target.
    lv_obj_t* rows = lv_obj_get_parent(tremAssetButton);
    int checkedRows = 0;
    bool clear = true;
    for (uint32_t row = 0; row < lv_obj_get_child_count(rows); ++row) {
      lv_obj_t* item = lv_obj_get_child(rows, static_cast<int32_t>(row));
      if (lv_obj_has_flag(item, LV_OBJ_FLAG_HIDDEN) || lv_obj_get_child_count(item) < 4) continue;
      lv_area_t subtitleArea{};
      lv_area_t gripArea{};
      lv_obj_get_coords(lv_obj_get_child(item, 2), &subtitleArea);
      lv_obj_get_coords(lv_obj_get_child(item, 3), &gripArea);
      clear = clear && subtitleArea.x2 < gripArea.x1;
      ++checkedRows;
    }
    if (require(checkedRows > 0 && clear,
                "drawer subtitles should end before the + target")) return 1;
  }
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
  drawer = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::panel), 480, 720);
  tremAssetLabel = drawer ? findLabel(drawer, "TAPE DELAY") : nullptr;
  tremAssetButton = tremAssetLabel ? lv_obj_get_parent(tremAssetLabel) : nullptr;
  roomReverbLabel = drawer ? findLabel(drawer, "ROOM REVERB") : nullptr;
  roomReverbButton = roomReverbLabel ? lv_obj_get_parent(roomReverbLabel) : nullptr;
  if (require(state.categoryFilter == "reverb" && tremAssetButton && roomReverbButton
                && lv_obj_has_flag(tremAssetButton, LV_OBJ_FLAG_HIDDEN)
                && !lv_obj_has_flag(roomReverbButton, LV_OBJ_FLAG_HIDDEN),
              "Reverbs should show reverb assets and hide delay assets")) return 1;

  allFilter = drawer ? findLabel(drawer, "ALL") : nullptr;
  lv_obj_send_event(lv_obj_get_parent(allFilter), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::panel), 480, 720);
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
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::panel), 480, 720) == drawer,
              "pending refresh should not delete the drawer during scrolling")) return 1;
  lv_obj_send_event(assetList, LV_EVENT_SCROLL_END, nullptr);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::panel), 480, 720);
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
  drawer = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::panel), 480, 720);
  firstAssetLabel = drawer ? findLabel(drawer, upper(fullState.assets.front().name).c_str()) : nullptr;
  if (require(drawer && findLabel(drawer, "CHAIN FULL - DELETE A BLOCK TO ADD")
                && firstAssetLabel
                && lv_obj_has_state(lv_obj_get_parent(firstAssetLabel), LV_STATE_DISABLED),
              "full chain should explain why drawer items are disabled")) return 1;
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());

  // The scrim dims the chain (Panel plate at ~55% opacity) rather than
  // covering it outright, so the chosen insertion point stays visible.
  lv_obj_t* scrim = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x08090a), 800, 720);
  if (require(scrim && lv_obj_has_flag(scrim, LV_OBJ_FLAG_CLICKABLE)
                && lv_obj_get_style_bg_opa(scrim, LV_PART_MAIN) == 184,
              "block drawer should dim the chain with a tappable modal scrim")) return 1;
  lv_obj_send_event(scrim, LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(!state.blockDrawerOpen, "tapping outside the block drawer should close it")) return 1;
  ardor::openBlockDrawer(state);
  ui.build(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  drawer = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::panel), 480, 720);
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
                               lv_color_hex(ardor::lvgl_ui::text)),
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
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::panel), 1280, 548),
              "EQ should open as the main editor surface")) return 1;
  lv_obj_t* eqPanel = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::panel), 1280, 548);
  lv_obj_t* eqGraph = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::bg), 1232, 186);
  if (require(eqGraph,
              "EQ main editor should reserve a tall response graph")) return 1;
  if (require(findLabel(eqPanel, "FIVE BAND EQ"), "EQ should render its block name")) return 1;
  if (require(findLabelContaining(lv_screen_active(), "BAND 1"), "EQ should name the selected band")) return 1;
  if (require(findLabel(lv_screen_active(), "RESET BAND"), "EQ should render a reset-band control")) return 1;
  lv_obj_t* eqHeaderDelete = lv_obj_get_parent(findLastLabel(lv_screen_active(), "DELETE"));
  lv_obj_t* eqBypassLabel = findLabel(eqPanel, "BLOCK");
  lv_obj_t* eqHeaderBypass = eqBypassLabel ? lv_obj_get_parent(eqBypassLabel) : nullptr;
  lv_obj_t* eqHeaderClose = lv_obj_get_parent(findLastLabel(lv_screen_active(), "DONE"));
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
                && eqCloseArea.y1 >= 612,
              "EQ Delete and Block share the header row; Done sits on the rail")) return 1;
  if (require(eqGraphArea.y1 > std::max(eqDeleteArea.y2, eqBypassArea.y2) + 10,
              "EQ response graph should not overlap the header actions")) return 1;
  lv_obj_t* eqNodeLabel = findLabel(eqGraph, "B1");
  if (require(eqNodeLabel && lv_obj_get_width(lv_obj_get_parent(eqNodeLabel)) == 44
                && lv_obj_get_height(lv_obj_get_parent(eqNodeLabel)) == 44,
              "EQ graph nodes should be finger-sized targets")) return 1;
  lv_obj_t* eqNode = lv_obj_get_parent(eqNodeLabel);
  lv_obj_t* eqNodeMark = lv_obj_get_child(eqNode, 0);
  if (require(eqNodeMark && lv_obj_get_width(eqNodeMark) == 19 && lv_obj_get_height(eqNodeMark) == 19
                && lv_color_eq(lv_obj_get_style_bg_color(eqNodeMark, LV_PART_MAIN), lv_color_hex(ardor::lvgl_ui::lamp)),
              "the selected band's node mark should be sized and coloured on the very first render, "
              "not only after the next drag or slider tweak repaints the graph")) return 1;
  lv_obj_t* frequencyLabel = findLabel(lv_screen_active(), "FREQUENCY");
  lv_obj_t* qLabel = findLabel(lv_screen_active(), "Q");
  lv_obj_t* gainLabel = findLabel(lv_screen_active(), "GAIN");
  if (require(frequencyLabel && qLabel && gainLabel,
              "EQ should render frequency, Q, and gain as dedicated sliders")) return 1;
  lv_obj_t* frequencySlider = lv_obj_get_parent(frequencyLabel);
  const int freqFillWidthBeforeNodeDrag = [&] {
    lv_obj_t* fill = findTravelFill(frequencySlider);
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
  lv_obj_t* freqFillDuringNodeDrag = findTravelFill(frequencySlider);
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
  lv_obj_t* qFill = findTravelFill(qSlider);
  if (require(qFill && !findObjectOfClass(qSlider, &lv_arc_class)
                && lv_obj_get_width(qSlider) == 403 && lv_obj_get_height(qSlider) == 166
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
  lv_obj_t* graph = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::bg), 1232, 186);
  lv_obj_t* responseLine = findLineWithPointCount(graph, ardor::kEqCurvePointCount);
  lv_obj_t* gainSlider = lv_obj_get_parent(findLabel(lv_screen_active(), "GAIN"));
  // Earlier drags moved band 1 and narrowed it, so compare the whole curve.
  std::vector<int32_t> responseYBefore;
  for (uint32_t point = 0; point < lv_line_get_point_count(responseLine); ++point) {
    responseYBefore.push_back(lv_line_get_points(responseLine)[point].y);
  }
  const auto responseChanged = [&]() {
    for (uint32_t point = 0; point < lv_line_get_point_count(responseLine); ++point) {
      if (lv_line_get_points(responseLine)[point].y != responseYBefore[point]) return true;
    }
    return false;
  };
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
  if (require(responseChanged(),
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
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::bg), 1232, 186)
                == retainedEqGraph
                && lv_obj_get_parent(findLabel(lv_screen_active(), "Q")) == retainedQSlider,
              "EQ band selection should retain the response graph and slider objects")) return 1;
  lv_obj_t* highPassLabel = findLabelContaining(lv_screen_active(), "HP  ");
  lv_obj_send_event(lv_obj_get_parent(highPassLabel), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(findLabelContaining(lv_screen_active(), "HIGH-PASS FILTER") != nullptr,
              "EQ should expose a selectable high-pass stage")) return 1;
  lv_obj_t* filterOffLabel = findLabel(lv_screen_active(), "FILTER OFF");
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
  lv_obj_t* deleteBlockLabel = findLastLabel(lv_screen_active(), "DELETE");
  if (require(deleteBlockLabel && lv_obj_get_width(lv_obj_get_parent(deleteBlockLabel)) >= 124
                && lv_obj_get_height(lv_obj_get_parent(deleteBlockLabel)) >= 48,
              "EQ should render a finger-sized delete-block control")) return 1;
  if (require(findLineWithPointCount(lv_screen_active(), ardor::kEqCurvePointCount),
              "EQ should render a sampled response curve")) return 1;
  lv_obj_t* eqCloseLabel = findLastLabel(lv_screen_active(), "DONE");
  lv_obj_t* eqCloseButton = eqCloseLabel ? lv_obj_get_parent(eqCloseLabel) : nullptr;
  lv_obj_send_event(eqCloseButton, LV_EVENT_PRESSED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(!state.paramDrawerOpen, "EQ close should act on touch-down")) return 1;
  if (require(!ui.applyFocusedParameterDelta(state, 1),
              "closing the editor should clear stale hardware-encoder focus")) return 1;
  ui.selectBlock(state, state.bank.presets[state.activePreset].blocks.size() - 1);
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  if (require(findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::bg), 1232, 186)
                == retainedEqGraph,
              "closing and reopening EQ should reactivate the retained editor")) return 1;
  deleteBlockLabel = findLastLabel(lv_screen_active(), "DELETE");
  const auto blocksBeforeDelete = state.bank.presets[state.activePreset].blocks.size();
  lv_obj_send_event(lv_obj_get_parent(deleteBlockLabel), LV_EVENT_CLICKED, nullptr);
  ui.refresh(lv_screen_active(), state);
  if (require(state.bank.presets[state.activePreset].blocks.size() == blocksBeforeDelete - 1,
              "delete-block control should remove the selected EQ block")) return 1;

  ardor::enterTunerMode(state);
  ardor::updateTunerTelemetry(state, {true, 82.4f, -2.0f, 0.96f, "E", 2});
  ui.refresh(lv_screen_active(), state);
  lv_obj_update_layout(lv_screen_active());
  // The tuner layer precedes the looper's, so the first EXIT is the tuner's.
  lv_obj_t* tunerExitLabel = findLabel(lv_screen_active(), "EXIT");
  lv_obj_t* tunerExitButton = tunerExitLabel ? lv_obj_get_parent(tunerExitLabel) : nullptr;
  if (require(findLabel(lv_screen_active(), "TUNER")
                && findLabel(lv_screen_active(), "OUTPUT MUTED")
                && findLabel(lv_screen_active(), "E2")
                && findLabel(lv_screen_active(), "FLAT")
                && findLabel(lv_screen_active(), "IN TUNE")
                && findLabel(lv_screen_active(), "SHARP")
                && findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(ardor::lvgl_ui::lamp),
                                                520, 250)
                && findLabel(lv_screen_active(), "PRESS ANY FOOTSWITCH TO EXIT")
                && tunerExitButton && lv_obj_get_width(tunerExitButton) == 124
                && lv_obj_get_height(tunerExitButton) == 60,
              "tuner mode should flood the note plate when in tune and show the verdict row")) return 1;
  lv_obj_send_event(tunerExitButton, LV_EVENT_PRESSED, nullptr);
  if (require(requestedTunerMode == 0 && state.mode == ardor::UiMode::Tuner,
              "the tuner Exit button should request a host-level audio restore")) return 1;
  requestedTunerMode = -1;
  ardor::enterPresetMode(state);
  ui.refresh(lv_screen_active(), state);
  if (require(findLabel(lv_screen_active(), "EDIT"),
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
  // Other dialogs carry the same legends, so search inside the prompt.
  lv_obj_t* navigationTitle = findLabel(lv_screen_active(), "UNSAVED CHANGES");
  lv_obj_t* navigationDialog = navigationTitle ? lv_obj_get_parent(navigationTitle) : nullptr;
  if (require(navigationState.activePreset == 0 && navigationState.navigationPrompt.has_value()
                && navigationDialog
                && findLabel(navigationDialog, "SAVE") && findLabel(navigationDialog, "DISCARD")
                && findLabel(navigationDialog, "CANCEL"),
              "dirty navigation should retain the draft and present Save/Discard/Cancel")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(navigationDialog, "CANCEL")), LV_EVENT_CLICKED, nullptr);
  if (require(navigationDecision == ardor::UiNavigationDecision::Cancel
                && !navigationState.navigationPrompt.has_value() && navigationState.activePreset == 0,
              "Cancel should retain the current draft and selection")) return 1;
  navigationUi.selectPreset(navigationState, 1);
  navigationUi.refresh(lv_screen_active(), navigationState);
  lv_obj_send_event(lv_obj_get_parent(findLabel(navigationDialog, "DISCARD")), LV_EVENT_CLICKED, nullptr);
  if (require(navigationDecision == ardor::UiNavigationDecision::Discard
                && !navigationState.navigationPrompt.has_value(),
              "Discard should release the selected destination for activation")) return 1;

  auto pickerState = ardor::makeDemoUiState();
  const auto whammyAsset = std::find_if(pickerState.assets.begin(), pickerState.assets.end(),
    [](const ardor::UiAsset& asset) { return asset.name == "Whammy"; });
  if (require(whammyAsset != pickerState.assets.end(),
              "Whammy should be available for the choice picker test")) return 1;
  ardor::appendAssetBlock(pickerState, static_cast<std::size_t>(
    std::distance(pickerState.assets.begin(), whammyAsset)));
  completePreview(pickerState);
  ardor::enterEditMode(pickerState);
  ardor::LvglUi pickerUi;
  pickerUi.selectBlock(pickerState,
                       pickerState.bank.presets[pickerState.activePreset].blocks.size() - 1);
  pickerUi.build(lv_screen_active(), pickerState);
  lv_obj_t* allOptions = findLastLabel(lv_screen_active(), "ALL OPTIONS");
  if (require(allOptions && !findLabel(lv_screen_active(), "19 OPTIONS"),
              "Whammy should show the All Options control before opening its picker")) return 1;
  lv_obj_send_event(lv_obj_get_parent(allOptions), LV_EVENT_CLICKED, nullptr);
  if (require(findLabel(lv_screen_active(), "19 OPTIONS")
                && findLabel(lv_screen_active(), "HARMONY")
                && findLabel(lv_screen_active(), "Dive bomb"),
              "touching All Options should open the grouped Whammy choice picker")) return 1;
  lv_obj_send_event(lv_obj_get_parent(findLabel(lv_screen_active(), "Dive bomb")),
                    LV_EVENT_CLICKED, nullptr);
  if (require(!findLabel(lv_screen_active(), "19 OPTIONS")
                && pickerState.bank.presets[pickerState.activePreset].blocks.back()
                     .params.value("p2", 0.0f) > 0.0f,
              "selecting a Whammy option should update the preset and close the picker")) return 1;

  auto harmonizerState = ardor::makeDemoUiState();
  const auto harmonizerAsset = std::find_if(harmonizerState.assets.begin(), harmonizerState.assets.end(),
    [](const ardor::UiAsset& asset) { return asset.name == "Harmonizer"; });
  if (require(harmonizerAsset != harmonizerState.assets.end(),
              "Harmonizer should be available for the map picker test")) return 1;
  ardor::appendAssetBlock(harmonizerState, static_cast<std::size_t>(
    std::distance(harmonizerState.assets.begin(), harmonizerAsset)));
  completePreview(harmonizerState);
  ardor::enterEditMode(harmonizerState);
  ardor::LvglUi harmonizerUi;
  harmonizerUi.selectBlock(harmonizerState,
    harmonizerState.bank.presets[harmonizerState.activePreset].blocks.size() - 1);
  harmonizerUi.build(lv_screen_active(), harmonizerState);
  lv_obj_t* openMap = findLastLabel(lv_screen_active(), "OPEN MAP");
  if (require(openMap && !findLabel(lv_screen_active(), "KEY & INTERVAL"),
              "Harmonizer should show Open Map before opening its picker")) return 1;
  lv_obj_send_event(lv_obj_get_parent(openMap), LV_EVENT_CLICKED, nullptr);
  if (require(findLabel(lv_screen_active(), "KEY & INTERVAL"),
              "touching Open Map should show the Harmonizer picker")) return 1;

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
  // The GR readout sits just before the Block control in the header row.
  lv_obj_t* gainMeterBlockButton = lv_obj_get_parent(findLastLabel(lv_screen_active(), "BLOCK"));
  lv_area_t gainMeterArea{};
  lv_area_t gainMeterBlockArea{};
  lv_obj_get_coords(gainMeterPill, &gainMeterArea);
  lv_obj_get_coords(gainMeterBlockButton, &gainMeterBlockArea);
  if (require(lv_obj_get_width(gainMeterPill) == 124 && lv_obj_get_height(gainMeterPill) == 60
                && gainMeterArea.x2 < gainMeterBlockArea.x1
                && gainMeterArea.y1 == gainMeterBlockArea.y1,
              "the gain-reduction meter should sit as a fixed-size readout just before Block"))
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
