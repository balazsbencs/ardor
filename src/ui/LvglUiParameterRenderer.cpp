#include "ui/LvglUi.h"

#include "ui/LvglUiParameterView.h"
#include "ui/LvglUiParameterWidgets.h"
#include "ui/LvglUiStyle.h"
#include "ui/fonts/SairaCondSemibold28.h"
#include "ui/fonts/SairaLight44.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

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
constexpr int kBypassMidiWidth = 72;
constexpr int kBypassMidiX = kBypassControlX - 84;
constexpr int kDeleteBlockWidth = 156;
constexpr int kDeleteBlockX = kBypassMidiX - 24 - kDeleteBlockWidth;
constexpr int kParameterTitleX = 270;
constexpr int kParameterTitleWidthFull = kDeleteBlockX - kParameterTitleX - 24;
// Gain-reduction meter: a compressor-only pill sitting between the title and
// Delete Block. Every other block type keeps the full-width title above.
constexpr int kGainMeterWidth = 120;
constexpr int kGainMeterHeight = kPanelActionHeight;
constexpr int kGainMeterX = kDeleteBlockX - 24 - kGainMeterWidth;
constexpr int kGainMeterBarWidth = 8;
constexpr int kGainMeterBarHeight = 30;
constexpr int kGainMeterBarX = 14;
constexpr int kGainMeterBarY = (kGainMeterHeight - kGainMeterBarHeight) / 2;
constexpr int kGainMeterLabelX = kGainMeterBarX + kGainMeterBarWidth + 10;
constexpr float kGainMeterFullScaleDb = 24.0f;
constexpr int kParameterTitleWidthWithGainMeter = kGainMeterX - kParameterTitleX - 24;
constexpr int kParameterSliderColumns = 3;
constexpr int kParameterSliderWidth = 385;
constexpr int kParameterSliderHeight = 132;
constexpr int kParameterSliderColumnGap = 14;
constexpr int kParameterSliderRowGap = 16;
constexpr int kParameterSliderGridX = 28;
constexpr int kParameterSliderGridY = 78;
constexpr int kParameterSliderRadius = 0;
constexpr int kParameterSliderTextInset = 24;
// Rail + Thumb: a conventional recessed track and a visibly grabbable handle.
// The 44 px thumb is deliberately wider than the selected mockup's first pass;
// the retained slider object remains the full 385 x 132 touch target.
constexpr int kTravelRailHeight = 18;
constexpr int kTravelFillHeight = kTravelRailHeight - 2;
constexpr int kTravelHandleWidth = 44;
constexpr int kTravelHandleHeight = 54;
constexpr int kTravelWidth = kParameterSliderWidth - 2 * kParameterSliderTextInset;
constexpr int kTravelTop = 88;
constexpr int kTravelInteriorX = kParameterSliderTextInset + 1;
constexpr int kTravelInteriorWidth = kTravelWidth - 2;
constexpr int kTravelHandleTop = kTravelTop - (kTravelHandleHeight - kTravelRailHeight) / 2;
constexpr int kDiscreteOptionsHeight = 60;
constexpr int kDiscreteOptionsTop = kParameterSliderHeight - kDiscreteOptionsHeight - 4;
constexpr int kChoiceStepperGap = 6;
constexpr int kChoiceStepperNudgeWidth = 72;
constexpr int kChoicePickerX = 48;
constexpr int kChoicePickerY = 96;
constexpr int kChoicePickerWidth = 1183;
constexpr int kChoicePickerHeaderHeight = 56;
constexpr int kChoicePickerBodyInset = 20;
constexpr int kChoicePickerColumns = 5;
constexpr int kChoicePickerTileHeight = 76;
constexpr int kChoicePickerTileGap = 10;
constexpr int kChoicePickerSectionHeight = 24;
constexpr int kParameterPanelHeight = 452;
constexpr int kMappingToolbarX = kParameterSliderGridX;
constexpr int kMappingToolbarY = kParameterSliderGridY
  + 2 * kParameterSliderHeight + kParameterSliderRowGap + 14;
constexpr int kMappingToolbarWidth = 1183;
constexpr int kMappingToolbarHeight = 60;
constexpr int kMappingButtonWidth = 148;
constexpr int kMappingButtonHeight = 40;
constexpr int kMappingMidiButtonX = kMappingToolbarWidth - 18 - kMappingButtonWidth;
constexpr int kMappingExpButtonX = kMappingMidiButtonX - 14 - kMappingButtonWidth;

struct ParameterSliderVisual {
  std::size_t controlIndex = 0;
  ParameterControlKind kind = ParameterControlKind::Continuous;
  lv_obj_t* keyLabel = nullptr;
  lv_obj_t* valueLabel = nullptr;
  lv_obj_t* unitLabel = nullptr;
  // Continuous: recessed rail and wide thumb.
  lv_obj_t* fill = nullptr;
  lv_obj_t* handle = nullptr;
  int travelX = kTravelInteriorX;
  int travelWidth = kTravelInteriorWidth;
  int handleWidth = kTravelHandleWidth;
  // Discrete: segmented option row.
  std::vector<lv_obj_t*> options;
};

// Splits a formatted value like "380 ms" or "34%" into a big numeral and a
// small unit suffix so the two can carry different type sizes, matching the
// mockup's Saira-Light-numeral-plus-muted-unit treatment. Non-numeric text
// (choice labels such as "Dual") comes back with an empty unit.
std::pair<std::string, std::string> splitFormattedValue(const std::string& formatted)
{
  std::size_t i = 0;
  const std::size_t n = formatted.size();
  while (i < n && (std::isdigit(static_cast<unsigned char>(formatted[i]))
                    || formatted[i] == '-' || formatted[i] == '+' || formatted[i] == '.')) {
    ++i;
  }
  if (i == 0 || i == n) {
    return {formatted, ""};
  }
  std::size_t unitStart = i;
  while (unitStart < n && formatted[unitStart] == ' ') ++unitStart;
  return {formatted.substr(0, i), formatted.substr(unitStart)};
}

std::string uppercase(const std::string& value)
{
  std::string result = value;
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return result;
}

struct ParameterMappingVisual {
  lv_obj_t* parameterLabel = nullptr;
  lv_obj_t* valueLabel = nullptr;
  lv_obj_t* expressionButton = nullptr;
  lv_obj_t* midiButton = nullptr;
  UiEventContext* expressionContext = nullptr;
  UiEventContext* midiContext = nullptr;
};

struct BypassControlVisual {
  lv_obj_t* fill = nullptr;
  lv_obj_t* inactiveValue = nullptr;
  lv_obj_t* activeValue = nullptr;
};

bool selectedBlockIsHarmonizer(const UiEventContext* context)
{
  if (!context || !context->state || context->state->paramTarget != UiParamTarget::Block) {
    return false;
  }
  const auto* block = selectedUiBlock(*context->state);
  return block && block->type == "mod" && block->params.value("mode", std::string{}) == "harmonizer";
}

bool usesHarmonizerMap(const UiEventContext* context, const ParameterControl& control)
{
  // The map is meaningful only for the two musically structured controls. Scale
  // deliberately remains a direct row: its five modes are short and comparable.
  return selectedBlockIsHarmonizer(context) && (control.key == "depth" || control.key == "p2");
}



void freeParameterSliderVisual(lv_event_t* event)
{
  delete static_cast<ParameterSliderVisual*>(lv_event_get_user_data(event));
}

void freeParameterMappingVisual(lv_event_t* event)
{
  delete static_cast<ParameterMappingVisual*>(lv_event_get_user_data(event));
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

void refreshParameterSliderVisual(lv_obj_t* slider, const ParameterControl& control,
                                  bool focused = true)
{
  const auto* visual = static_cast<const ParameterSliderVisual*>(lv_obj_get_user_data(slider));
  if (!visual) {
    return;
  }
  if (visual->keyLabel) {
    lv_label_set_text(visual->keyLabel, uppercase(control.label).c_str());
  }
  const auto [valueText, unitText] = splitFormattedValue(control.formatted);
  if (visual->valueLabel) {
    lv_label_set_text(visual->valueLabel, valueText.c_str());
  }
  if (visual->unitLabel) {
    lv_label_set_text(visual->unitLabel, unitText.c_str());
    lv_obj_align_to(visual->unitLabel, visual->valueLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -2);
  }

  if (visual->fill && visual->handle) {
    const float range = control.maximum - control.minimum;
    const float ratio = range == 0.0f ? 0.0f
      : std::clamp((control.value - control.minimum) / range, 0.0f, 1.0f);
    lv_obj_set_width(visual->fill,
                     static_cast<int32_t>(std::lround(ratio * visual->travelWidth)));
    lv_obj_set_x(visual->handle, visual->travelX + static_cast<int32_t>(std::lround(
      ratio * static_cast<float>(visual->travelWidth))) - visual->handleWidth / 2);
    // Design law 3: the lamp colour is reserved for the running preset and
    // the selected parameter. An idle rail reads in the same ink as the
    // surrounding nomenclature.
    const int accent = focused ? lamp : text;
    lv_obj_set_style_bg_color(visual->fill, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_color(visual->handle, lv_color_hex(focused ? lamp : text), 0);
  }

  const auto selectedIndex = static_cast<int>(std::lround(control.value));
  for (std::size_t i = 0; i < visual->options.size(); ++i) {
    lv_obj_t* option = visual->options[i];
    if (!option) continue;
    const bool on = static_cast<int>(i) == selectedIndex;
    styleSurface(option, on ? text : bg);
    lv_obj_set_style_bg_opa(option, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(option, i == 0 ? 0 : 1, 0);
    lv_obj_set_style_border_color(option, lv_color_hex(rule), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(option, 0), lv_color_hex(on ? bg : muted), 0);
  }

  lv_obj_set_style_outline_width(slider, focused ? 1 : 0, 0);
  lv_obj_set_style_outline_color(slider, lv_color_hex(lamp), 0);
  lv_obj_set_style_outline_pad(slider, 2, 0);
}

void styleMappingButton(lv_obj_t* control, bool supported, bool assigned)
{
  if (supported) {
    lv_obj_remove_state(control, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(control, LV_STATE_DISABLED);
  }
  styleSurface(control, assigned ? panel : panelAlt);
  lv_obj_set_style_border_width(control, assigned ? 2 : 1, 0);
  lv_obj_set_style_border_color(control,
                                lv_color_hex(assigned ? lamp : disabled), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(control, 0),
                              lv_color_hex(assigned ? lamp : (supported ? text : muted)), 0);
}

void refreshParameterMappingVisual(lv_obj_t* toolbar, const ParameterControl& control,
                                   std::size_t controlIndex, bool expressionSupported,
                                   bool midiSupported, bool expressionAssigned,
                                   bool midiAssigned)
{
  auto* visual = static_cast<ParameterMappingVisual*>(lv_obj_get_user_data(toolbar));
  if (!visual) return;

  const auto selected = "Selected  /  " + control.label;
  lv_label_set_text(visual->parameterLabel, selected.c_str());
  lv_label_set_text(visual->valueLabel, control.formatted.c_str());
  lv_label_set_text(lv_obj_get_child(visual->expressionButton, 0),
                    expressionAssigned ? "EXP Assigned" : "Assign EXP");
  lv_label_set_text(lv_obj_get_child(visual->midiButton, 0),
                    midiAssigned ? "MIDI Mapped" : "MIDI Learn");
  visual->expressionContext->index = controlIndex;
  visual->midiContext->index = controlIndex;
  styleMappingButton(visual->expressionButton, expressionSupported, expressionAssigned);
  styleMappingButton(visual->midiButton, midiSupported, midiAssigned);
}

bool expressionAssignedTo(const UiState& state, const ParameterControl& control)
{
  if (state.paramTarget != UiParamTarget::Block) return false;
  const auto* block = selectedUiBlock(state);
  const auto& assignment = state.bank.presets[state.activePreset].expression;
  return block && assignment && assignment->blockId == block->id
    && assignment->parameter == control.key;
}

void onExpressionAssignmentClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto controls = parameterPage(*context->state, context->ui->parameterPage());
  if (context->index >= controls.size()) return;
  context->ui->toggleExpressionAssignment(*context->state, controls[context->index]);
  redraw(context);
}

void onMidiLearnClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto controls = parameterPage(*context->state, context->ui->parameterPage());
  if (context->index >= controls.size()) return;
  beginMidiLearn(*context->state, controls[context->index]);
  context->ui->invalidate(UiChange::Parameters | UiChange::Status);
  redraw(context);
}

void refreshGainMeterVisual(lv_obj_t* fill, lv_obj_t* valueLabel, float reductionDb)
{
  // Reduction is <= 0 (0 = no reduction); the bar fills downward from the
  // 0 dB line at top as the cut deepens, like a hardware GR meter.
  const float magnitude = std::clamp(-reductionDb, 0.0f, kGainMeterFullScaleDb);
  const int fillHeight = static_cast<int>(
    std::lround(magnitude / kGainMeterFullScaleDb * kGainMeterBarHeight));
  if (fill) {
    lv_obj_set_height(fill, fillHeight);
  }
  if (valueLabel) {
    char buffer[24]{};
    std::snprintf(buffer, sizeof(buffer), "%.1f dB", reductionDb);
    lv_label_set_text(valueLabel, buffer);
  }
}

void renderGainMeter(lv_obj_t* parent, float reductionDb, lv_obj_t** fillOut, lv_obj_t** labelOut)
{
  lv_obj_t* pill = lv_obj_create(parent);
  lv_obj_set_size(pill, kGainMeterWidth, kGainMeterHeight);
  lv_obj_set_pos(pill, kGainMeterX, kPanelActionTop);
  lv_obj_remove_flag(pill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);
  styleSurface(pill, panel);
  lv_obj_set_style_radius(pill, 0, 0);
  lv_obj_set_style_pad_all(pill, 0, 0);

  lv_obj_t* track = lv_obj_create(pill);
  lv_obj_remove_style_all(track);
  lv_obj_set_size(track, kGainMeterBarWidth, kGainMeterBarHeight);
  lv_obj_set_pos(track, kGainMeterBarX, kGainMeterBarY);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(track, lv_color_hex(rule), 0);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);

  // Anchored to the track's top so growing height reads as "filling down
  // from 0 dB", matching refreshGainMeterVisual's fill-height math.
  lv_obj_t* fill = lv_obj_create(pill);
  lv_obj_remove_style_all(fill);
  lv_obj_set_size(fill, kGainMeterBarWidth, 0);
  lv_obj_set_pos(fill, kGainMeterBarX, kGainMeterBarY);
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(fill, lv_color_hex(lamp), 0);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* valueLabel = label(pill, "0.0 dB", LV_ALIGN_LEFT_MID, kGainMeterLabelX, 0,
                               &ardor_font_saira_cond_semibold_22, text);
  lv_obj_set_width(valueLabel, kGainMeterWidth - kGainMeterLabelX - 10);
  lv_label_set_long_mode(valueLabel, LV_LABEL_LONG_CLIP);

  refreshGainMeterVisual(fill, valueLabel, reductionDb);
  if (fillOut) *fillOut = fill;
  if (labelOut) *labelOut = valueLabel;
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

void onDiscreteOptionSelected(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto controls = parameterPage(*context->state, context->ui->parameterPage());
  if (context->index >= controls.size()) {
    return;
  }
  const auto& control = controls[context->index];
  context->filter = control.key;
  lv_obj_t* optsRow = lv_obj_get_parent(lv_event_get_target_obj(event));
  context->ui->setFocusedWidgets(lv_obj_get_parent(optsRow));
  context->ui->focusParameter(context->filter);
  const int delta = static_cast<int>(context->parentIndex)
    - static_cast<int>(std::lround(control.value));
  context->ui->applyFocusedParameterDelta(*context->state, delta, false);
  redraw(context);
}

void closeChoicePicker(UiEventContext* context)
{
  if (context && context->controlledObject) {
    lv_obj_delete(context->controlledObject);
    context->controlledObject = nullptr;
  }
}

void onChoicePickerClosed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (lv_event_get_target_obj(event) == context->controlledObject) {
    closeChoicePicker(context);
  }
}

void onChoicePickerCloseButton(lv_event_t* event)
{
  closeChoicePicker(static_cast<UiEventContext*>(lv_event_get_user_data(event)));
}

void onChoicePickerOptionSelected(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto controls = parameterPage(*context->state, context->ui->parameterPage());
  if (context->index >= controls.size()) return;
  const auto& control = controls[context->index];
  context->filter = control.key;
  context->ui->setFocusedWidgets(context->ghost);
  context->ui->focusParameter(context->filter);
  const int delta = static_cast<int>(context->parentIndex)
    - static_cast<int>(std::lround(control.value));
  context->ui->applyFocusedParameterDelta(*context->state, delta, false);
  closeChoicePicker(context);
  redraw(context);
}

void onChoiceStepperNudged(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto controls = parameterPage(*context->state, context->ui->parameterPage());
  if (context->index >= controls.size()) return;
  const auto& control = controls[context->index];
  context->filter = control.key;
  context->ui->setFocusedWidgets(lv_obj_get_parent(lv_event_get_target_obj(event)));
  context->ui->focusParameter(context->filter);
  context->ui->applyFocusedParameterDelta(*context->state,
                                          context->parentIndex == 0 ? -1 : 1, false);
  redraw(context);
}

lv_obj_t* choicePickerOverlay(lv_obj_t* slider)
{
  lv_obj_t* panelObject = lv_obj_get_parent(slider);
  return panelObject ? lv_obj_get_parent(panelObject) : nullptr;
}

UiEventContext* pickerContext(UiEventContext* context, std::size_t controlIndex,
                              std::size_t choiceIndex, lv_obj_t* overlay)
{
  auto* optionContext = context->ui->remember(*context->state, controlIndex);
  optionContext->parentIndex = choiceIndex;
  optionContext->controlledObject = overlay;
  // The harmonizer map can change either Key or Interval. Only reuse the
  // opening card when the picked value belongs to that same control; otherwise
  // let the normal parameter sync refresh the correct card.
  optionContext->ghost = context->index == controlIndex ? context->ghost : nullptr;
  return optionContext;
}

lv_obj_t* pickerTile(lv_obj_t* parent, const std::string& choice, int x, int y, int width,
                     bool selected, UiEventContext* context, std::size_t controlIndex,
                     std::size_t choiceIndex, lv_obj_t* overlay)
{
  lv_obj_t* tile = lv_button_create(parent);
  lv_obj_set_size(tile, width, kChoicePickerTileHeight);
  lv_obj_set_pos(tile, x, y);
  styleSurface(tile, selected ? lamp : panelAlt);
  lv_obj_set_style_border_color(tile, lv_color_hex(selected ? lamp : rule), 0);
  lv_obj_set_style_pad_all(tile, 8, 0);
  lv_obj_t* tileLabel = lv_label_create(tile);
  lv_label_set_text(tileLabel, choice.c_str());
  setText(tileLabel, selected ? bg : text, &ardor_font_saira_cond_medium_18);
  lv_obj_set_width(tileLabel, width - 16);
  lv_label_set_long_mode(tileLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(tileLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(tileLabel);
  lv_obj_add_event_cb(tile, onChoicePickerOptionSelected, LV_EVENT_CLICKED,
                      pickerContext(context, controlIndex, choiceIndex, overlay));
  return tile;
}

void addChoicePickerSection(lv_obj_t* sheet, int* y, const std::string& title,
                            const ParameterControl& control, std::size_t first,
                            std::size_t count, UiEventContext* context,
                            std::size_t controlIndex, lv_obj_t* overlay)
{
  if (!title.empty()) {
    lv_obj_t* heading = label(sheet, uppercase(title), LV_ALIGN_TOP_LEFT,
                              kChoicePickerBodyInset, *y,
                              &ardor_font_saira_cond_semibold_11, disabled);
    lv_obj_set_style_text_letter_space(heading, 2, 0);
    *y += kChoicePickerSectionHeight;
  }
  const int contentWidth = kChoicePickerWidth - 2 * kChoicePickerBodyInset;
  const int tileWidth = (contentWidth - (kChoicePickerColumns - 1) * kChoicePickerTileGap)
    / kChoicePickerColumns;
  for (std::size_t offset = 0; offset < count; ++offset) {
    const int column = static_cast<int>(offset % kChoicePickerColumns);
    const int row = static_cast<int>(offset / kChoicePickerColumns);
    const int width = column + 1 == kChoicePickerColumns
      ? contentWidth - column * (tileWidth + kChoicePickerTileGap)
      : tileWidth;
    const std::size_t index = first + offset;
    pickerTile(sheet, control.choices[index], kChoicePickerBodyInset + column * (tileWidth + kChoicePickerTileGap),
               *y + row * (kChoicePickerTileHeight + kChoicePickerTileGap), width,
               static_cast<int>(index) == static_cast<int>(std::lround(control.value)),
               context, controlIndex, index, overlay);
  }
  const int rows = static_cast<int>((count + kChoicePickerColumns - 1) / kChoicePickerColumns);
  *y += rows * kChoicePickerTileHeight + std::max(0, rows - 1) * kChoicePickerTileGap;
}

void openChoiceGridPicker(lv_obj_t* slider, UiEventContext* context)
{
  const auto controls = parameterPage(*context->state, context->ui->parameterPage());
  const auto* visual = static_cast<const ParameterSliderVisual*>(lv_obj_get_user_data(slider));
  if (!visual || visual->controlIndex >= controls.size()) return;
  const auto& control = controls[visual->controlIndex];
  lv_obj_t* root = choicePickerOverlay(slider);
  if (!root) return;

  lv_obj_t* overlay = lv_obj_create(root);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(overlay, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(0x0a0c0d), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  auto* closeContext = context->ui->remember(*context->state);
  closeContext->controlledObject = overlay;
  lv_obj_add_event_cb(overlay, onChoicePickerClosed, LV_EVENT_CLICKED, closeContext);

  const bool groupedWhammy = !control.choices.empty() && control.choices.size() == 19
    && context->state->paramTarget == UiParamTarget::Block
    && selectedUiBlock(*context->state)
    && selectedUiBlock(*context->state)->params.value("mode", std::string{}) == "whammy";
  const int sectionCount = groupedWhammy ? 2 : 1;
  const int sectionHeadingCount = groupedWhammy ? 2 : 0;
  const int rows = groupedWhammy ? 4
    : static_cast<int>((control.choices.size() + kChoicePickerColumns - 1) / kChoicePickerColumns);
  const int sheetHeight = kChoicePickerHeaderHeight + 2 * kChoicePickerBodyInset
    + rows * kChoicePickerTileHeight + std::max(0, rows - sectionCount) * kChoicePickerTileGap
    + sectionHeadingCount * kChoicePickerSectionHeight + (groupedWhammy ? kChoicePickerTileGap : 0);
  lv_obj_t* sheet = lv_obj_create(overlay);
  lv_obj_set_size(sheet, kChoicePickerWidth, sheetHeight);
  lv_obj_set_pos(sheet, kChoicePickerX, kChoicePickerY);
  lv_obj_remove_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(sheet, panel);
  lv_obj_set_style_pad_all(sheet, 0, 0);

  lv_obj_t* title = label(sheet, uppercase(control.label), LV_ALIGN_TOP_LEFT,
                          kChoicePickerBodyInset, 16,
                          &ardor_font_saira_cond_semibold_22, text);
  lv_obj_t* count = label(sheet, std::to_string(control.choices.size()) + " OPTIONS",
                          LV_ALIGN_TOP_LEFT, 20 + lv_obj_get_width(title), 21,
                          &ardor_font_saira_cond_medium_18, muted);
  (void) count;
  lv_obj_t* close = button(sheet, "Close");
  lv_obj_set_size(close, 88, 40);
  lv_obj_set_pos(close, kChoicePickerWidth - kChoicePickerBodyInset - 88, 8);
  lv_obj_add_event_cb(close, onChoicePickerCloseButton, LV_EVENT_CLICKED, closeContext);
  lv_obj_t* headerRule = lv_obj_create(sheet);
  lv_obj_remove_style_all(headerRule);
  lv_obj_set_size(headerRule, kChoicePickerWidth, 1);
  lv_obj_set_pos(headerRule, 0, kChoicePickerHeaderHeight - 1);
  lv_obj_set_style_bg_opa(headerRule, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(headerRule, lv_color_hex(rule), 0);

  int y = kChoicePickerHeaderHeight + kChoicePickerBodyInset;
  if (groupedWhammy) {
    addChoicePickerSection(sheet, &y, "Whammy", control, 0, 10, context,
                            visual->controlIndex, overlay);
    y += kChoicePickerTileGap;
    addChoicePickerSection(sheet, &y, "Harmony", control, 10, 9, context,
                            visual->controlIndex, overlay);
  } else {
    addChoicePickerSection(sheet, &y, "", control, 0, control.choices.size(), context,
                            visual->controlIndex, overlay);
  }
  lv_obj_move_foreground(overlay);
}

void openHarmonizerMap(lv_obj_t* slider, UiEventContext* context)
{
  const auto controls = parameterPage(*context->state, context->ui->parameterPage());
  std::size_t intervalIndex = controls.size();
  std::size_t keyIndex = controls.size();
  for (std::size_t i = 0; i < controls.size(); ++i) {
    if (controls[i].key == "depth") intervalIndex = i;
    if (controls[i].key == "p2") keyIndex = i;
  }
  if (intervalIndex == controls.size() || keyIndex == controls.size()) return;
  lv_obj_t* root = choicePickerOverlay(slider);
  if (!root) return;

  lv_obj_t* overlay = lv_obj_create(root);
  lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(overlay, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(0x0a0c0d), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
  lv_obj_set_style_border_width(overlay, 0, 0);
  auto* closeContext = context->ui->remember(*context->state);
  closeContext->controlledObject = overlay;
  lv_obj_add_event_cb(overlay, onChoicePickerClosed, LV_EVENT_CLICKED, closeContext);

  constexpr int kMapSheetHeight = 492;
  lv_obj_t* sheet = lv_obj_create(overlay);
  lv_obj_set_size(sheet, kChoicePickerWidth, kMapSheetHeight);
  lv_obj_set_pos(sheet, kChoicePickerX, 72);
  lv_obj_remove_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(sheet, panel);
  lv_obj_set_style_pad_all(sheet, 0, 0);
  label(sheet, "KEY & INTERVAL", LV_ALIGN_TOP_LEFT, kChoicePickerBodyInset, 16,
        &ardor_font_saira_cond_semibold_22, text);
  label(sheet, "HARMONIZER", LV_ALIGN_TOP_LEFT, 220, 21,
        &ardor_font_saira_cond_medium_18, muted);
  lv_obj_t* close = button(sheet, "Close");
  lv_obj_set_size(close, 88, 40);
  lv_obj_set_pos(close, kChoicePickerWidth - kChoicePickerBodyInset - 88, 8);
  lv_obj_add_event_cb(close, onChoicePickerCloseButton, LV_EVENT_CLICKED, closeContext);

  label(sheet, "KEY  ·  12 SEMITONES", LV_ALIGN_TOP_LEFT, kChoicePickerBodyInset, 72,
        &ardor_font_saira_cond_semibold_11, disabled);
  constexpr std::array<int, 7> kWhiteNotes = {0, 2, 4, 5, 7, 9, 11};
  constexpr std::array<int, 5> kBlackNotes = {1, 3, 6, 8, 10};
  constexpr std::array<int, 5> kBlackOffsets = {110, 270, 590, 750, 910};
  constexpr int kKeyboardY = 94;
  constexpr int kWhiteKeyWidth = 158;
  constexpr int kWhiteKeyHeight = 148;
  for (std::size_t i = 0; i < kWhiteNotes.size(); ++i) {
    const int note = kWhiteNotes[i];
    const bool selected = static_cast<int>(std::lround(controls[keyIndex].value)) == note;
    lv_obj_t* key = pickerTile(sheet, controls[keyIndex].choices[note],
                               kChoicePickerBodyInset + static_cast<int>(i) * 160,
                               kKeyboardY, kWhiteKeyWidth, selected,
                               context, keyIndex, note, overlay);
    lv_obj_set_height(key, kWhiteKeyHeight);
    if (!selected) {
      styleSurface(key, text);
      lv_obj_set_style_text_color(lv_obj_get_child(key, 0), lv_color_hex(bg), 0);
    }
    lv_obj_align(lv_obj_get_child(key, 0), LV_ALIGN_BOTTOM_MID, 0, -14);
  }
  for (std::size_t i = 0; i < kBlackNotes.size(); ++i) {
    const int note = kBlackNotes[i];
    const bool selected = static_cast<int>(std::lround(controls[keyIndex].value)) == note;
    lv_obj_t* key = pickerTile(sheet, controls[keyIndex].choices[note],
                               kChoicePickerBodyInset + kBlackOffsets[i], kKeyboardY, 92, selected,
                               context, keyIndex, note, overlay);
    lv_obj_set_height(key, 94);
    if (!selected) {
      styleSurface(key, bg);
      lv_obj_set_style_text_color(lv_obj_get_child(key, 0), lv_color_hex(text), 0);
    }
    lv_obj_align(lv_obj_get_child(key, 0), LV_ALIGN_BOTTOM_MID, 0, -10);
  }

  constexpr int kLadderY = 292;
  label(sheet, "INTERVAL  ·  DOWN THROUGH UNISON TO UP", LV_ALIGN_TOP_LEFT,
        kChoicePickerBodyInset, kLadderY - 24, &ardor_font_saira_cond_semibold_11, disabled);
  const int ladderWidth = (kChoicePickerWidth - 2 * kChoicePickerBodyInset
    - static_cast<int>(controls[intervalIndex].choices.size() - 1))
    / static_cast<int>(controls[intervalIndex].choices.size());
  for (std::size_t i = 0; i < controls[intervalIndex].choices.size(); ++i) {
    pickerTile(sheet, controls[intervalIndex].choices[i],
               kChoicePickerBodyInset + static_cast<int>(i) * (ladderWidth + 1), kLadderY,
               ladderWidth, static_cast<int>(std::lround(controls[intervalIndex].value)) == static_cast<int>(i),
               context, intervalIndex, i, overlay);
  }
  label(sheet, "<  OCTAVE DOWN", LV_ALIGN_TOP_LEFT, kChoicePickerBodyInset, kLadderY + 88,
        &ardor_font_saira_cond_semibold_11, disabled);
  label(sheet, "UNISON", LV_ALIGN_TOP_MID, 0, kLadderY + 88,
        &ardor_font_saira_cond_semibold_11, disabled);
  label(sheet, "OCTAVE UP  >", LV_ALIGN_TOP_RIGHT, -kChoicePickerBodyInset, kLadderY + 88,
        &ardor_font_saira_cond_semibold_11, disabled);
  lv_obj_move_foreground(overlay);
}

void onChoiceGridOpened(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  openChoiceGridPicker(lv_obj_get_user_data(target) ? target : lv_obj_get_parent(target), context);
}

void onHarmonizerMapOpened(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_obj_t* target = lv_event_get_target_obj(event);
  openHarmonizerMap(lv_obj_get_user_data(target) ? target : lv_obj_get_parent(target), context);
}

void onBypassClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_obj_t* control = lv_event_get_target_obj(event);
  const auto* selected = selectedUiBlock(*context->state);
  if (!selected || !previewIsSynchronized(*context->state)) return;
  const bool enabled = !selected->enabled;
  const bool updatedLive = context->ui->actions().updateBlockEnabled
    && context->ui->actions().updateBlockEnabled(selected->id, enabled);
  if (updatedLive) setSelectedBlockEnabledLive(*context->state, enabled);
  else setSelectedBlockEnabled(*context->state, enabled);
  selected = selectedUiBlock(*context->state);
  if (selected) refreshBypassControlVisual(control, !selected->enabled);
  redraw(context);
}

void onBypassMidiLearnClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  beginMidiLearnForBlockEnabled(*context->state);
  context->ui->invalidate(UiChange::Parameters | UiChange::Status);
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
  styleSurface(slider, panel);
  lv_obj_set_style_border_width(slider, 1, 0);
  lv_obj_set_style_border_color(slider, lv_color_hex(rule), 0);
  lv_obj_set_style_radius(slider, kParameterSliderRadius, 0);
  lv_obj_set_style_pad_all(slider, 0, 0);
  lv_obj_set_style_shadow_width(slider, 0, 0);

  auto* visual = new ParameterSliderVisual{};
  visual->controlIndex = controlIndex;
  visual->kind = control.kind;
  lv_obj_set_user_data(slider, visual);
  lv_obj_add_event_cb(slider, freeParameterSliderVisual, LV_EVENT_DELETE, visual);

  visual->keyLabel = label(slider, uppercase(control.label), LV_ALIGN_TOP_LEFT,
                           kParameterSliderTextInset, 8,
                           &ardor_font_saira_cond_medium_18, muted);
  lv_obj_set_style_text_letter_space(visual->keyLabel, 2, 0);
  lv_obj_set_width(visual->keyLabel, kTravelWidth);
  lv_label_set_long_mode(visual->keyLabel, LV_LABEL_LONG_CLIP);

  const bool continuous = control.kind == ParameterControlKind::Continuous;
  visual->valueLabel = label(slider, "", LV_ALIGN_TOP_LEFT, kParameterSliderTextInset, 27,
                             continuous ? &ardor_font_saira_light_44
                                        : &ardor_font_saira_cond_semibold_28,
                             text);
  lv_obj_set_width(visual->valueLabel, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(visual->valueLabel, kTravelWidth, 0);
  lv_label_set_long_mode(visual->valueLabel, LV_LABEL_LONG_CLIP);

  if (continuous) {
    lv_obj_add_flag(slider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(slider, onPressed, LV_EVENT_PRESSED, context);
    lv_obj_add_event_cb(slider, onPressing, LV_EVENT_PRESSING, context);
    lv_obj_add_event_cb(slider, onParameterControlReleased, LV_EVENT_RELEASED, context);
    lv_obj_add_event_cb(slider, onParameterControlReleased, LV_EVENT_PRESS_LOST, context);

    visual->unitLabel = label(slider, "", LV_ALIGN_TOP_LEFT, 0, 0,
                              &ardor_font_saira_cond_medium_18, muted);

    lv_obj_t* rail = lv_obj_create(slider);
    lv_obj_remove_style_all(rail);
    lv_obj_set_size(rail, kTravelWidth, kTravelRailHeight);
    lv_obj_set_pos(rail, kParameterSliderTextInset, kTravelTop);
    lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(rail, lv_color_hex(panelAlt), 0);
    lv_obj_set_style_border_width(rail, 1, 0);
    lv_obj_set_style_border_color(rail, lv_color_hex(rule), 0);
    lv_obj_remove_flag(rail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(rail, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* fill = lv_obj_create(slider);
    lv_obj_remove_style_all(fill);
    lv_obj_set_size(fill, 0, kTravelFillHeight);
    lv_obj_set_pos(fill, kTravelInteriorX, kTravelTop + 1);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_CLICKABLE);
    visual->fill = fill;

    lv_obj_t* handle = lv_obj_create(slider);
    lv_obj_remove_style_all(handle);
    lv_obj_set_size(handle, kTravelHandleWidth, kTravelHandleHeight);
    lv_obj_set_pos(handle, kTravelInteriorX - kTravelHandleWidth / 2, kTravelHandleTop);
    lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(handle, 4, 0);
    lv_obj_set_style_border_color(handle, lv_color_hex(panel), 0);
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    visual->handle = handle;

    lv_obj_t* grip = lv_obj_create(handle);
    lv_obj_remove_style_all(grip);
    lv_obj_set_size(grip, 2, 26);
    lv_obj_center(grip);
    lv_obj_set_style_bg_opa(grip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(grip, lv_color_hex(panelAlt), 0);
    lv_obj_remove_flag(grip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(grip, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* minimum = label(slider, "MIN", LV_ALIGN_TOP_LEFT,
                              kParameterSliderTextInset, kTravelTop + kTravelRailHeight + 2,
                              &ardor_font_saira_cond_semibold_11, muted);
    lv_obj_set_width(minimum, 32);
    lv_obj_t* maximum = label(slider, "MAX", LV_ALIGN_TOP_LEFT,
                              kParameterSliderWidth - kParameterSliderTextInset - 32,
                              kTravelTop + kTravelRailHeight + 2,
                              &ardor_font_saira_cond_semibold_11, muted);
    lv_obj_set_width(maximum, 32);
    lv_obj_set_style_text_align(maximum, LV_TEXT_ALIGN_RIGHT, 0);
  } else {
    const auto count = std::max<std::size_t>(1, control.choices.size());
    const bool directRow = count <= 4
      || (selectedBlockIsHarmonizer(context) && control.key == "p1");
    if (directRow) {
      const int optionWidth = kTravelWidth / static_cast<int>(count);
      lv_obj_t* optsRow = lv_obj_create(slider);
      lv_obj_remove_flag(optsRow, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_remove_flag(optsRow, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(optsRow, kTravelWidth, kDiscreteOptionsHeight);
      lv_obj_set_pos(optsRow, kParameterSliderTextInset, kDiscreteOptionsTop);
      lv_obj_set_style_bg_opa(optsRow, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(optsRow, 1, 0);
      lv_obj_set_style_border_color(optsRow, lv_color_hex(rule), 0);
      lv_obj_set_style_radius(optsRow, 0, 0);
      lv_obj_set_style_pad_all(optsRow, 0, 0);
      for (std::size_t i = 0; i < count; ++i) {
        lv_obj_t* option = lv_obj_create(optsRow);
        lv_obj_remove_flag(option, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(option, LV_OBJ_FLAG_CLICKABLE);
        const int width = i + 1 == count
          ? kTravelWidth - static_cast<int>(i) * optionWidth
          : optionWidth;
        lv_obj_set_size(option, width, kDiscreteOptionsHeight);
        lv_obj_set_pos(option, static_cast<int>(i) * optionWidth, 0);
        lv_obj_set_style_radius(option, 0, 0);
        lv_obj_set_style_pad_all(option, 0, 0);
        lv_obj_set_style_border_side(option, i == 0 ? LV_BORDER_SIDE_NONE : LV_BORDER_SIDE_LEFT, 0);
        lv_obj_t* optionLabel = lv_label_create(option);
        lv_label_set_text(optionLabel,
                          i < control.choices.size() ? control.choices[i].c_str() : "");
        setText(optionLabel, muted, count > 3
          ? &ardor_font_saira_cond_semibold_11
          : &ardor_font_saira_cond_medium_18);
        lv_label_set_long_mode(optionLabel, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(optionLabel, width - 10);
        lv_obj_center(optionLabel);
        auto* optionContext = context->ui->remember(*context->state, controlIndex);
        optionContext->parentIndex = i;
        lv_obj_add_event_cb(option, onDiscreteOptionSelected, LV_EVENT_CLICKED, optionContext);
        visual->options.push_back(option);
      }
    } else {
      auto* openContext = context->ui->remember(*context->state, controlIndex);
      openContext->ghost = slider;
      const bool map = usesHarmonizerMap(context, control);
      lv_obj_add_flag(slider, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(slider, map ? onHarmonizerMapOpened : onChoiceGridOpened,
                          LV_EVENT_CLICKED, openContext);
      if (map) {
        lv_obj_t* open = button(slider, "Open map");
        lv_obj_set_size(open, kTravelWidth, kDiscreteOptionsHeight);
        lv_obj_set_pos(open, kParameterSliderTextInset, kDiscreteOptionsTop);
        styleSurface(open, panelAlt);
        lv_obj_add_event_cb(open, onHarmonizerMapOpened, LV_EVENT_CLICKED, openContext);
      } else {
        lv_obj_t* previous = button(slider, "<");
        lv_obj_set_size(previous, kChoiceStepperNudgeWidth, kDiscreteOptionsHeight);
        lv_obj_set_pos(previous, kParameterSliderTextInset, kDiscreteOptionsTop);
        styleSurface(previous, panelAlt);
        auto* previousContext = context->ui->remember(*context->state, controlIndex);
        previousContext->parentIndex = 0;
        lv_obj_add_event_cb(previous, onChoiceStepperNudged, LV_EVENT_CLICKED, previousContext);

        const int openWidth = kTravelWidth - 2 * kChoiceStepperNudgeWidth - 2 * kChoiceStepperGap;
        lv_obj_t* open = button(slider, "All options");
        lv_obj_set_size(open, openWidth, kDiscreteOptionsHeight);
        lv_obj_set_pos(open, kParameterSliderTextInset + kChoiceStepperNudgeWidth + kChoiceStepperGap,
                       kDiscreteOptionsTop);
        styleSurface(open, panelAlt);
        lv_obj_add_event_cb(open, onChoiceGridOpened, LV_EVENT_CLICKED, openContext);

        lv_obj_t* next = button(slider, ">");
        lv_obj_set_size(next, kChoiceStepperNudgeWidth, kDiscreteOptionsHeight);
        lv_obj_set_pos(next, kParameterSliderTextInset + kTravelWidth - kChoiceStepperNudgeWidth,
                       kDiscreteOptionsTop);
        styleSurface(next, panelAlt);
        auto* nextContext = context->ui->remember(*context->state, controlIndex);
        nextContext->parentIndex = 1;
        lv_obj_add_event_cb(next, onChoiceStepperNudged, LV_EVENT_CLICKED, nextContext);
      }
    }
  }

  refreshParameterSliderVisual(slider, control, focused);
  return slider;
}

lv_obj_t* renderParameterMappingToolbar(lv_obj_t* parent, UiState& state,
                                        UiEventContext* context,
                                        const ParameterControl& control,
                                        std::size_t controlIndex)
{
  lv_obj_t* toolbar = lv_obj_create(parent);
  lv_obj_set_size(toolbar, kMappingToolbarWidth, kMappingToolbarHeight);
  lv_obj_set_pos(toolbar, kMappingToolbarX, kMappingToolbarY);
  lv_obj_remove_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(toolbar, LV_OBJ_FLAG_GESTURE_BUBBLE);
  styleSurface(toolbar, panelAlt);
  lv_obj_set_style_radius(toolbar, kParameterSliderRadius, 0);
  lv_obj_set_style_border_width(toolbar, 1, 0);
  lv_obj_set_style_border_color(toolbar, lv_color_hex(rule), 0);
  lv_obj_set_style_pad_all(toolbar, 0, 0);

  auto* visual = new ParameterMappingVisual{};
  lv_obj_set_user_data(toolbar, visual);
  lv_obj_add_event_cb(toolbar, freeParameterMappingVisual, LV_EVENT_DELETE, visual);

  visual->parameterLabel = label(toolbar, "", LV_ALIGN_LEFT_MID, 18, 0,
                                 &ardor_font_saira_cond_semibold_22, text);
  lv_obj_set_width(visual->parameterLabel, 600);
  lv_label_set_long_mode(visual->parameterLabel, LV_LABEL_LONG_CLIP);

  visual->valueLabel = label(toolbar, "", LV_ALIGN_LEFT_MID, 650, 0,
                             &ardor_font_saira_cond_semibold_22, text);
  lv_obj_set_width(visual->valueLabel, 180);
  lv_label_set_long_mode(visual->valueLabel, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(visual->valueLabel, LV_TEXT_ALIGN_RIGHT, 0);

  visual->expressionButton = button(toolbar, "Assign EXP");
  lv_obj_set_size(visual->expressionButton, kMappingButtonWidth, kMappingButtonHeight);
  lv_obj_set_pos(visual->expressionButton, kMappingExpButtonX, 10);
  visual->expressionContext = context->ui->remember(state, controlIndex);
  lv_obj_add_event_cb(visual->expressionButton, onExpressionAssignmentClicked,
                      LV_EVENT_CLICKED, visual->expressionContext);

  visual->midiButton = button(toolbar, "MIDI Learn");
  lv_obj_set_size(visual->midiButton, kMappingButtonWidth, kMappingButtonHeight);
  lv_obj_set_pos(visual->midiButton, kMappingMidiButtonX, 10);
  visual->midiContext = context->ui->remember(state, controlIndex);
  lv_obj_add_event_cb(visual->midiButton, onMidiLearnClicked,
                      LV_EVENT_CLICKED, visual->midiContext);

  refreshParameterMappingVisual(
    toolbar, control, controlIndex, parameterSupportsExpression(state, control),
    parameterSupportsExpression(state, control) && !selectedBlockIsLaneChild(state),
    expressionAssignedTo(state, control), parameterHasMidiBinding(state, control));
  return toolbar;
}

lv_obj_t* renderPanelCloseButton(lv_obj_t* parent, UiEventContext* context)
{
  lv_obj_t* close = button(parent, "Close");
  lv_obj_set_size(close, kPanelCloseButtonWidth, kPanelCloseButtonHeight);
  lv_obj_set_pos(close, kPanelCloseButtonX, kPanelActionTop);
  styleSurface(close, panel);
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
  styleSurface(control, panel);
  lv_obj_set_style_radius(control, 0, 0);
  lv_obj_set_style_clip_corner(control, true, 0);
  lv_obj_set_style_pad_all(control, 0, 0);

  auto* visual = new BypassControlVisual{};
  lv_obj_set_user_data(control, visual);
  lv_obj_add_event_cb(control, freeBypassControlVisual, LV_EVENT_DELETE, visual);
  lv_obj_add_event_cb(control, onBypassClicked, LV_EVENT_CLICKED, context);

  const auto addTextPair = [&](lv_obj_t* layer, int color, lv_obj_t** valueOut) {
    lv_obj_t* title = label(layer, "Bypass", LV_ALIGN_LEFT_MID, 16, 0,
                            &ardor_font_saira_cond_semibold_22, color);
    lv_obj_set_width(title, 90);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    lv_obj_t* value = label(layer, block.enabled ? "Off" : "On", LV_ALIGN_RIGHT_MID, -16, 0,
                            &ardor_font_saira_cond_semibold_22, color);
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
  styleSurface(fill, lamp);
  lv_obj_set_style_radius(fill, 0, 0);
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
  lv_obj_t* bypassMidi = button(parent, "MIDI");
  lv_obj_set_size(bypassMidi, kBypassMidiWidth, kPanelActionHeight);
  lv_obj_set_pos(bypassMidi, kBypassMidiX, kPanelActionTop);
  lv_obj_add_event_cb(bypassMidi, onBypassMidiLearnClicked, LV_EVENT_CLICKED, context);
  lv_obj_t* remove = button(parent, "Delete Block");
  lv_obj_set_size(remove, kDeleteBlockWidth, kPanelActionHeight);
  lv_obj_set_pos(remove, kDeleteBlockX, kPanelActionTop);
  styleSurface(remove, panelAlt);
  lv_obj_set_style_text_color(lv_obj_get_child(remove, 0), lv_color_hex(danger), 0);
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
                              LV_ALIGN_TOP_LEFT, 88, 25, &ardor_font_saira_cond_medium_18, muted);
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
                          lv_obj_t** titleOut, lv_obj_t** mappingToolbarOut,
                          lv_obj_t** gainMeterFillOut, lv_obj_t** gainMeterLabelOut)
{
  lv_obj_t* panelObject = lv_obj_create(root);
  lv_obj_set_size(panelObject, kParameterPanelWidth, kParameterPanelHeight);
  lv_obj_align(panelObject, LV_ALIGN_BOTTOM_MID, 0, -52);
  lv_obj_remove_flag(panelObject, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(panelObject, panelAlt);
  lv_obj_set_style_pad_all(panelObject, 0, 0);
  lv_obj_add_event_cb(panelObject, onParameterGesture, LV_EVENT_GESTURE, context);

  renderPanelCloseButton(panelObject, context);

  if (state.paramTarget == UiParamTarget::Globals) {
    lv_obj_t* title = label(panelObject, "Global", LV_ALIGN_TOP_LEFT, 270, 22, &ardor_font_saira_cond_semibold_22);
    if (titleOut) *titleOut = title;
    lv_obj_set_width(title, 660);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
  } else {
    const auto* selected = selectedUiBlock(state);
    if (!selected) return;
    const auto& block = *selected;
    const bool isCompressor = block.type == "dynamics"
      && block.params.value("mode", std::string{}) == "compressor";
    lv_obj_t* title = label(panelObject, block.label + "  /  " + block.assetName,
                            LV_ALIGN_TOP_LEFT, kParameterTitleX, 22,
                            &ardor_font_saira_cond_semibold_22);
    lv_obj_set_width(title, isCompressor ? kParameterTitleWidthWithGainMeter : kParameterTitleWidthFull);
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    if (titleOut) *titleOut = title;
    renderBlockPanelActions(panelObject, state, context, bypassOut);
    if (isCompressor) {
      renderGainMeter(panelObject, state.compressorGainReductionDb, gainMeterFillOut, gainMeterLabelOut);
    }
  }

  renderPageNavigation(panelObject, state, context);
  const auto controls = parameterPage(state, context->ui->parameterPage());
  if (!controls.empty()
      && std::none_of(controls.begin(), controls.end(), [&](const auto& control) {
           return context->ui->isParameterFocused(control.key);
         })) {
    context->ui->focusParameter(controls.front().key);
  }
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
  if (!controls.empty()) {
    const auto selected = std::find_if(controls.begin(), controls.end(), [&](const auto& control) {
      return context->ui->isParameterFocused(control.key);
    });
    const auto selectedIndex = selected == controls.end()
      ? std::size_t{0}
      : static_cast<std::size_t>(std::distance(controls.begin(), selected));
    lv_obj_t* toolbar = renderParameterMappingToolbar(
      panelObject, state, context, controls[selectedIndex], selectedIndex);
    if (mappingToolbarOut) *mappingToolbarOut = toolbar;
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

void syncMappingToolbar(lv_obj_t* toolbar, const ParameterControl& control,
                        std::size_t controlIndex, bool expressionSupported,
                        bool midiSupported, bool expressionAssigned, bool midiAssigned)
{
  refreshParameterMappingVisual(toolbar, control, controlIndex, expressionSupported,
                                midiSupported, expressionAssigned, midiAssigned);
}

void syncBypass(lv_obj_t* control, bool bypassed)
{
  refreshBypassControlVisual(control, bypassed);
}

void syncCompressorGainMeter(lv_obj_t* fill, lv_obj_t* valueLabel, float reductionDb)
{
  refreshGainMeterVisual(fill, valueLabel, reductionDb);
}

void buildPanel(lv_obj_t* root, UiState& state, UiEventContext* context,
                std::vector<lv_obj_t*>* controlsOut,
                lv_obj_t** bypassOut, lv_obj_t** titleOut,
                lv_obj_t** mappingToolbarOut,
                lv_obj_t** gainMeterFillOut, lv_obj_t** gainMeterLabelOut)
{
  renderParameterPanel(root, state, context, controlsOut, bypassOut, titleOut,
                       mappingToolbarOut, gainMeterFillOut, gainMeterLabelOut);
}

} // namespace parameter_view

} // namespace ardor
