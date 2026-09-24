#include "ui/LvglUi.h"

#include "ui/LvglUiParameterView.h"
#include "ui/LvglUiParameterWidgets.h"
#include "ui/LampBlack.h"
#include "ui/LvglUiStyle.h"

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

// Lamp Black parameter drawer (mockups/lvgl-taste/1-lamp-black.html). The
// drawer spans the screen from y = 148 under the chip strip to the rail at
// 612, with a 4 px family-colour top edge. All positions below are relative
// to the drawer's outer box unless they say otherwise.
constexpr int kDrawerY = 148;
constexpr int kDrawerHeight = lb::kRailY - kDrawerY;
constexpr int kDrawerTopEdge = 4;
constexpr int kParameterPanelWidth = kDesignWidth;
constexpr int kPanelEdgeInset = lb::kGutter;
// Header row: family tag, block name, pager, then Block / MIDI / Delete.
constexpr int kPanelActionTop = 22;
constexpr int kPanelActionHeight = lb::kButtonHeight;
constexpr int kTypeTagTop = 35;
constexpr int kTypeTagHeight = 35;
constexpr int kTypeTagPadding = 12;
constexpr int kTypeTagGap = 16;
constexpr int kDrawerNameTop = 20;
constexpr int kDrawerNameGap = 20;
constexpr int kPagerStepWidth = 60;
constexpr int kPagerLegendPad = 14;
constexpr int kDeleteBlockWidth = lb::kButtonMinWidth;
constexpr int kDeleteBlockX = kParameterPanelWidth - kPanelEdgeInset - kDeleteBlockWidth;
constexpr int kBypassMidiWidth = lb::kButtonMinWidth;
constexpr int kBypassMidiX = kDeleteBlockX - lb::kGap - kBypassMidiWidth;
// "BLOCK [ON]": legend, a 12 px gap and a 48 x 38 state badge.
constexpr int kBypassBadgeWidth = 48;
constexpr int kBypassBadgeHeight = 38;
constexpr int kBypassBadgePad = 10;
constexpr int kBypassControlWidth = 167;
constexpr int kBypassControlX = kBypassMidiX - lb::kGap - kBypassControlWidth;
// Gain-reduction meter: a compressor-only readout left of the Block button.
constexpr int kGainMeterWidth = lb::kButtonMinWidth;
constexpr int kGainMeterHeight = kPanelActionHeight;
constexpr int kGainMeterX = kBypassControlX - lb::kGap - kGainMeterWidth;
constexpr int kGainMeterBarWidth = 8;
constexpr int kGainMeterBarHeight = 36;
constexpr int kGainMeterBarX = 16;
constexpr int kGainMeterBarY = (kGainMeterHeight - 2 - kGainMeterBarHeight) / 2;
constexpr int kGainMeterLabelX = kGainMeterBarX + kGainMeterBarWidth + 12;
constexpr float kGainMeterFullScaleDb = 24.0f;
constexpr int kParameterTitleX = kPanelEdgeInset;
// Control grid: 3 x 2 cards of 403 x 166 with 12 px gaps, 96 px down.
constexpr int kParameterSliderColumns = 3;
constexpr int kParameterSliderWidth = 403;
constexpr int kParameterSliderHeight = 166;
constexpr int kParameterSliderColumnGap = lb::kGap;
constexpr int kParameterSliderRowGap = lb::kGap;
constexpr int kParameterSliderGridX = kPanelEdgeInset;
constexpr int kParameterSliderGridY = 100;
// Card interior, from the outer edge: 20 px padding inside the border.
constexpr int kCardPad = 20;
constexpr int kCardLabelX = 21;
constexpr int kCardLabelTop = 16;
constexpr int kCardValueBox = 46;
constexpr int kCardValueHeight = 50;
constexpr int kCardUnitGap = 6;
constexpr int kCardTagHeight = 24;
constexpr int kCardTagTop = 12;
constexpr int kCardEncoderRight = 14;
constexpr int kCardScopeRight = 16;
constexpr int kCardScopeTop = 14;
constexpr int kCardTagGap = 8;
constexpr int kFocusedBorder = 3;
// Travel scale: 11 ticks over an 8 px track, a family fill and a bone handle.
constexpr int kTravelBottom = 18;
constexpr int kTravelHeight = 30;
constexpr int kTravelTicks = 11;
constexpr int kTravelTickWidth = 2;
constexpr int kTravelTickHeight = 8;
constexpr int kTravelTrackTop = 14;
constexpr int kTravelTrackHeight = 8;
constexpr int kTravelHandleTop = 2;
constexpr int kTravelHandleWidth = 8;
constexpr int kTravelHandleHeight = 32;
// The travel box is widened by this much on each side so the end ticks and
// the handle, which overhang the track, are not clipped.
constexpr int kTravelOverhang = 4;
// Segmented choices: 56 px cells, 16 px above the card's foot.
constexpr int kDiscreteOptionsHeight = 56;
constexpr int kDiscreteOptionsBottom = 16;
constexpr int kSegmentMarkHeight = 5;
constexpr int kChoiceStepperNudgeWidth = 72;
constexpr int kChoicePickerX = 48;
constexpr int kChoicePickerY = 96;
constexpr int kChoicePickerWidth = 1183;
constexpr int kChoicePickerHeaderHeight = 72;
constexpr int kChoicePickerBodyInset = 24;
constexpr int kChoicePickerColumns = 5;
constexpr int kChoicePickerTileHeight = 76;
constexpr int kChoicePickerTileGap = 10;
constexpr int kChoicePickerSectionHeight = 32;
constexpr int kParameterPanelHeight = kDrawerHeight;
// Context rail: the selected control's name and value, fine steps, EXP and
// MIDI assignment, and Done. It replaces the edit rail while the drawer is
// open. The value keeps a fixed 150 px column so the buttons never move.
constexpr int kRailValueWidth = 150;
constexpr int kRailValueGap = 8;
constexpr int kRailStepWidth = 72;
constexpr int kRailNameTop = 631;
constexpr int kRailValueTop = 644;

struct ParameterSliderVisual {
  std::size_t controlIndex = 0;
  ParameterControlKind kind = ParameterControlKind::Continuous;
  lv_obj_t* keyLabel = nullptr;
  lv_obj_t* valueLabel = nullptr;
  lv_obj_t* unitLabel = nullptr;
  // ENC marks the control the encoder turns; the scope tag names scene
  // ownership. Both sit top right.
  lv_obj_t* encoderTag = nullptr;
  lv_obj_t* scopeTag = nullptr;
  // Continuous: the travel scale.
  lv_obj_t* travel = nullptr;
  lv_obj_t* fill = nullptr;
  lv_obj_t* handle = nullptr;
  std::vector<lv_obj_t*> ticks;
  // Family colour of the owning block, or bone-3 for globals.
  std::uint32_t accent = 0;
  // Discrete: segmented option row, or the stepper for long lists.
  lv_obj_t* optionRow = nullptr;
  std::vector<lv_obj_t*> options;
  // Long lists: previous, all options and next cells, or one map cell.
  std::vector<lv_obj_t*> stepper;
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
  lv_obj_t* stepDownButton = nullptr;
  lv_obj_t* doneButton = nullptr;
  lv_obj_t* stepUpButton = nullptr;
  lv_obj_t* expressionButton = nullptr;
  lv_obj_t* midiButton = nullptr;
  UiEventContext* expressionContext = nullptr;
  UiEventContext* midiContext = nullptr;
};

struct BypassControlVisual {
  lv_obj_t* badge = nullptr;
  lv_obj_t* badgeLabel = nullptr;
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

// Places a card's children for its border width: a focused card draws a
// 3 px lamp border inside the same outer box, and LVGL offsets children by
// the border, so every position subtracts it.
void layoutParameterCard(lv_obj_t* slider, ParameterSliderVisual& visual, int border)
{
  const int inner = kParameterSliderWidth - 2 * border - 2 * kCardPad;
  lv_obj_set_pos(visual.keyLabel, kCardLabelX - border,
                 lb::textTop(lb::type::controlLabel, kCardLabelTop) - border);
  int tagRight = kParameterSliderWidth - 2 * border;
  if (visual.encoderTag) {
    const int width = lv_obj_get_style_width(visual.encoderTag, LV_PART_MAIN);
    tagRight -= kCardEncoderRight + width;
    lv_obj_set_pos(visual.encoderTag, tagRight, kCardTagTop);
    tagRight -= kCardTagGap;
  }
  if (visual.scopeTag) {
    const int width = lv_obj_get_style_width(visual.scopeTag, LV_PART_MAIN);
    const bool encoderShown = visual.encoderTag
      && !lv_obj_has_flag(visual.encoderTag, LV_OBJ_FLAG_HIDDEN);
    const int right = encoderShown ? tagRight : kParameterSliderWidth - 2 * border - kCardScopeRight;
    lv_obj_set_pos(visual.scopeTag, right - width, encoderShown ? kCardTagTop : kCardScopeTop);
  }
  if (visual.valueLabel) {
    // The value box sits 20 px in and 46 px down inside the border, so it
    // moves with the border as CSS absolute positioning does.
    const int valueX = kCardPad;
    const int valueTop = lb::centeredTextTop(lb::type::controlValue, kCardValueBox,
                                             kCardValueHeight);
    lv_obj_set_pos(visual.valueLabel, valueX, valueTop);
    if (visual.unitLabel && visual.valueLabel) {
      // The unit shares the numeral's baseline.
      const int baseline = valueTop + lv_font_get_line_height(lb::type::controlValue.font)
        - lb::type::controlValue.font->base_line;
      const int unitTop = baseline - (lv_font_get_line_height(lb::type::controlUnit.font)
        - lb::type::controlUnit.font->base_line);
      lv_obj_set_pos(visual.unitLabel,
                     valueX + lb::textWidth(lb::type::controlValue,
                                            lv_label_get_text(visual.valueLabel)) + kCardUnitGap,
                     unitTop);
    }
  }
  if (visual.travel) {
    lv_obj_set_pos(visual.travel, kCardPad - kTravelOverhang,
                   kParameterSliderHeight - 2 * border - kTravelBottom - kTravelHeight);
    lv_obj_set_width(visual.travel, inner + 2 * kTravelOverhang);
    for (std::size_t i = 0; i < visual.ticks.size(); ++i) {
      lv_obj_set_x(visual.ticks[i], kTravelOverhang + static_cast<int>(std::lround(
        inner * static_cast<double>(i) / (kTravelTicks - 1))));
    }
    lv_obj_t* track = lv_obj_get_child(visual.travel, static_cast<int32_t>(visual.ticks.size()));
    lv_obj_set_x(track, kTravelOverhang);
    lv_obj_set_width(track, inner);
    lv_obj_set_x(visual.fill, kTravelOverhang);
  }
  if (visual.optionRow) {
    lv_obj_set_pos(visual.optionRow, kCardPad - 1,
                   kParameterSliderHeight - 2 * border - kDiscreteOptionsBottom
                     - kDiscreteOptionsHeight);
    lv_obj_set_width(visual.optionRow, inner + 1);
    if (visual.stepper.size() == 3) {
      const int middle = inner + 1 - 2 * kChoiceStepperNudgeWidth + 2;
      lv_obj_set_pos(visual.stepper[0], 0, 0);
      lv_obj_set_width(visual.stepper[0], kChoiceStepperNudgeWidth);
      lv_obj_set_pos(visual.stepper[1], kChoiceStepperNudgeWidth - 1, 0);
      lv_obj_set_width(visual.stepper[1], middle);
      lv_obj_set_pos(visual.stepper[2], inner + 1 - kChoiceStepperNudgeWidth, 0);
      lv_obj_set_width(visual.stepper[2], kChoiceStepperNudgeWidth);
    } else if (visual.stepper.size() == 1) {
      lv_obj_set_pos(visual.stepper[0], 0, 0);
      lv_obj_set_width(visual.stepper[0], inner + 1);
    }
    for (lv_obj_t* cell : visual.stepper) {
      lv_obj_set_style_text_color(lb::buttonLabel(cell), lv_color_hex(text), 0);
      lb::setButtonText(cell, lv_label_get_text(lb::buttonLabel(cell)));
    }
    // CSS: flex cells with a -1 px left margin, so neighbours share a rule.
    const int count = static_cast<int>(visual.options.size());
    const double cell = static_cast<double>(inner + count) / std::max(1, count);
    lv_obj_set_width(visual.optionRow, inner + 1);
    for (int i = 0; i < count; ++i) {
      const int left = static_cast<int>(std::lround(i * (cell - 1.0)));
      const int right = static_cast<int>(std::lround(i * (cell - 1.0) + cell));
      lv_obj_t* option = visual.options[static_cast<std::size_t>(i)];
      lv_obj_set_pos(option, left, 0);
      lv_obj_set_width(option, right - left);
      lb::fitButtonText(option, lv_label_get_text(lb::buttonLabel(option)), lb::type::segment,
                        lb::type::segmentSmall, right - left - 12);
    }
  }
  (void) slider;
}

void refreshParameterSliderVisual(lv_obj_t* slider, const ParameterControl& control,
                                  bool focused = true)
{
  auto* visual = static_cast<ParameterSliderVisual*>(lv_obj_get_user_data(slider));
  if (!visual) {
    return;
  }
  const int border = focused ? kFocusedBorder : 1;
  lv_obj_set_style_border_width(slider, border, 0);
  lv_obj_set_style_border_color(slider, lv_color_hex(focused ? lamp : rule), 0);
  if (visual->keyLabel) {
    lv_label_set_text(visual->keyLabel, uppercase(control.label).c_str());
    lv_obj_set_style_text_color(visual->keyLabel, lv_color_hex(focused ? lamp : muted), 0);
  }
  if (visual->encoderTag) {
    if (focused) lv_obj_remove_flag(visual->encoderTag, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(visual->encoderTag, LV_OBJ_FLAG_HIDDEN);
  }
  const auto [valueText, unitText] = splitFormattedValue(control.formatted);
  if (visual->valueLabel) {
    lv_label_set_text(visual->valueLabel, valueText.c_str());
  }
  if (visual->unitLabel) {
    lv_label_set_text(visual->unitLabel, unitText.c_str());
  }

  if (visual->fill && visual->handle && visual->travel) {
    const float range = control.maximum - control.minimum;
    const float ratio = range == 0.0f ? 0.0f
      : std::clamp((control.value - control.minimum) / range, 0.0f, 1.0f);
    const int inner = kParameterSliderWidth - 2 * border - 2 * kCardPad;
    const int position = static_cast<int32_t>(std::lround(ratio * inner));
    lv_obj_set_width(visual->fill, position);
    lv_obj_set_x(visual->handle, kTravelOverhang + position - kTravelHandleWidth / 2);
    // Design law 3: the lamp colour marks the selected control's frame and
    // legend; the travel keeps the block's family colour.
    lv_obj_set_style_bg_color(visual->fill, lv_color_hex(visual->accent), 0);
  }

  const auto selectedIndex = static_cast<int>(std::lround(control.value));
  for (std::size_t i = 0; i < visual->options.size(); ++i) {
    lv_obj_t* option = visual->options[i];
    if (!option) continue;
    const bool on = static_cast<int>(i) == selectedIndex;
    lv_obj_set_style_bg_color(option, lv_color_hex(on ? plateHi : panel), 0);
    lv_obj_set_style_text_color(lb::buttonLabel(option), lv_color_hex(on ? text : disabled), 0);
    if (lv_obj_t* mark = lv_obj_get_child(option, 1)) {
      if (on) lv_obj_remove_flag(mark, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
    }
  }
  layoutParameterCard(slider, *visual, border);
}

void styleMappingButton(lv_obj_t* control, bool supported, bool assigned)
{
  if (supported) {
    lv_obj_remove_state(control, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(control, LV_STATE_DISABLED);
  }
  // An assignment reads as a bone frame; lamp stays on the selected control.
  lb::styleButton(control, supported ? lb::ButtonKind::Normal : lb::ButtonKind::Off);
  lv_obj_set_style_border_color(control, lv_color_hex(assigned ? text : rule), 0);
}

void refreshParameterMappingVisual(lv_obj_t* toolbar, const ParameterControl& control,
                                   std::size_t controlIndex, bool expressionSupported,
                                   bool midiSupported, bool expressionAssigned,
                                   bool midiAssigned)
{
  auto* visual = static_cast<ParameterMappingVisual*>(lv_obj_get_user_data(toolbar));
  if (!visual) return;

  lv_label_set_text(visual->parameterLabel, uppercase(control.label).c_str());
  // A long choice name drops to the smaller value face on the same baseline.
  const bool fits = lb::textWidth(lb::type::contextValue, control.formatted) <= kRailValueWidth;
  const lb::Type& valueType = fits ? lb::type::contextValue : lb::type::contextValueSmall;
  lb::applyType(visual->valueLabel, valueType, text);
  const int valueBaseline = kRailValueTop - lb::kRailY - 1
    + lb::type::contextValue.size * lb::kSairaAscent / 1000;
  lv_obj_set_y(visual->valueLabel, valueBaseline
    - (lv_font_get_line_height(valueType.font) - valueType.font->base_line));
  lv_label_set_long_mode(visual->valueLabel, LV_LABEL_LONG_MODE_DOTS);
  lv_label_set_text(visual->valueLabel, control.formatted.c_str());
  // Fine steps only make sense on a continuous travel; choices keep their
  // own segmented buttons.
  const bool steppable = control.kind == ParameterControlKind::Continuous;
  for (lv_obj_t* step : {visual->stepDownButton, visual->stepUpButton}) {
    if (!step) continue;
    if (steppable) lv_obj_remove_state(step, LV_STATE_DISABLED);
    else lv_obj_add_state(step, LV_STATE_DISABLED);
    lb::styleButton(step, steppable ? lb::ButtonKind::Normal : lb::ButtonKind::Off);
  }
  lb::setButtonText(visual->expressionButton, expressionAssigned ? "EXP Assigned" : "Assign EXP");
  lb::setButtonText(visual->midiButton, midiAssigned ? "MIDI Mapped" : "MIDI Learn");
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

void stepFocusedParameter(lv_event_t* event, int delta)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->applyFocusedParameterDelta(*context->state, delta)) {
    context->ui->invalidate(UiChange::Parameters | UiChange::Header);
  }
}

void onParameterStepDown(lv_event_t* event) { stepFocusedParameter(event, -1); }
void onParameterStepUp(lv_event_t* event) { stepFocusedParameter(event, 1); }

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
  lv_obj_t* pill = lb::box(parent, kGainMeterX, kPanelActionTop, kGainMeterWidth,
                           kGainMeterHeight, bg, rule, 1);
  lb::box(pill, kGainMeterBarX, kGainMeterBarY, kGainMeterBarWidth, kGainMeterBarHeight, plateHi);
  // Anchored to the track's top so growing height reads as "filling down
  // from 0 dB", matching refreshGainMeterVisual's fill-height math.
  lv_obj_t* fill = lb::box(pill, kGainMeterBarX, kGainMeterBarY, kGainMeterBarWidth, 0, text);
  lb::textLabel(pill, lb::type::chipSmall, "GR", disabled, kGainMeterLabelX, 6);
  lv_obj_t* valueLabel = lb::textLabel(pill, lb::type::page, "0.0 dB", text, kGainMeterLabelX, 22);
  lv_obj_set_width(valueLabel, kGainMeterWidth - kGainMeterLabelX - 8);
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
  // ON prints ground on bone; OFF drops to a ruled, recessed badge.
  if (visual->badge) {
    lv_obj_set_style_bg_color(visual->badge, lv_color_hex(bypassed ? bg : text), 0);
    lb::setBorder(visual->badge, bypassed ? disabled : text, bypassed ? 1 : 0);
  }
  if (visual->badgeLabel) {
    lv_label_set_text(visual->badgeLabel, bypassed ? "OFF" : "ON");
    lv_obj_set_style_text_color(visual->badgeLabel, lv_color_hex(bypassed ? muted : bg), 0);
    const int width = lb::textWidth(lb::type::buttonState, bypassed ? "OFF" : "ON");
    const int border = bypassed ? 1 : 0;
    lv_obj_set_pos(visual->badgeLabel, (kBypassBadgeWidth - width) / 2 - border,
                   lb::centeredTextTop(lb::type::buttonState, 0, kBypassBadgeHeight) - border);
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
  const auto sceneEnabled = selectedParameterSceneValue(*context->state, "blockEnabled");
  const bool currentlyEnabled = sceneEnabled && sceneEnabled->is_boolean()
    ? sceneEnabled->get<bool>() : selected->enabled;
  const bool enabled = !currentlyEnabled;
  const bool wdwRequired = context->state->bank.presets[context->state->activePreset].routing == "wdw"
    && selectedBlockIsLaneChild(*context->state)
    && selected->type == "nam";
  if (wdwRequired && !enabled) {
    setUiStatus(*context->state, "Each WDW lane needs its required " + selected->label, true);
    redraw(context);
    return;
  }
  bool updatedLive = false;
  if (const auto targetIndex = selectedParameterSceneTargetIndex(
        *context->state, "blockEnabled")) {
    updatedLive = context->ui->actions().updateSceneTarget
      && context->ui->actions().updateSceneTarget(*targetIndex, enabled ? 1.0f : 0.0f);
  } else {
    updatedLive = context->ui->actions().updateBlockEnabled
      && context->ui->actions().updateBlockEnabled(selected->id, enabled);
  }
  if (updatedLive) setSelectedBlockEnabledLive(*context->state, enabled);
  else setSelectedBlockEnabled(*context->state, enabled);
  selected = selectedUiBlock(*context->state);
  if (selected) {
    const auto displayed = selectedParameterSceneValue(*context->state, "blockEnabled");
    const bool displayedEnabled = displayed && displayed->is_boolean()
      ? displayed->get<bool>() : selected->enabled;
    refreshBypassControlVisual(control, !displayedEnabled);
  }
  redraw(context);
}

void onBypassMidiLearnClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  beginMidiLearnForBlockEnabled(*context->state);
  context->ui->invalidate(UiChange::Parameters | UiChange::Status);
  redraw(context);
}

void onBypassSceneScopeClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto scope = selectedParameterSceneScope(*context->state, "blockEnabled");
  if (scope == UiSceneScope::Unavailable) return;
  setSelectedParameterSceneScope(*context->state, "blockEnabled",
    scope == UiSceneScope::ThisScene ? UiSceneScope::Shared : UiSceneScope::ThisScene);
  context->ui->invalidate(UiChange::Presets | UiChange::Parameters);
}

void onSceneScopeClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto controls = parameterPage(*context->state, context->ui->parameterPage());
  if (context->index >= controls.size()) return;
  const auto& control = controls[context->index];
  if (control.sceneScope == UiSceneScope::Unavailable) return;
  setSelectedParameterSceneScope(*context->state, control.key,
    control.sceneScope == UiSceneScope::ThisScene ? UiSceneScope::Shared
                                                  : UiSceneScope::ThisScene);
  context->ui->invalidate(UiChange::Presets | UiChange::Parameters);
}


// CSS divides the 1232 px grid into three 402.67 px columns, so columns land
// on 24, 439 and 853 while every card still renders 403 px wide.
int parameterColumnX(int column)
{
  const double pitch = (kDesignWidth - 2 * kPanelEdgeInset
                        - (kParameterSliderColumns - 1) * kParameterSliderColumnGap)
    / static_cast<double>(kParameterSliderColumns) + kParameterSliderColumnGap;
  return kParameterSliderGridX + static_cast<int>(std::lround(column * pitch - 0.001));
}

// A 1 px bordered tag with small bold capitals, used for ENC and scope.
lv_obj_t* cardTag(lv_obj_t* parent, const lb::Type& type, const std::string& legend,
                  std::uint32_t fill, std::uint32_t ink, std::uint32_t border)
{
  const int width = lb::textWidth(type, legend) + 2 * (border ? 9 : 8);
  lv_obj_t* tag = lb::box(parent, 0, 0, width, kCardTagHeight, fill, border, border ? 1 : 0);
  if (!border) lv_obj_set_style_bg_color(tag, lv_color_hex(fill), 0);
  if (fill == bg && border) lv_obj_set_style_bg_opa(tag, LV_OPA_TRANSP, 0);
  lb::centeredText(tag, type, legend, ink, border ? -1 : 0, border ? -1 : 0, width,
                   kCardTagHeight);
  return tag;
}

// One segment cell: a ruled plate with its legend and a 5 px family mark
// along the foot of the selected cell.
lv_obj_t* segmentCell(lv_obj_t* row, const std::string& legend, std::uint32_t accent)
{
  lv_obj_t* cell = lb::button(row, legend, lb::ButtonKind::Normal, 0, 0, 91,
                              kDiscreteOptionsHeight, lb::type::segment);
  lv_label_set_long_mode(lb::buttonLabel(cell), LV_LABEL_LONG_CLIP);
  lv_obj_t* mark = lb::box(cell, 0, kDiscreteOptionsHeight - 2 - kSegmentMarkHeight, LV_PCT(100),
                           kSegmentMarkHeight, accent);
  lv_obj_add_flag(mark, LV_OBJ_FLAG_HIDDEN);
  return cell;
}

lv_obj_t* createParameterSlider(lv_obj_t* parent, const ParameterControl& control, int x, int y,
                                bool focused, UiEventContext* context,
                                lv_event_cb_t onPressed, lv_event_cb_t onPressing,
                                std::size_t controlIndex)
{
  lv_obj_t* slider = lv_obj_create(parent);
  lv_obj_remove_style_all(slider);
  lv_obj_set_size(slider, kParameterSliderWidth, kParameterSliderHeight);
  lv_obj_set_pos(slider, x, y);
  lv_obj_remove_flag(slider, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(slider, lv_color_hex(bg), 0);
  lb::setBorder(slider, rule, 1);

  auto* visual = new ParameterSliderVisual{};
  visual->controlIndex = controlIndex;
  visual->kind = control.kind;
  const auto* owner = context->state->paramTarget == UiParamTarget::Block
    ? selectedUiBlock(*context->state) : nullptr;
  visual->accent = owner ? static_cast<std::uint32_t>(categoryColor(owner->type)) : text;
  lv_obj_set_user_data(slider, visual);
  lv_obj_add_event_cb(slider, freeParameterSliderVisual, LV_EVENT_DELETE, visual);

  visual->keyLabel = lb::textLabel(slider, lb::type::controlLabel, uppercase(control.label),
                                   muted, 0, 0);
  lv_obj_set_width(visual->keyLabel, kParameterSliderWidth - 2 * kCardPad - 110);
  lv_label_set_long_mode(visual->keyLabel, LV_LABEL_LONG_CLIP);
  visual->encoderTag = cardTag(slider, lb::type::encoderTag, "ENC", lamp, lampInk, 0);

  const bool scenesEnabled = context->state->bank.presets[context->state->activePreset]
    .sceneSet.has_value();
  if (scenesEnabled) {
    const bool perScene = control.sceneScope == UiSceneScope::ThisScene;
    lv_obj_t* scope = cardTag(slider, lb::type::controlTag, perScene ? "THIS SCENE" : "SHARED",
                              bg, perScene ? text : muted, disabled);
    lv_obj_add_flag(scope, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(scope, 12);
    lv_obj_set_style_opa(scope, LV_OPA_40, LV_STATE_DISABLED);
    if (control.sceneScope == UiSceneScope::Unavailable
        || !previewIsSynchronized(*context->state)) lv_obj_add_state(scope, LV_STATE_DISABLED);
    lv_obj_add_event_cb(scope, onSceneScopeClicked, LV_EVENT_CLICKED,
                        context->ui->remember(*context->state, controlIndex));
    visual->scopeTag = scope;
  }

  const bool continuous = control.kind == ParameterControlKind::Continuous;
  if (continuous) {
    visual->valueLabel = lb::textLabel(slider, lb::type::controlValue, "", text, 0, 0);
    lv_obj_set_style_max_width(visual->valueLabel, kParameterSliderWidth - 2 * kCardPad - 60, 0);
    lv_label_set_long_mode(visual->valueLabel, LV_LABEL_LONG_CLIP);
    visual->unitLabel = lb::textLabel(slider, lb::type::controlUnit, "", muted, 0, 0);

    lv_obj_add_flag(slider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(slider, onPressed, LV_EVENT_PRESSED, context);
    lv_obj_add_event_cb(slider, onPressing, LV_EVENT_PRESSING, context);
    lv_obj_add_event_cb(slider, onParameterControlReleased, LV_EVENT_RELEASED, context);
    lv_obj_add_event_cb(slider, onParameterControlReleased, LV_EVENT_PRESS_LOST, context);

    const int inner = kParameterSliderWidth - 2 - 2 * kCardPad;
    lv_obj_t* travel = lv_obj_create(slider);
    lv_obj_remove_style_all(travel);
    lv_obj_set_size(travel, inner, kTravelHeight);
    lv_obj_remove_flag(travel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(travel, LV_OBJ_FLAG_CLICKABLE);
    visual->travel = travel;
    for (int tick = 0; tick < kTravelTicks; ++tick) {
      visual->ticks.push_back(lb::box(travel, 0, 0, kTravelTickWidth, kTravelTickHeight, disabled));
    }
    lb::box(travel, 0, kTravelTrackTop, inner, kTravelTrackHeight, plateHi);
    visual->fill = lb::box(travel, 0, kTravelTrackTop, 0, kTravelTrackHeight, visual->accent);
    visual->handle = lb::box(travel, 0, kTravelHandleTop, kTravelHandleWidth,
                             kTravelHandleHeight, text);
  } else {
    const auto count = std::max<std::size_t>(1, control.choices.size());
    const bool directRow = count <= 4
      || (selectedBlockIsHarmonizer(context) && control.key == "p1");
    lv_obj_t* row = lv_obj_create(slider);
    lv_obj_remove_style_all(row);
    lv_obj_set_height(row, kDiscreteOptionsHeight);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    visual->optionRow = row;
    if (directRow) {
      for (std::size_t i = 0; i < count; ++i) {
        lv_obj_t* option = segmentCell(
          row, i < control.choices.size() ? uppercase(control.choices[i]) : "", visual->accent);
        auto* optionContext = context->ui->remember(*context->state, controlIndex);
        optionContext->parentIndex = i;
        lv_obj_add_event_cb(option, onDiscreteOptionSelected, LV_EVENT_CLICKED, optionContext);
        visual->options.push_back(option);
      }
    } else {
      // Long lists: step through the choices, or open the full picker.
      // The current choice is the value; the centre cell opens the list.
      visual->valueLabel = lb::textLabel(slider, lb::type::controlValue, "", text, 0, 0);
      lv_obj_set_style_max_width(visual->valueLabel, kParameterSliderWidth - 2 * kCardPad, 0);
      lv_label_set_long_mode(visual->valueLabel, LV_LABEL_LONG_MODE_DOTS);
      auto* openContext = context->ui->remember(*context->state, controlIndex);
      openContext->ghost = slider;
      const bool map = usesHarmonizerMap(context, control);
      lv_obj_add_flag(slider, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(slider, map ? onHarmonizerMapOpened : onChoiceGridOpened,
                          LV_EVENT_CLICKED, openContext);
      if (map) {
        lv_obj_t* open = segmentCell(row, "OPEN MAP", visual->accent);
        lv_obj_add_event_cb(open, onHarmonizerMapOpened, LV_EVENT_CLICKED, openContext);
        visual->stepper.push_back(open);
      } else {
        lv_obj_t* previous = segmentCell(row, "<", visual->accent);
        auto* previousContext = context->ui->remember(*context->state, controlIndex);
        previousContext->parentIndex = 0;
        lv_obj_add_event_cb(previous, onChoiceStepperNudged, LV_EVENT_CLICKED, previousContext);
        lv_obj_t* open = segmentCell(row, "ALL OPTIONS", visual->accent);
        lv_obj_add_event_cb(open, onChoiceGridOpened, LV_EVENT_CLICKED, openContext);
        lv_obj_t* next = segmentCell(row, ">", visual->accent);
        auto* nextContext = context->ui->remember(*context->state, controlIndex);
        nextContext->parentIndex = 1;
        lv_obj_add_event_cb(next, onChoiceStepperNudged, LV_EVENT_CLICKED, nextContext);
        visual->stepper = {previous, open, next};
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
  // The context rail covers the edit rail: same 108 px band, same rule.
  lv_obj_t* toolbar = lb::rail(parent);
  lv_obj_add_flag(toolbar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(toolbar, LV_OBJ_FLAG_GESTURE_BUBBLE);

  auto* visual = new ParameterMappingVisual{};
  lv_obj_set_user_data(toolbar, visual);
  lv_obj_add_event_cb(toolbar, freeParameterMappingVisual, LV_EVENT_DELETE, visual);

  // Rail children sit below its 1 px top rule; screen positions lose a pixel.
  const int top = lb::kRailY + 1;
  visual->parameterLabel = lb::textLabel(toolbar, lb::type::contextName, "", muted, lb::kGutter,
                                         kRailNameTop - top);
  lv_obj_set_width(visual->parameterLabel, kRailValueWidth + kRailValueGap);
  lv_label_set_long_mode(visual->parameterLabel, LV_LABEL_LONG_CLIP);
  visual->valueLabel = lb::textLabel(toolbar, lb::type::contextValue, "", text, lb::kGutter,
                                     kRailValueTop - top);
  lv_obj_set_width(visual->valueLabel, kRailValueWidth);
  lv_label_set_long_mode(visual->valueLabel, LV_LABEL_LONG_CLIP);

  int x = lb::kGutter + kRailValueWidth + kRailValueGap + lb::kGap;
  const int buttonY = lb::kRailButtonY - top;
  auto* stepContext = context->ui->remember(state, controlIndex);
  visual->stepDownButton = lb::button(toolbar, "-", lb::ButtonKind::Normal, x, buttonY,
                                      kRailStepWidth, lb::kButtonHeight, lb::type::stepButton);
  lv_obj_add_event_cb(visual->stepDownButton, onParameterStepDown, LV_EVENT_CLICKED, stepContext);
  x += kRailStepWidth + lb::kGap;
  visual->stepUpButton = lb::button(toolbar, "+", lb::ButtonKind::Normal, x, buttonY,
                                    kRailStepWidth, lb::kButtonHeight, lb::type::stepButton);
  lv_obj_add_event_cb(visual->stepUpButton, onParameterStepUp, LV_EVENT_CLICKED, stepContext);
  x += kRailStepWidth + lb::kGap;

  // Both mapping buttons keep the width of their longer legend, so a
  // relabel never moves its neighbour.
  const int expWidth = lb::buttonWidth("EXP Assigned");
  visual->expressionButton = lb::button(toolbar, "Assign EXP", lb::ButtonKind::Normal, x, buttonY,
                                        expWidth);
  visual->expressionContext = context->ui->remember(state, controlIndex);
  lv_obj_add_event_cb(visual->expressionButton, onExpressionAssignmentClicked,
                      LV_EVENT_CLICKED, visual->expressionContext);
  x += expWidth + lb::kGap;
  const int midiWidth = lb::buttonWidth("MIDI Mapped");
  visual->midiButton = lb::button(toolbar, "MIDI Learn", lb::ButtonKind::Normal, x, buttonY,
                                  midiWidth);
  visual->midiContext = context->ui->remember(state, controlIndex);
  lv_obj_add_event_cb(visual->midiButton, onMidiLearnClicked,
                      LV_EVENT_CLICKED, visual->midiContext);

  visual->doneButton = lb::button(toolbar, "DONE", lb::ButtonKind::Primary,
                                  kDesignWidth - lb::kGutter - lb::kButtonMinWidth, buttonY);
  // Close on touch-down: a finger can move slightly before release, which
  // would otherwise cancel LV_EVENT_CLICKED.
  lv_obj_add_event_cb(visual->doneButton, onCloseParamDrawer, LV_EVENT_PRESSED, context);

  refreshParameterMappingVisual(
    toolbar, control, controlIndex, parameterSupportsExpression(state, control),
    parameterSupportsExpression(state, control) && !selectedBlockIsLaneChild(state),
    expressionAssignedTo(state, control), parameterHasMidiBinding(state, control));
  return toolbar;
}

// The context rail's Done closes the drawer; this keeps a Done-only rail
// for views without a mapping target (EQ, empty pages).
lv_obj_t* renderPanelCloseButton(lv_obj_t* parent, UiEventContext* context)
{
  lv_obj_t* railBand = lb::rail(parent);
  lv_obj_add_flag(railBand, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* close = lb::button(railBand, "DONE", lb::ButtonKind::Primary,
                               kDesignWidth - lb::kGutter - lb::kButtonMinWidth,
                               lb::kRailButtonY - lb::kRailY - 1);
  lv_obj_add_event_cb(close, onCloseParamDrawer, LV_EVENT_PRESSED, context);
  return close;
}

void renderBypassControl(lv_obj_t* parent, UiState& state, UiEventContext* context,
                         lv_obj_t** controlOut = nullptr)
{
  const auto* selected = selectedUiBlock(state);
  if (!selected) return;
  const auto& block = *selected;
  const auto sceneEnabled = selectedParameterSceneValue(state, "blockEnabled");
  const bool displayedEnabled = sceneEnabled && sceneEnabled->is_boolean()
    ? sceneEnabled->get<bool>() : block.enabled;
  lv_obj_t* control = lb::button(parent, "", lb::ButtonKind::Normal, kBypassControlX,
                                 kPanelActionTop, kBypassControlWidth);
  lv_obj_t* legend = lb::buttonLabel(control);
  lv_label_set_text(legend, "BLOCK");
  const int legendWidth = lb::textWidth(lb::type::button, "BLOCK");
  const int contentWidth = legendWidth + lb::kGap + kBypassBadgeWidth;
  const int legendX = (kBypassControlWidth - contentWidth) / 2;
  lv_obj_set_x(legend, legendX - 1);

  auto* visual = new BypassControlVisual{};
  visual->badge = lb::box(control, legendX + legendWidth + lb::kGap - 1,
                          (kPanelActionHeight - kBypassBadgeHeight) / 2 - 1,
                          kBypassBadgeWidth, kBypassBadgeHeight, text);
  visual->badgeLabel = lb::textLabel(visual->badge, lb::type::buttonState, "ON", bg, 0, 0);
  lv_obj_set_user_data(control, visual);
  lv_obj_add_event_cb(control, freeBypassControlVisual, LV_EVENT_DELETE, visual);
  lv_obj_add_event_cb(control, onBypassClicked, LV_EVENT_CLICKED, context);

  if (state.bank.presets[state.activePreset].sceneSet) {
    const auto scope = selectedParameterSceneScope(state, "blockEnabled");
    const bool perScene = scope == UiSceneScope::ThisScene;
    const std::string scopeText = perScene ? "THIS SCENE" : "SHARED";
    lv_obj_t* scopeButton = lb::button(parent, scopeText, lb::ButtonKind::Normal, 0,
                                       kPanelActionTop, 0, kPanelActionHeight,
                                       lb::type::page);
    lv_obj_set_x(scopeButton, kBypassControlX - lb::kGap
                 - lv_obj_get_style_width(scopeButton, LV_PART_MAIN));
    lv_obj_set_style_text_color(lb::buttonLabel(scopeButton),
                                lv_color_hex(perScene ? text : muted), 0);
    if (scope == UiSceneScope::Unavailable || !previewIsSynchronized(state))
      lv_obj_add_state(scopeButton, LV_STATE_DISABLED);
    lv_obj_add_event_cb(scopeButton, onBypassSceneScopeClicked, LV_EVENT_CLICKED,
                        context->ui->remember(state));
  }

  refreshBypassControlVisual(control, !displayedEnabled);
  if (controlOut) *controlOut = control;
}

void renderBlockPanelActions(lv_obj_t* parent, UiState& state, UiEventContext* context,
                             lv_obj_t** bypassOut = nullptr)
{
  renderBypassControl(parent, state, context, bypassOut);
  lv_obj_t* bypassMidi = lb::button(parent, "MIDI", lb::ButtonKind::Normal, kBypassMidiX,
                                    kPanelActionTop, kBypassMidiWidth);
  lv_obj_add_event_cb(bypassMidi, onBypassMidiLearnClicked, LV_EVENT_CLICKED, context);
  lv_obj_t* remove = lb::button(parent, "Delete", lb::ButtonKind::Danger, kDeleteBlockX,
                                kPanelActionTop, kDeleteBlockWidth);
  lv_obj_add_event_cb(remove, onDeleteSelectedBlock, LV_EVENT_CLICKED, context);
}

// "< PAGE 1 / 2 >": two 60 px steps around a recessed legend. A single
// page keeps the legend so the header does not shift between blocks.
void renderPageNavigation(lv_obj_t* parent, UiState& state, UiEventContext* context, int x)
{
  const auto count = std::max<std::size_t>(1, parameterPageCount(state));
  const auto page = std::min(context->ui->parameterPage(), count - 1);
  lv_obj_t* previous = lb::button(parent, "<", page == 0 ? lb::ButtonKind::Off
                                                        : lb::ButtonKind::Normal,
                                  x, kPanelActionTop, kPagerStepWidth);
  if (page == 0) lv_obj_add_state(previous, LV_STATE_DISABLED);
  lv_obj_set_style_opa(previous, LV_OPA_COVER, LV_STATE_DISABLED);
  lv_obj_add_event_cb(previous, onPreviousParameterPage, LV_EVENT_CLICKED, context);
  const std::string legend = "PAGE " + std::to_string(page + 1) + " / " + std::to_string(count);
  const int legendWidth = lb::textWidth(lb::type::page, legend) + 2 * kPagerLegendPad;
  lv_obj_t* legendBox = lb::box(parent, x + kPagerStepWidth, kPanelActionTop, legendWidth,
                                kPanelActionHeight, bg);
  lb::setBorder(legendBox, rule, 1,
                static_cast<lv_border_side_t>(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM));
  lb::centeredText(legendBox, lb::type::page, legend, muted, 0, -1, legendWidth,
                   kPanelActionHeight);
  const bool last = page + 1 >= count;
  lv_obj_t* next = lb::button(parent, ">", last ? lb::ButtonKind::Off : lb::ButtonKind::Normal,
                              x + kPagerStepWidth + legendWidth, kPanelActionTop, kPagerStepWidth);
  if (last) lv_obj_add_state(next, LV_STATE_DISABLED);
  lv_obj_set_style_opa(next, LV_OPA_COVER, LV_STATE_DISABLED);
  lv_obj_add_event_cb(next, onNextParameterPage, LV_EVENT_CLICKED, context);
}

void renderParameterPanel(lv_obj_t* root, UiState& state, UiEventContext* context,
                          std::vector<lv_obj_t*>* controlsOut, lv_obj_t** bypassOut,
                          lv_obj_t** titleOut, lv_obj_t** mappingToolbarOut,
                          lv_obj_t** gainMeterFillOut, lv_obj_t** gainMeterLabelOut)
{
  // The chip strip band above the drawer is ground; the chip strip itself is
  // a shared object drawn over it.
  lb::box(root, 0, lb::kHeaderHeight, kDesignWidth, kDrawerY - lb::kHeaderHeight, bg);
  lv_obj_t* panelObject = lb::box(root, 0, kDrawerY, kParameterPanelWidth, kParameterPanelHeight,
                                  panel);
  lv_obj_add_flag(panelObject, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(panelObject, onParameterGesture, LV_EVENT_GESTURE, context);

  const auto* selected = state.paramTarget == UiParamTarget::Block ? selectedUiBlock(state) : nullptr;
  const auto family = selected ? static_cast<std::uint32_t>(categoryColor(selected->type))
                               : disabled;
  lb::box(panelObject, 0, 0, kParameterPanelWidth, kDrawerTopEdge, family);

  int x = kParameterTitleX;
  if (state.paramTarget == UiParamTarget::Globals) {
    lv_obj_t* title = lb::textLabel(panelObject, lb::type::drawerName, "GLOBAL", text, x,
                                    kDrawerNameTop);
    if (titleOut) *titleOut = title;
    x += lb::textWidth(lb::type::drawerName, "GLOBAL") + kDrawerNameGap;
  } else {
    if (!selected) return;
    const auto& block = *selected;
    const bool isCompressor = block.type == "dynamics"
      && block.params.value("mode", std::string{}) == "compressor";
    // Views are cached per block type and mode, so the family edge and type
    // tag stay valid for every block this view shows; only the name syncs.
    const std::string tagText = uppercase(block.label);
    const int tagWidth = lb::textWidth(lb::type::category, tagText) + 2 * kTypeTagPadding;
    lv_obj_t* tag = lb::box(panelObject, x, kTypeTagTop, tagWidth, kTypeTagHeight, family);
    lb::textLabel(tag, lb::type::category, tagText, bg, kTypeTagPadding, 4);
    x += tagWidth + kTypeTagGap;
    // The pager follows the name; the name's width is fixed per view so a
    // long name clips instead of pushing the pager into the actions.
    const int titleRight = (isCompressor ? kGainMeterX : kBypassControlX) - lb::kGap
      - (2 * kPagerStepWidth + 106) - kDrawerNameGap;
    const std::string name = uppercase(block.assetName);
    lv_obj_t* title = lb::textLabel(panelObject, lb::type::drawerName, name, text, x,
                                    kDrawerNameTop);
    const int nameWidth = std::min(lb::textWidth(lb::type::drawerName, name), titleRight - x);
    lv_obj_set_width(title, std::max(80, nameWidth));
    lv_label_set_long_mode(title, LV_LABEL_LONG_CLIP);
    if (titleOut) *titleOut = title;
    x += std::max(80, nameWidth) + kDrawerNameGap;
    renderBlockPanelActions(panelObject, state, context, bypassOut);
    if (isCompressor) {
      renderGainMeter(panelObject, state.compressorGainReductionDb, gainMeterFillOut, gainMeterLabelOut);
    }
  }

  renderPageNavigation(panelObject, state, context, x);
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
      panelObject, controls[i], parameterColumnX(column),
      kParameterSliderGridY + row * (kParameterSliderHeight + kParameterSliderRowGap),
      context->ui->isParameterFocused(controls[i].key), context,
      onParameterSliderPressed, onParameterSliderPressing, i);
    if (controlsOut) controlsOut->push_back(slider);
  }
  if (!controls.empty()) {
    const auto selectedControl = std::find_if(controls.begin(), controls.end(), [&](const auto& control) {
      return context->ui->isParameterFocused(control.key);
    });
    const auto selectedIndex = selectedControl == controls.end()
      ? std::size_t{0}
      : static_cast<std::size_t>(std::distance(controls.begin(), selectedControl));
    lv_obj_t* toolbar = renderParameterMappingToolbar(
      root, state, context, controls[selectedIndex], selectedIndex);
    if (mappingToolbarOut) *mappingToolbarOut = toolbar;
  } else {
    renderPanelCloseButton(root, context);
  }
}

} // namespace

namespace parameter_widgets {

int columnX(int column)
{
  return parameterColumnX(column);
}

void bindClose(lv_obj_t* button, UiEventContext* context)
{
  lv_obj_add_event_cb(button, onCloseParamDrawer, LV_EVENT_PRESSED, context);
}

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
