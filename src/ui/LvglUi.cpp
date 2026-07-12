#include "ui/LvglUi.h"

#include "ui/fonts/OpenSansRegular.h"
#include "ui/fonts/OpenSansSemibold.h"

#include "daisyfx/DaisyFxCatalog.h"

#include <algorithm>
#include <array>
#include <cstdint>
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
constexpr auto accent = 0xb6ff00;

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

void label(lv_obj_t* parent, const std::string& value, lv_align_t align, int x, int y,
           const lv_font_t* font = &ardor_font_open_sans_regular_18, int color = text)
{
  lv_obj_t* object = lv_label_create(parent);
  lv_label_set_text(object, value.c_str());
  setText(object, color, font);
  lv_obj_align(object, align, x, y);
}

void redraw(UiEventContext* context)
{
  context->ui->requestRebuild();
}

lv_obj_t* button(lv_obj_t* parent, const std::string& value);

void onPresetClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().selectPreset) {
    context->ui->actions().selectPreset(context->index);
  } else {
    selectPreset(*context->state, context->index);
  }
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
  context->ui->focusParameter(context->filter);
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
    redraw(context);
  }
}

void onBypassChanged(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_obj_t* switchObject = lv_event_get_target_obj(event);
  setSelectedBlockEnabled(*context->state, lv_obj_has_state(switchObject, LV_STATE_CHECKED));
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
  const auto blockCount = state.bank.presets[state.activePreset].blocks.size();
  if (blockCount == 0) {
    return 0;
  }
  const int slotWidth = 1224 / static_cast<int>(blockCount);
  return std::min<std::size_t>(static_cast<std::size_t>(std::max(0, point.x - 28) / slotWidth), blockCount - 1);
}

std::size_t insertSlotFromPoint(const UiState& state, const lv_point_t& point)
{
  const auto blockCount = state.bank.presets[state.activePreset].blocks.size();
  if (blockCount == 0) {
    return 0;
  }
  const int slotWidth = 1224 / static_cast<int>(blockCount);
  return std::min<std::size_t>(static_cast<std::size_t>(std::max(0, point.x - 28) / slotWidth), blockCount);
}

bool pointInChain(const lv_point_t& point)
{
  return point.x >= 20 && point.x <= 1260 && point.y >= 116 && point.y <= 232;
}

void moveToFront(lv_obj_t* object)
{
  lv_obj_t* parent = lv_obj_get_parent(object);
  lv_obj_move_to_index(object, static_cast<int32_t>(lv_obj_get_child_count(parent)) - 1);
}

void placeDragIndicatorAtSlot(UiEventContext* context, std::size_t slot)
{
  const auto blockCount = context->state->bank.presets[context->state->activePreset].blocks.size();
  const int slotWidth = blockCount == 0 ? 1224 : 1224 / static_cast<int>(blockCount);

  if (!context->indicator) {
    context->indicator = lv_obj_create(context->ui->canvas());
    lv_obj_set_size(context->indicator, 5, 104);
    lv_obj_set_style_bg_color(context->indicator, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(context->indicator, 0, 0);
    lv_obj_set_style_radius(context->indicator, 2, 0);
  }

  lv_obj_set_pos(context->indicator, 28 + static_cast<int>(slot) * slotWidth, 126);
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

lv_obj_t* createKnob(lv_obj_t* parent, const ParameterControl& control, int x, UiEventContext* context)
{
  const float range = control.maximum - control.minimum;
  const float ratio = range == 0.0f ? 0.0f : std::clamp((control.value - control.minimum) / range, 0.0f, 1.0f);

  lv_obj_t* knob = lv_obj_create(parent);
  lv_obj_set_size(knob, 154, 184);
  lv_obj_set_pos(knob, x, 68);
  lv_obj_remove_flag(knob, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(knob, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(knob, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(knob, 0, 0);
  lv_obj_add_event_cb(knob, onKnobPressed, LV_EVENT_PRESSED, context);
  lv_obj_add_event_cb(knob, onKnobPressing, LV_EVENT_PRESSING, context);

  lv_obj_t* arc = lv_arc_create(knob);
  lv_obj_set_size(arc, 94, 94);
  lv_obj_center(arc);
  lv_obj_set_y(arc, -25);
  lv_arc_set_rotation(arc, 135);
  lv_arc_set_bg_angles(arc, 0, 270);
  lv_arc_set_range(arc, 0, 1000);
  lv_arc_set_value(arc, static_cast<int>(std::lround(ratio * 1000.0f)));
  lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_hex(0x454545), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(arc, lv_color_hex(accent), LV_PART_INDICATOR);
  lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* rim = lv_obj_create(knob);
  lv_obj_set_size(rim, 56, 56);
  lv_obj_align(rim, LV_ALIGN_TOP_MID, 0, 20);
  styleSurface(rim, 0x000000);
  lv_obj_set_style_radius(rim, LV_RADIUS_CIRCLE, 0);
  lv_obj_remove_flag(rim, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* centre = lv_obj_create(rim);
  lv_obj_set_size(centre, 48, 48);
  lv_obj_center(centre);
  styleSurface(centre, panel);
  lv_obj_set_style_radius(centre, LV_RADIUS_CIRCLE, 0);
  lv_obj_remove_flag(centre, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* pointer = lv_obj_create(rim);
  lv_obj_set_size(pointer, 3, 20);
  lv_obj_set_pos(pointer, 27, 7);
  lv_obj_set_style_bg_color(pointer, lv_color_hex(text), 0);
  lv_obj_set_style_border_width(pointer, 0, 0);
  lv_obj_set_style_radius(pointer, 2, 0);
  lv_obj_set_style_transform_pivot_x(pointer, 1, 21);
  lv_obj_set_style_transform_pivot_y(pointer, 1, 21);
  lv_obj_set_style_transform_rotation(pointer, static_cast<int32_t>((45.0f + ratio * 270.0f) * 10.0f), 0);
  lv_obj_remove_flag(pointer, LV_OBJ_FLAG_CLICKABLE);

  label(knob, control.label, LV_ALIGN_BOTTOM_MID, 0, -30, &ardor_font_open_sans_semibold_22, text);
  label(knob, control.formatted, LV_ALIGN_BOTTOM_MID, 0, -7, &ardor_font_open_sans_regular_18, muted);
  return knob;
}

void renderBypassSwitch(lv_obj_t* parent, UiState& state, UiEventContext* context)
{
  const auto& block = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
  label(parent, "Bypass", LV_ALIGN_TOP_RIGHT, -132, 24, &ardor_font_open_sans_regular_18, muted);
  lv_obj_t* switchObject = lv_switch_create(parent);
  lv_obj_set_size(switchObject, 64, 30);
  lv_obj_align(switchObject, LV_ALIGN_TOP_RIGHT, -28, 20);
  lv_obj_set_style_bg_color(switchObject, lv_color_hex(0x111111), LV_PART_MAIN);
  lv_obj_set_style_bg_color(switchObject, lv_color_hex(accent),
                            static_cast<lv_style_selector_t>(LV_PART_MAIN)
                              | static_cast<lv_style_selector_t>(LV_STATE_CHECKED));
  lv_obj_set_style_bg_color(switchObject, lv_color_hex(text), LV_PART_KNOB);
  lv_obj_set_style_radius(switchObject, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_radius(switchObject, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_border_width(switchObject, 0, LV_PART_MAIN);
  if (block.enabled) {
    lv_obj_add_state(switchObject, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(switchObject, onBypassChanged, LV_EVENT_VALUE_CHANGED, context);
}

void renderPageNavigation(lv_obj_t* parent, UiState& state, UiEventContext* context)
{
  const auto count = parameterPageCount(state);
  if (count < 2) {
    return;
  }
  const auto page = context->ui->parameterPage();
  lv_obj_t* previous = button(parent, "<");
  lv_obj_set_size(previous, 36, 32);
  lv_obj_align(previous, LV_ALIGN_TOP_LEFT, 28, 20);
  lv_obj_add_event_cb(previous, onPreviousParameterPage, LV_EVENT_CLICKED, context);
  label(parent, std::to_string(page + 1) + " / " + std::to_string(count), LV_ALIGN_TOP_LEFT, 72, 25,
        &ardor_font_open_sans_regular_18, muted);
  lv_obj_t* next = button(parent, ">");
  lv_obj_set_size(next, 36, 32);
  lv_obj_align(next, LV_ALIGN_TOP_LEFT, 134, 20);
  lv_obj_add_event_cb(next, onNextParameterPage, LV_EVENT_CLICKED, context);
}

void renderParameterPanel(lv_obj_t* root, UiState& state, UiEventContext* context)
{
  lv_obj_t* panelObject = lv_obj_create(root);
  lv_obj_set_size(panelObject, 1240, 286);
  lv_obj_align(panelObject, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_obj_remove_flag(panelObject, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(panelObject, panelAlt);
  lv_obj_add_event_cb(panelObject, onParameterGesture, LV_EVENT_GESTURE, context);

  lv_obj_t* close = button(panelObject, "X");
  lv_obj_set_size(close, 36, 32);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -220, 20);
  lv_obj_add_event_cb(close, onCloseParamDrawer, LV_EVENT_CLICKED, context);

  if (state.paramTarget == UiParamTarget::Globals) {
    label(panelObject, "Global", LV_ALIGN_TOP_LEFT, 28, 22, &ardor_font_open_sans_semibold_22);
  } else {
    const auto& blocks = state.bank.presets[state.activePreset].blocks;
    if (state.selectedBlock >= blocks.size()) {
      return;
    }
    const auto& block = blocks[state.selectedBlock];
    label(panelObject, block.label + "  /  " + block.assetName, LV_ALIGN_TOP_LEFT, 28, 22,
          &ardor_font_open_sans_semibold_22);
    renderBypassSwitch(panelObject, state, context);
  }

  renderPageNavigation(panelObject, state, context);
  const auto controls = parameterPage(state, context->ui->parameterPage());
  const int firstX = 180;
  const int spacing = 164;
  for (std::size_t i = 0; i < controls.size(); ++i) {
    lv_obj_t* knob = createKnob(panelObject, controls[i], firstX + static_cast<int>(i) * spacing, context);
    lv_obj_set_user_data(knob, reinterpret_cast<void*>(static_cast<uintptr_t>(i + 1)));
  }
}

} // namespace

LvglUi::LvglUi(UiActions actions)
  : actions_(std::move(actions))
{
}

void LvglUi::selectBlock(UiState& state, std::size_t blockIndex)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (blockIndex >= blocks.size()) {
    return;
  }
  ardor::selectBlock(state, blockIndex);
  resetParameterPage();
}

void LvglUi::selectGlobalParams(UiState& state)
{
  ardor::selectGlobalParams(state);
  resetParameterPage();
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
  rebuildPending_ = true;
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
    lv_obj_set_style_text_color(lv_obj_get_child(preset, 0), lv_color_hex(i == state.activePreset ? accent : text), 0);
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

  lv_obj_t* chain = lv_obj_create(root);
  lv_obj_set_size(chain, 1240, 126);
  lv_obj_align(chain, LV_ALIGN_TOP_MID, 0, 110);
  lv_obj_remove_flag(chain, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(chain);
  label(root, "Input", LV_ALIGN_TOP_LEFT, 28, 88, &ardor_font_open_sans_regular_18, muted);
  label(root, "Output", LV_ALIGN_TOP_RIGHT, -28, 248, &ardor_font_open_sans_regular_18, muted);

  for (std::size_t i = 0; i < blocks.size(); ++i) {
    const auto& block = blocks[i];
    const int slotWidth = 1212 / std::max<int>(1, static_cast<int>(blocks.size()));
    lv_obj_t* object = button(chain, block.label + "\n" + block.assetName);
    lv_obj_set_size(object, slotWidth - 10, 92);
    lv_obj_set_pos(object, 14 + static_cast<int>(i) * slotWidth, 17);
    styleSurface(object, block.enabled ? panel : 0x171717);
    const bool selected = state.paramTarget == UiParamTarget::Block && state.selectedBlock == i;
    label(object, block.type, LV_ALIGN_TOP_LEFT, 9, 7, &ardor_font_open_sans_regular_18, selected ? accent : muted);
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
  lv_obj_set_size(drawer, 320, 720);
  lv_obj_align(drawer, LV_ALIGN_LEFT_MID, 0, 0);
  styleSurface(drawer, panel);
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
    std::pair{"Dyn", "dynamics"}, std::pair{"Mod", "modulation"}, std::pair{"Time", "time"},
  };

  lv_obj_t* filterRow = lv_obj_create(drawer);
  lv_obj_set_size(filterRow, 284, 92);
  lv_obj_align(filterRow, LV_ALIGN_TOP_LEFT, 0, 42);
  lv_obj_set_style_bg_opa(filterRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(filterRow, 0, 0);
  lv_obj_set_style_pad_all(filterRow, 0, 0);
  lv_obj_set_style_pad_column(filterRow, 6, 0);
  lv_obj_set_style_pad_row(filterRow, 6, 0);
  lv_obj_set_flex_flow(filterRow, LV_FLEX_FLOW_ROW_WRAP);

  for (const auto& [name, filter] : filters) {
    lv_obj_t* filterButton = button(filterRow, name);
    lv_obj_set_size(filterButton, 74, 34);
    styleSurface(filterButton, state.categoryFilter == filter ? 0x333333 : 0x1b1b1b);
    lv_obj_set_style_text_color(lv_obj_get_child(filterButton, 0),
                                lv_color_hex(state.categoryFilter == filter ? accent : text), 0);
    lv_obj_add_event_cb(filterButton, onFilterClicked, LV_EVENT_CLICKED, remember(state, 0, filter));
  }

  lv_obj_t* list = lv_obj_create(drawer);
  lv_obj_set_size(list, 284, 540);
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
    lv_obj_set_width(item, 270);
    lv_obj_set_height(item, 38);
    styleSurface(item, 0x1b1b1b);
    auto* context = remember(state, i);
    lv_obj_add_event_cb(item, onAssetPressed, LV_EVENT_PRESSED, context);
    lv_obj_add_event_cb(item, onAssetPressing, LV_EVENT_PRESSING, context);
    lv_obj_add_event_cb(item, onAssetReleased, LV_EVENT_RELEASED, context);
    lv_obj_add_event_cb(item, onAssetClicked, LV_EVENT_CLICKED, context);
  }
}

} // namespace ardor
