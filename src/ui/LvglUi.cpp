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

void renderStatusBar(LvglUi* ui, lv_obj_t* root, UiState& state,
                     lv_obj_t** telemetryOut, lv_obj_t** messageOut, lv_obj_t** undoOut);

namespace {

// The layout below is authored against this fixed design grid; build() scales
// the whole canvas to the real display resolution.
constexpr int32_t kDesignWidth = 1280;
constexpr int32_t kDesignHeight = 720;
constexpr int kStatusBarHeight = 48;
constexpr int kHeaderButtonHeight = 60;
constexpr int kHeaderBlocksButtonWidth = 164;
constexpr int kHeaderTunerButtonWidth = 120;
constexpr int kBlockDrawerWidth = 480;
constexpr int kBlockDrawerPadding = 18;
constexpr int kBlockDrawerContentWidth = kBlockDrawerWidth - 2 * kBlockDrawerPadding;
constexpr int kBlockDrawerContentHeight = kDesignHeight - kStatusBarHeight - 2 * kBlockDrawerPadding;
constexpr int kCategoryColumns = 4;
constexpr int kCategoryButtonWidth = 105;
constexpr int kCategoryButtonHeight = 58;
constexpr int kCategoryButtonGap = 8;
constexpr int kDrawerCategoryTop = 60;
constexpr int kDrawerCategoryHeight = 2 * kCategoryButtonHeight + kCategoryButtonGap;
constexpr int kDrawerSeparatorY = kDrawerCategoryTop + kDrawerCategoryHeight + 14;
constexpr int kDrawerInstructionY = kDrawerSeparatorY + 16;
constexpr int kDrawerListTop = kDrawerInstructionY + 36;
constexpr int kDrawerListHeight = kBlockDrawerContentHeight - kDrawerListTop;
constexpr int kDrawerAssetButtonHeight = 72;
constexpr std::array<std::pair<const char*, const char*>, 8> kDrawerFilters = {{
  {"All", "all"}, {"Amps", "amps"}, {"Cabs", "cabs"}, {"EQ", "eq"},
  {"Dyn", "dynamics"}, {"Mod", "modulation"},
  {"Delays", "delay"}, {"Reverbs", "reverb"},
}};

std::string assetRenderKey(const UiAsset& asset)
{
  return asset.type + "\x1f" + asset.blockType + "\x1f" + asset.mode + "\x1f"
    + asset.path + "\x1f" + asset.name;
}
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
constexpr int kMinBank = 0;
constexpr int kMaxBank = 99;

constexpr auto bg = 0x000000;
constexpr auto panel = 0x242424;
constexpr auto panelAlt = 0x242424;
constexpr auto text = 0xf5f5f5;
constexpr auto muted = 0xa6a6a6;
constexpr auto accent = 0x43f05a;
constexpr auto warning = 0xffb347;
constexpr auto danger = 0xf97373;
constexpr auto eqCombined = 0xff9f43;
constexpr std::array<int, kParametricEqBandCount> eqBandColors = {
  0x56c7ff, 0x8be28b, 0xf5d76e, 0xff8c69, 0xc792ea,
};

void formatTelemetryLabel(const UiState& state, char* textOut, std::size_t capacity)
{
  if (!state.clipDebug.enabled) {
    std::snprintf(textOut, capacity, "%s  over %llu  max %.2fms",
                  state.effectsBypassed ? "BYPASS" : "LIVE",
                  static_cast<unsigned long long>(state.telemetry.overBudget), state.telemetry.maxMs);
    return;
  }

  if (state.clipDebug.overloaded) {
    std::snprintf(textOut, capacity, "CLIP  %s  %+.1fdB  %lluf",
                  state.clipDebug.firstStage.c_str(), state.clipDebug.peakDb,
                  static_cast<unsigned long long>(state.clipDebug.overloadFrames));
  } else if (state.clipDebug.limiterFrames > 0) {
    std::snprintf(textOut, capacity, "LIMIT  %+.1fdB  %lluf",
                  state.clipDebug.peakDb,
                  static_cast<unsigned long long>(state.clipDebug.limiterFrames));
  } else {
    std::snprintf(textOut, capacity, "LEVEL OK  %+.1fdB", state.clipDebug.peakDb);
  }
}

int telemetryColor(const UiState& state)
{
  if (state.effectsBypassed || (state.clipDebug.enabled && state.clipDebug.overloaded)) return danger;
  if (state.clipDebug.enabled && state.clipDebug.limiterFrames > 0) return warning;
  if (state.clipDebug.enabled) return accent;
  return muted;
}
constexpr int kEqPanelTop = 94;
constexpr int kEqPanelHeight = 578;
constexpr int kEqGraphX = kPanelEdgeInset;
constexpr int kEqGraphY = 80;
constexpr int kEqGraphWidth = kParameterPanelWidth - 2 * kPanelEdgeInset;
constexpr int kEqGraphHeight = 236;
constexpr int kEqBandControlsY = 330;
constexpr int kEqSlidersY = 398;
constexpr int kEqNodeSize = 48;
constexpr int kEqNodeRadius = kEqNodeSize / 2;
constexpr uint32_t kEqCurveRefreshIntervalMs = 33;
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
constexpr int kChainHandleWidth = 60;
constexpr int kChainTextX = 12;
constexpr int kChainTextWidth = kChainTileWidth - kChainHandleWidth - 28;
constexpr int kChainFirstRowCentreY = kChainTop + kChainTileTop + kChainTileHeight / 2;
constexpr int kChainSecondRowCentreY = kChainFirstRowCentreY + kChainRowHeight;
constexpr int kChainConnectorY = (kChainTop + kChainTileTop + kChainTileHeight
                                  + kChainTop + kChainTileTop + kChainRowHeight) / 2 + 10;
constexpr int kChainFirstRowEndX = kChainLeft + (kChainColumns - 1) * kChainSlotWidth + kChainTileWidth - 1;
constexpr int kChainConnectorRightX = kChainLeft + kChainColumns * kChainSlotWidth - 1;
constexpr int kChainConnectorLeftX = kChainLeft - 10;
constexpr int kChainConnectorRadius = 10;
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

struct EqGraphVisual {
  std::array<lv_obj_t*, kParametricEqBandCount + 1> responseLines{};
  std::array<lv_obj_t*, kParametricEqBandCount> nodes{};
  uint32_t lastCurveRefresh = 0;
};

void freeLinePoints(lv_event_t* event)
{
  lv_free(lv_event_get_user_data(event));
}

void freeParameterSliderVisual(lv_event_t* event)
{
  delete static_cast<ParameterSliderVisual*>(lv_event_get_user_data(event));
}

void freeBypassControlVisual(lv_event_t* event)
{
  delete static_cast<BypassControlVisual*>(lv_event_get_user_data(event));
}

void freeEqGraphVisual(lv_event_t* event)
{
  delete static_cast<EqGraphVisual*>(lv_event_get_user_data(event));
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
  // Model mutators publish typed revisions. This helper remains at event call
  // sites solely to make local focus/page changes visible.
  context->ui->invalidate(UiChange::None);
}

lv_obj_t* button(lv_obj_t* parent, const std::string& value);

lv_obj_t* renderChainWrapConnector(lv_obj_t* root)
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
  return connector;
}

void onPresetClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectPreset(*context->state, context->index);
  redraw(context);
}

void requestBankChange(UiEventContext* context, int delta)
{
  const int target = context->state->activeBank + delta;
  if (target < kMinBank || target > kMaxBank || !context->ui->actions().changeBank) {
    return;
  }
  context->ui->actions().changeBank(delta);
}

void onBankDownClicked(lv_event_t* event)
{
  requestBankChange(static_cast<UiEventContext*>(lv_event_get_user_data(event)), -1);
}

void onBankUpClicked(lv_event_t* event)
{
  requestBankChange(static_cast<UiEventContext*>(lv_event_get_user_data(event)), 1);
}

void onSaveClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().savePreset) {
    context->ui->actions().savePreset();
  }
  redraw(context);
}

void onNavigationDecision(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (!context->ui->actions().resolveNavigation) return;
  const auto decision = context->index == 0 ? UiNavigationDecision::Save
    : context->index == 1 ? UiNavigationDecision::Discard : UiNavigationDecision::Cancel;
  context->ui->actions().resolveNavigation(decision);
  redraw(context);
}

void onPresetModeClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->state->mode == UiMode::Tuner && context->ui->actions().setTunerMode) {
    context->ui->actions().setTunerMode(false);
  } else {
    enterPresetMode(*context->state);
  }
  redraw(context);
}

void onTunerModeClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().setTunerMode) {
    context->ui->actions().setTunerMode(true);
  } else {
    enterTunerMode(*context->state);
  }
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

void onUndoBlockEdit(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (undoLastBlockEdit(*context->state)) {
    context->ui->resetParameterPage();
    redraw(context);
  }
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

ParameterControl eqSliderControl(EqBandField field, const EqBandParams& band);
void refreshEqGraphCurve(lv_obj_t* graph, const ParametricEqParams& params, bool throttle = false);

void applyEqSliderPosition(lv_obj_t* slider, UiEventContext* context, lv_indev_t* input)
{
  const auto params = selectedParametricEqParams(*context->state);
  const auto& band = params.bands[context->index];
  const auto field = eqBandFieldForKey(context->filter);
  const float ratio = parameterSliderRatioForInput(slider, input);
  int delta = 0;
  switch (field) {
  case EqBandField::Frequency: {
    const float target = kEqMinimumFrequencyHz
      * std::exp(ratio * std::log(kEqMaximumFrequencyHz / kEqMinimumFrequencyHz));
    delta = static_cast<int>(std::lround(24.0f * std::log2(target / band.frequencyHz)));
    break;
  }
  case EqBandField::Q: {
    const float target = kEqMinimumQ * std::exp(ratio * std::log(kEqMaximumQ / kEqMinimumQ));
    delta = static_cast<int>(std::lround(24.0f * std::log2(target / band.q)));
    break;
  }
  case EqBandField::Gain: {
    const float target = kEqMinimumGainDb + ratio * (kEqMaximumGainDb - kEqMinimumGainDb);
    delta = static_cast<int>(std::lround((target - band.gainDb) / 0.5f));
    break;
  }
  }
  context->ui->applyFocusedParameterDelta(*context->state, delta, true);
}

void onEqSliderPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }
  const auto field = eqBandFieldForKey(context->filter);
  // Focus changes request a redraw. Mark the interaction first so the
  // currently-dragged LVGL object cannot be deleted before release.
  context->ui->setFocusedWidgets(lv_event_get_target_obj(event), context->controlledObject);
  context->ui->beginParameterInteraction();
  context->ui->selectEqBand(context->index);
  context->ui->focusEqBandField(field);
  const auto params = selectedParametricEqParams(*context->state);
  refreshParameterSliderVisual(lv_event_get_target_obj(event),
                               eqSliderControl(field, params.bands[context->index]), true);
  applyEqSliderPosition(lv_event_get_target_obj(event), context, input);
}

void onEqSliderPressing(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (input) {
    applyEqSliderPosition(lv_event_get_target_obj(event), context, input);
  }
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
  // Suppress rebuilds before selecting the node.
  context->ui->setFocusedWidgets(nullptr, context->controlledObject);
  context->ui->beginParameterInteraction();
  context->ui->selectEqBand(context->index);
  context->ui->focusEqBandField(EqBandField::Gain);
  lv_indev_get_point(input, &context->pressPoint);
  context->pressPoint = context->ui->toCanvas(context->pressPoint);
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
  lv_area_t graphArea{};
  lv_area_t canvasArea{};
  lv_obj_get_content_coords(context->controlledObject, &graphArea);
  lv_obj_get_coords(context->ui->canvas(), &canvasArea);
  const int x = std::clamp(static_cast<int>(point.x) - (graphArea.x1 - canvasArea.x1),
                           0, kEqGraphWidth - 1);
  const int y = std::clamp(static_cast<int>(point.y) - (graphArea.y1 - canvasArea.y1),
                           0, kEqGraphHeight - 1);
  auto params = selectedParametricEqParams(*context->state);
  auto& band = params.bands[context->index];
  band.frequencyHz = eqFrequencyFromX(x, kEqGraphWidth);
  band.gainDb = eqGainFromY(y, kEqGraphHeight);
  context->ui->updateSelectedEqBand(*context->state, band, false);
  refreshEqGraphCurve(context->controlledObject, selectedParametricEqParams(*context->state), true);
}

void onEqNodeReleased(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->endParameterInteraction();
  redraw(context);
}

void onBlockClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->suppressClick) {
    context->suppressClick = false;
    return;
  }
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

bool pointInVisibleChain(const UiState& state, const lv_point_t& point)
{
  const int right = state.blockDrawerOpen ? kDesignWidth - kBlockDrawerWidth : kDesignWidth - 20;
  return point.x >= 20 && point.x <= right
    && point.y >= kChainTop && point.y <= kChainTop + kChainHeight;
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
  const auto blockCount = context->state->bank.presets[context->state->activePreset].blocks.size();
  const auto target = chainSlotFromPoint(*context->state, point);
  const auto position = LvglUi::chainReorderIndicatorPosition(blockCount, context->index, target);

  if (!context->indicator) {
    context->indicator = lv_obj_create(context->ui->canvas());
    lv_obj_set_size(context->indicator, 5, kChainTileHeight);
    lv_obj_set_style_bg_color(context->indicator, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(context->indicator, 0, 0);
    lv_obj_set_style_radius(context->indicator, 2, 0);
  }

  lv_obj_set_pos(context->indicator, position.x, position.y);
  moveToFront(context->indicator);
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

void restoreChainPreview(UiEventContext* context)
{
  if (!context->controlledObject || !context->dragText.empty()) {
    return;
  }
  lv_obj_t* chain = lv_obj_get_parent(context->controlledObject);
  const auto blockCount = context->state->bank.presets[context->state->activePreset].blocks.size();
  const auto cardCount = std::min<std::size_t>(blockCount, lv_obj_get_child_count(chain));
  for (std::size_t i = 0; i < cardCount; ++i) {
    lv_obj_t* card = lv_obj_get_child(chain, static_cast<int32_t>(i));
    lv_obj_set_pos(card, 14 + static_cast<int>(i % kChainColumns) * kChainSlotWidth,
                   kChainTileTop + static_cast<int>(i / kChainColumns) * kChainRowHeight);
  }
}

void previewBlockMove(UiEventContext* context, std::size_t target)
{
  if (!context->controlledObject || !context->dragText.empty()) {
    return;
  }
  lv_obj_t* chain = lv_obj_get_parent(context->controlledObject);
  const auto blockCount = context->state->bank.presets[context->state->activePreset].blocks.size();
  if (blockCount == 0) {
    return;
  }
  target = std::min(target, blockCount - 1);
  const auto cardCount = std::min<std::size_t>(blockCount, lv_obj_get_child_count(chain));
  for (std::size_t i = 0; i < cardCount; ++i) {
    std::size_t previewIndex = i;
    if (target > context->index && i > context->index && i <= target) {
      previewIndex = i - 1;
    } else if (target < context->index && i >= target && i < context->index) {
      previewIndex = i + 1;
    }
    lv_obj_t* card = lv_obj_get_child(chain, static_cast<int32_t>(i));
    lv_obj_set_pos(card, 14 + static_cast<int>(previewIndex % kChainColumns) * kChainSlotWidth,
                   kChainTileTop + static_cast<int>(previewIndex / kChainColumns) * kChainRowHeight);
  }
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
  if (pointInVisibleChain(*context->state, point)) {
    placeDragIndicator(context, point);
    previewBlockMove(context, chainSlotFromPoint(*context->state, point));
  } else if (context->indicator) {
    lv_obj_delete(context->indicator);
    context->indicator = nullptr;
    restoreChainPreview(context);
  }
}

void clearDragVisuals(UiEventContext* context)
{
  restoreChainPreview(context);
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
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->dragging = false;
  context->suppressClick = false;
  lv_indev_t* input = lv_event_get_indev(event);
  if (input) {
    lv_indev_get_point(input, &context->pressPoint);
    context->pressPoint = context->ui->toCanvas(context->pressPoint);
  }
}

void onBlockPressing(lv_event_t* event)
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
  if (!context->dragging) {
    context->dragging = true;
    context->ui->beginInteraction();
    lv_obj_set_style_opa(context->controlledObject ? context->controlledObject
                                                   : lv_event_get_target_obj(event), LV_OPA_TRANSP, 0);
  }
  updateDragVisuals(context, event);
}

void onBlockReleased(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto& blocks = context->state->bank.presets[context->state->activePreset].blocks;
  const lv_opa_t restingOpacity = context->index < blocks.size() && !blocks[context->index].enabled
    ? LV_OPA_70 : LV_OPA_COVER;
  lv_obj_set_style_opa(context->controlledObject ? context->controlledObject
                                                 : lv_event_get_target_obj(event), restingOpacity, 0);
  if (!context->dragging) {
    return;
  }
  context->suppressClick = true;
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    clearDragVisuals(context);
    context->ui->endInteraction();
    return;
  }

  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  const bool droppedOnChain = pointInVisibleChain(*context->state, point);
  const auto target = chainSlotFromPoint(*context->state, point);
  clearDragVisuals(context);
  if (!droppedOnChain || target == context->index) {
    context->ui->endInteraction();
    return;
  }

  moveBlock(*context->state, context->index, target);
  redraw(context);
  context->ui->endInteraction();
}

void onBlockPressLost(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto& blocks = context->state->bank.presets[context->state->activePreset].blocks;
  const lv_opa_t restingOpacity = context->index < blocks.size() && !blocks[context->index].enabled
    ? LV_OPA_70 : LV_OPA_COVER;
  lv_obj_set_style_opa(context->controlledObject ? context->controlledObject
                                                 : lv_event_get_target_obj(event), restingOpacity, 0);
  context->suppressClick = context->dragging;
  const bool wasDragging = context->dragging;
  clearDragVisuals(context);
  if (wasDragging) {
    context->ui->endInteraction();
  }
}

void onFilterClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  setCategoryFilter(*context->state, context->filter);
  context->state->assetScrollOffset = 0;
  context->ui->invalidate(UiChange::Drawers);
}

void onAssetListScrollBegin(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->beginInteraction();
}

void onAssetListScroll(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->state->assetScrollOffset = lv_obj_get_scroll_y(lv_event_get_target_obj(event));
}

void onAssetListScrollEnd(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->state->assetScrollOffset = lv_obj_get_scroll_y(lv_event_get_target_obj(event));
  context->ui->endInteraction(false);
}

void onAssetClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->suppressClick) {
    context->suppressClick = false;
    return;
  }
  const auto before = context->state->bank.presets[context->state->activePreset].blocks.size();
  appendAssetBlock(*context->state, context->index);
  const auto& blocks = context->state->bank.presets[context->state->activePreset].blocks;
  if (blocks.size() > before) {
    context->ui->highlightBlock(blocks[context->state->selectedBlock].id);
    context->ui->resetParameterPage();
  }
  redraw(context);
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

void onAssetPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->dragging = false;
  context->suppressClick = false;
}

void onAssetLongPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->dragging = true;
  context->ui->beginInteraction();
  context->dragText = assetDragText(context->state->assets[context->index]);
  lv_obj_set_style_opa(lv_event_get_target_obj(event), LV_OPA_50, 0);

  // A normal swipe belongs to the list. Once a stationary long press has
  // explicitly armed dragging, temporarily stop the list from taking the
  // subsequent movement and cancelling the item's drag gesture.
  if (context->controlledObject) {
    lv_obj_remove_flag(context->controlledObject, LV_OBJ_FLAG_SCROLLABLE);
  }

  lv_indev_t* input = lv_event_get_indev(event);
  if (input) {
    lv_point_t point{};
    lv_indev_get_point(input, &point);
    point = context->ui->toCanvas(point);
    placeDragGhost(context, point);
  }
}

void onAssetPressing(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (!context->dragging) {
    return;
  }
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }

  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  placeDragGhost(context, point);
  if (pointInVisibleChain(*context->state, point)) {
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
  if (context->controlledObject) {
    lv_obj_add_flag(context->controlledObject, LV_OBJ_FLAG_SCROLLABLE);
  }
  if (!context->dragging) {
    return;
  }

  context->suppressClick = true;
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    clearDragVisuals(context);
    context->ui->endInteraction();
    return;
  }

  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  const bool droppedOnChain = pointInVisibleChain(*context->state, point);
  const auto target = insertSlotFromPoint(*context->state, point);
  clearDragVisuals(context);
  if (!droppedOnChain) {
    context->ui->endInteraction();
    return;
  }

  const auto before = context->state->bank.presets[context->state->activePreset].blocks.size();
  insertAssetBlock(*context->state, context->index, target);
  const auto& blocks = context->state->bank.presets[context->state->activePreset].blocks;
  if (blocks.size() > before) {
    context->ui->highlightBlock(blocks[context->state->selectedBlock].id);
    context->ui->resetParameterPage();
  }
  redraw(context);
  context->ui->endInteraction();
}

void onAssetPressLost(lv_event_t* event)
{
  lv_obj_set_style_opa(lv_event_get_target_obj(event), LV_OPA_COVER, 0);
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->controlledObject) {
    lv_obj_add_flag(context->controlledObject, LV_OBJ_FLAG_SCROLLABLE);
  }
  context->suppressClick = context->dragging;
  const bool wasDragging = context->dragging;
  clearDragVisuals(context);
  if (wasDragging) {
    context->ui->endInteraction();
  }
}

lv_obj_t* button(lv_obj_t* parent, const std::string& value)
{
  lv_obj_t* object = lv_button_create(parent);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
  styleSurface(object);
  lv_obj_t* buttonLabel = lv_label_create(object);
  lv_label_set_text(buttonLabel, value.c_str());
  setText(buttonLabel, text, &ardor_font_open_sans_semibold_22);
  lv_obj_center(buttonLabel);
  return object;
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
  const auto& block = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
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

void refreshEqGraphCurve(lv_obj_t* graph, const ParametricEqParams& params, bool throttle)
{
  auto* visual = graph ? static_cast<EqGraphVisual*>(lv_obj_get_user_data(graph)) : nullptr;
  if (!visual) {
    return;
  }

  const bool updateCurve = !throttle || visual->lastCurveRefresh == 0
    || lv_tick_elaps(visual->lastCurveRefresh) >= kEqCurveRefreshIntervalMs;
  if (updateCurve) {
    visual->lastCurveRefresh = lv_tick_get();
    const auto curve = makeEqCurveData(params, 48000.0f);
    for (std::size_t responseIndex = 0; responseIndex < visual->responseLines.size(); ++responseIndex) {
      lv_obj_t* line = visual->responseLines[responseIndex];
      if (!line) {
        continue;
      }
      lv_point_precise_t* points = lv_line_get_points_mutable(line);
      if (!points) {
        continue;
      }
      const auto& response = responseIndex < kParametricEqBandCount
        ? curve.bandDb[responseIndex] : curve.combinedDb;
      for (std::size_t point = 0; point < kEqCurvePointCount; ++point) {
        points[point].y = eqYFromGain(response[point], kEqGraphHeight);
      }
      if (responseIndex < kParametricEqBandCount) {
        lv_obj_set_style_line_opa(line, params.bands[responseIndex].enabled ? LV_OPA_40 : LV_OPA_20,
                                  LV_PART_MAIN);
      }
      lv_obj_invalidate(line);
    }
  }

  for (std::size_t bandIndex = 0; bandIndex < visual->nodes.size(); ++bandIndex) {
    lv_obj_t* node = visual->nodes[bandIndex];
    if (!node) {
      continue;
    }
    const auto& band = params.bands[bandIndex];
    const int x = std::clamp(eqXFromFrequency(band.frequencyHz, kEqGraphWidth),
                             kEqNodeRadius, kEqGraphWidth - kEqNodeRadius);
    const int y = std::clamp(eqYFromGain(band.gainDb, kEqGraphHeight),
                             kEqNodeRadius, kEqGraphHeight - kEqNodeRadius);
    lv_obj_set_pos(node, x - kEqNodeRadius, y - kEqNodeRadius);
  }
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

ParameterControl eqSliderControl(EqBandField field, const EqBandParams& band)
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

void renderParametricEqPanel(lv_obj_t* root, UiState& state, UiEventContext* context,
                             lv_obj_t** graphOut,
                             std::array<lv_obj_t*, kParametricEqBandCount>* bandButtonsOut,
                             lv_obj_t** enabledOut, UiEventContext** enabledContextOut,
                             UiEventContext** resetContextOut,
                             std::array<lv_obj_t*, 3>* slidersOut,
                             std::array<UiEventContext*, 3>* sliderContextsOut,
                             lv_obj_t** bypassOut)
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
  // Every EQ child uses panel-local coordinates. Theme padding used to add an
  // invisible left inset and consume the intended right margin.
  lv_obj_set_style_pad_all(panelObject, 0, 0);
  label(panelObject, "Parametric EQ", LV_ALIGN_TOP_LEFT, 28, 15, &ardor_font_open_sans_semibold_22);
  label(panelObject, "Five bands", LV_ALIGN_TOP_LEFT, 205, 18, &ardor_font_open_sans_regular_18, muted);

  const auto params = selectedParametricEqParams(state);
  const auto curve = makeEqCurveData(params, 48000.0f);
  lv_obj_t* graph = lv_obj_create(panelObject);
  lv_obj_set_size(graph, kEqGraphWidth, kEqGraphHeight);
  lv_obj_set_pos(graph, kEqGraphX, kEqGraphY);
  lv_obj_remove_flag(graph, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(graph, 0x111111);
  lv_obj_set_style_border_color(graph, lv_color_hex(0x3a3a3a), 0);
  lv_obj_set_style_border_width(graph, 1, 0);
  auto* graphVisual = new EqGraphVisual{};
  lv_obj_set_user_data(graph, graphVisual);
  lv_obj_add_event_cb(graph, freeEqGraphVisual, LV_EVENT_DELETE, graphVisual);
  if (graphOut) *graphOut = graph;

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
    graphVisual->responseLines[i] = createEqResponseLine(graph, curve.bandDb[i], eqBandColors[i],
                                                           params.bands[i].enabled ? LV_OPA_40 : LV_OPA_20);
  }
  graphVisual->responseLines[kParametricEqBandCount] = createEqResponseLine(graph, curve.combinedDb,
                                                                               eqCombined, LV_OPA_COVER);

  for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
    const auto& band = params.bands[i];
    lv_obj_t* node = button(graph, std::to_string(i + 1));
    graphVisual->nodes[i] = node;
    lv_obj_set_size(node, kEqNodeSize, kEqNodeSize);
    const int x = std::clamp(eqXFromFrequency(band.frequencyHz, kEqGraphWidth),
                             kEqNodeRadius, kEqGraphWidth - kEqNodeRadius);
    const int y = std::clamp(eqYFromGain(band.gainDb, kEqGraphHeight),
                             kEqNodeRadius, kEqGraphHeight - kEqNodeRadius);
    lv_obj_set_pos(node, x - kEqNodeRadius, y - kEqNodeRadius);
    styleSurface(node, eqBandColors[i]);
    lv_obj_set_style_radius(node, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_opa(node, band.enabled ? LV_OPA_COVER : LV_OPA_50, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(node, 0), lv_color_hex(bg), 0);
    if (context->ui->selectedEqBand() == i) {
      lv_obj_set_style_border_color(node, lv_color_hex(text), 0);
      lv_obj_set_style_border_width(node, 2, 0);
    }
    auto* nodeContext = context->ui->remember(state, i);
    nodeContext->controlledObject = graph;
    lv_obj_add_event_cb(node, onEqNodePressed, LV_EVENT_PRESSED, nodeContext);
    lv_obj_add_event_cb(node, onEqNodePressing, LV_EVENT_PRESSING, nodeContext);
    lv_obj_add_event_cb(node, onEqNodeReleased, LV_EVENT_RELEASED, nodeContext);
    lv_obj_add_event_cb(node, onEqNodeReleased, LV_EVENT_PRESS_LOST, nodeContext);
  }

  // Create the header actions after the graph so they remain topmost even if
  // a future layout adjustment accidentally brings the two regions close.
  renderPanelCloseButton(panelObject, context);
  renderBlockPanelActions(panelObject, state, context, bypassOut);

  for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
    lv_obj_t* bandButton = button(panelObject, "Band " + std::to_string(i + 1));
    lv_obj_set_size(bandButton, 106, 50);
    lv_obj_set_pos(bandButton, 28 + static_cast<int>(i) * 112, kEqBandControlsY);
    styleSurface(bandButton, context->ui->selectedEqBand() == i ? eqBandColors[i] : 0x171717);
    lv_obj_set_style_text_color(lv_obj_get_child(bandButton, 0),
                                lv_color_hex(context->ui->selectedEqBand() == i ? bg : text), 0);
    lv_obj_add_event_cb(bandButton, onEqBandSelected, LV_EVENT_CLICKED, context->ui->remember(state, i));
    if (bandButtonsOut) (*bandButtonsOut)[i] = bandButton;
  }

  const auto selectedBand = context->ui->selectedEqBand();
  const auto& band = params.bands[selectedBand];
  lv_obj_t* enabled = button(panelObject, band.enabled ? "Band On" : "Band Off");
  lv_obj_set_size(enabled, 130, 50);
  lv_obj_set_pos(enabled, 600, kEqBandControlsY);
  styleSurface(enabled, band.enabled ? 0x25442a : 0x3a2020);
  lv_obj_set_style_text_color(lv_obj_get_child(enabled, 0), lv_color_hex(band.enabled ? accent : 0xf97373), 0);
  auto* enabledContext = context->ui->remember(state, selectedBand);
  lv_obj_add_event_cb(enabled, onEqBandEnabled, LV_EVENT_CLICKED, enabledContext);
  if (enabledOut) *enabledOut = enabled;
  if (enabledContextOut) *enabledContextOut = enabledContext;

  lv_obj_t* reset = button(panelObject, "Reset Band");
  lv_obj_set_size(reset, 148, 50);
  lv_obj_set_pos(reset, 744, kEqBandControlsY);
  styleSurface(reset, 0x171717);
  auto* resetContext = context->ui->remember(state, selectedBand);
  lv_obj_add_event_cb(reset, onEqBandReset, LV_EVENT_CLICKED, resetContext);
  if (resetContextOut) *resetContextOut = resetContext;

  constexpr std::array<EqBandField, 3> eqSliderFields = {
    EqBandField::Frequency, EqBandField::Q, EqBandField::Gain,
  };
  for (std::size_t i = 0; i < eqSliderFields.size(); ++i) {
    const auto field = eqSliderFields[i];
    auto* sliderContext = context->ui->remember(state, selectedBand,
                                                 field == EqBandField::Frequency ? "frequency"
                                                 : field == EqBandField::Q ? "q" : "gain");
    sliderContext->controlledObject = graph;
    lv_obj_t* slider = createParameterSlider(
      panelObject, eqSliderControl(field, band),
      kParameterSliderGridX + static_cast<int>(i) * (kParameterSliderWidth + kParameterSliderColumnGap),
      kEqSlidersY, context->ui->isEqBandFieldFocused(field), sliderContext,
      onEqSliderPressed, onEqSliderPressing, i);
    if (slidersOut) (*slidersOut)[i] = slider;
    if (sliderContextsOut) (*sliderContextsOut)[i] = sliderContext;
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
    const auto& blocks = state.bank.presets[state.activePreset].blocks;
    if (state.selectedBlock >= blocks.size()) {
      return;
    }
    const auto& block = blocks[state.selectedBlock];
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
  const int boundary = std::clamp((canvasPoint.x - kChainLeft + kChainSlotWidth / 2) / kChainSlotWidth,
                                  0, kChainColumns);
  const int row = std::clamp((canvasPoint.y - (kChainTop + kChainTileTop)) / kChainRowHeight, 0, kChainRows - 1);
  return std::min(blockCount, static_cast<std::size_t>(row * kChainColumns + boundary));
}

lv_point_t LvglUi::chainIndicatorPosition(std::size_t blockCount, std::size_t slot)
{
  slot = std::min(slot, std::min(blockCount, kMaxEffectBlocks));
  const int row = static_cast<int>(slot / kChainColumns);
  const int column = static_cast<int>(slot % kChainColumns);
  return {kChainLeft + column * kChainSlotWidth, kChainTop + kChainTileTop + row * kChainRowHeight};
}

lv_point_t LvglUi::chainReorderIndicatorPosition(std::size_t blockCount, std::size_t source,
                                                  std::size_t target)
{
  if (blockCount == 0) {
    return chainIndicatorPosition(0, 0);
  }
  source = std::min(source, blockCount - 1);
  target = std::min(target, blockCount - 1);
  auto position = chainIndicatorPosition(blockCount, target);
  if (target > source) {
    // Moving forward inserts after the hovered block. Keep the marker on that
    // block's row instead of wrapping it to the next row at column five.
    position.x += kChainSlotWidth;
  }
  return position;
}

void LvglUi::selectBlock(UiState& state, std::size_t blockIndex)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (blockIndex >= blocks.size()) {
    return;
  }
  ardor::selectBlock(state, blockIndex);
  highlightedBlockId_.clear();
  selectedEqBand_ = 0;
  resetParameterPage();
}

void LvglUi::selectGlobalParams(UiState& state)
{
  ardor::selectGlobalParams(state);
  resetParameterPage();
}

void LvglUi::highlightBlock(std::string blockId)
{
  highlightedBlockId_ = std::move(blockId);
  highlightUntil_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(1400);
}

bool LvglUi::isBlockHighlighted(const std::string& blockId) const
{
  return highlightedBlockId_ == blockId && std::chrono::steady_clock::now() < highlightUntil_;
}

bool LvglUi::updateSelectedEqBand(UiState& state, EqBandParams params, bool requestUiRebuild)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return false;
  }
  const auto& block = blocks[state.selectedBlock];
  const auto before = selectedParametricEqParams(state).bands[selectedEqBand_];
  const bool dirtyBefore = state.dirty;
  const auto previewRollback = captureUiPreviewSnapshot(state);
  if (!setSelectedEqBand(state, selectedEqBand_, params)) {
    return false;
  }
  const auto after = selectedParametricEqParams(state).bands[selectedEqBand_];
  if (after == before) {
    return false;
  }
  if (actions_.updateEqBand) {
    if (!actions_.updateEqBand(block.id, selectedEqBand_, after)) {
      // A missing runtime ID is an unexpected draft/runtime divergence. Keep
      // the candidate and heal it through the same complete-preview path used
      // for topology edits rather than showing a generic live-update error.
      if (queuePreview(state, previewRollback, "update " + block.assetName + " EQ")) {
        invalidate(UiChange::Parameters | UiChange::Header);
        return true;
      }
      setSelectedEqBand(state, selectedEqBand_, before);
      state.dirty = dirtyBefore;
      invalidate(UiChange::Parameters | UiChange::Header);
      return false;
    }
  }
  if (requestUiRebuild) {
    invalidate(UiChange::Parameters);
  }
  return true;
}

bool LvglUi::applyFocusedParameterDelta(UiState& state, int delta, bool continuousTouch)
{
  if (focusedEqField_.has_value()) {
    const auto& blocks = state.bank.presets[state.activePreset].blocks;
    if (state.paramTarget != UiParamTarget::Block || state.selectedBlock >= blocks.size()
        || blocks[state.selectedBlock].type != "eq" || !isParametricEqMode(blocks[state.selectedBlock].params)) {
      return false;
    }
    auto params = selectedParametricEqParams(state);
    adjustEqBandField(params.bands[selectedEqBand_], *focusedEqField_, delta);
    if (!updateSelectedEqBand(state, params.bands[selectedEqBand_], focusedEqGraph_ == nullptr)) {
      return false;
    }
    const auto updated = selectedParametricEqParams(state);
    if (focusedControl_) {
      refreshParameterSliderVisual(focusedControl_,
                                   eqSliderControl(*focusedEqField_, updated.bands[selectedEqBand_]));
    }
    refreshEqGraphCurve(focusedEqGraph_, updated, continuousTouch);
    renderedRevisions_.parameters = state.revisions.parameters;
    renderedRevisions_.header = state.revisions.header;
    syncPersistentViews(state);
    return true;
  }
  if (focusedKey_.empty()) {
    return false;
  }
  for (const auto& control : ardor::parameterPage(state, parameterPage_)) {
    if (control.key != focusedKey_) {
      continue;
    }
    const bool dirtyBefore = state.dirty;
    const auto previewRollback = captureUiPreviewSnapshot(state);
    nlohmann::json paramsBefore;
    bool hasBlockSnapshot = false;
    if (state.paramTarget == UiParamTarget::Block) {
      const auto& blocks = state.bank.presets[state.activePreset].blocks;
      if (state.selectedBlock < blocks.size()) {
        paramsBefore = blocks[state.selectedBlock].params;
        hasBlockSnapshot = true;
      }
    }
    if (applyParameterDelta(state, control, delta)) {
      bool liveUpdateSucceeded = true;
      if (state.paramTarget == UiParamTarget::Globals && actions_.updateGlobalGains) {
        const auto& global = state.bank.presets[state.activePreset].global;
        actions_.updateGlobalGains(global.inputGainDb, global.outputGainDb);
      }
      if (actions_.updateDaisyParameter && state.paramTarget == UiParamTarget::Block) {
        const auto& blocks = state.bank.presets[state.activePreset].blocks;
        if (state.selectedBlock < blocks.size()) {
          const auto& block = blocks[state.selectedBlock];
          if ((block.type == "mod" || block.type == "delay" || block.type == "reverb")
              && block.params.contains(control.key) && block.params[control.key].is_number()) {
            liveUpdateSucceeded = actions_.updateDaisyParameter(
              block.id, control.key, block.params[control.key].get<float>());
          }
        }
      }
      if (actions_.updateCompressorParameter && state.paramTarget == UiParamTarget::Block) {
        const auto& blocks = state.bank.presets[state.activePreset].blocks;
        if (state.selectedBlock < blocks.size()) {
          const auto& block = blocks[state.selectedBlock];
          if (block.type == "dynamics" && block.params.value("mode", "") == "compressor"
              && block.params.contains(control.key) && block.params[control.key].is_number()) {
            liveUpdateSucceeded = actions_.updateCompressorParameter(
              block.id, control.key, block.params[control.key].get<float>());
          }
        }
      }
      if (actions_.updateCabParameters && state.paramTarget == UiParamTarget::Block) {
        const auto& blocks = state.bank.presets[state.activePreset].blocks;
        if (state.selectedBlock < blocks.size()) {
          const auto& block = blocks[state.selectedBlock];
          if (block.type == "cab") {
            actions_.updateCabParameters(block.params.value("levelDb", 0.0f),
                                         block.params.value("mix", 1.0f));
          }
        }
      }
      if (!liveUpdateSucceeded && hasBlockSnapshot) {
        const auto& blocks = state.bank.presets[state.activePreset].blocks;
        const std::string operation = state.selectedBlock < blocks.size()
          ? "update " + blocks[state.selectedBlock].assetName : "update effect";
        if (queuePreview(state, previewRollback, operation)) {
          invalidate(UiChange::Parameters | UiChange::Header);
          return true;
        }
        auto& restoredBlocks = state.bank.presets[state.activePreset].blocks;
        if (state.selectedBlock < restoredBlocks.size()) restoredBlocks[state.selectedBlock].params = std::move(paramsBefore);
        state.dirty = dirtyBefore;
        invalidate(UiChange::Parameters | UiChange::Header);
        return true;
      }
      const auto updatedControls = ardor::parameterPage(state, parameterPage_);
      const auto updated = std::find_if(updatedControls.begin(), updatedControls.end(), [this](const auto& item) {
        return item.key == focusedKey_;
      });
      if (focusedControl_ && updated != updatedControls.end()) {
        refreshParameterSliderVisual(focusedControl_, *updated);
        renderedRevisions_.parameters = state.revisions.parameters;
        renderedRevisions_.header = state.revisions.header;
        syncPersistentViews(state);
      } else {
        invalidate(UiChange::Parameters);
      }
    }
    return true;
  }
  return false;
}

UiEventContext* LvglUi::remember(UiState& state, std::size_t index, std::string filter)
{
  contexts_.emplace_back();
  auto& context = contexts_.back();
  context.ui = this;
  context.state = &state;
  context.index = index;
  context.filter = std::move(filter);
  context.region = contextRegion_;
  return &context;
}

void LvglUi::build(lv_obj_t* root, UiState& state)
{
  viewsInitialized_ = false;
  pendingChanges_ = UiChange::None;
  focusedControl_ = nullptr;
  focusedEqGraph_ = nullptr;
  parameterViews_.clear();
  activeParameterLayer_ = nullptr;
  if (state.mode == UiMode::Preset || !state.paramDrawerOpen) {
    resetParameterPage();
  }
  lv_obj_clean(root);
  contexts_.clear();
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

  const auto createLayer = [canvas]() {
    lv_obj_t* layer = lv_obj_create(canvas);
    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, kDesignWidth, kDesignHeight);
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    return layer;
  };
  presetLayer_ = createLayer();
  editLayer_ = createLayer();
  tunerLayer_ = createLayer();
  parameterLayer_ = createLayer();
  drawerLayer_ = createLayer();
  statusLayer_ = createLayer();
  previewOverlay_ = lv_obj_create(canvas);
  lv_obj_set_size(previewOverlay_, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(previewOverlay_, 0, 0);
  lv_obj_set_style_bg_color(previewOverlay_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(previewOverlay_, LV_OPA_70, 0);
  lv_obj_set_style_border_width(previewOverlay_, 0, 0);
  lv_obj_remove_flag(previewOverlay_, LV_OBJ_FLAG_SCROLLABLE);
  previewOverlayLabel_ = lv_label_create(previewOverlay_);
  lv_label_set_text(previewOverlayLabel_, "Applying effect chain...\n\n◌");
  lv_obj_set_style_text_color(previewOverlayLabel_, lv_color_hex(text), 0);
  lv_obj_set_style_text_align(previewOverlayLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(previewOverlayLabel_);
  lv_obj_add_flag(previewOverlay_, LV_OBJ_FLAG_HIDDEN);
  navigationOverlay_ = lv_obj_create(canvas);
  lv_obj_set_size(navigationOverlay_, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(navigationOverlay_, 0, 0);
  lv_obj_set_style_bg_color(navigationOverlay_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(navigationOverlay_, LV_OPA_70, 0);
  lv_obj_set_style_border_width(navigationOverlay_, 0, 0);
  lv_obj_remove_flag(navigationOverlay_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* prompt = lv_label_create(navigationOverlay_);
  lv_label_set_text(prompt, "Unsaved changes");
  lv_obj_set_style_text_color(prompt, lv_color_hex(text), 0);
  lv_obj_set_style_text_font(prompt, &ardor_font_open_sans_semibold_22, 0);
  lv_obj_align(prompt, LV_ALIGN_CENTER, 0, -92);
  lv_obj_t* guidance = lv_label_create(navigationOverlay_);
  lv_label_set_text(guidance, "Save changes before switching presets?");
  lv_obj_set_style_text_color(guidance, lv_color_hex(muted), 0);
  lv_obj_align(guidance, LV_ALIGN_CENTER, 0, -52);
  const std::array<std::string, 3> choices = {"Save", "Discard", "Cancel"};
  for (std::size_t i = 0; i < choices.size(); ++i) {
    lv_obj_t* choice = button(navigationOverlay_, choices[i]);
    lv_obj_set_size(choice, 150, 56);
    lv_obj_align(choice, LV_ALIGN_CENTER, static_cast<int>(i) * 166 - 166, 18);
    if (i == 0) styleSurface(choice, 0x25442a);
    lv_obj_add_event_cb(choice, onNavigationDecision, LV_EVENT_CLICKED, remember(state, i));
  }
  lv_obj_add_flag(navigationOverlay_, LV_OBJ_FLAG_HIDDEN);

  rebuildPresetView(state);
  rebuildEditView(state);
  contextRegion_ = UiContextRegion::Tuner;
  renderTunerMode(tunerLayer_, state);
  contextRegion_ = UiContextRegion::None;
  rebuildParameterView(state);
  rebuildDrawerView(state);
  contextRegion_ = UiContextRegion::Status;
  renderStatusBar(this, statusLayer_, state, &telemetryLabel_, &statusMessageLabel_, &undoButton_);
  contextRegion_ = UiContextRegion::None;

  renderedRevisions_ = state.revisions;
  renderedBank_ = state.activeBank;
  renderedPreset_ = state.activePreset;
  viewsInitialized_ = true;
  syncPersistentViews(state);
}

void LvglUi::rebuildPresetView(UiState& state)
{
  if (!presetLayer_) return;
  lv_obj_clean(presetLayer_);
  contexts_.remove_if([](const UiEventContext& context) {
    return context.region == UiContextRegion::Preset;
  });
  presetCardLabels_.fill(nullptr);
  presetCardButtons_.fill(nullptr);
  presetIndicators_.fill(nullptr);
  presetBankLabel_ = nullptr;
  masterVolumeLabel_ = nullptr;
  bankDownButton_ = nullptr;
  bankUpButton_ = nullptr;
  contextRegion_ = UiContextRegion::Preset;
  renderPresetMode(presetLayer_, state);
  contextRegion_ = UiContextRegion::None;
}

void LvglUi::rebuildEditView(UiState& state)
{
  if (!editLayer_) return;
  lv_obj_clean(editLayer_);
  contexts_.remove_if([](const UiEventContext& context) {
    return context.region == UiContextRegion::Edit;
  });
  editPresetLabel_ = nullptr;
  saveButtonLabel_ = nullptr;
  chainCards_.fill(nullptr);
  chainCategoryLabels_.fill(nullptr);
  chainAssetLabels_.fill(nullptr);
  chainBypassLabels_.fill(nullptr);
  chainSelectionIndicators_.fill(nullptr);
  chainClickContexts_.fill(nullptr);
  chainDragContexts_.fill(nullptr);
  renderedBlockIds_.clear();
  chainWrapConnector_ = nullptr;
  contextRegion_ = UiContextRegion::Edit;
  renderEditMode(editLayer_, state);
  contextRegion_ = UiContextRegion::None;
}

void LvglUi::syncChainCards(UiState& state)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  const std::size_t count = std::min(blocks.size(), kMaxEffectBlocks);
  std::array<std::size_t, kMaxEffectBlocks> source{};
  source.fill(kMaxEffectBlocks);
  std::array<bool, kMaxEffectBlocks> used{};

  for (std::size_t i = 0; i < count; ++i) {
    for (std::size_t j = 0; j < renderedBlockIds_.size(); ++j) {
      if (!used[j] && renderedBlockIds_[j] == blocks[i].id) {
        source[i] = j;
        used[j] = true;
        break;
      }
    }
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (source[i] == kMaxEffectBlocks) {
      for (std::size_t j = 0; j < kMaxEffectBlocks; ++j) {
        if (!used[j]) {
          source[i] = j;
          used[j] = true;
          break;
        }
      }
    }
  }
  std::size_t next = count;
  for (std::size_t j = 0; j < kMaxEffectBlocks; ++j) {
    if (!used[j]) source[next++] = j;
  }

  const auto remap = [&source](auto& values) {
    const auto old = values;
    for (std::size_t i = 0; i < kMaxEffectBlocks; ++i) values[i] = old[source[i]];
  };
  remap(chainCards_);
  remap(chainCategoryLabels_);
  remap(chainAssetLabels_);
  remap(chainBypassLabels_);
  remap(chainSelectionIndicators_);
  remap(chainClickContexts_);
  remap(chainDragContexts_);

  renderedBlockIds_.clear();
  for (std::size_t i = 0; i < kMaxEffectBlocks; ++i) {
    lv_obj_t* card = chainCards_[i];
    if (!card) continue;
    if (i >= count) {
      lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    const auto& block = blocks[i];
    renderedBlockIds_.push_back(block.id);
    std::string category = block.label;
    std::transform(category.begin(), category.end(), category.begin(), [](unsigned char character) {
      return static_cast<char>(std::toupper(character));
    });
    lv_label_set_text(chainCategoryLabels_[i], category.c_str());
    lv_label_set_text(chainAssetLabels_[i], block.assetName.c_str());
    lv_obj_set_pos(card, 14 + static_cast<int>(i % kChainColumns) * kChainSlotWidth,
                   kChainTileTop + static_cast<int>(i / kChainColumns) * kChainRowHeight);
    styleSurface(card, block.enabled ? panel : 0x171717);
    lv_obj_set_style_opa(card, block.enabled ? LV_OPA_COVER : LV_OPA_70, 0);
    lv_obj_set_style_border_width(card, isBlockHighlighted(block.id) ? 3 : 0, 0);
    if (isBlockHighlighted(block.id)) {
      lv_obj_set_style_border_color(card, lv_color_hex(accent), 0);
    }
    if (block.enabled) lv_obj_add_flag(chainBypassLabels_[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(chainBypassLabels_[i], LV_OBJ_FLAG_HIDDEN);
    const bool selected = state.paramTarget == UiParamTarget::Block && state.selectedBlock == i;
    if (selected) lv_obj_remove_flag(chainSelectionIndicators_[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(chainSelectionIndicators_[i], LV_OBJ_FLAG_HIDDEN);
    chainClickContexts_[i]->index = i;
    chainDragContexts_[i]->index = i;
    chainDragContexts_[i]->controlledObject = card;
    lv_obj_remove_flag(card, LV_OBJ_FLAG_HIDDEN);
  }
  if (chainWrapConnector_) {
    if (count > kChainColumns) lv_obj_remove_flag(chainWrapConnector_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(chainWrapConnector_, LV_OBJ_FLAG_HIDDEN);
  }
}

void LvglUi::rebuildParameterView(UiState& state)
{
  if (!parameterLayer_) return;
  focusedControl_ = nullptr;
  focusedEqGraph_ = nullptr;
  if (state.mode != UiMode::Edit || !state.paramDrawerOpen) {
    resetParameterPage();
    return;
  }

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  const bool editingEq = state.paramTarget == UiParamTarget::Block
    && state.selectedBlock < blocks.size() && blocks[state.selectedBlock].type == "eq"
    && isParametricEqMode(blocks[state.selectedBlock].params);
  const std::string signature = editingEq
    ? "eq:parametric"
    : (state.paramTarget == UiParamTarget::Globals
        ? "globals:" + std::to_string(parameterPage_)
        : (state.selectedBlock < blocks.size()
            ? "block:" + blocks[state.selectedBlock].type + ":"
                + blocks[state.selectedBlock].params.value("mode", std::string{}) + ":"
                + std::to_string(parameterPage_)
            : "none"));

  for (auto& [key, view] : parameterViews_) {
    (void) key;
    if (view.layer) lv_obj_add_flag(view.layer, LV_OBJ_FLAG_HIDDEN);
  }

  const auto activate = [this](ParameterViewRefs& view) {
    activeParameterLayer_ = view.layer;
    parameterControls_ = view.controls;
    parameterTitleLabel_ = view.titleLabel;
    parameterBypassControl_ = view.bypassControl;
    eqGraph_ = view.eqGraph;
    eqBandButtons_ = view.eqBandButtons;
    eqSliders_ = view.eqSliders;
    eqEnabledButton_ = view.eqEnabledButton;
    eqEnabledContext_ = view.eqEnabledContext;
    eqResetContext_ = view.eqResetContext;
    eqSliderContexts_ = view.eqSliderContexts;
    lv_obj_remove_flag(view.layer, LV_OBJ_FLAG_HIDDEN);
  };
  if (auto existing = parameterViews_.find(signature); existing != parameterViews_.end()) {
    activate(existing->second);
    renderedParameterSignature_ = signature;
    syncParameterView(state);
    return;
  }

  lv_obj_t* viewLayer = lv_obj_create(parameterLayer_);
  lv_obj_remove_style_all(viewLayer);
  lv_obj_set_size(viewLayer, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(viewLayer, 0, 0);
  lv_obj_remove_flag(viewLayer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(viewLayer, LV_OBJ_FLAG_CLICKABLE);
  parameterControls_.clear();
  parameterTitleLabel_ = nullptr;
  parameterBypassControl_ = nullptr;
  eqGraph_ = nullptr;
  eqBandButtons_.fill(nullptr);
  eqSliders_.fill(nullptr);
  eqEnabledButton_ = nullptr;
  eqEnabledContext_ = nullptr;
  eqResetContext_ = nullptr;
  eqSliderContexts_.fill(nullptr);
  if (editingEq) {
    contextRegion_ = UiContextRegion::Parameters;
    renderParametricEqPanel(viewLayer, state, remember(state), &eqGraph_, &eqBandButtons_,
                            &eqEnabledButton_, &eqEnabledContext_, &eqResetContext_,
                            &eqSliders_, &eqSliderContexts_, &parameterBypassControl_);
  } else {
    contextRegion_ = UiContextRegion::Parameters;
    renderParameterPanel(viewLayer, state, remember(state),
                         &parameterControls_, &parameterBypassControl_, &parameterTitleLabel_);
  }
  contextRegion_ = UiContextRegion::None;
  ParameterViewRefs refs;
  refs.layer = viewLayer;
  refs.controls = parameterControls_;
  refs.titleLabel = parameterTitleLabel_;
  refs.bypassControl = parameterBypassControl_;
  refs.eqGraph = eqGraph_;
  refs.eqBandButtons = eqBandButtons_;
  refs.eqSliders = eqSliders_;
  refs.eqEnabledButton = eqEnabledButton_;
  refs.eqEnabledContext = eqEnabledContext_;
  refs.eqResetContext = eqResetContext_;
  refs.eqSliderContexts = eqSliderContexts_;
  auto [inserted, unused] = parameterViews_.emplace(signature, std::move(refs));
  (void) unused;
  activate(inserted->second);
  renderedParameterSignature_ = signature;
}

void LvglUi::rebuildDrawerView(UiState& state)
{
  if (!drawerLayer_) return;
  lv_obj_clean(drawerLayer_);
  contexts_.remove_if([](const UiEventContext& context) {
    return context.region == UiContextRegion::Drawer;
  });
  drawerCategoryButtons_.fill(nullptr);
  drawerAssetButtons_.clear();
  drawerAssetContexts_.clear();
  renderedAssetKeys_.clear();
  drawerAssetList_ = nullptr;
  drawerInstructionLabel_ = nullptr;
  contextRegion_ = UiContextRegion::Drawer;
  renderBlockDrawer(drawerLayer_, state);
  contextRegion_ = UiContextRegion::None;
}

void LvglUi::syncDrawerAssets(UiState& state)
{
  if (!drawerAssetList_) {
    rebuildDrawerView(state);
    return;
  }

  const auto oldButtons = drawerAssetButtons_;
  const auto oldContexts = drawerAssetContexts_;
  const auto oldKeys = renderedAssetKeys_;
  std::vector<bool> used(oldKeys.size(), false);
  std::vector<lv_obj_t*> buttons(state.assets.size(), nullptr);
  std::vector<UiEventContext*> contexts(state.assets.size(), nullptr);
  std::vector<std::string> keys;
  keys.reserve(state.assets.size());

  for (std::size_t i = 0; i < state.assets.size(); ++i) {
    const auto key = assetRenderKey(state.assets[i]);
    keys.push_back(key);
    for (std::size_t j = 0; j < oldKeys.size(); ++j) {
      if (!used[j] && oldKeys[j] == key) {
        used[j] = true;
        buttons[i] = oldButtons[j];
        contexts[i] = oldContexts[j];
        break;
      }
    }
    if (buttons[i]) continue;

    lv_obj_t* item = button(drawerAssetList_, state.assets[i].name);
    lv_obj_set_width(item, kBlockDrawerContentWidth - 14);
    lv_obj_set_height(item, kDrawerAssetButtonHeight);
    lv_obj_set_style_min_height(item, kDrawerAssetButtonHeight, 0);
    styleSurface(item, panel);
    contextRegion_ = UiContextRegion::Drawer;
    auto* context = remember(state, i);
    contextRegion_ = UiContextRegion::None;
    context->controlledObject = drawerAssetList_;
    lv_obj_add_event_cb(item, onAssetPressed, LV_EVENT_PRESSED, context);
    lv_obj_add_event_cb(item, onAssetLongPressed, LV_EVENT_LONG_PRESSED, context);
    lv_obj_add_event_cb(item, onAssetPressing, LV_EVENT_PRESSING, context);
    lv_obj_add_event_cb(item, onAssetReleased, LV_EVENT_RELEASED, context);
    lv_obj_add_event_cb(item, onAssetPressLost, LV_EVENT_PRESS_LOST, context);
    lv_obj_add_event_cb(item, onAssetClicked, LV_EVENT_CLICKED, context);
    buttons[i] = item;
    contexts[i] = context;
  }

  for (std::size_t j = 0; j < oldButtons.size(); ++j) {
    if (used[j]) continue;
    lv_obj_delete(oldButtons[j]);
    UiEventContext* removed = oldContexts[j];
    contexts_.remove_if([removed](const UiEventContext& context) { return &context == removed; });
  }

  for (std::size_t i = 0; i < buttons.size(); ++i) {
    lv_obj_move_to_index(buttons[i], static_cast<int32_t>(i));
    lv_label_set_text(lv_obj_get_child(buttons[i], 0), state.assets[i].name.c_str());
    contexts[i]->index = i;
    contexts[i]->controlledObject = drawerAssetList_;
  }
  drawerAssetButtons_ = std::move(buttons);
  drawerAssetContexts_ = std::move(contexts);
  renderedAssetKeys_ = std::move(keys);
}

void LvglUi::syncDrawerView(UiState& state)
{
  for (std::size_t i = 0; i < drawerCategoryButtons_.size(); ++i) {
    lv_obj_t* category = drawerCategoryButtons_[i];
    if (!category) continue;
    const bool selected = state.categoryFilter == kDrawerFilters[i].second;
    styleSurface(category, selected ? 0x333333 : 0x1b1b1b);
    lv_obj_set_style_text_color(lv_obj_get_child(category, 0),
                                lv_color_hex(selected ? accent : text), 0);
  }

  const bool chainFull = state.bank.presets[state.activePreset].blocks.size() >= kMaxEffectBlocks;
  if (drawerInstructionLabel_) {
    lv_label_set_text(drawerInstructionLabel_, chainFull
      ? "Chain full - delete a block to add" : "Tap to add - hold to drag");
    lv_obj_set_style_text_color(drawerInstructionLabel_,
                                lv_color_hex(chainFull ? 0xf97373 : muted), 0);
  }
  for (std::size_t i = 0; i < drawerAssetButtons_.size() && i < state.assets.size(); ++i) {
    lv_obj_t* item = drawerAssetButtons_[i];
    const bool visible = state.categoryFilter == "all" || state.assets[i].type == state.categoryFilter;
    if (visible) lv_obj_remove_flag(item, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
    if (chainFull) lv_obj_add_state(item, LV_STATE_DISABLED);
    else lv_obj_remove_state(item, LV_STATE_DISABLED);
  }
  if (drawerAssetList_) {
    lv_obj_update_layout(drawerAssetList_);
    lv_obj_scroll_to_y(drawerAssetList_, state.assetScrollOffset, LV_ANIM_OFF);
    state.assetScrollOffset = lv_obj_get_scroll_y(drawerAssetList_);
  }
}

void LvglUi::syncParameterView(UiState& state)
{
  if (state.mode != UiMode::Edit || !state.paramDrawerOpen) {
    resetParameterPage();
    return;
  }

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  const bool editingEq = state.paramTarget == UiParamTarget::Block
    && state.selectedBlock < blocks.size() && blocks[state.selectedBlock].type == "eq"
    && isParametricEqMode(blocks[state.selectedBlock].params);
  const std::string signature = editingEq
    ? "eq:parametric"
    : (state.paramTarget == UiParamTarget::Globals
        ? "globals:" + std::to_string(parameterPage_)
        : (state.selectedBlock < blocks.size()
            ? "block:" + blocks[state.selectedBlock].type + ":"
                + blocks[state.selectedBlock].params.value("mode", std::string{}) + ":"
                + std::to_string(parameterPage_)
            : "none"));
  if (signature != renderedParameterSignature_) {
    rebuildParameterView(state);
    return;
  }

  if (parameterBypassControl_ && state.paramTarget == UiParamTarget::Block
      && state.selectedBlock < blocks.size()) {
    refreshBypassControlVisual(parameterBypassControl_, !blocks[state.selectedBlock].enabled);
  }

  if (!editingEq) {
    if (parameterTitleLabel_) {
      if (state.paramTarget == UiParamTarget::Globals) {
        lv_label_set_text(parameterTitleLabel_, "Global");
      } else if (state.selectedBlock < blocks.size()) {
        const auto title = blocks[state.selectedBlock].label + "  /  " + blocks[state.selectedBlock].assetName;
        lv_label_set_text(parameterTitleLabel_, title.c_str());
      }
    }
    const auto controls = ardor::parameterPage(state, parameterPage_);
    if (controls.size() != parameterControls_.size()) {
      rebuildParameterView(state);
      return;
    }
    for (std::size_t i = 0; i < controls.size(); ++i) {
      refreshParameterSliderVisual(parameterControls_[i], controls[i], isParameterFocused(controls[i].key));
    }
    return;
  }

  const auto params = selectedParametricEqParams(state);
  refreshEqGraphCurve(eqGraph_, params);
  auto* graphVisual = eqGraph_ ? static_cast<EqGraphVisual*>(lv_obj_get_user_data(eqGraph_)) : nullptr;
  for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
    const bool selected = selectedEqBand_ == i;
    if (graphVisual && graphVisual->nodes[i]) {
      lv_obj_set_style_opa(graphVisual->nodes[i], params.bands[i].enabled ? LV_OPA_COVER : LV_OPA_50, 0);
      lv_obj_set_style_border_width(graphVisual->nodes[i], selected ? 2 : 0, 0);
      if (selected) lv_obj_set_style_border_color(graphVisual->nodes[i], lv_color_hex(text), 0);
    }
    if (eqBandButtons_[i]) {
      styleSurface(eqBandButtons_[i], selected ? eqBandColors[i] : 0x171717);
      lv_obj_set_style_text_color(lv_obj_get_child(eqBandButtons_[i], 0),
                                  lv_color_hex(selected ? bg : text), 0);
    }
  }

  const auto& band = params.bands[selectedEqBand_];
  if (eqEnabledButton_) {
    lv_label_set_text(lv_obj_get_child(eqEnabledButton_, 0), band.enabled ? "Band On" : "Band Off");
    styleSurface(eqEnabledButton_, band.enabled ? 0x25442a : 0x3a2020);
    lv_obj_set_style_text_color(lv_obj_get_child(eqEnabledButton_, 0),
                                lv_color_hex(band.enabled ? accent : 0xf97373), 0);
  }
  if (eqEnabledContext_) eqEnabledContext_->index = selectedEqBand_;
  if (eqResetContext_) eqResetContext_->index = selectedEqBand_;
  constexpr std::array<EqBandField, 3> fields = {
    EqBandField::Frequency, EqBandField::Q, EqBandField::Gain,
  };
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (eqSliderContexts_[i]) {
      eqSliderContexts_[i]->index = selectedEqBand_;
      eqSliderContexts_[i]->controlledObject = eqGraph_;
    }
    if (eqSliders_[i]) {
      refreshParameterSliderVisual(eqSliders_[i], eqSliderControl(fields[i], band),
                                   isEqBandFieldFocused(fields[i]));
    }
  }
}

void LvglUi::syncPersistentViews(UiState& state)
{
  if (!viewsInitialized_) return;
  if (state.mode == UiMode::Preset) {
    lv_obj_remove_flag(presetLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(editLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tunerLayer_, LV_OBJ_FLAG_HIDDEN);
  } else if (state.mode == UiMode::Edit) {
    lv_obj_add_flag(presetLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(editLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tunerLayer_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(presetLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(editLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(tunerLayer_, LV_OBJ_FLAG_HIDDEN);
  }

  const bool showParameters = state.mode == UiMode::Edit && state.paramDrawerOpen;
  if (showParameters) lv_obj_remove_flag(parameterLayer_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(parameterLayer_, LV_OBJ_FLAG_HIDDEN);
  const bool showDrawer = state.mode == UiMode::Edit && state.blockDrawerOpen;
  if (showDrawer) lv_obj_remove_flag(drawerLayer_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(drawerLayer_, LV_OBJ_FLAG_HIDDEN);
  if (state.mode == UiMode::Tuner) lv_obj_add_flag(statusLayer_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(statusLayer_, LV_OBJ_FLAG_HIDDEN);

  syncTunerView(state);

  if (presetBankLabel_) lv_label_set_text(presetBankLabel_, state.bank.name.c_str());
  if (masterVolumeLabel_) {
    const auto value = "Master " + std::to_string(state.masterVolume) + "%";
    lv_label_set_text(masterVolumeLabel_, value.c_str());
  }
  if (bankDownButton_) {
    if (state.activeBank == kMinBank) lv_obj_add_state(bankDownButton_, LV_STATE_DISABLED);
    else lv_obj_remove_state(bankDownButton_, LV_STATE_DISABLED);
  }
  if (bankUpButton_) {
    if (state.activeBank == kMaxBank) lv_obj_add_state(bankUpButton_, LV_STATE_DISABLED);
    else lv_obj_remove_state(bankUpButton_, LV_STATE_DISABLED);
  }
  for (std::size_t i = 0; i < presetCardButtons_.size(); ++i) {
    if (!presetCardButtons_[i]) continue;
    if (i >= state.bank.presets.size()) {
      lv_obj_add_flag(presetCardButtons_[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_label_set_text(presetCardLabels_[i], state.bank.presets[i].name.c_str());
    lv_obj_set_style_text_color(presetCardLabels_[i], lv_color_hex(i == state.activePreset ? accent : text), 0);
    if (i == state.activePreset) lv_obj_remove_flag(presetIndicators_[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(presetIndicators_[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(presetCardButtons_[i], LV_OBJ_FLAG_HIDDEN);
  }
  if (editPresetLabel_) {
    lv_label_set_text(editPresetLabel_, state.bank.presets[state.activePreset].name.c_str());
  }
  if (saveButtonLabel_) {
    lv_label_set_text(saveButtonLabel_, state.dirty ? "Save*" : "Save");
    lv_obj_set_style_text_color(saveButtonLabel_, lv_color_hex(state.dirty ? accent : text), 0);
  }

  if (telemetryLabel_) {
    char telemetry[96]{};
    formatTelemetryLabel(state, telemetry, sizeof(telemetry));
    lv_label_set_text(telemetryLabel_, telemetry);
    lv_obj_set_style_text_color(telemetryLabel_, lv_color_hex(telemetryColor(state)), 0);
  }
  if (statusMessageLabel_) {
    lv_label_set_text(statusMessageLabel_, state.statusMessage.c_str());
    lv_obj_set_style_text_color(statusMessageLabel_,
                                lv_color_hex(state.statusIsError ? 0xf97373 : accent), 0);
    lv_obj_align(statusMessageLabel_, LV_ALIGN_RIGHT_MID,
                 state.blockEditUndo.has_value() ? -132 : -18, 0);
    if (state.statusMessage.empty()) lv_obj_add_flag(statusMessageLabel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(statusMessageLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  if (undoButton_) {
    if (state.blockEditUndo.has_value()) lv_obj_remove_flag(undoButton_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(undoButton_, LV_OBJ_FLAG_HIDDEN);
  }
  syncBlockingOverlays(state);
}

void LvglUi::syncBlockingOverlays(const UiState& state)
{
  if (previewOverlay_) {
    if (previewIsSynchronized(state)) lv_obj_add_flag(previewOverlay_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(previewOverlay_, LV_OBJ_FLAG_HIDDEN);
  }
  if (navigationOverlay_) {
    if (state.navigationPrompt.has_value()) lv_obj_remove_flag(navigationOverlay_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(navigationOverlay_, LV_OBJ_FLAG_HIDDEN);
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
  if (!highlightedBlockId_.empty() && std::chrono::steady_clock::now() >= highlightUntil_) {
    highlightedBlockId_.clear();
    invalidate(UiChange::Chain);
  }
  if (!viewsInitialized_ || root != lv_obj_get_parent(canvas_)) {
    build(root, state);
    return;
  }

  UiChange changes = pendingChanges_;
  const auto add = [&changes](UiChange change) { changes = changes | change; };
  if (state.revisions.navigation != renderedRevisions_.navigation) add(UiChange::Navigation);
  if (state.revisions.header != renderedRevisions_.header) add(UiChange::Header);
  if (state.revisions.presets != renderedRevisions_.presets) add(UiChange::Presets);
  if (state.revisions.chain != renderedRevisions_.chain) add(UiChange::Chain);
  if (state.revisions.parameters != renderedRevisions_.parameters) add(UiChange::Parameters);
  if (state.revisions.assets != renderedRevisions_.assets) add(UiChange::Assets);
  if (state.revisions.drawers != renderedRevisions_.drawers) add(UiChange::Drawers);
  if (state.revisions.status != renderedRevisions_.status) add(UiChange::Status);
  if (state.revisions.telemetry != renderedRevisions_.telemetry) add(UiChange::Telemetry);

  // Text-only regions are always safe while an input device owns a widget.
  if (hasUiChange(changes, UiChange::Status) || hasUiChange(changes, UiChange::Telemetry)) {
    syncPersistentViews(state);
    renderedRevisions_.status = state.revisions.status;
    renderedRevisions_.telemetry = state.revisions.telemetry;
  }
  // Blocking overlays must become visible even while a slider or drag owns an
  // input device. The control loop forces this retained state to the display
  // before it starts synchronous engine preparation.
  syncBlockingOverlays(state);
  if (activeInteractions_ > 0) {
    pendingChanges_ = changes;
    return;
  }

  const bool presetChanged = state.activePreset != renderedPreset_ || state.activeBank != renderedBank_;
  if (presetChanged || hasUiChange(changes, UiChange::Presets)) {
    syncChainCards(state);
    syncParameterView(state);
    if (hasUiChange(changes, UiChange::Assets)) syncDrawerAssets(state);
    syncDrawerView(state);
  } else {
    if (hasUiChange(changes, UiChange::Chain)) syncChainCards(state);
    if (hasUiChange(changes, UiChange::Parameters)) syncParameterView(state);
    if (hasUiChange(changes, UiChange::Assets)) {
      syncDrawerAssets(state);
      syncDrawerView(state);
    } else if (hasUiChange(changes, UiChange::Drawers)) {
      syncDrawerView(state);
    }
  }
  syncPersistentViews(state);
  renderedRevisions_ = state.revisions;
  renderedBank_ = state.activeBank;
  renderedPreset_ = state.activePreset;
  pendingChanges_ = UiChange::None;
}

void LvglUi::invalidate(UiChange changes)
{
  pendingChanges_ = pendingChanges_ | changes;
}

void LvglUi::endInteraction(bool requestUiRebuild)
{
  if (activeInteractions_ > 0) {
    --activeInteractions_;
  }
  if (requestUiRebuild) {
    invalidate(UiChange::Parameters);
  }
}

void renderStatusBar(LvglUi* ui, lv_obj_t* root, UiState& state,
                     lv_obj_t** telemetryOut, lv_obj_t** messageOut, lv_obj_t** undoOut)
{
  lv_obj_t* bar = lv_obj_create(root);
  lv_obj_set_size(bar, kDesignWidth, kStatusBarHeight);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(bar, 0x111111);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(0x343434), 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);

  char status[96]{};
  formatTelemetryLabel(state, status, sizeof(status));
  lv_obj_t* telemetryLabel = label(bar, status, LV_ALIGN_LEFT_MID, 18, 0,
                                   &ardor_font_open_sans_regular_18, telemetryColor(state));
  lv_obj_set_width(telemetryLabel, 560);
  lv_label_set_long_mode(telemetryLabel, LV_LABEL_LONG_CLIP);
  if (telemetryOut) *telemetryOut = telemetryLabel;

  const bool canUndo = state.blockEditUndo.has_value();
  lv_obj_t* message = label(bar, state.statusMessage, LV_ALIGN_RIGHT_MID,
                            canUndo ? -132 : -18, 0,
                            &ardor_font_open_sans_regular_18,
                            state.statusIsError ? 0xf97373 : accent);
  lv_obj_set_width(message, 620);
  lv_label_set_long_mode(message, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_RIGHT, 0);
  if (state.statusMessage.empty()) lv_obj_add_flag(message, LV_OBJ_FLAG_HIDDEN);
  if (messageOut) *messageOut = message;

  lv_obj_t* undo = button(bar, "Undo");
  lv_obj_set_size(undo, 108, 40);
  lv_obj_align(undo, LV_ALIGN_RIGHT_MID, -8, 0);
  styleSurface(undo, 0x25442a);
  lv_obj_set_style_text_color(lv_obj_get_child(undo, 0), lv_color_hex(accent), 0);
  lv_obj_add_event_cb(undo, onUndoBlockEdit, LV_EVENT_CLICKED, ui->remember(state));
  if (!canUndo) lv_obj_add_flag(undo, LV_OBJ_FLAG_HIDDEN);
  if (undoOut) *undoOut = undo;
}

void LvglUi::renderPresetMode(lv_obj_t* root, UiState& state)
{
  presetBankLabel_ = label(root, state.bank.name, LV_ALIGN_TOP_MID, 0, 28,
                           &ardor_font_open_sans_semibold_28);
  masterVolumeLabel_ = label(root, "Master " + std::to_string(state.masterVolume) + "%",
                             LV_ALIGN_TOP_LEFT, 28, 28, &ardor_font_open_sans_regular_18, muted);

  lv_obj_t* tuner = button(root, "Tuner");
  lv_obj_set_size(tuner, kHeaderTunerButtonWidth, kHeaderButtonHeight);
  lv_obj_align(tuner, LV_ALIGN_TOP_LEFT, 132, 20);
  styleSurface(tuner, 0x25442a);
  lv_obj_set_style_text_color(lv_obj_get_child(tuner, 0), lv_color_hex(accent), 0);
  lv_obj_add_event_cb(tuner, onTunerModeClicked, LV_EVENT_PRESSED, remember(state));

  lv_obj_t* edit = button(root, "Edit");
  lv_obj_set_size(edit, kHeaderBlocksButtonWidth, kHeaderButtonHeight);
  lv_obj_align(edit, LV_ALIGN_TOP_RIGHT, -28, 20);
  // Opening an editor is safe on press and does not depend on the release
  // landing on a small target after a finger has shifted on the touchscreen.
  lv_obj_add_event_cb(edit, onEditModeClicked, LV_EVENT_PRESSED, remember(state));

  lv_obj_t* bankDown = button(root, "Bank -");
  bankDownButton_ = bankDown;
  lv_obj_set_size(bankDown, 144, 52);
  lv_obj_align(bankDown, LV_ALIGN_TOP_MID, -300, 20);
  if (state.activeBank == kMinBank) {
    lv_obj_add_state(bankDown, LV_STATE_DISABLED);
  }
  lv_obj_add_event_cb(bankDown, onBankDownClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* bankUp = button(root, "Bank +");
  bankUpButton_ = bankUp;
  lv_obj_set_size(bankUp, 144, 52);
  lv_obj_align(bankUp, LV_ALIGN_TOP_MID, 300, 20);
  if (state.activeBank == kMaxBank) {
    lv_obj_add_state(bankUp, LV_STATE_DISABLED);
  }
  lv_obj_add_event_cb(bankUp, onBankUpClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* grid = lv_obj_create(root);
  lv_obj_set_size(grid, 1200, 540);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -60);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);

  static int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(grid, cols, rows);

  for (std::size_t i = 0; i < presetCardButtons_.size(); ++i) {
    const bool populated = i < state.bank.presets.size();
    lv_obj_t* preset = button(grid, populated ? state.bank.presets[i].name : "");
    presetCardButtons_[i] = preset;
    lv_obj_set_grid_cell(preset, LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i % 2), 1,
                         LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i / 2), 1);
    styleSurface(preset, panel);
    lv_obj_t* presetName = lv_obj_get_child(preset, 0);
    presetCardLabels_[i] = presetName;
    lv_obj_set_style_text_color(presetName, lv_color_hex(i == state.activePreset ? accent : text), 0);
    lv_obj_set_style_text_font(presetName, &ardor_font_open_sans_semibold_28, 0);
    // Use the ample card space for the preset name: 28 px at 2x scale gives
    // a 56 px label, more than triple the standard 18 px button text.
    lv_obj_set_style_transform_pivot_x(presetName, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(presetName, LV_PCT(50), 0);
    lv_obj_set_style_transform_scale(presetName, 2 * LV_SCALE_NONE, 0);
    lv_obj_t* indicator = lv_obj_create(preset);
    lv_obj_set_size(indicator, 4, LV_PCT(100));
    lv_obj_align(indicator, LV_ALIGN_LEFT_MID, 0, 0);
    styleSurface(indicator, accent);
    lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
    presetIndicators_[i] = indicator;
    if (!populated || i != state.activePreset) lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
    if (!populated) lv_obj_add_flag(preset, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(preset, onPresetClicked, LV_EVENT_CLICKED, remember(state, i));
  }
}

void LvglUi::renderTunerMode(lv_obj_t* root, UiState& state)
{
  lv_obj_t* exit = button(root, "Exit");
  lv_obj_set_size(exit, 120, kHeaderButtonHeight);
  lv_obj_align(exit, LV_ALIGN_TOP_LEFT, 28, 20);
  styleSurface(exit, 0x343434);
  lv_obj_add_event_cb(exit, onPresetModeClicked, LV_EVENT_PRESSED, remember(state));

  label(root, "TUNER", LV_ALIGN_TOP_MID, 0, 24,
        &ardor_font_open_sans_semibold_28, accent);
  label(root, "OUTPUT MUTED", LV_ALIGN_TOP_RIGHT, -34, 30,
        &ardor_font_open_sans_semibold_22, danger);

  tunerNoteLabel_ = label(root, "--", LV_ALIGN_TOP_MID, 0, 128,
                          &ardor_font_open_sans_semibold_28, text);
  lv_obj_set_width(tunerNoteLabel_, 360);
  lv_obj_set_style_text_align(tunerNoteLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_transform_pivot_x(tunerNoteLabel_, LV_PCT(50), 0);
  lv_obj_set_style_transform_pivot_y(tunerNoteLabel_, LV_PCT(50), 0);
  lv_obj_set_style_transform_scale(tunerNoteLabel_, 4 * LV_SCALE_NONE, 0);

  tunerFrequencyLabel_ = label(root, "Play a string", LV_ALIGN_TOP_MID, 0, 286,
                               &ardor_font_open_sans_semibold_22, muted);
  tunerCentsLabel_ = label(root, "", LV_ALIGN_TOP_MID, 0, 332,
                           &ardor_font_open_sans_semibold_28, muted);

  lv_obj_t* meter = lv_obj_create(root);
  lv_obj_set_size(meter, 760, 112);
  lv_obj_set_pos(meter, 260, 398);
  lv_obj_remove_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(meter, 0x151515);
  lv_obj_set_style_border_color(meter, lv_color_hex(0x383838), 0);
  lv_obj_set_style_border_width(meter, 1, 0);
  for (int tick = -5; tick <= 5; ++tick) {
    lv_obj_t* line = lv_obj_create(meter);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, tick == 0 ? 4 : 2, tick == 0 ? 76 : (tick % 5 == 0 ? 54 : 34));
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(tick == 0 ? accent : 0x737373), 0);
    lv_obj_align(line, LV_ALIGN_CENTER, tick * 70, 0);
  }
  label(meter, "-50", LV_ALIGN_BOTTOM_LEFT, 12, -7,
        &ardor_font_open_sans_regular_18, muted);
  label(meter, "0", LV_ALIGN_BOTTOM_MID, 0, -7,
        &ardor_font_open_sans_regular_18, muted);
  label(meter, "+50", LV_ALIGN_BOTTOM_RIGHT, -12, -7,
        &ardor_font_open_sans_regular_18, muted);

  tunerNeedle_ = lv_obj_create(root);
  lv_obj_remove_style_all(tunerNeedle_);
  lv_obj_set_size(tunerNeedle_, 8, 92);
  lv_obj_set_style_radius(tunerNeedle_, 4, 0);
  lv_obj_set_style_bg_opa(tunerNeedle_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(tunerNeedle_, lv_color_hex(accent), 0);

  tunerGuidanceLabel_ = label(root, "PLAY A STRING", LV_ALIGN_TOP_MID, 0, 536,
                              &ardor_font_open_sans_semibold_28, muted);
  label(root, "Press any footswitch to exit", LV_ALIGN_BOTTOM_MID, 0, -30,
        &ardor_font_open_sans_semibold_22, muted);
  syncTunerView(state);
}

void LvglUi::syncTunerView(UiState& state)
{
  if (!tunerNoteLabel_ || !tunerNeedle_) return;
  const auto& tuner = state.tuner;
  if (!tuner.signalDetected) {
    lv_label_set_text(tunerNoteLabel_, "--");
    lv_label_set_text(tunerFrequencyLabel_, "Play a string");
    lv_label_set_text(tunerCentsLabel_, "");
    lv_label_set_text(tunerGuidanceLabel_, "PLAY A STRING");
    lv_obj_set_style_text_color(tunerGuidanceLabel_, lv_color_hex(muted), 0);
    lv_obj_add_flag(tunerNeedle_, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  char note[16]{};
  char frequency[32]{};
  char cents[32]{};
  std::snprintf(note, sizeof(note), "%s%d", tuner.note.c_str(), tuner.octave);
  std::snprintf(frequency, sizeof(frequency), "%.1f Hz", tuner.frequencyHz);
  std::snprintf(cents, sizeof(cents), "%+.1f cents", tuner.cents);
  lv_label_set_text(tunerNoteLabel_, note);
  lv_label_set_text(tunerFrequencyLabel_, frequency);
  lv_label_set_text(tunerCentsLabel_, cents);

  const float absoluteCents = std::fabs(tuner.cents);
  const int color = absoluteCents <= 3.0f ? accent : (absoluteCents <= 10.0f ? warning : danger);
  const char* guidance = absoluteCents <= 3.0f ? "IN TUNE"
    : (tuner.cents < 0.0f ? "FLAT  -  TUNE UP" : "SHARP  -  TUNE DOWN");
  lv_label_set_text(tunerGuidanceLabel_, guidance);
  lv_obj_set_style_text_color(tunerGuidanceLabel_, lv_color_hex(color), 0);
  lv_obj_set_style_bg_color(tunerNeedle_, lv_color_hex(color), 0);
  const int needleX = 636 + static_cast<int>(std::lround(std::clamp(tuner.cents, -50.0f, 50.0f) * 7.0f));
  lv_obj_set_pos(tunerNeedle_, needleX, 408);
  lv_obj_remove_flag(tunerNeedle_, LV_OBJ_FLAG_HIDDEN);
}

void LvglUi::renderEditMode(lv_obj_t* root, UiState& state)
{
  editPresetLabel_ = label(root, state.bank.presets[state.activePreset].name,
                           LV_ALIGN_TOP_MID, 0, 24, &ardor_font_open_sans_semibold_28);

  lv_obj_t* presets = button(root, "Presets");
  lv_obj_set_size(presets, 164, kHeaderButtonHeight);
  lv_obj_align(presets, LV_ALIGN_TOP_LEFT, 28, 20);
  lv_obj_add_event_cb(presets, onPresetModeClicked, LV_EVENT_PRESSED, remember(state));

  lv_obj_t* globalButton = button(root, "Global");
  lv_obj_set_size(globalButton, 144, kHeaderButtonHeight);
  lv_obj_align(globalButton, LV_ALIGN_TOP_RIGHT, -372, 20);
  lv_obj_add_event_cb(globalButton, onGlobalParamsClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* save = button(root, state.dirty ? "Save*" : "Save");
  saveButtonLabel_ = lv_obj_get_child(save, 0);
  lv_obj_set_size(save, 128, kHeaderButtonHeight);
  lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -220, 20);
  lv_obj_set_style_text_color(lv_obj_get_child(save, 0), lv_color_hex(state.dirty ? accent : text), 0);
  lv_obj_add_event_cb(save, onSaveClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* blocksButton = button(root, "Blocks");
  lv_obj_set_size(blocksButton, kHeaderBlocksButtonWidth, kHeaderButtonHeight);
  lv_obj_align(blocksButton, LV_ALIGN_TOP_RIGHT, -28, 20);
  lv_obj_add_event_cb(blocksButton, onOpenBlockDrawer, LV_EVENT_PRESSED, remember(state));

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  const bool editingEq = state.paramDrawerOpen && state.paramTarget == UiParamTarget::Block
    && state.selectedBlock < blocks.size() && blocks[state.selectedBlock].type == "eq"
    && isParametricEqMode(blocks[state.selectedBlock].params);
  if (editingEq) {
    // The retained parameter layer owns the EQ editor.
  }

  lv_obj_t* chain = lv_obj_create(root);
  lv_obj_set_size(chain, 1240, kChainHeight);
  lv_obj_align(chain, LV_ALIGN_TOP_MID, 0, kChainTop);
  lv_obj_remove_flag(chain, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(chain, bg);
  // Card and connector coordinates share the fixed design grid. Explicitly
  // remove theme padding so the cards cannot drift away from the wrap line.
  lv_obj_set_style_pad_all(chain, 0, 0);
  chainWrapConnector_ = renderChainWrapConnector(root);
  if (blocks.size() <= kChainColumns) {
    lv_obj_add_flag(chainWrapConnector_, LV_OBJ_FLAG_HIDDEN);
  }
  label(root, "Input", LV_ALIGN_TOP_LEFT, 28, 88, &ardor_font_open_sans_regular_18, muted);
  label(root, "Output", LV_ALIGN_TOP_RIGHT, -28, 408, &ardor_font_open_sans_regular_18, muted);

  renderedBlockIds_.clear();
  for (std::size_t i = 0; i < kMaxEffectBlocks; ++i) {
    const bool populated = i < blocks.size();
    const UiBlock* block = populated ? &blocks[i] : nullptr;
    std::string category = populated ? block->label : std::string{};
    std::transform(category.begin(), category.end(), category.begin(), [](unsigned char character) {
      return static_cast<char>(std::toupper(character));
    });

    lv_obj_t* object = button(chain, "");
    lv_obj_set_size(object, kChainTileWidth, kChainTileHeight);
    lv_obj_set_pos(object, 14 + static_cast<int>(i % kChainColumns) * kChainSlotWidth,
                   kChainTileTop + static_cast<int>(i / kChainColumns) * kChainRowHeight);
    styleSurface(object, !populated || block->enabled ? panel : 0x171717);
    // Card content uses three fixed text rows and a separate handle column.
    // Explicit bounds keep LVGL's independent label alignment from allowing
    // long names or the bypass state to paint over neighboring content.
    lv_obj_set_style_pad_all(object, 0, 0);
    if (populated && !block->enabled) {
      lv_obj_set_style_opa(object, LV_OPA_70, 0);
    }
    if (populated && isBlockHighlighted(block->id)) {
      lv_obj_set_style_border_color(object, lv_color_hex(accent), 0);
      lv_obj_set_style_border_width(object, 3, 0);
    }
    const bool selected = populated && state.paramTarget == UiParamTarget::Block && state.selectedBlock == i;
    lv_obj_t* categoryLabel = label(object, category, LV_ALIGN_TOP_LEFT, kChainTextX, 7,
                                    &ardor_font_open_sans_regular_18, muted);
    lv_obj_set_width(categoryLabel, kChainTextWidth);
    lv_label_set_long_mode(categoryLabel, LV_LABEL_LONG_CLIP);
    lv_obj_t* assetName = label(object, populated ? block->assetName : "", LV_ALIGN_TOP_LEFT, kChainTextX, 32,
                                &ardor_font_open_sans_semibold_22);
    lv_obj_set_width(assetName, kChainTextWidth);
    lv_label_set_long_mode(assetName, LV_LABEL_LONG_CLIP);
    lv_obj_t* bypassed = label(object, "BYPASSED", LV_ALIGN_TOP_LEFT, kChainTextX, 67,
                               &ardor_font_open_sans_regular_18, 0xf97373);
    lv_obj_set_width(bypassed, kChainTextWidth);
    lv_label_set_long_mode(bypassed, LV_LABEL_LONG_CLIP);
    if (!populated || block->enabled) lv_obj_add_flag(bypassed, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* indicator = lv_obj_create(object);
    lv_obj_set_size(indicator, 5, 5);
    lv_obj_align(indicator, LV_ALIGN_BOTTOM_MID, 0, -7);
    styleSurface(indicator, accent);
    lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
    if (!selected) lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
    auto* clickContext = remember(state, i);
    lv_obj_add_event_cb(object, onBlockClicked, LV_EVENT_CLICKED, clickContext);

    lv_obj_t* dragHandle = button(object, "|||");
    lv_obj_set_size(dragHandle, kChainHandleWidth, 56);
    lv_obj_align(dragHandle, LV_ALIGN_RIGHT_MID, -8, 0);
    styleSurface(dragHandle, 0x333333);
    lv_obj_set_style_text_color(lv_obj_get_child(dragHandle, 0), lv_color_hex(muted), 0);
    auto* dragContext = remember(state, i);
    dragContext->controlledObject = object;
    lv_obj_add_event_cb(dragHandle, onBlockPressed, LV_EVENT_PRESSED, dragContext);
    lv_obj_add_event_cb(dragHandle, onBlockPressing, LV_EVENT_PRESSING, dragContext);
    lv_obj_add_event_cb(dragHandle, onBlockReleased, LV_EVENT_RELEASED, dragContext);
    lv_obj_add_event_cb(dragHandle, onBlockPressLost, LV_EVENT_PRESS_LOST, dragContext);

    chainCards_[i] = object;
    chainCategoryLabels_[i] = categoryLabel;
    chainAssetLabels_[i] = assetName;
    chainBypassLabels_[i] = bypassed;
    chainSelectionIndicators_[i] = indicator;
    chainClickContexts_[i] = clickContext;
    chainDragContexts_[i] = dragContext;
    if (populated) renderedBlockIds_.push_back(block->id);
    else lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  }

  label(root, "Tap a block to edit  |  Drag the handle to reorder",
        LV_ALIGN_TOP_LEFT, 28, 390, &ardor_font_open_sans_regular_18, muted);

}

void LvglUi::renderBlockDrawer(lv_obj_t* root, UiState& state)
{
  lv_obj_t* scrim = lv_obj_create(root);
  lv_obj_set_size(scrim, kDesignWidth - kBlockDrawerWidth, kDesignHeight - kStatusBarHeight);
  lv_obj_set_pos(scrim, 0, 0);
  lv_obj_set_style_bg_color(scrim, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(scrim, LV_OPA_40, 0);
  lv_obj_set_style_border_width(scrim, 0, 0);
  lv_obj_set_style_radius(scrim, 0, 0);
  lv_obj_remove_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scrim, onCloseBlockDrawer, LV_EVENT_PRESSED, remember(state));

  lv_obj_t* drawer = lv_obj_create(root);
  lv_obj_set_size(drawer, kBlockDrawerWidth, kDesignHeight - kStatusBarHeight);
  lv_obj_align(drawer, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(drawer, lv_color_hex(0x000000), 0);
  lv_obj_set_style_border_color(drawer, lv_color_hex(panel), 0);
  lv_obj_set_style_border_width(drawer, 1, 0);
  lv_obj_set_style_border_side(drawer, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_radius(drawer, 0, 0);
  lv_obj_set_style_pad_all(drawer, kBlockDrawerPadding, 0);
  // Content fits; the inner list scrolls on its own. A scrollable drawer would
  // steal taps on the close button on a jittery finger touch.
  lv_obj_remove_flag(drawer, LV_OBJ_FLAG_SCROLLABLE);

  label(drawer, "Blocks", LV_ALIGN_TOP_LEFT, 0, 0, &ardor_font_open_sans_semibold_22);
  lv_obj_t* close = button(drawer, "Close");
  lv_obj_set_size(close, 100, 56);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -4);
  lv_obj_add_event_cb(close, onCloseBlockDrawer, LV_EVENT_PRESSED, remember(state));

  lv_obj_t* filterRow = lv_obj_create(drawer);
  lv_obj_set_size(filterRow, kBlockDrawerContentWidth, kDrawerCategoryHeight);
  lv_obj_align(filterRow, LV_ALIGN_TOP_LEFT, 0, kDrawerCategoryTop);
  lv_obj_set_style_bg_opa(filterRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(filterRow, 0, 0);
  lv_obj_set_style_pad_all(filterRow, 0, 0);
  lv_obj_remove_flag(filterRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(filterRow, LV_SCROLLBAR_MODE_OFF);

  static int32_t categoryColumns[] = {
    kCategoryButtonWidth, kCategoryButtonWidth, kCategoryButtonWidth, kCategoryButtonWidth,
    LV_GRID_TEMPLATE_LAST,
  };
  static int32_t categoryRows[] = {
    kCategoryButtonHeight, kCategoryButtonHeight, LV_GRID_TEMPLATE_LAST,
  };
  lv_obj_set_style_pad_column(filterRow, kCategoryButtonGap, 0);
  lv_obj_set_style_pad_row(filterRow, kCategoryButtonGap, 0);
  lv_obj_set_grid_dsc_array(filterRow, categoryColumns, categoryRows);

  for (std::size_t i = 0; i < kDrawerFilters.size(); ++i) {
    const auto& [name, filter] = kDrawerFilters[i];
    lv_obj_t* filterButton = button(filterRow, name);
    lv_obj_set_grid_cell(filterButton, LV_GRID_ALIGN_STRETCH,
                         static_cast<int32_t>(i % kCategoryColumns), 1,
                         LV_GRID_ALIGN_STRETCH,
                         static_cast<int32_t>(i / kCategoryColumns), 1);
    styleSurface(filterButton, state.categoryFilter == filter ? 0x333333 : 0x1b1b1b);
    lv_obj_set_style_text_color(lv_obj_get_child(filterButton, 0),
                                lv_color_hex(state.categoryFilter == filter ? accent : text), 0);
    lv_obj_add_event_cb(filterButton, onFilterClicked, LV_EVENT_CLICKED, remember(state, 0, filter));
    drawerCategoryButtons_[i] = filterButton;
  }

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  const bool chainFull = blocks.size() >= kMaxEffectBlocks;

  lv_obj_t* separator = lv_obj_create(drawer);
  lv_obj_set_size(separator, kBlockDrawerContentWidth, 1);
  lv_obj_align(separator, LV_ALIGN_TOP_LEFT, 0, kDrawerSeparatorY);
  styleSurface(separator, 0x3a3a3a);
  lv_obj_set_style_radius(separator, 0, 0);
  lv_obj_remove_flag(separator, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(separator, LV_OBJ_FLAG_CLICKABLE);

  drawerInstructionLabel_ = label(drawer,
    chainFull ? "Chain full - delete a block to add" : "Tap to add - hold to drag",
    LV_ALIGN_TOP_LEFT, 0, kDrawerInstructionY, &ardor_font_open_sans_regular_18,
    chainFull ? 0xf97373 : muted);
  lv_obj_set_width(drawerInstructionLabel_, kBlockDrawerContentWidth);
  lv_label_set_long_mode(drawerInstructionLabel_, LV_LABEL_LONG_CLIP);

  lv_obj_t* list = lv_obj_create(drawer);
  lv_obj_set_size(list, kBlockDrawerContentWidth, kDrawerListHeight);
  lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, kDrawerListTop);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  drawerAssetList_ = list;

  for (std::size_t i = 0; i < state.assets.size(); ++i) {
    const auto& asset = state.assets[i];
    lv_obj_t* item = button(list, asset.name);
    lv_obj_set_width(item, kBlockDrawerContentWidth - 14);
    lv_obj_set_height(item, kDrawerAssetButtonHeight);
    lv_obj_set_style_min_height(item, kDrawerAssetButtonHeight, 0);
    styleSurface(item, panel);
    if (chainFull) lv_obj_add_state(item, LV_STATE_DISABLED);
    if (state.categoryFilter != "all" && asset.type != state.categoryFilter) {
      lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
    }
    auto* context = remember(state, i);
    context->controlledObject = list;
    lv_obj_add_event_cb(item, onAssetPressed, LV_EVENT_PRESSED, context);
    lv_obj_add_event_cb(item, onAssetLongPressed, LV_EVENT_LONG_PRESSED, context);
    lv_obj_add_event_cb(item, onAssetPressing, LV_EVENT_PRESSING, context);
    lv_obj_add_event_cb(item, onAssetReleased, LV_EVENT_RELEASED, context);
    lv_obj_add_event_cb(item, onAssetPressLost, LV_EVENT_PRESS_LOST, context);
    lv_obj_add_event_cb(item, onAssetClicked, LV_EVENT_CLICKED, context);
    drawerAssetButtons_.push_back(item);
    drawerAssetContexts_.push_back(context);
    renderedAssetKeys_.push_back(assetRenderKey(asset));
  }

  lv_obj_update_layout(list);
  lv_obj_scroll_to_y(list, state.assetScrollOffset, LV_ANIM_OFF);
  state.assetScrollOffset = lv_obj_get_scroll_y(list);
  auto* scrollContext = remember(state);
  lv_obj_add_event_cb(list, onAssetListScrollBegin, LV_EVENT_SCROLL_BEGIN, scrollContext);
  lv_obj_add_event_cb(list, onAssetListScroll, LV_EVENT_SCROLL, scrollContext);
  lv_obj_add_event_cb(list, onAssetListScrollEnd, LV_EVENT_SCROLL_END, scrollContext);
}

} // namespace ardor
