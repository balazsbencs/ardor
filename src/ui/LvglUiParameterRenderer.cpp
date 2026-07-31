#include "ui/LvglUi.h"

#include "ui/LvglUiParameterView.h"
#include "ui/LvglUiParameterWidgets.h"
#include "ui/LvglUiStyle.h"
#include "ui/fonts/OpenSansRegular.h"
#include "ui/fonts/OpenSansSemibold.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace ardor {
namespace {

using namespace lvgl_ui;

constexpr int kParameterPanelWidth = 1240;
constexpr int kPanelEdgeInset = 28;
constexpr int kPanelActionTop = 14;
constexpr int kPanelActionHeight = 52;
constexpr int kPanelCloseButtonWidth = 88;
constexpr int kPanelCloseButtonHeight = kPanelActionHeight;
constexpr int kPanelCloseButtonX = kParameterPanelWidth - kPanelEdgeInset - kPanelCloseButtonWidth;
constexpr int kBypassControlWidth = 160;
constexpr int kBypassControlX = kPanelCloseButtonX - 36 - kBypassControlWidth;
constexpr int kDeleteBlockWidth = 156;
constexpr int kDeleteBlockX = kBypassControlX - 24 - kDeleteBlockWidth;
constexpr int kParameterTitleX = 270;
constexpr int kParameterTitleWidth = kDeleteBlockX - kParameterTitleX - 24;
constexpr int kParameterSliderColumns = 3;
constexpr int kParameterSliderWidth = 385;
constexpr int kParameterSliderHeight = 76;
constexpr int kParameterSliderColumnGap = 14;
constexpr int kParameterSliderRowGap = 14;
constexpr int kParameterSliderGridX = 28;
constexpr int kParameterSliderGridY = 82;
constexpr int kParameterSliderRadius = 5;
constexpr int kParameterSliderTextInset = 20;

struct ParameterSliderVisual {
  std::size_t controlIndex = 0;
  lv_obj_t* fill = nullptr;
  lv_obj_t* inactiveLabel = nullptr;
  lv_obj_t* inactiveValue = nullptr;
  lv_obj_t* activeLabel = nullptr;
  lv_obj_t* activeValue = nullptr;
};

struct BypassControlVisual {
  lv_obj_t* fill = nullptr;
  lv_obj_t* inactiveValue = nullptr;
  lv_obj_t* activeValue = nullptr;
};



void freeParameterSliderVisual(lv_event_t* event)
{
  delete static_cast<ParameterSliderVisual*>(lv_event_get_user_data(event));
}

void freeBypassControlVisual(lv_event_t* event)
{
  delete static_cast<BypassControlVisual*>(lv_event_get_user_data(event));
}


void redraw(UiEventContext* context)
{
  // Model mutators publish typed revisions. This helper remains at event call
  // sites solely to make local focus/page changes visible.
  context->ui->invalidate(UiChange::None);
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
    // A touched slider is retained as the focused encoder target after release.
    // Clear that object before swapping pages so focusParameter() requests a
    // fresh parameter view instead of leaving the old page visible.
    context->ui->setFocusedWidgets(nullptr);
    context->ui->setParameterPage(next);
    context->ui->focusParameter("");
    context->ui->invalidate(UiChange::Parameters);
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

void refreshParameterSliderVisual(lv_obj_t* slider, const ParameterControl& control, bool focused = true)
{
  const auto* visual = static_cast<const ParameterSliderVisual*>(lv_obj_get_user_data(slider));
  if (!visual) {
    return;
  }
  const float range = control.maximum - control.minimum;
  const float ratio = range == 0.0f ? 0.0f
    : std::clamp((control.value - control.minimum) / range, 0.0f, 1.0f);
  if (visual->fill) {
    lv_obj_set_width(visual->fill,
                     static_cast<int32_t>(std::lround(ratio * kParameterSliderWidth)));
  }
  for (lv_obj_t* item : {visual->inactiveLabel, visual->activeLabel}) {
    if (item) lv_label_set_text(item, control.label.c_str());
  }
  for (lv_obj_t* item : {visual->inactiveValue, visual->activeValue}) {
    if (item) lv_label_set_text(item, control.formatted.c_str());
  }
  lv_obj_set_style_outline_width(slider, focused ? 2 : 0, 0);
  lv_obj_set_style_outline_color(slider, lv_color_hex(accent), 0);
  lv_obj_set_style_outline_pad(slider, 2, 0);
}

void refreshBypassControlVisual(lv_obj_t* control, bool bypassed)
{
  const auto* visual = static_cast<const BypassControlVisual*>(lv_obj_get_user_data(control));
  if (!visual) {
    return;
  }
  if (bypassed) {
    lv_obj_add_state(control, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(control, LV_STATE_CHECKED);
  }
  if (visual->fill) {
    lv_obj_set_width(visual->fill, bypassed ? kBypassControlWidth : 0);
  }
  for (lv_obj_t* value : {visual->inactiveValue, visual->activeValue}) {
    if (value) lv_label_set_text(value, bypassed ? "On" : "Off");
  }
}

float parameterSliderRatioForInput(lv_obj_t* slider, lv_indev_t* input)
{
  lv_point_t point{};
  lv_indev_get_point(input, &point);
  lv_area_t area{};
  lv_obj_get_coords(slider, &area);
  lv_point_t horizontalEdges[2] = {{area.x1, area.y1}, {area.x2, area.y1}};
  lv_obj_transform_point_array(slider, horizontalEdges, 2, LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE);
  const int32_t left = std::min(horizontalEdges[0].x, horizontalEdges[1].x);
  const int32_t right = std::max(horizontalEdges[0].x, horizontalEdges[1].x);
  const int32_t width = std::max<int32_t>(1, right - left);
  return std::clamp(static_cast<float>(point.x - left) / static_cast<float>(width),
                    0.0f, 1.0f);
}

void applyParameterSliderPosition(lv_obj_t* slider, UiEventContext* context, lv_indev_t* input)
{
  const auto* visual = static_cast<const ParameterSliderVisual*>(lv_obj_get_user_data(slider));
  const auto controls = parameterPage(*context->state, context->ui->parameterPage());
  if (!visual || visual->controlIndex >= controls.size()) {
    return;
  }
  const float ratio = parameterSliderRatioForInput(slider, input);
  const auto& control = controls[visual->controlIndex];
  const float desired = control.minimum + ratio * (control.maximum - control.minimum);
  const float step = control.step > 0.0f ? control.step : 1.0f;
  const int delta = static_cast<int>(std::lround((desired - control.value) / step));
  context->ui->applyFocusedParameterDelta(*context->state, delta, true);
}

void onParameterSliderPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  lv_obj_t* slider = lv_event_get_target_obj(event);
  if (!input || !slider) {
    return;
  }
  const auto* visual = static_cast<const ParameterSliderVisual*>(lv_obj_get_user_data(slider));
  const auto controls = parameterPage(*context->state, context->ui->parameterPage());
  if (!visual || visual->controlIndex >= controls.size()) {
    return;
  }
  context->filter = controls[visual->controlIndex].key;
  context->ui->setFocusedWidgets(slider);
  context->ui->beginParameterInteraction();
  context->ui->focusParameter(context->filter);
  refreshParameterSliderVisual(slider, controls[visual->controlIndex], true);
  applyParameterSliderPosition(slider, context, input);
}

void onParameterSliderPressing(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (input) {
    applyParameterSliderPosition(lv_event_get_target_obj(event), context, input);
  }
}

void onParameterControlReleased(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->endParameterInteraction();
}

void onBypassClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_obj_t* control = lv_event_get_target_obj(event);
  const bool bypassed = !lv_obj_has_state(control, LV_STATE_CHECKED);
  refreshBypassControlVisual(control, bypassed);
  setSelectedBlockEnabled(*context->state, !bypassed);
  redraw(context);
}


lv_obj_t* createParameterSlider(lv_obj_t* parent, const ParameterControl& control, int x, int y,
                                bool focused, UiEventContext* context,
                                lv_event_cb_t onPressed, lv_event_cb_t onPressing,
                                std::size_t controlIndex)
{
  lv_obj_t* slider = lv_obj_create(parent);
  lv_obj_set_size(slider, kParameterSliderWidth, kParameterSliderHeight);
  lv_obj_set_pos(slider, x, y);
  lv_obj_remove_flag(slider, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(slider, LV_OBJ_FLAG_CLICKABLE);
  styleSurface(slider, 0x343434);
  lv_obj_set_style_radius(slider, kParameterSliderRadius, 0);
  lv_obj_set_style_clip_corner(slider, true, 0);
  lv_obj_set_style_pad_all(slider, 0, 0);

  auto* visual = new ParameterSliderVisual{};
  visual->controlIndex = controlIndex;
  lv_obj_set_user_data(slider, visual);
  lv_obj_add_event_cb(slider, freeParameterSliderVisual, LV_EVENT_DELETE, visual);
  lv_obj_add_event_cb(slider, onPressed, LV_EVENT_PRESSED, context);
  lv_obj_add_event_cb(slider, onPressing, LV_EVENT_PRESSING, context);
  lv_obj_add_event_cb(slider, onParameterControlReleased, LV_EVENT_RELEASED, context);
  lv_obj_add_event_cb(slider, onParameterControlReleased, LV_EVENT_PRESS_LOST, context);

  const auto addTextPair = [&](lv_obj_t* layer, int color,
                               lv_obj_t** labelOut, lv_obj_t** valueOut) {
    lv_obj_t* controlLabel = label(layer, control.label, LV_ALIGN_LEFT_MID,
                                   kParameterSliderTextInset, 0,
                                   &ardor_font_open_sans_semibold_22, color);
    lv_obj_set_width(controlLabel, 230);
    lv_label_set_long_mode(controlLabel, LV_LABEL_LONG_CLIP);
    lv_obj_t* valueLabel = label(layer, control.formatted, LV_ALIGN_RIGHT_MID,
                                 -kParameterSliderTextInset, 0,
                                 &ardor_font_open_sans_semibold_22, color);
    lv_obj_set_width(valueLabel, 120);
    lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(valueLabel, LV_TEXT_ALIGN_RIGHT, 0);
    *labelOut = controlLabel;
    *valueOut = valueLabel;
  };

  addTextPair(slider, text, &visual->inactiveLabel, &visual->inactiveValue);

  lv_obj_t* fill = lv_obj_create(slider);
  lv_obj_set_size(fill, 0, kParameterSliderHeight);
  lv_obj_set_pos(fill, 0, 0);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);
  styleSurface(fill, accent);
  lv_obj_set_style_radius(fill, kParameterSliderRadius, 0);
  lv_obj_set_style_clip_corner(fill, true, 0);
  lv_obj_set_style_pad_all(fill, 0, 0);
  visual->fill = fill;

  lv_obj_t* activeTextLayer = lv_obj_create(fill);
  lv_obj_set_size(activeTextLayer, kParameterSliderWidth, kParameterSliderHeight);
  lv_obj_set_pos(activeTextLayer, 0, 0);
  lv_obj_remove_flag(activeTextLayer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(activeTextLayer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(activeTextLayer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(activeTextLayer, 0, 0);
  lv_obj_set_style_pad_all(activeTextLayer, 0, 0);
  addTextPair(activeTextLayer, 0x102014, &visual->activeLabel, &visual->activeValue);

  refreshParameterSliderVisual(slider, control, focused);
  return slider;
}

lv_obj_t* renderPanelCloseButton(lv_obj_t* parent, UiEventContext* context)
{
  lv_obj_t* close = button(parent, "Close");
  lv_obj_set_size(close, kPanelCloseButtonWidth, kPanelCloseButtonHeight);
  lv_obj_set_pos(close, kPanelCloseButtonX, kPanelActionTop);
  styleSurface(close, bg);
  // Close on touch-down: a finger can move slightly before release, which
  // would otherwise cancel LV_EVENT_CLICKED on these overlay panels.
  lv_obj_add_event_cb(close, onCloseParamDrawer, LV_EVENT_PRESSED, context);
  return close;
}

void renderBypassControl(lv_obj_t* parent, UiState& state, UiEventContext* context,
                         lv_obj_t** controlOut = nullptr)
{
  const auto* selected = selectedUiBlock(state);
  if (!selected) return;
  const auto& block = *selected;
  lv_obj_t* control = lv_obj_create(parent);
  lv_obj_set_size(control, kBypassControlWidth, kPanelActionHeight);
  lv_obj_set_pos(control, kBypassControlX, kPanelActionTop);
  lv_obj_remove_flag(control, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(control, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_flag(control, LV_OBJ_FLAG_CLICKABLE);
  styleSurface(control, 0x343434);
  lv_obj_set_style_radius(control, kParameterSliderRadius, 0);
  lv_obj_set_style_clip_corner(control, true, 0);
  lv_obj_set_style_pad_all(control, 0, 0);

  auto* visual = new BypassControlVisual{};
  lv_obj_set_user_data(control, visual);
  lv_obj_add_event_cb(control, freeBypassControlVisual, LV_EVENT_DELETE, visual);
  lv_obj_add_event_cb(control, onBypassClicked, LV_EVENT_CLICKED, context);

  const auto addTextPair = [&](lv_obj_t* layer, int color, lv_obj_t** valueOut) {
    lv_obj_t* title = label(layer, "Bypass", LV_ALIGN_LEFT_MID, 16, 0,
                            &ardor_font_open_sans_semibold_22, color);
    lv_obj_set_width(title, 90);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    lv_obj_t* value = label(layer, block.enabled ? "Off" : "On", LV_ALIGN_RIGHT_MID, -16, 0,
                            &ardor_font_open_sans_semibold_22, color);
    lv_obj_set_width(value, 44);
    lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    *valueOut = value;
  };

  addTextPair(control, text, &visual->inactiveValue);

  lv_obj_t* fill = lv_obj_create(control);
  lv_obj_set_size(fill, 0, kPanelActionHeight);
  lv_obj_set_pos(fill, 0, 0);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);
  styleSurface(fill, accent);
  lv_obj_set_style_radius(fill, kParameterSliderRadius, 0);
  lv_obj_set_style_clip_corner(fill, true, 0);
  lv_obj_set_style_pad_all(fill, 0, 0);
  visual->fill = fill;

  lv_obj_t* activeTextLayer = lv_obj_create(fill);
  lv_obj_set_size(activeTextLayer, kBypassControlWidth, kPanelActionHeight);
  lv_obj_set_pos(activeTextLayer, 0, 0);
  lv_obj_remove_flag(activeTextLayer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(activeTextLayer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_opa(activeTextLayer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(activeTextLayer, 0, 0);
  lv_obj_set_style_pad_all(activeTextLayer, 0, 0);
  addTextPair(activeTextLayer, 0x102014, &visual->activeValue);

  refreshBypassControlVisual(control, !block.enabled);
  if (controlOut) *controlOut = control;
}

void renderBlockPanelActions(lv_obj_t* parent, UiState& state, UiEventContext* context,
                             lv_obj_t** bypassOut = nullptr)
{
  renderBypassControl(parent, state, context, bypassOut);
  lv_obj_t* remove = button(parent, "Delete Block");
  lv_obj_set_size(remove, kDeleteBlockWidth, kPanelActionHeight);
  lv_obj_set_pos(remove, kDeleteBlockX, kPanelActionTop);
  styleSurface(remove, 0x4a2024);
  lv_obj_set_style_text_color(lv_obj_get_child(remove, 0), lv_color_hex(0xf97373), 0);
  lv_obj_add_event_cb(remove, onDeleteSelectedBlock, LV_EVENT_CLICKED, context);
}

void renderPageNavigation(lv_obj_t* parent, UiState& state, UiEventContext* context)
{
  const auto count = std::max<std::size_t>(1, parameterPageCount(state));
  const auto page = std::min(context->ui->parameterPage(), count - 1);
  if (count > 1) {
    lv_obj_t* previous = button(parent, "<");
    lv_obj_set_size(previous, 48, 48);
    lv_obj_align(previous, LV_ALIGN_TOP_LEFT, 28, 12);
    lv_obj_add_event_cb(previous, onPreviousParameterPage, LV_EVENT_CLICKED, context);
  }
  lv_obj_t* pageLabel = label(parent, "PAGE " + std::to_string(page + 1) + " / " + std::to_string(count),
                              LV_ALIGN_TOP_LEFT, 88, 25, &ardor_font_open_sans_regular_18, muted);
  lv_obj_set_width(pageLabel, 118);
  lv_label_set_long_mode(pageLabel, LV_LABEL_LONG_CLIP);
  if (count > 1) {
    lv_obj_t* next = button(parent, ">");
    lv_obj_set_size(next, 48, 48);
    lv_obj_align(next, LV_ALIGN_TOP_LEFT, 214, 12);
    lv_obj_add_event_cb(next, onNextParameterPage, LV_EVENT_CLICKED, context);
  }
}


void renderParameterPanel(lv_obj_t* root, UiState& state, UiEventContext* context,
                          std::vector<lv_obj_t*>* controlsOut, lv_obj_t** bypassOut,
                          lv_obj_t** titleOut)
{
  lv_obj_t* panelObject = lv_obj_create(root);
  lv_obj_set_size(panelObject, 1240, 286);
  lv_obj_align(panelObject, LV_ALIGN_BOTTOM_MID, 0, -52);
  lv_obj_remove_flag(panelObject, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(panelObject, panelAlt);
  lv_obj_set_style_pad_all(panelObject, 0, 0);
  lv_obj_add_event_cb(panelObject, onParameterGesture, LV_EVENT_GESTURE, context);

  renderPanelCloseButton(panelObject, context);

  if (state.paramTarget == UiParamTarget::Globals) {
    lv_obj_t* title = label(panelObject, "Global", LV_ALIGN_TOP_LEFT, 270, 22, &ardor_font_open_sans_semibold_22);
    if (titleOut) *titleOut = title;
    lv_obj_set_width(title, 660);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
  } else {
    const auto* selected = selectedUiBlock(state);
    if (!selected) return;
    const auto& block = *selected;
    lv_obj_t* title = label(panelObject, block.label + "  /  " + block.assetName,
                            LV_ALIGN_TOP_LEFT, kParameterTitleX, 22,
                            &ardor_font_open_sans_semibold_22);
    lv_obj_set_width(title, kParameterTitleWidth);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    if (titleOut) *titleOut = title;
    renderBlockPanelActions(panelObject, state, context, bypassOut);
  }

  renderPageNavigation(panelObject, state, context);
  const auto controls = parameterPage(state, context->ui->parameterPage());
  for (std::size_t i = 0; i < controls.size(); ++i) {
    const int column = static_cast<int>(i % kParameterSliderColumns);
    const int row = static_cast<int>(i / kParameterSliderColumns);
    lv_obj_t* slider = createParameterSlider(
      panelObject, controls[i],
      kParameterSliderGridX + column * (kParameterSliderWidth + kParameterSliderColumnGap),
      kParameterSliderGridY + row * (kParameterSliderHeight + kParameterSliderRowGap),
      context->ui->isParameterFocused(controls[i].key), context,
      onParameterSliderPressed, onParameterSliderPressing, i);
    if (controlsOut) controlsOut->push_back(slider);
  }
}

} // namespace

namespace parameter_widgets {

float sliderRatioForInput(lv_obj_t* slider, lv_indev_t* input)
{
  return parameterSliderRatioForInput(slider, input);
}

lv_obj_t* createSlider(lv_obj_t* parent, const ParameterControl& control,
                       int x, int y, bool focused, UiEventContext* context,
                       lv_event_cb_t pressedCallback,
                       lv_event_cb_t pressingCallback,
                       std::size_t controlIndex)
{
  return createParameterSlider(parent, control, x, y, focused, context,
                               pressedCallback, pressingCallback, controlIndex);
}

lv_obj_t* renderCloseButton(lv_obj_t* parent, UiEventContext* context)
{
  return renderPanelCloseButton(parent, context);
}

void renderBlockActions(lv_obj_t* parent, UiState& state,
                        UiEventContext* context, lv_obj_t** bypassOut)
{
  renderBlockPanelActions(parent, state, context, bypassOut);
}

} // namespace parameter_widgets

namespace parameter_view {

void syncSlider(lv_obj_t* slider, const ParameterControl& control, bool focused)
{
  refreshParameterSliderVisual(slider, control, focused);
}

void syncBypass(lv_obj_t* control, bool bypassed)
{
  refreshBypassControlVisual(control, bypassed);
}


void buildPanel(lv_obj_t* root, UiState& state, UiEventContext* context,
                std::vector<lv_obj_t*>* controlsOut,
                lv_obj_t** bypassOut, lv_obj_t** titleOut)
{
  renderParameterPanel(root, state, context, controlsOut, bypassOut, titleOut);
}

} // namespace parameter_view

} // namespace ardor
