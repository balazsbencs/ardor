#include "ui/LvglUi.h"

#include "ui/fonts/OpenSansRegular.h"
#include "ui/fonts/OpenSansSemibold.h"

#include "daisyfx/DaisyFxCatalog.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <string>
#include <utility>

namespace ardor {

namespace {

// The layout below is authored against this fixed design grid; build() scales
// the whole canvas to the real display resolution.
constexpr int32_t kDesignWidth = 1280;
constexpr int32_t kDesignHeight = 720;

constexpr auto bg = 0x000000;
constexpr auto panel = 0x242424;
constexpr auto panelAlt = 0x242424;
constexpr auto text = 0xf5f5f5;
constexpr auto muted = 0xa6a6a6;
constexpr auto accent = 0x43f05a;
constexpr auto eqCombined = 0xff9f43;
constexpr std::array<int, kParametricEqBandCount> eqBandColors = {
  0x56c7ff, 0x8be28b, 0xf5d76e, 0xff8c69, 0xc792ea,
};
constexpr int kEqPanelTop = 100;
constexpr int kEqPanelHeight = 600;
constexpr int kEqGraphX = 28;
constexpr int kEqGraphY = 60;
constexpr int kEqGraphWidth = 1184;
constexpr int kEqGraphHeight = 270;
constexpr int kEqGraphCanvasX = (kDesignWidth - 1240) / 2 + kEqGraphX;
constexpr int kEqGraphCanvasY = kEqPanelTop + kEqGraphY;
constexpr int kChainLeft = 34;
constexpr int kChainWidth = 1212;
constexpr int kChainTop = 110;
constexpr int kChainHeight = 276;
constexpr int kChainColumns = 5;
constexpr int kChainRows = 2;
constexpr int kChainSlotWidth = kChainWidth / kChainColumns;
constexpr int kChainTileHeight = 92;
constexpr int kChainRowHeight = 140;
constexpr int kChainTileTop = 17;
constexpr int kChainTileWidth = kChainSlotWidth - 10;
constexpr int kChainFirstRowCentreY = kChainTop + kChainTileTop + kChainTileHeight / 2;
constexpr int kChainSecondRowCentreY = kChainFirstRowCentreY + kChainRowHeight;
constexpr int kChainConnectorY = (kChainTop + kChainTileTop + kChainTileHeight
                                  + kChainTop + kChainTileTop + kChainRowHeight) / 2 + 10;
constexpr int kChainFirstRowEndX = kChainLeft + (kChainColumns - 1) * kChainSlotWidth + kChainTileWidth - 1;
constexpr int kChainConnectorRightX = kChainLeft + kChainColumns * kChainSlotWidth - 1;
constexpr int kChainConnectorLeftX = kChainLeft - 10;
constexpr int kChainConnectorRadius = 10;
constexpr float kKnobPointerStartDegrees = -135.0f;
constexpr float kKnobPointerSweepDegrees = 270.0f;
constexpr int32_t kKnobPointerCentre = 28;
constexpr int32_t kKnobPointerLength = 20;
constexpr int32_t kKnobArcDiameter = 64;
constexpr int32_t kKnobStrokeWidth = 4;
constexpr int32_t kKnobStrokeGap = 4;
constexpr int32_t kKnobRimDiameter = kKnobArcDiameter - 2 * (kKnobStrokeWidth + kKnobStrokeGap);
constexpr int32_t kKnobCentreDiameter = kKnobRimDiameter - 2 * kKnobStrokeWidth;

void updateKnobPointer(lv_obj_t* pointer, float ratio)
{
  lv_point_precise_t* points = lv_line_get_points_mutable(pointer);
  if (!points || lv_line_get_point_count(pointer) != 2) {
    return;
  }

  constexpr float degreesToRadians = 3.14159265358979323846f / 180.0f;
  const float angle = (kKnobPointerStartDegrees + ratio * kKnobPointerSweepDegrees) * degreesToRadians;
  points[0].x = kKnobPointerCentre;
  points[0].y = kKnobPointerCentre;
  points[1].x = kKnobPointerCentre + static_cast<int32_t>(std::lround(std::sin(angle) * kKnobPointerLength));
  points[1].y = kKnobPointerCentre - static_cast<int32_t>(std::lround(std::cos(angle) * kKnobPointerLength));
  lv_line_set_points_mutable(pointer, points, 2);
}

void freeKnobPointerPoints(lv_event_t* event)
{
  lv_free(lv_event_get_user_data(event));
}

void freeLinePoints(lv_event_t* event)
{
  lv_free(lv_event_get_user_data(event));
}

void setText(lv_obj_t* object, int color = text, const lv_font_t* font = &ardor_font_open_sans_regular_18)
{
  lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(object, font, 0);
}

void styleSurface(lv_obj_t* object, int color = panel)
{
  lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_radius(object, 5, 0);
  lv_obj_set_style_shadow_width(object, 0, 0);
}

lv_obj_t* label(lv_obj_t* parent, const std::string& value, lv_align_t align, int x, int y,
                const lv_font_t* font = &ardor_font_open_sans_regular_18, int color = text)
{
  lv_obj_t* object = lv_label_create(parent);
  lv_label_set_text(object, value.c_str());
  setText(object, color, font);
  lv_obj_align(object, align, x, y);
  return object;
}

void redraw(UiEventContext* context)
{
  context->ui->requestRebuild();
}

lv_obj_t* button(lv_obj_t* parent, const std::string& value);

void renderChainWrapConnector(lv_obj_t* root)
{
  // These short samples approximate the four quarter-circles in the wrap,
  // keeping the signal path visibly continuous without sharp corners.
  static constexpr std::array<lv_point_precise_t, 20> route = {{
    {kChainFirstRowEndX, kChainFirstRowCentreY},
    {kChainConnectorRightX - 6, kChainFirstRowCentreY + 1},
    {kChainConnectorRightX - 3, kChainFirstRowCentreY + 3},
    {kChainConnectorRightX - 1, kChainFirstRowCentreY + 6},
    {kChainConnectorRightX, kChainFirstRowCentreY + kChainConnectorRadius},
    {kChainConnectorRightX, kChainConnectorY - kChainConnectorRadius},
    {kChainConnectorRightX - 1, kChainConnectorY - 6},
    {kChainConnectorRightX - 3, kChainConnectorY - 3},
    {kChainConnectorRightX - 6, kChainConnectorY - 1},
    {kChainConnectorRightX - kChainConnectorRadius, kChainConnectorY},
    {kChainConnectorLeftX + kChainConnectorRadius, kChainConnectorY},
    {kChainConnectorLeftX + 6, kChainConnectorY + 1},
    {kChainConnectorLeftX + 3, kChainConnectorY + 3},
    {kChainConnectorLeftX + 1, kChainConnectorY + 6},
    {kChainConnectorLeftX, kChainConnectorY + kChainConnectorRadius},
    {kChainConnectorLeftX, kChainSecondRowCentreY - kChainConnectorRadius},
    {kChainConnectorLeftX + 1, kChainSecondRowCentreY - 6},
    {kChainConnectorLeftX + 3, kChainSecondRowCentreY - 3},
    {kChainConnectorLeftX + 6, kChainSecondRowCentreY - 1},
    {kChainConnectorLeftX + kChainConnectorRadius, kChainSecondRowCentreY},
  }};
  lv_obj_t* connector = lv_line_create(root);
  lv_obj_set_size(connector, kDesignWidth, kDesignHeight);
  lv_line_set_points(connector, route.data(), route.size());
  lv_obj_set_style_line_color(connector, lv_color_hex(muted), LV_PART_MAIN);
  lv_obj_set_style_line_width(connector, 2, LV_PART_MAIN);
  lv_obj_set_style_line_opa(connector, LV_OPA_70, LV_PART_MAIN);
  lv_obj_set_style_line_rounded(connector, true, LV_PART_MAIN);
  lv_obj_remove_flag(connector, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(connector, LV_OBJ_FLAG_CLICKABLE);
}

void onPresetClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectPreset(*context->state, context->index);
  redraw(context);
}

void onSaveClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().savePreset) {
    context->ui->actions().savePreset();
  }
  redraw(context);
}

void onPresetModeClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  enterPresetMode(*context->state);
  redraw(context);
}

void onEditModeClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  enterEditMode(*context->state);
  redraw(context);
}

void onOpenBlockDrawer(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  openBlockDrawer(*context->state);
  redraw(context);
}

void onCloseBlockDrawer(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  closeBlockDrawer(*context->state);
  redraw(context);
}

void onCloseParamDrawer(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  closeParamDrawer(*context->state);
  redraw(context);
}

void onDeleteSelectedBlock(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (deleteSelectedBlock(*context->state)) {
    context->ui->resetParameterPage();
  }
  redraw(context);
}

void onGlobalParamsClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectGlobalParams(*context->state);
  redraw(context);
}

void changeParameterPage(UiEventContext* context, int delta)
{
  const auto count = parameterPageCount(*context->state);
  if (count == 0) {
    return;
  }
  const auto current = context->ui->parameterPage();
  const auto next = delta < 0
    ? (current == 0 ? 0 : current - 1)
    : std::min(current + 1, count - 1);
  if (next != current) {
    context->ui->setParameterPage(next);
    context->ui->focusParameter("");
    redraw(context);
  }
}

void onPreviousParameterPage(lv_event_t* event)
{
  changeParameterPage(static_cast<UiEventContext*>(lv_event_get_user_data(event)), -1);
}

void onNextParameterPage(lv_event_t* event)
{
  changeParameterPage(static_cast<UiEventContext*>(lv_event_get_user_data(event)), 1);
}

void onParameterGesture(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }
  const auto direction = lv_indev_get_gesture_dir(input);
  if (direction == LV_DIR_LEFT) {
    changeParameterPage(context, 1);
  } else if (direction == LV_DIR_RIGHT) {
    changeParameterPage(context, -1);
  }
}

void onKnobPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }
  lv_indev_get_point(input, &context->pressPoint);
  context->pressPoint = context->ui->toCanvas(context->pressPoint);
  const auto index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event)));
  const auto controls = parameterPage(*context->state, context->ui->parameterPage());
  if (index == 0 || index > controls.size()) {
    return;
  }
  context->filter = controls[index - 1].key;
  context->ui->beginKnobInteraction();
  context->ui->focusParameter(context->filter);
}

void refreshKnobVisual(lv_obj_t* knob, const ParameterControl& control)
{
  const float range = control.maximum - control.minimum;
  const float ratio = range == 0.0f ? 0.0f : std::clamp((control.value - control.minimum) / range, 0.0f, 1.0f);

  for (uint32_t index = 0; index < lv_obj_get_child_count(knob); ++index) {
    lv_obj_t* child = lv_obj_get_child(knob, static_cast<int32_t>(index));
    if (lv_obj_check_type(child, &lv_arc_class)) {
      lv_arc_set_value(child, static_cast<int>(std::lround(ratio * 1000.0f)));
    } else if (lv_obj_check_type(child, &lv_label_class)) {
      if (control.label == lv_label_get_text(child)) {
        lv_obj_set_style_text_color(child, lv_color_hex(accent), 0);
      } else {
        lv_label_set_text(child, control.formatted.c_str());
      }
    } else if (lv_obj_get_width(child) == 56 && lv_obj_get_height(child) == 56) {
      lv_obj_t* marker = lv_obj_get_child(child, 0);
      if (marker && lv_obj_check_type(marker, &lv_line_class)) {
        updateKnobPointer(marker, ratio);
      }
    }
  }
}

void onKnobPressing(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }
  lv_point_t current{};
  lv_indev_get_point(input, &current);
  current = context->ui->toCanvas(current);
  const int delta = (context->pressPoint.y - current.y) / 12;
  if (delta == 0) {
    return;
  }
  context->pressPoint.y = current.y;
  if (context->ui->applyFocusedParameterDelta(*context->state, delta)) {
    const auto controls = parameterPage(*context->state, context->ui->parameterPage());
    const auto control = std::find_if(controls.begin(), controls.end(), [context](const auto& item) {
      return item.key == context->filter;
    });
    if (control != controls.end()) {
      refreshKnobVisual(lv_event_get_target_obj(event), *control);
    }
  }
}

void onKnobReleased(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->endKnobInteraction();
}

EqBandField eqBandFieldForKey(const std::string& key)
{
  if (key == "frequency") {
    return EqBandField::Frequency;
  }
  if (key == "q") {
    return EqBandField::Q;
  }
  return EqBandField::Gain;
}

ParameterControl eqKnobControl(EqBandField field, const EqBandParams& band);

void onEqKnobPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }
  const auto field = eqBandFieldForKey(context->filter);
  context->ui->selectEqBand(context->index);
  context->ui->focusEqBandField(field);
  lv_indev_get_point(input, &context->pressPoint);
  context->pressPoint = context->ui->toCanvas(context->pressPoint);
  context->ui->beginKnobInteraction();
}

void onEqKnobPressing(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }
  lv_point_t current{};
  lv_indev_get_point(input, &current);
  current = context->ui->toCanvas(current);
  const int delta = (context->pressPoint.y - current.y) / 12;
  if (delta == 0) {
    return;
  }
  context->pressPoint.y = current.y;
  if (context->ui->applyFocusedParameterDelta(*context->state, delta)) {
    const auto params = selectedParametricEqParams(*context->state);
    refreshKnobVisual(lv_event_get_target_obj(event),
                      eqKnobControl(eqBandFieldForKey(context->filter), params.bands[context->index]));
  }
}

void onBypassChanged(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_obj_t* switchObject = lv_event_get_target_obj(event);
  setSelectedBlockEnabled(*context->state, lv_obj_has_state(switchObject, LV_STATE_CHECKED));
  redraw(context);
}

void onEqBandSelected(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectEqBand(context->index);
  redraw(context);
}

void onEqBandEnabled(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectEqBand(context->index);
  auto params = selectedParametricEqParams(*context->state);
  auto& band = params.bands[context->index];
  band.enabled = !band.enabled;
  context->ui->updateSelectedEqBand(*context->state, band);
  redraw(context);
}

void onEqBandReset(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectEqBand(context->index);
  context->ui->updateSelectedEqBand(*context->state, defaultParametricEqBand(context->index));
  redraw(context);
}

void onEqNodePressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }
  context->ui->selectEqBand(context->index);
  context->ui->focusEqBandField(EqBandField::Gain);
  lv_indev_get_point(input, &context->pressPoint);
  context->pressPoint = context->ui->toCanvas(context->pressPoint);
  context->ui->beginKnobInteraction();
}

void onEqNodePressing(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }
  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  const int x = std::clamp(static_cast<int>(point.x) - kEqGraphCanvasX, 0, kEqGraphWidth - 1);
  const int y = std::clamp(static_cast<int>(point.y) - kEqGraphCanvasY, 0, kEqGraphHeight - 1);
  auto params = selectedParametricEqParams(*context->state);
  auto& band = params.bands[context->index];
  band.frequencyHz = eqFrequencyFromX(x, kEqGraphWidth);
  band.gainDb = eqGainFromY(y, kEqGraphHeight);
  context->ui->updateSelectedEqBand(*context->state, band);
  lv_obj_set_pos(lv_event_get_target_obj(event), x - 16, y - 16);
}

void onEqNodeReleased(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->endKnobInteraction();
  redraw(context);
}

void onBlockClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectBlock(*context->state, context->index);
  redraw(context);
}

std::size_t chainSlotFromPoint(const UiState& state, const lv_point_t& point)
{
  return LvglUi::chainSlotForPoint(state.bank.presets[state.activePreset].blocks.size(), point);
}

std::size_t insertSlotFromPoint(const UiState& state, const lv_point_t& point)
{
  return LvglUi::chainInsertionSlotForPoint(state.bank.presets[state.activePreset].blocks.size(), point);
}

bool pointInChain(const lv_point_t& point)
{
  return point.x >= 20 && point.x <= 1260 && point.y >= kChainTop && point.y <= kChainTop + kChainHeight;
}

void moveToFront(lv_obj_t* object)
{
  lv_obj_t* parent = lv_obj_get_parent(object);
  lv_obj_move_to_index(object, static_cast<int32_t>(lv_obj_get_child_count(parent)) - 1);
}

void placeDragIndicatorAtSlot(UiEventContext* context, std::size_t slot)
{
  const auto blockCount = context->state->bank.presets[context->state->activePreset].blocks.size();

  if (!context->indicator) {
    context->indicator = lv_obj_create(context->ui->canvas());
    lv_obj_set_size(context->indicator, 5, 92);
    lv_obj_set_style_bg_color(context->indicator, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(context->indicator, 0, 0);
    lv_obj_set_style_radius(context->indicator, 2, 0);
  }

  const auto position = LvglUi::chainIndicatorPosition(blockCount, slot);
  lv_obj_set_pos(context->indicator, position.x, position.y);
  moveToFront(context->indicator);
}

void placeDragIndicator(UiEventContext* context, const lv_point_t& point)
{
  placeDragIndicatorAtSlot(context, chainSlotFromPoint(*context->state, point));
}

void placeDragGhost(UiEventContext* context, const lv_point_t& point)
{
  if (!context->ghost) {
    std::string textValue = context->dragText;
    if (textValue.empty()) {
      const auto& block = context->state->bank.presets[context->state->activePreset].blocks[context->index];
      textValue = block.label + "\n" + block.assetName;
    }
    context->ghost = button(context->ui->canvas(), textValue);
    lv_obj_set_size(context->ghost, 160, 84);
    lv_obj_set_style_bg_color(context->ghost, lv_color_hex(0x243044), 0);
    lv_obj_set_style_opa(context->ghost, LV_OPA_50, 0);
    lv_obj_add_flag(context->ghost, LV_OBJ_FLAG_FLOATING);
  }

  lv_obj_set_pos(context->ghost, point.x - 80, point.y - 42);
  moveToFront(context->ghost);
}

void updateDragVisuals(UiEventContext* context, lv_event_t* event)
{
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }

  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  placeDragGhost(context, point);
  placeDragIndicator(context, point);
}

void clearDragVisuals(UiEventContext* context)
{
  if (context->ghost) {
    lv_obj_delete(context->ghost);
    context->ghost = nullptr;
  }
  if (context->indicator) {
    lv_obj_delete(context->indicator);
    context->indicator = nullptr;
  }
  context->dragging = false;
  context->dragText.clear();
}

void onBlockPressed(lv_event_t* event)
{
  lv_obj_set_style_opa(lv_event_get_target_obj(event), LV_OPA_50, 0);
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  updateDragVisuals(context, event);
}

void onBlockPressing(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  updateDragVisuals(context, event);
}

void onBlockReleased(lv_event_t* event)
{
  lv_obj_set_style_opa(lv_event_get_target_obj(event), LV_OPA_COVER, 0);

  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    clearDragVisuals(context);
    return;
  }

  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  const auto target = chainSlotFromPoint(*context->state, point);
  clearDragVisuals(context);
  if (target == context->index) {
    return;
  }

  moveBlock(*context->state, context->index, target);
  redraw(context);
}

void onFilterClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  setCategoryFilter(*context->state, context->filter);
  redraw(context);
}

void onAssetClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->suppressClick) {
    context->suppressClick = false;
    return;
  }
  appendAssetBlock(*context->state, context->index);
  redraw(context);
}

void onAssetPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->dragging = false;
  context->suppressClick = false;
  lv_indev_t* input = lv_event_get_indev(event);
  if (input) {
    lv_indev_get_point(input, &context->pressPoint);
    context->pressPoint = context->ui->toCanvas(context->pressPoint);
  }
}

std::string assetDragText(const UiAsset& asset)
{
  if (asset.type == "amps") {
    return "Neural Amp\n" + asset.name;
  }
  if (asset.type == "cabs") {
    return "Cab\n" + asset.name;
  }
  return asset.name;
}

void onAssetPressing(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }

  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  const int dx = point.x - context->pressPoint.x;
  const int dy = point.y - context->pressPoint.y;
  if (!context->dragging && dx * dx + dy * dy < 64) {
    return;
  }

  context->dragging = true;
  context->dragText = assetDragText(context->state->assets[context->index]);
  lv_obj_set_style_opa(lv_event_get_target_obj(event), LV_OPA_50, 0);
  placeDragGhost(context, point);
  if (pointInChain(point)) {
    placeDragIndicatorAtSlot(context, insertSlotFromPoint(*context->state, point));
  } else if (context->indicator) {
    lv_obj_delete(context->indicator);
    context->indicator = nullptr;
  }
}

void onAssetReleased(lv_event_t* event)
{
  lv_obj_set_style_opa(lv_event_get_target_obj(event), LV_OPA_COVER, 0);

  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (!context->dragging) {
    return;
  }

  context->suppressClick = true;
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    clearDragVisuals(context);
    return;
  }

  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  const bool droppedOnChain = pointInChain(point);
  const auto target = insertSlotFromPoint(*context->state, point);
  clearDragVisuals(context);
  if (!droppedOnChain) {
    return;
  }

  insertAssetBlock(*context->state, context->index, target);
  redraw(context);
}

lv_obj_t* button(lv_obj_t* parent, const std::string& value)
{
  lv_obj_t* object = lv_button_create(parent);
  styleSurface(object);
  lv_obj_t* buttonLabel = lv_label_create(object);
  lv_label_set_text(buttonLabel, value.c_str());
  setText(buttonLabel);
  lv_obj_center(buttonLabel);
  return object;
}

lv_obj_t* createKnob(lv_obj_t* parent, const ParameterControl& control, int x, int y, bool focused,
                     UiEventContext* context, lv_event_cb_t onPressed, lv_event_cb_t onPressing)
{
  const float range = control.maximum - control.minimum;
  const float ratio = range == 0.0f ? 0.0f : std::clamp((control.value - control.minimum) / range, 0.0f, 1.0f);

  lv_obj_t* knob = lv_obj_create(parent);
  lv_obj_set_size(knob, 154, 184);
  lv_obj_set_pos(knob, x, y);
  lv_obj_remove_flag(knob, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(knob, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(knob, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(knob, 0, 0);
  lv_obj_add_event_cb(knob, onPressed, LV_EVENT_PRESSED, context);
  lv_obj_add_event_cb(knob, onPressing, LV_EVENT_PRESSING, context);
  lv_obj_add_event_cb(knob, onKnobReleased, LV_EVENT_RELEASED, context);
  lv_obj_add_event_cb(knob, onKnobReleased, LV_EVENT_PRESS_LOST, context);

  lv_obj_t* arc = lv_arc_create(knob);
  lv_obj_set_size(arc, kKnobArcDiameter, kKnobArcDiameter);
  lv_obj_center(arc);
  lv_obj_set_y(arc, -25);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, 0, 1000);
  lv_arc_set_value(arc, static_cast<int>(std::lround(ratio * 1000.0f)));
  lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, kKnobStrokeWidth, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x454545), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, kKnobStrokeWidth, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(accent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* rim = lv_obj_create(knob);
  lv_obj_set_size(rim, kKnobRimDiameter, kKnobRimDiameter);
  lv_obj_align_to(rim, arc, LV_ALIGN_CENTER, 0, 0);
  styleSurface(rim, 0x000000);
  lv_obj_set_style_pad_all(rim, 0, 0);
  lv_obj_set_style_radius(rim, LV_RADIUS_CIRCLE, 0);
  lv_obj_remove_flag(rim, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(rim, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* centre = lv_obj_create(rim);
  lv_obj_set_size(centre, kKnobCentreDiameter, kKnobCentreDiameter);
  lv_obj_center(centre);
  styleSurface(centre, panel);
  lv_obj_set_style_radius(centre, LV_RADIUS_CIRCLE, 0);
  lv_obj_remove_flag(centre, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(centre, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* pointerLayer = lv_obj_create(knob);
  lv_obj_set_size(pointerLayer, 56, 56);
  lv_obj_align_to(pointerLayer, arc, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_opa(pointerLayer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pointerLayer, 0, 0);
  lv_obj_set_style_pad_all(pointerLayer, 0, 0);
  lv_obj_remove_flag(pointerLayer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(pointerLayer, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* pointer = lv_line_create(pointerLayer);
  lv_obj_set_size(pointer, 56, 56);
  lv_obj_set_pos(pointer, 0, 0);
  lv_obj_set_style_line_color(pointer, lv_color_hex(text), LV_PART_MAIN);
  lv_obj_set_style_line_width(pointer, 3, LV_PART_MAIN);
  lv_obj_set_style_line_rounded(pointer, true, LV_PART_MAIN);
  auto* pointerPoints = static_cast<lv_point_precise_t*>(lv_malloc(sizeof(lv_point_precise_t) * 2));
  LV_ASSERT_MALLOC(pointerPoints);
  if (pointerPoints) {
    lv_line_set_points_mutable(pointer, pointerPoints, 2);
    lv_obj_add_event_cb(pointer, freeKnobPointerPoints, LV_EVENT_DELETE, pointerPoints);
    updateKnobPointer(pointer, ratio);
  }

  label(knob, control.label, LV_ALIGN_BOTTOM_MID, 0, -30, &ardor_font_open_sans_semibold_22,
        focused ? accent : text);
  label(knob, control.formatted, LV_ALIGN_BOTTOM_MID, 0, -7, &ardor_font_open_sans_regular_18, muted);
  return knob;
}

void renderBypassSwitch(lv_obj_t* parent, UiState& state, UiEventContext* context)
{
  const auto& block = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
  label(parent, "Bypass", LV_ALIGN_TOP_LEFT, 970, 25, &ardor_font_open_sans_regular_18, muted);
  lv_obj_t* switchObject = lv_switch_create(parent);
  lv_obj_set_size(switchObject, 64, 30);
  lv_obj_set_pos(switchObject, 1060, 20);
  lv_obj_set_style_bg_color(switchObject, lv_color_hex(0x111111), LV_PART_MAIN);
  lv_obj_set_style_bg_color(switchObject, lv_color_hex(0x343434), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(switchObject, lv_color_hex(accent),
                            static_cast<lv_style_selector_t>(LV_PART_INDICATOR)
                              | static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
  lv_obj_set_style_bg_color(switchObject, lv_color_hex(text), LV_PART_KNOB);
  lv_obj_set_style_radius(switchObject, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_radius(switchObject, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_radius(switchObject, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_border_width(switchObject, 0, LV_PART_MAIN);
  if (block.enabled) {
    lv_obj_add_state(switchObject, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(switchObject, onBypassChanged, LV_EVENT_VALUE_CHANGED, context);
}

void renderPageNavigation(lv_obj_t* parent, UiState& state, UiEventContext* context)
{
  const auto count = std::max<std::size_t>(1, parameterPageCount(state));
  const auto page = std::min(context->ui->parameterPage(), count - 1);
  if (count > 1) {
    lv_obj_t* previous = button(parent, "<");
    lv_obj_set_size(previous, 36, 32);
    lv_obj_align(previous, LV_ALIGN_TOP_LEFT, 28, 20);
    lv_obj_add_event_cb(previous, onPreviousParameterPage, LV_EVENT_CLICKED, context);
  }
  lv_obj_t* pageLabel = label(parent, "PAGE " + std::to_string(page + 1) + " / " + std::to_string(count),
                              LV_ALIGN_TOP_LEFT, 76, 25, &ardor_font_open_sans_regular_18, muted);
  lv_obj_set_width(pageLabel, 128);
  lv_label_set_long_mode(pageLabel, LV_LABEL_LONG_CLIP);
  if (count > 1) {
    lv_obj_t* next = button(parent, ">");
    lv_obj_set_size(next, 36, 32);
    lv_obj_align(next, LV_ALIGN_TOP_LEFT, 214, 20);
    lv_obj_add_event_cb(next, onNextParameterPage, LV_EVENT_CLICKED, context);
  }
}

lv_obj_t* createEqResponseLine(lv_obj_t* parent, const std::array<float, kEqCurvePointCount>& response,
                               int color, lv_opa_t opacity)
{
  auto* points = static_cast<lv_point_precise_t*>(lv_malloc(sizeof(lv_point_precise_t) * kEqCurvePointCount));
  LV_ASSERT_MALLOC(points);
  if (!points) {
    return nullptr;
  }
  for (std::size_t i = 0; i < kEqCurvePointCount; ++i) {
    points[i] = {static_cast<int32_t>(i * (kEqGraphWidth - 1) / (kEqCurvePointCount - 1)),
                 static_cast<int32_t>(eqYFromGain(response[i], kEqGraphHeight))};
  }
  lv_obj_t* line = lv_line_create(parent);
  lv_obj_set_size(line, kEqGraphWidth, kEqGraphHeight);
  lv_line_set_points_mutable(line, points, kEqCurvePointCount);
  lv_obj_set_style_line_color(line, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_line_width(line, 2, LV_PART_MAIN);
  lv_obj_set_style_line_opa(line, opacity, LV_PART_MAIN);
  lv_obj_add_event_cb(line, freeLinePoints, LV_EVENT_DELETE, points);
  lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
  return line;
}

std::string eqFrequencyLabel(float frequencyHz)
{
  if (frequencyHz >= 1000.0f) {
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%.1f kHz", frequencyHz / 1000.0f);
    return buffer;
  }
  return std::to_string(static_cast<int>(std::lround(frequencyHz))) + " Hz";
}

std::string eqQLabel(float q)
{
  char buffer[16]{};
  std::snprintf(buffer, sizeof(buffer), "%.2f", q);
  return buffer;
}

std::string eqGainLabel(float gainDb)
{
  char buffer[16]{};
  std::snprintf(buffer, sizeof(buffer), "%+.1f dB", gainDb);
  return buffer;
}

ParameterControl eqKnobControl(EqBandField field, const EqBandParams& band)
{
  ParameterControl control;
  control.minimum = 0.0f;
  control.maximum = 1.0f;
  switch (field) {
  case EqBandField::Frequency:
    control.key = "frequency";
    control.label = "Frequency";
    control.value = std::log(band.frequencyHz / kEqMinimumFrequencyHz)
      / std::log(kEqMaximumFrequencyHz / kEqMinimumFrequencyHz);
    control.formatted = eqFrequencyLabel(band.frequencyHz);
    break;
  case EqBandField::Q:
    control.key = "q";
    control.label = "Q";
    control.value = std::log(band.q / kEqMinimumQ) / std::log(kEqMaximumQ / kEqMinimumQ);
    control.formatted = eqQLabel(band.q);
    break;
  case EqBandField::Gain:
    control.key = "gain";
    control.label = "Gain";
    control.minimum = kEqMinimumGainDb;
    control.maximum = kEqMaximumGainDb;
    control.value = band.gainDb;
    control.formatted = eqGainLabel(band.gainDb);
    break;
  }
  control.value = std::clamp(control.value, control.minimum, control.maximum);
  return control;
}

void renderParametricEqPanel(lv_obj_t* root, UiState& state, UiEventContext* context)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return;
  }

  lv_obj_t* panelObject = lv_obj_create(root);
  lv_obj_set_size(panelObject, 1240, kEqPanelHeight);
  lv_obj_align(panelObject, LV_ALIGN_TOP_MID, 0, kEqPanelTop);
  lv_obj_remove_flag(panelObject, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(panelObject, panelAlt);
  label(panelObject, "Parametric EQ", LV_ALIGN_TOP_LEFT, 28, 15, &ardor_font_open_sans_semibold_22);
  label(panelObject, "Five bands", LV_ALIGN_TOP_LEFT, 205, 18, &ardor_font_open_sans_regular_18, muted);

  lv_obj_t* close = button(panelObject, "X");
  lv_obj_set_size(close, 36, 32);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -8, 16);
  styleSurface(close, bg);
  lv_obj_add_event_cb(close, onCloseParamDrawer, LV_EVENT_CLICKED, context);
  renderBypassSwitch(panelObject, state, context);
  lv_obj_t* remove = button(panelObject, "Delete Block");
  lv_obj_set_size(remove, 142, 32);
  lv_obj_set_pos(remove, 792, 16);
  styleSurface(remove, 0x4a2024);
  lv_obj_set_style_text_color(lv_obj_get_child(remove, 0), lv_color_hex(0xf97373), 0);
  lv_obj_add_event_cb(remove, onDeleteSelectedBlock, LV_EVENT_CLICKED, context);

  const auto params = selectedParametricEqParams(state);
  const auto curve = makeEqCurveData(params, 48000.0f);
  lv_obj_t* graph = lv_obj_create(panelObject);
  lv_obj_set_size(graph, kEqGraphWidth, kEqGraphHeight);
  lv_obj_set_pos(graph, kEqGraphX, kEqGraphY);
  lv_obj_remove_flag(graph, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(graph, 0x111111);
  lv_obj_set_style_border_color(graph, lv_color_hex(0x3a3a3a), 0);
  lv_obj_set_style_border_width(graph, 1, 0);

  for (int i = 1; i < 4; ++i) {
    lv_obj_t* gridLine = lv_obj_create(graph);
    lv_obj_set_size(gridLine, kEqGraphWidth - 2, 1);
    lv_obj_set_pos(gridLine, 1, i * (kEqGraphHeight - 1) / 4);
    styleSurface(gridLine, 0x2f2f2f);
    lv_obj_remove_flag(gridLine, LV_OBJ_FLAG_CLICKABLE);
  }
  for (const float frequency : {100.0f, 1000.0f, 10000.0f}) {
    lv_obj_t* gridLine = lv_obj_create(graph);
    lv_obj_set_size(gridLine, 1, kEqGraphHeight - 2);
    lv_obj_set_pos(gridLine, eqXFromFrequency(frequency, kEqGraphWidth), 1);
    styleSurface(gridLine, 0x2f2f2f);
    lv_obj_remove_flag(gridLine, LV_OBJ_FLAG_CLICKABLE);
  }

  for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
    createEqResponseLine(graph, curve.bandDb[i], eqBandColors[i], params.bands[i].enabled ? LV_OPA_40 : LV_OPA_20);
  }
  createEqResponseLine(graph, curve.combinedDb, eqCombined, LV_OPA_COVER);

  for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
    const auto& band = params.bands[i];
    lv_obj_t* node = button(graph, std::to_string(i + 1));
    lv_obj_set_size(node, 32, 32);
    lv_obj_set_pos(node, eqXFromFrequency(band.frequencyHz, kEqGraphWidth) - 16,
                   eqYFromGain(band.gainDb, kEqGraphHeight) - 16);
    styleSurface(node, eqBandColors[i]);
    lv_obj_set_style_radius(node, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_opa(node, band.enabled ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(node, 0), lv_color_hex(bg), 0);
    if (context->ui->selectedEqBand() == i) {
      lv_obj_set_style_border_color(node, lv_color_hex(text), 0);
      lv_obj_set_style_border_width(node, 2, 0);
    }
    auto* nodeContext = context->ui->remember(state, i);
    lv_obj_add_event_cb(node, onEqNodePressed, LV_EVENT_PRESSED, nodeContext);
    lv_obj_add_event_cb(node, onEqNodePressing, LV_EVENT_PRESSING, nodeContext);
    lv_obj_add_event_cb(node, onEqNodeReleased, LV_EVENT_RELEASED, nodeContext);
    lv_obj_add_event_cb(node, onEqNodeReleased, LV_EVENT_PRESS_LOST, nodeContext);
  }

  for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
    lv_obj_t* bandButton = button(panelObject, "Band " + std::to_string(i + 1));
    lv_obj_set_size(bandButton, 106, 42);
    lv_obj_set_pos(bandButton, 28 + static_cast<int>(i) * 112, 350);
    styleSurface(bandButton, context->ui->selectedEqBand() == i ? eqBandColors[i] : 0x171717);
    lv_obj_set_style_text_color(lv_obj_get_child(bandButton, 0),
                                lv_color_hex(context->ui->selectedEqBand() == i ? bg : text), 0);
    lv_obj_add_event_cb(bandButton, onEqBandSelected, LV_EVENT_CLICKED, context->ui->remember(state, i));
  }

  const auto selectedBand = context->ui->selectedEqBand();
  const auto& band = params.bands[selectedBand];
  lv_obj_t* enabled = button(panelObject, band.enabled ? "Band On" : "Band Off");
  lv_obj_set_size(enabled, 130, 42);
  lv_obj_set_pos(enabled, 600, 350);
  styleSurface(enabled, band.enabled ? 0x25442a : 0x3a2020);
  lv_obj_set_style_text_color(lv_obj_get_child(enabled, 0), lv_color_hex(band.enabled ? accent : 0xf97373), 0);
  lv_obj_add_event_cb(enabled, onEqBandEnabled, LV_EVENT_CLICKED, context->ui->remember(state, selectedBand));

  lv_obj_t* reset = button(panelObject, "Reset Band");
  lv_obj_set_size(reset, 148, 42);
  lv_obj_set_pos(reset, 744, 350);
  styleSurface(reset, 0x171717);
  lv_obj_add_event_cb(reset, onEqBandReset, LV_EVENT_CLICKED, context->ui->remember(state, selectedBand));

  constexpr std::array<EqBandField, 3> eqKnobFields = {
    EqBandField::Frequency, EqBandField::Q, EqBandField::Gain,
  };
  for (std::size_t i = 0; i < eqKnobFields.size(); ++i) {
    const auto field = eqKnobFields[i];
    lv_obj_t* knob = createKnob(panelObject, eqKnobControl(field, band), 205 + static_cast<int>(i) * 338, 394,
                                 context->ui->isEqBandFieldFocused(field),
                                 context->ui->remember(state, selectedBand,
                                                       field == EqBandField::Frequency ? "frequency"
                                                       : field == EqBandField::Q ? "q" : "gain"),
                                 onEqKnobPressed, onEqKnobPressing);
    lv_obj_remove_flag(knob, LV_OBJ_FLAG_SCROLLABLE);
  }
}

void renderParameterPanel(lv_obj_t* root, UiState& state, UiEventContext* context)
{
  lv_obj_t* panelObject = lv_obj_create(root);
  lv_obj_set_size(panelObject, 1240, 286);
  lv_obj_align(panelObject, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_remove_flag(panelObject, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(panelObject, panelAlt);
  lv_obj_add_event_cb(panelObject, onParameterGesture, LV_EVENT_GESTURE, context);

  lv_obj_t* close = button(panelObject, "X");
  lv_obj_set_size(close, 36, 32);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -8, 20);
  styleSurface(close, bg);
  lv_obj_add_event_cb(close, onCloseParamDrawer, LV_EVENT_CLICKED, context);

  if (state.paramTarget == UiParamTarget::Globals) {
    lv_obj_t* title = label(panelObject, "Global", LV_ALIGN_TOP_LEFT, 270, 22, &ardor_font_open_sans_semibold_22);
    lv_obj_set_width(title, 660);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
  } else {
    const auto& blocks = state.bank.presets[state.activePreset].blocks;
    if (state.selectedBlock >= blocks.size()) {
      return;
    }
    const auto& block = blocks[state.selectedBlock];
    lv_obj_t* title = label(panelObject, block.label + "  /  " + block.assetName,
                            LV_ALIGN_TOP_LEFT, 270, 22, &ardor_font_open_sans_semibold_22);
    lv_obj_set_width(title, 500);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    renderBypassSwitch(panelObject, state, context);
    lv_obj_t* remove = button(panelObject, "Delete Block");
    lv_obj_set_size(remove, 142, 32);
    lv_obj_set_pos(remove, 800, 20);
    styleSurface(remove, 0x4a2024);
    lv_obj_set_style_text_color(lv_obj_get_child(remove, 0), lv_color_hex(0xf97373), 0);
    lv_obj_add_event_cb(remove, onDeleteSelectedBlock, LV_EVENT_CLICKED, context);
  }

  renderPageNavigation(panelObject, state, context);
  const auto controls = parameterPage(state, context->ui->parameterPage());
  const int firstX = 72;
  const int spacing = 164;
  for (std::size_t i = 0; i < controls.size(); ++i) {
    lv_obj_t* knob = createKnob(panelObject, controls[i], firstX + static_cast<int>(i) * spacing, 68,
                                 context->ui->isParameterFocused(controls[i].key), context,
                                 onKnobPressed, onKnobPressing);
    lv_obj_set_user_data(knob, reinterpret_cast<void*>(static_cast<uintptr_t>(i + 1)));
  }
}

} // namespace

LvglUi::LvglUi(UiActions actions)
  : actions_(std::move(actions))
{
}

void LvglUi::selectPreset(UiState& state, std::size_t presetIndex)
{
  if (actions_.selectPreset) {
    actions_.selectPreset(presetIndex);
  } else {
    ardor::selectPreset(state, presetIndex);
  }
  resetParameterPage();
}

std::size_t LvglUi::chainSlotForPoint(std::size_t blockCount, lv_point_t canvasPoint)
{
  if (blockCount == 0) {
    return 0;
  }
  const int column = std::clamp((canvasPoint.x - kChainLeft) / kChainSlotWidth, 0, kChainColumns - 1);
  const int row = std::clamp((canvasPoint.y - (kChainTop + kChainTileTop)) / kChainRowHeight, 0, kChainRows - 1);
  return std::min(blockCount - 1, static_cast<std::size_t>(row * kChainColumns + column));
}

std::size_t LvglUi::chainInsertionSlotForPoint(std::size_t blockCount, lv_point_t canvasPoint)
{
  const int column = std::clamp((canvasPoint.x - kChainLeft) / kChainSlotWidth, 0, kChainColumns - 1);
  const int row = std::clamp((canvasPoint.y - (kChainTop + kChainTileTop)) / kChainRowHeight, 0, kChainRows - 1);
  return std::min(blockCount, static_cast<std::size_t>(row * kChainColumns + column));
}

lv_point_t LvglUi::chainIndicatorPosition(std::size_t blockCount, std::size_t slot)
{
  slot = std::min(slot, std::min(blockCount, kMaxEffectBlocks));
  const int row = static_cast<int>(slot / kChainColumns);
  const int column = static_cast<int>(slot % kChainColumns);
  return {kChainLeft + column * kChainSlotWidth, kChainTop + kChainTileTop + row * kChainRowHeight};
}

void LvglUi::selectBlock(UiState& state, std::size_t blockIndex)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (blockIndex >= blocks.size()) {
    return;
  }
  ardor::selectBlock(state, blockIndex);
  selectedEqBand_ = 0;
  resetParameterPage();
}

void LvglUi::selectGlobalParams(UiState& state)
{
  ardor::selectGlobalParams(state);
  resetParameterPage();
}

bool LvglUi::updateSelectedEqBand(UiState& state, EqBandParams params)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return false;
  }
  const auto& block = blocks[state.selectedBlock];
  const auto before = selectedParametricEqParams(state).bands[selectedEqBand_];
  if (!setSelectedEqBand(state, selectedEqBand_, params)) {
    return false;
  }
  const auto after = selectedParametricEqParams(state).bands[selectedEqBand_];
  if (after == before) {
    return false;
  }
  if (actions_.updateEqBand) {
    actions_.updateEqBand(block.id, selectedEqBand_, after);
  }
  requestRebuild();
  return true;
}

UiEventContext* LvglUi::remember(UiState& state, std::size_t index, std::string filter)
{
  contexts_.push_back({this, &state, index, std::move(filter)});
  return &contexts_.back();
}

void LvglUi::build(lv_obj_t* root, UiState& state)
{
  rebuildPending_ = false;
  contexts_.clear();
  lv_obj_clean(root);
  lv_obj_set_style_bg_color(root, lv_color_hex(bg), 0);

  // The UI is authored on a 1280x720 design grid. Rather than re-flow every
  // widget for the panel, build it on a fixed 1280x720 canvas and scale that
  // uniformly to fill the active display. LVGL inverse-transforms pointer input
  // for hit-testing, so touches still land; fonts and paddings scale for free.
  // The screen and canvas must never scroll: a scrollable ancestor wins gesture
  // arbitration on a jittery finger touch and cancels child clicks/drags.
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* canvas = lv_obj_create(root);
  lv_obj_remove_style_all(canvas);
  lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(canvas, kDesignWidth, kDesignHeight);

  lv_display_t* display = lv_obj_get_display(root);
  const int32_t dispW = lv_display_get_horizontal_resolution(display);
  const int32_t dispH = lv_display_get_vertical_resolution(display);
  const int32_t scale =
    LV_MIN((dispW * 256) / kDesignWidth, (dispH * 256) / kDesignHeight);
  const int32_t offsetX = (dispW - (kDesignWidth * scale) / 256) / 2;
  const int32_t offsetY = (dispH - (kDesignHeight * scale) / 256) / 2;
  lv_obj_set_style_transform_pivot_x(canvas, 0, 0);
  lv_obj_set_style_transform_pivot_y(canvas, 0, 0);
  lv_obj_set_style_transform_scale(canvas, scale, 0);
  lv_obj_set_pos(canvas, offsetX, offsetY);

  canvas_ = canvas;
  canvasScale_ = scale;
  canvasOffset_ = {offsetX, offsetY};

  if (state.mode == UiMode::Preset) {
    renderPresetMode(canvas, state);
  } else {
    renderEditMode(canvas, state);
  }
}

lv_point_t LvglUi::toCanvas(lv_point_t displayPoint) const
{
  const int32_t scale = canvasScale_ == 0 ? 256 : canvasScale_;
  return {((displayPoint.x - canvasOffset_.x) * 256) / scale,
          ((displayPoint.y - canvasOffset_.y) * 256) / scale};
}

void LvglUi::refresh(lv_obj_t* root, UiState& state)
{
  if (rebuildPending_) {
    build(root, state);
  }
}

void LvglUi::requestRebuild()
{
  if (!knobInteractionActive_) {
    rebuildPending_ = true;
  }
}

void telemetryLine(lv_obj_t* root, const RuntimeTelemetry& telemetry, bool bypassed)
{
  const std::string status = bypassed ? "BYPASS" : "LIVE";
  const int color = bypassed ? 0xf97373 : muted;
  label(root,
        status + "  over " + std::to_string(telemetry.overBudget) + "  max "
          + std::to_string(static_cast<int>(telemetry.maxMs * 100.0) / 100.0) + "ms",
        LV_ALIGN_BOTTOM_LEFT, 18, -14, &ardor_font_open_sans_regular_18, color);
}

void LvglUi::renderPresetMode(lv_obj_t* root, UiState& state)
{
  label(root, state.bank.name, LV_ALIGN_TOP_MID, 0, 28, &ardor_font_open_sans_semibold_28);
  label(root, "Master " + std::to_string(state.masterVolume) + "%", LV_ALIGN_TOP_LEFT, 28, 28,
        &ardor_font_open_sans_regular_18, muted);

  lv_obj_t* edit = button(root, "Edit");
  lv_obj_set_size(edit, 112, 46);
  lv_obj_align(edit, LV_ALIGN_TOP_RIGHT, -28, 20);
  lv_obj_add_event_cb(edit, onEditModeClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* grid = lv_obj_create(root);
  lv_obj_set_size(grid, 1200, 580);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -28);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);

  static int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(grid, cols, rows);

  for (std::size_t i = 0; i < state.bank.presets.size(); ++i) {
    lv_obj_t* preset = button(grid, state.bank.presets[i].name);
    lv_obj_set_grid_cell(preset, LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i % 2), 1,
                         LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i / 2), 1);
    styleSurface(preset, panel);
    lv_obj_t* presetName = lv_obj_get_child(preset, 0);
    lv_obj_set_style_text_color(presetName, lv_color_hex(i == state.activePreset ? accent : text), 0);
    lv_obj_set_style_text_font(presetName, &ardor_font_open_sans_semibold_28, 0);
    // Use the ample card space for the preset name: 28 px at 2x scale gives
    // a 56 px label, more than triple the standard 18 px button text.
    lv_obj_set_style_transform_pivot_x(presetName, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(presetName, LV_PCT(50), 0);
    lv_obj_set_style_transform_scale(presetName, 2 * LV_SCALE_NONE, 0);
    if (i == state.activePreset) {
      lv_obj_t* indicator = lv_obj_create(preset);
      lv_obj_set_size(indicator, 4, LV_PCT(100));
      lv_obj_align(indicator, LV_ALIGN_LEFT_MID, 0, 0);
      styleSurface(indicator, accent);
      lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_add_event_cb(preset, onPresetClicked, LV_EVENT_CLICKED, remember(state, i));
  }
  telemetryLine(root, state.telemetry, state.effectsBypassed);
}

void LvglUi::renderEditMode(lv_obj_t* root, UiState& state)
{
  label(root, state.bank.presets[state.activePreset].name, LV_ALIGN_TOP_MID, 0, 24, &ardor_font_open_sans_semibold_28);

  lv_obj_t* presets = button(root, "Presets");
  lv_obj_set_size(presets, 112, 42);
  lv_obj_align(presets, LV_ALIGN_TOP_LEFT, 28, 20);
  lv_obj_add_event_cb(presets, onPresetModeClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* globalButton = button(root, "Global");
  lv_obj_set_size(globalButton, 112, 42);
  lv_obj_align(globalButton, LV_ALIGN_TOP_RIGHT, -292, 20);
  lv_obj_add_event_cb(globalButton, onGlobalParamsClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* save = button(root, state.dirty ? "Save*" : "Save");
  lv_obj_set_size(save, 96, 42);
  lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -170, 20);
  lv_obj_set_style_text_color(lv_obj_get_child(save, 0), lv_color_hex(state.dirty ? accent : text), 0);
  lv_obj_add_event_cb(save, onSaveClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* blocksButton = button(root, "Blocks");
  lv_obj_set_size(blocksButton, 112, 42);
  lv_obj_align(blocksButton, LV_ALIGN_TOP_RIGHT, -28, 20);
  lv_obj_add_event_cb(blocksButton, onOpenBlockDrawer, LV_EVENT_CLICKED, remember(state));

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  const bool editingEq = state.paramDrawerOpen && state.paramTarget == UiParamTarget::Block
    && state.selectedBlock < blocks.size() && blocks[state.selectedBlock].type == "eq"
    && isParametricEqMode(blocks[state.selectedBlock].params);
  if (editingEq) {
    renderParametricEqPanel(root, state, remember(state));
    if (state.blockDrawerOpen) {
      renderBlockDrawer(root, state);
    }
    return;
  }

  lv_obj_t* chain = lv_obj_create(root);
  lv_obj_set_size(chain, 1240, kChainHeight);
  lv_obj_align(chain, LV_ALIGN_TOP_MID, 0, kChainTop);
  lv_obj_remove_flag(chain, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(chain, bg);
  if (blocks.size() > kChainColumns) {
    renderChainWrapConnector(root);
  }
  label(root, "Input", LV_ALIGN_TOP_LEFT, 28, 88, &ardor_font_open_sans_regular_18, muted);
  label(root, "Output", LV_ALIGN_TOP_RIGHT, -28, 408, &ardor_font_open_sans_regular_18, muted);

  for (std::size_t i = 0; i < std::min(blocks.size(), kMaxEffectBlocks); ++i) {
    const auto& block = blocks[i];
    std::string category = block.label;
    std::transform(category.begin(), category.end(), category.begin(), [](unsigned char character) {
      return static_cast<char>(std::toupper(character));
    });

    lv_obj_t* object = button(chain, "");
    lv_obj_set_size(object, kChainTileWidth, kChainTileHeight);
    lv_obj_set_pos(object, 14 + static_cast<int>(i % kChainColumns) * kChainSlotWidth,
                   kChainTileTop + static_cast<int>(i / kChainColumns) * kChainRowHeight);
    styleSurface(object, block.enabled ? panel : 0x171717);
    const bool selected = state.paramTarget == UiParamTarget::Block && state.selectedBlock == i;
    label(object, category, LV_ALIGN_TOP_LEFT, 0, 8, &ardor_font_open_sans_regular_18, muted);
    label(object, block.assetName, LV_ALIGN_LEFT_MID, 0, 8, &ardor_font_open_sans_semibold_22);
    if (selected) {
      lv_obj_t* indicator = lv_obj_create(object);
      lv_obj_set_size(indicator, 5, 5);
      lv_obj_align(indicator, LV_ALIGN_BOTTOM_MID, 0, -7);
      styleSurface(indicator, accent);
      lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
      lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
    }
    auto* context = remember(state, i);
    lv_obj_add_event_cb(object, onBlockPressed, LV_EVENT_PRESSED, context);
    lv_obj_add_event_cb(object, onBlockPressing, LV_EVENT_PRESSING, context);
    lv_obj_add_event_cb(object, onBlockClicked, LV_EVENT_CLICKED, context);
    lv_obj_add_event_cb(object, onBlockReleased, LV_EVENT_RELEASED, context);
  }

  telemetryLine(root, state.telemetry, state.effectsBypassed);
  if (state.blockDrawerOpen) {
    renderBlockDrawer(root, state);
  }
  if (state.paramDrawerOpen) {
    renderParameterPanel(root, state, remember(state));
  }
}

void LvglUi::renderBlockDrawer(lv_obj_t* root, UiState& state)
{
  lv_obj_t* drawer = lv_obj_create(root);
  lv_obj_set_size(drawer, 360, 720);
  lv_obj_align(drawer, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(drawer, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_color(drawer, lv_color_hex(panel), 0);
  lv_obj_set_style_border_width(drawer, 1, 0);
  lv_obj_set_style_border_side(drawer, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_radius(drawer, 0, 0);
  lv_obj_set_style_pad_all(drawer, 18, 0);
  // Content fits; the inner list scrolls on its own. A scrollable drawer would
  // steal taps on the close button on a jittery finger touch.
  lv_obj_remove_flag(drawer, LV_OBJ_FLAG_SCROLLABLE);

  label(drawer, "Blocks", LV_ALIGN_TOP_LEFT, 0, 0, &ardor_font_open_sans_semibold_22);
  lv_obj_t* close = button(drawer, "X");
  lv_obj_set_size(close, 40, 36);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -4);
  lv_obj_add_event_cb(close, onCloseBlockDrawer, LV_EVENT_CLICKED, remember(state));

  static constexpr std::array filters = {
    std::pair{"All", "all"}, std::pair{"Amps", "amps"}, std::pair{"Cabs", "cabs"},
    std::pair{"EQ", "eq"}, std::pair{"Dyn", "dynamics"}, std::pair{"Mod", "modulation"},
    std::pair{"Time", "time"},
  };

  lv_obj_t* filterRow = lv_obj_create(drawer);
  lv_obj_set_size(filterRow, 324, 34);
  lv_obj_align(filterRow, LV_ALIGN_TOP_LEFT, 0, 42);
  lv_obj_set_style_bg_opa(filterRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(filterRow, 0, 0);
  lv_obj_set_style_pad_all(filterRow, 0, 0);
  lv_obj_set_style_pad_column(filterRow, 6, 0);
  lv_obj_set_scroll_dir(filterRow, LV_DIR_HOR);
  lv_obj_set_flex_flow(filterRow, LV_FLEX_FLOW_ROW);

  for (const auto& [name, filter] : filters) {
    lv_obj_t* filterButton = button(filterRow, name);
    lv_obj_set_size(filterButton, 74, 34);
    styleSurface(filterButton, state.categoryFilter == filter ? 0x333333 : 0x1b1b1b);
    lv_obj_set_style_text_color(lv_obj_get_child(filterButton, 0),
                                lv_color_hex(state.categoryFilter == filter ? accent : text), 0);
    lv_obj_add_event_cb(filterButton, onFilterClicked, LV_EVENT_CLICKED, remember(state, 0, filter));
  }

  lv_obj_t* list = lv_obj_create(drawer);
  lv_obj_set_size(list, 324, 556);
  lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, 146);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

  for (std::size_t i = 0; i < state.assets.size(); ++i) {
    const auto& asset = state.assets[i];
    if (state.categoryFilter != "all" && asset.type != state.categoryFilter) {
      continue;
    }
    lv_obj_t* item = button(list, asset.name);
    lv_obj_set_width(item, 310);
    lv_obj_set_height(item, 38);
    styleSurface(item, panel);
    auto* context = remember(state, i);
    lv_obj_add_event_cb(item, onAssetPressed, LV_EVENT_PRESSED, context);
    lv_obj_add_event_cb(item, onAssetPressing, LV_EVENT_PRESSING, context);
    lv_obj_add_event_cb(item, onAssetReleased, LV_EVENT_RELEASED, context);
    lv_obj_add_event_cb(item, onAssetClicked, LV_EVENT_CLICKED, context);
  }
}

} // namespace ardor
