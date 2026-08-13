#include "ui/LvglUi.h"

#include "ui/LvglUiParameterView.h"
#include "ui/LvglUiParameterWidgets.h"
#include "ui/LvglUiStyle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace ardor {
namespace {

using namespace lvgl_ui;

constexpr auto eqCombined = 0xff9f43;
constexpr std::array<int, kEqStageCount> eqStageColors = {
  0x56c7ff, 0x8be28b, 0xf5d76e, 0xffb45c, 0xff8c69, 0xc792ea, 0xf06eb7,
};

constexpr int kEqPanelTop = 94;
constexpr int kEqPanelHeight = 578;
constexpr int kEqGraphX = parameter_widgets::panelEdgeInset;
constexpr int kEqGraphY = 80;
constexpr int kEqGraphWidth = parameter_widgets::panelWidth - 2 * parameter_widgets::panelEdgeInset;
constexpr int kEqGraphHeight = 236;
// Handles carry a small, quiet visual mark but keep a 44 px touch target
// underneath it, per the redesign's minimum-touch-target rule.
constexpr int kEqNodeHitSize = 44;
constexpr int kEqNodeVisualSize = 15;
constexpr int kEqNodeVisualSizeSelected = 19;
constexpr int kEqGripWidth = 13;
constexpr int kEqGripHeight = 26;
constexpr int kEqGripHitSize = 44;
constexpr int kEqStageStripY = 330;
constexpr int kEqStageChipWidth = 158;
constexpr int kEqStageChipGap = 10;
constexpr int kEqEditorHeadingY = 392;
constexpr int kEqSlidersY = 436;
constexpr uint32_t kEqCurveRefreshIntervalMs = 33;

struct EqGraphVisual {
  std::array<lv_obj_t*, kEqStageCount + 1> responseLines{};
  std::array<lv_obj_t*, kEqStageCount> nodes{};
  std::array<lv_obj_t*, kEqStageCount> nodeMarks{};
  std::array<lv_obj_t*, kEqStageCount> nodeLabels{};
  lv_obj_t* qSpan = nullptr;
  std::array<lv_obj_t*, 2> qGrips{};
  lv_obj_t* qLabel = nullptr;
  lv_obj_t* editorTitle = nullptr;
  lv_obj_t* resetButton = nullptr;
  std::size_t selectedStage = kEqFirstBandStage;
  uint32_t lastCurveRefresh = 0;
};

bool isPassStage(std::size_t stage)
{
  return stage == kEqHighPassStage || stage == kEqLowPassStage;
}

const EqPassFilterParams& passFilterForStage(const ParametricEqParams& params, std::size_t stage)
{
  return stage == kEqHighPassStage ? params.highPass : params.lowPass;
}

float stageFrequency(const ParametricEqParams& params, std::size_t stage)
{
  return isPassStage(stage) ? passFilterForStage(params, stage).frequencyHz
                            : params.bands[stage - kEqFirstBandStage].frequencyHz;
}

float stageQ(const ParametricEqParams& params, std::size_t stage)
{
  return isPassStage(stage) ? passFilterForStage(params, stage).q
                            : params.bands[stage - kEqFirstBandStage].q;
}

float stageGain(const ParametricEqParams& params, std::size_t stage)
{
  return isPassStage(stage) ? -3.0f : params.bands[stage - kEqFirstBandStage].gainDb;
}

bool stageEnabled(const ParametricEqParams& params, std::size_t stage)
{
  return isPassStage(stage) ? passFilterForStage(params, stage).enabled
                            : params.bands[stage - kEqFirstBandStage].enabled;
}

std::string stageName(std::size_t stage)
{
  if (stage == kEqHighPassStage) return "HP";
  if (stage == kEqLowPassStage) return "LP";
  return "B" + std::to_string(stage);
}

std::string stageEditorTitle(std::size_t stage)
{
  if (stage == kEqHighPassStage || stage == kEqLowPassStage) {
    return (stage == kEqHighPassStage ? "High-pass filter" : "Low-pass filter")
      + std::string("  \xC2\xB7  Encoder -> Cutoff");
  }
  return "Band " + std::to_string(stage) + "  \xC2\xB7  Encoder -> Q";
}

void freeLinePoints(lv_event_t* event)
{
  lv_free(lv_event_get_user_data(event));
}

void freeEqGraphVisual(lv_event_t* event)
{
  delete static_cast<EqGraphVisual*>(lv_event_get_user_data(event));
}

void redraw(UiEventContext* context)
{
  context->ui->invalidate(UiChange::Parameters);
}

EqBandField eqBandFieldForKey(const std::string& key)
{
  if (key == "frequency") {
    return EqBandField::Frequency;
  }
  if (key == "q") {
    return EqBandField::Q;
  }
  if (key == "slope") {
    return EqBandField::Slope;
  }
  return EqBandField::Gain;
}

ParameterControl eqSliderControl(EqBandField field, const EqBandParams& band);
ParameterControl eqSliderControl(EqBandField field, const EqPassFilterParams& filter);
void refreshEqGraphCurve(lv_obj_t* graph, const ParametricEqParams& params, bool throttle = false);

void applyEqSliderPosition(lv_obj_t* slider, UiEventContext* context, lv_indev_t* input)
{
  const auto params = selectedParametricEqParams(*context->state);
  const float currentFrequency = stageFrequency(params, context->index);
  const float currentQ = stageQ(params, context->index);
  const auto field = eqBandFieldForKey(context->filter);
  const float ratio = parameter_widgets::sliderRatioForInput(slider, input);
  int delta = 0;
  switch (field) {
  case EqBandField::Frequency: {
    const float target = kEqMinimumFrequencyHz
      * std::exp(ratio * std::log(kEqMaximumFrequencyHz / kEqMinimumFrequencyHz));
    delta = static_cast<int>(std::lround(24.0f * std::log2(target / currentFrequency)));
    break;
  }
  case EqBandField::Q: {
    const float target = kEqMinimumQ * std::exp(ratio * std::log(kEqMaximumQ / kEqMinimumQ));
    delta = static_cast<int>(std::lround(24.0f * std::log2(target / currentQ)));
    break;
  }
  case EqBandField::Gain: {
    const float target = kEqMinimumGainDb + ratio * (kEqMaximumGainDb - kEqMinimumGainDb);
    if (isPassStage(context->index)) return;
    const auto& band = params.bands[context->index - kEqFirstBandStage];
    delta = static_cast<int>(std::lround((target - band.gainDb) / 0.5f));
    break;
  }
  case EqBandField::Slope: {
    if (!isPassStage(context->index)) return;
    const auto& filter = passFilterForStage(params, context->index);
    const auto current = std::find(kEqPassFilterSlopesDbPerOctave.begin(),
                                   kEqPassFilterSlopesDbPerOctave.end(),
                                   normalizedEqPassFilterSlope(filter.slopeDbPerOctave));
    const int currentIndex = static_cast<int>(std::distance(
      kEqPassFilterSlopesDbPerOctave.begin(), current));
    const int targetIndex = std::clamp(static_cast<int>(std::lround(
      ratio * static_cast<float>(kEqPassFilterSlopesDbPerOctave.size() - 1))), 0,
      static_cast<int>(kEqPassFilterSlopesDbPerOctave.size()) - 1);
    delta = targetIndex - currentIndex;
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
  context->ui->selectEqStage(context->index);
  context->ui->focusEqBandField(field);
  const auto params = selectedParametricEqParams(*context->state);
  const auto control = isPassStage(context->index)
    ? eqSliderControl(field, passFilterForStage(params, context->index))
    : eqSliderControl(field, params.bands[context->index - kEqFirstBandStage]);
  parameter_view::syncSlider(lv_event_get_target_obj(event), control, true);
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

void onEqStageSelected(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectEqStage(context->index);
  redraw(context);
}

void onEqStageEnabled(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectEqStage(context->index);
  auto params = selectedParametricEqParams(*context->state);
  if (isPassStage(context->index)) {
    auto filter = passFilterForStage(params, context->index);
    filter.enabled = !filter.enabled;
    context->ui->updateSelectedEqPassFilter(*context->state, filter);
  } else {
    auto band = params.bands[context->index - kEqFirstBandStage];
    band.enabled = !band.enabled;
    context->ui->updateSelectedEqBand(*context->state, band);
  }
  redraw(context);
}

void onEqStageReset(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectEqStage(context->index);
  if (isPassStage(context->index)) {
    const auto kind = context->index == kEqHighPassStage
      ? EqPassFilterKind::HighPass : EqPassFilterKind::LowPass;
    context->ui->updateSelectedEqPassFilter(*context->state, defaultEqPassFilter(kind));
  } else {
    context->ui->updateSelectedEqBand(
      *context->state, defaultParametricEqBand(context->index - kEqFirstBandStage));
  }
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
  context->ui->selectEqStage(context->index);
  context->ui->focusEqBandField(
    isPassStage(context->index) ? EqBandField::Frequency : EqBandField::Gain);
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
  if (isPassStage(context->index)) {
    auto filter = passFilterForStage(params, context->index);
    filter.frequencyHz = eqFrequencyFromX(x, kEqGraphWidth);
    context->ui->updateSelectedEqPassFilter(*context->state, filter, false);
  } else {
    auto band = params.bands[context->index - kEqFirstBandStage];
    band.frequencyHz = eqFrequencyFromX(x, kEqGraphWidth);
    band.gainDb = eqGainFromY(y, kEqGraphHeight);
    context->ui->updateSelectedEqBand(*context->state, band, false);
  }
  refreshEqGraphCurve(context->controlledObject, selectedParametricEqParams(*context->state), true);
  context->ui->syncEqSliders(*context->state);
}

void applyEqGripPosition(UiEventContext* context, lv_indev_t* input)
{
  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  lv_area_t graphArea{};
  lv_area_t canvasArea{};
  lv_obj_get_content_coords(context->controlledObject, &graphArea);
  lv_obj_get_coords(context->ui->canvas(), &canvasArea);
  const int x = std::clamp(static_cast<int>(point.x) - (graphArea.x1 - canvasArea.x1),
                           0, kEqGraphWidth - 1);
  const float gripHz = eqFrequencyFromX(x, kEqGraphWidth);
  auto params = selectedParametricEqParams(*context->state);
  const auto selectedStage = context->ui->selectedEqStage();
  if (isPassStage(selectedStage)) return;
  auto& band = params.bands[selectedStage - kEqFirstBandStage];
  // The pair is symmetric about the centre frequency (spec 9.1): whichever
  // grip moved, its distance from centre in octaves sets the whole band's
  // width, so both grips end up equidistant again once Q is applied.
  const float halfOctaves = std::fabs(std::log2(std::max(gripHz, 1.0f) / band.frequencyHz));
  const float totalOctaves = std::max(0.02f, 2.0f * halfOctaves);
  const float newQ = std::clamp(
    1.0f / (2.0f * std::sinh(totalOctaves * std::log(2.0f) / 2.0f)),
    kEqMinimumQ, kEqMaximumQ);
  band.q = newQ;
  context->ui->updateSelectedEqBand(*context->state, band, false);
  refreshEqGraphCurve(context->controlledObject, selectedParametricEqParams(*context->state), true);
  context->ui->syncEqSliders(*context->state);
}

void onEqGripPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) return;
  context->ui->setFocusedWidgets(nullptr, context->controlledObject);
  context->ui->beginParameterInteraction();
  context->ui->focusEqBandField(EqBandField::Q);
  applyEqGripPosition(context, input);
}

void onEqGripPressing(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (input) applyEqGripPosition(context, input);
}

void onEqNodeReleased(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->endParameterInteraction();
  redraw(context);
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
      const auto& response = responseIndex < kEqStageCount
        ? curve.stageDb[responseIndex] : curve.combinedDb;
      for (std::size_t point = 0; point < kEqCurvePointCount; ++point) {
        points[point].y = eqYFromGain(response[point], kEqGraphHeight);
      }
      if (responseIndex < kEqStageCount) {
        lv_obj_set_style_line_opa(line, stageEnabled(params, responseIndex) ? LV_OPA_40 : LV_OPA_20,
                                  LV_PART_MAIN);
      }
      lv_obj_invalidate(line);
    }
  }

  const int hitRadius = kEqNodeHitSize / 2;
  for (std::size_t stageIndex = 0; stageIndex < visual->nodes.size(); ++stageIndex) {
    lv_obj_t* node = visual->nodes[stageIndex];
    if (!node) {
      continue;
    }
    const bool selected = stageIndex == visual->selectedStage;
    const int x = std::clamp(eqXFromFrequency(stageFrequency(params, stageIndex), kEqGraphWidth),
                             hitRadius, kEqGraphWidth - hitRadius);
    const int y = std::clamp(eqYFromGain(stageGain(params, stageIndex), kEqGraphHeight),
                             hitRadius, kEqGraphHeight - hitRadius);
    lv_obj_set_pos(node, x - hitRadius, y - hitRadius);

    if (lv_obj_t* mark = visual->nodeMarks[stageIndex]) {
      const int markSize = selected ? kEqNodeVisualSizeSelected : kEqNodeVisualSize;
      lv_obj_set_size(mark, markSize, markSize);
      lv_obj_center(mark);
      styleSurface(mark, selected ? lamp : bg);
      lv_obj_set_style_border_width(mark, 2, 0);
      lv_obj_set_style_border_color(mark, lv_color_hex(selected ? lamp : muted), 0);
      lv_obj_set_style_opa(mark, stageEnabled(params, stageIndex) ? LV_OPA_COVER : LV_OPA_50, 0);
    }
    if (lv_obj_t* mark = visual->nodeLabels[stageIndex]) {
      lv_obj_align(mark, LV_ALIGN_TOP_MID, 0, selected ? -24 : -21);
      lv_obj_set_style_text_color(mark, lv_color_hex(selected ? lamp : muted), 0);
    }
  }

  const bool selectedIsPass = isPassStage(visual->selectedStage);
  const auto& selectedBand = params.bands[selectedIsPass
    ? 0 : visual->selectedStage - kEqFirstBandStage];
  const bool showGrips = !selectedIsPass && selectedBand.enabled;
  const auto [lowHz, highHz] = eqShoulderFrequencies(selectedBand.frequencyHz, selectedBand.q);
  const int centerX = std::clamp(eqXFromFrequency(selectedBand.frequencyHz, kEqGraphWidth),
                                 hitRadius, kEqGraphWidth - hitRadius);
  const int centerY = std::clamp(eqYFromGain(selectedBand.gainDb, kEqGraphHeight),
                                 hitRadius, kEqGraphHeight - hitRadius);
  const int lowX = eqXFromFrequency(lowHz, kEqGraphWidth);
  const int highX = eqXFromFrequency(highHz, kEqGraphWidth);
  if (visual->qSpan) {
    lv_obj_set_pos(visual->qSpan, lowX, centerY);
    lv_obj_set_width(visual->qSpan, std::max(1, highX - lowX));
    lv_obj_add_flag(visual->qSpan, LV_OBJ_FLAG_HIDDEN);
    if (showGrips) lv_obj_remove_flag(visual->qSpan, LV_OBJ_FLAG_HIDDEN);
  }
  const std::array<int, 2> gripX = {lowX, highX};
  for (std::size_t i = 0; i < visual->qGrips.size(); ++i) {
    lv_obj_t* grip = visual->qGrips[i];
    if (!grip) continue;
    lv_obj_set_pos(grip, gripX[i] - kEqGripHitSize / 2, centerY - kEqGripHitSize / 2);
    lv_obj_add_flag(grip, LV_OBJ_FLAG_HIDDEN);
    if (showGrips) lv_obj_remove_flag(grip, LV_OBJ_FLAG_HIDDEN);
  }
  if (visual->qLabel) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "Q %.1f \xC2\xB7 %.1f oct",
                 selectedBand.q, eqBandwidthOctaves(selectedBand.q));
    lv_label_set_text(visual->qLabel, buffer);
    lv_obj_update_layout(visual->qLabel);
    const int labelWidth = lv_obj_get_width(visual->qLabel);
    lv_obj_set_pos(visual->qLabel, centerX - labelWidth / 2, std::max(0, centerY - 46));
    lv_obj_add_flag(visual->qLabel, LV_OBJ_FLAG_HIDDEN);
    if (showGrips) lv_obj_remove_flag(visual->qLabel, LV_OBJ_FLAG_HIDDEN);
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

std::string eqFrequencyCompact(float frequencyHz)
{
  if (frequencyHz >= 1000.0f) {
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%.1fk", frequencyHz / 1000.0f);
    return buffer;
  }
  return std::to_string(static_cast<int>(std::lround(frequencyHz)));
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
  case EqBandField::Slope:
    break;
  }
  control.value = std::clamp(control.value, control.minimum, control.maximum);
  return control;
}

ParameterControl eqSliderControl(EqBandField field, const EqPassFilterParams& filter)
{
  ParameterControl control;
  control.minimum = 0.0f;
  control.maximum = 1.0f;
  switch (field) {
  case EqBandField::Frequency:
    control.key = "frequency";
    control.label = "Cutoff";
    control.value = std::log(filter.frequencyHz / kEqMinimumFrequencyHz)
      / std::log(kEqMaximumFrequencyHz / kEqMinimumFrequencyHz);
    control.formatted = eqFrequencyLabel(filter.frequencyHz);
    break;
  case EqBandField::Q:
    control.key = "q";
    control.label = "Resonance (12+)";
    control.value = std::log(filter.q / kEqMinimumQ) / std::log(kEqMaximumQ / kEqMinimumQ);
    control.formatted = eqQLabel(filter.q);
    break;
  case EqBandField::Gain:
    break;
  case EqBandField::Slope: {
    control.key = "slope";
    control.label = "Slope";
    control.minimum = 0.0f;
    control.maximum = static_cast<float>(kEqPassFilterSlopesDbPerOctave.size() - 1);
    const auto slope = normalizedEqPassFilterSlope(filter.slopeDbPerOctave);
    const auto found = std::find(kEqPassFilterSlopesDbPerOctave.begin(),
                                 kEqPassFilterSlopesDbPerOctave.end(), slope);
    control.value = static_cast<float>(std::distance(
      kEqPassFilterSlopesDbPerOctave.begin(), found));
    control.formatted = std::to_string(slope) + " dB/oct";
    break;
  }
  }
  control.value = std::clamp(control.value, control.minimum, control.maximum);
  return control;
}

void renderParametricEqPanel(lv_obj_t* root, UiState& state, UiEventContext* context,
                             lv_obj_t** graphOut,
                             std::array<lv_obj_t*, kEqStageCount>* bandButtonsOut,
                             lv_obj_t** enabledOut, UiEventContext** enabledContextOut,
                             UiEventContext** resetContextOut,
                             std::array<lv_obj_t*, 3>* slidersOut,
                             std::array<UiEventContext*, 3>* sliderContextsOut,
                             lv_obj_t** bypassOut)
{
  if (!selectedUiBlock(state)) return;

  lv_obj_t* panelObject = lv_obj_create(root);
  lv_obj_set_size(panelObject, 1240, kEqPanelHeight);
  lv_obj_align(panelObject, LV_ALIGN_TOP_MID, 0, kEqPanelTop);
  lv_obj_remove_flag(panelObject, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(panelObject, panelAlt);
  // Every EQ child uses panel-local coordinates. Theme padding used to add an
  // invisible left inset and consume the intended right margin.
  lv_obj_set_style_pad_all(panelObject, 0, 0);
  label(panelObject, "Parametric EQ", LV_ALIGN_TOP_LEFT, 28, 15, &ardor_font_saira_cond_semibold_22);
  label(panelObject, "High-pass  \xC2\xB7  five bands  \xC2\xB7  low-pass", LV_ALIGN_TOP_LEFT, 205, 18,
        &ardor_font_saira_cond_medium_18, muted);

  const auto params = selectedParametricEqParams(state);
  const auto curve = makeEqCurveData(params, 48000.0f);
  lv_obj_t* graph = lv_obj_create(panelObject);
  lv_obj_set_size(graph, kEqGraphWidth, kEqGraphHeight);
  lv_obj_set_pos(graph, kEqGraphX, kEqGraphY);
  lv_obj_remove_flag(graph, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(graph, panelAlt);
  lv_obj_set_style_border_color(graph, lv_color_hex(rule), 0);
  lv_obj_set_style_border_width(graph, 1, 0);
  auto* graphVisual = new EqGraphVisual{};
  lv_obj_set_user_data(graph, graphVisual);
  lv_obj_add_event_cb(graph, freeEqGraphVisual, LV_EVENT_DELETE, graphVisual);
  if (graphOut) *graphOut = graph;

  for (int i = 1; i < 4; ++i) {
    lv_obj_t* gridLine = lv_obj_create(graph);
    lv_obj_set_size(gridLine, kEqGraphWidth - 2, 1);
    lv_obj_set_pos(gridLine, 1, i * (kEqGraphHeight - 1) / 4);
    styleSurface(gridLine, i == 2 ? disabled : rule);
    lv_obj_remove_flag(gridLine, LV_OBJ_FLAG_CLICKABLE);
  }
  // The bottom-most gridline sits too close to the frequency axis labels for
  // its own numeral to fit without colliding; +18/+9/0/-9 already carries the
  // scale.
  for (const float gainDb : {18.0f, 9.0f, 0.0f, -9.0f}) {
    char buffer[8]{};
    std::snprintf(buffer, sizeof(buffer), "%+d", static_cast<int>(gainDb));
    lv_obj_t* gainLabel = label(graph, gainDb == 0.0f ? "0" : buffer, LV_ALIGN_TOP_LEFT,
                                8, std::clamp(eqYFromGain(gainDb, kEqGraphHeight) + 2, 0, kEqGraphHeight - 14),
                                &ardor_font_saira_cond_medium_18, muted);
    lv_obj_remove_flag(gainLabel, LV_OBJ_FLAG_CLICKABLE);
  }
  for (const float frequency : {100.0f, 1000.0f, 10000.0f}) {
    lv_obj_t* gridLine = lv_obj_create(graph);
    lv_obj_set_size(gridLine, 1, kEqGraphHeight - 2);
    lv_obj_set_pos(gridLine, eqXFromFrequency(frequency, kEqGraphWidth), 1);
    styleSurface(gridLine, rule);
    lv_obj_remove_flag(gridLine, LV_OBJ_FLAG_CLICKABLE);
    const std::string freqLabel = frequency >= 1000.0f
      ? std::to_string(static_cast<int>(frequency / 1000.0f)) + "k" : "100";
    lv_obj_t* xLabel = label(graph, freqLabel, LV_ALIGN_BOTTOM_LEFT,
                             eqXFromFrequency(frequency, kEqGraphWidth) + 4, -6,
                             &ardor_font_saira_cond_medium_18, muted);
    lv_obj_remove_flag(xLabel, LV_OBJ_FLAG_CLICKABLE);
  }

  for (std::size_t i = kEqFirstBandStage; i < kEqLowPassStage; ++i) {
    graphVisual->responseLines[i] = createEqResponseLine(
      graph, curve.stageDb[i], eqStageColors[i], stageEnabled(params, i) ? LV_OPA_40 : LV_OPA_20);
  }
  for (const std::size_t i : {kEqHighPassStage, kEqLowPassStage}) {
    graphVisual->responseLines[i] = createEqResponseLine(
      graph, curve.stageDb[i], eqStageColors[i], stageEnabled(params, i) ? LV_OPA_40 : LV_OPA_20);
  }
  graphVisual->responseLines[kEqStageCount] = createEqResponseLine(
    graph, curve.combinedDb, eqCombined, LV_OPA_COVER);

  graphVisual->selectedStage = context->ui->selectedEqStage();
  for (std::size_t i = 0; i < kEqStageCount; ++i) {
    lv_obj_t* node = lv_obj_create(graph);
    graphVisual->nodes[i] = node;
    lv_obj_remove_flag(node, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(node, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(node, 0, 0);
    lv_obj_set_size(node, kEqNodeHitSize, kEqNodeHitSize);

    lv_obj_t* mark = lv_obj_create(node);
    lv_obj_remove_flag(mark, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(mark, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(mark, 0, 0);
    graphVisual->nodeMarks[i] = mark;

    lv_obj_t* markLabel = label(node, stageName(i), LV_ALIGN_TOP_MID, 0, -21,
                                &ardor_font_saira_cond_medium_18, muted);
    lv_obj_remove_flag(markLabel, LV_OBJ_FLAG_CLICKABLE);
    graphVisual->nodeLabels[i] = markLabel;
    auto* nodeContext = context->ui->remember(state, i);
    nodeContext->controlledObject = graph;
    lv_obj_add_event_cb(node, onEqNodePressed, LV_EVENT_PRESSED, nodeContext);
    lv_obj_add_event_cb(node, onEqNodePressing, LV_EVENT_PRESSING, nodeContext);
    lv_obj_add_event_cb(node, onEqNodeReleased, LV_EVENT_RELEASED, nodeContext);
    lv_obj_add_event_cb(node, onEqNodeReleased, LV_EVENT_PRESS_LOST, nodeContext);
  }

  // The shoulder grips and Q span belong only to the selected band. They are
  // built once, hidden by default, and repositioned/shown by
  // refreshEqGraphCurve so dragging Freq/Q keeps them glued to the curve.
  lv_obj_t* qSpan = lv_obj_create(graph);
  lv_obj_remove_style_all(qSpan);
  lv_obj_set_size(qSpan, 1, 1);
  lv_obj_set_style_border_width(qSpan, 1, 0);
  lv_obj_set_style_border_color(qSpan, lv_color_hex(lamp), 0);
  lv_obj_set_style_border_opa(qSpan, LV_OPA_80, 0);
  lv_obj_remove_flag(qSpan, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(qSpan, LV_OBJ_FLAG_HIDDEN);
  graphVisual->qSpan = qSpan;

  for (std::size_t i = 0; i < graphVisual->qGrips.size(); ++i) {
    lv_obj_t* grip = lv_obj_create(graph);
    lv_obj_remove_flag(grip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(grip, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grip, 0, 0);
    lv_obj_set_size(grip, kEqGripHitSize, kEqGripHitSize);
    lv_obj_add_flag(grip, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* gripMark = lv_obj_create(grip);
    lv_obj_remove_flag(gripMark, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(gripMark, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(gripMark, kEqGripWidth, kEqGripHeight);
    lv_obj_center(gripMark);
    styleSurface(gripMark, bg);
    lv_obj_set_style_border_width(gripMark, 2, 0);
    lv_obj_set_style_border_color(gripMark, lv_color_hex(lamp), 0);

    auto* gripContext = context->ui->remember(state, i);
    gripContext->controlledObject = graph;
    lv_obj_add_event_cb(grip, onEqGripPressed, LV_EVENT_PRESSED, gripContext);
    lv_obj_add_event_cb(grip, onEqGripPressing, LV_EVENT_PRESSING, gripContext);
    lv_obj_add_event_cb(grip, onEqNodeReleased, LV_EVENT_RELEASED, gripContext);
    lv_obj_add_event_cb(grip, onEqNodeReleased, LV_EVENT_PRESS_LOST, gripContext);
    graphVisual->qGrips[i] = grip;
  }

  lv_obj_t* qLabel = lv_label_create(graph);
  setText(qLabel, muted, &ardor_font_saira_cond_medium_18);
  lv_obj_set_style_bg_opa(qLabel, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(qLabel, lv_color_hex(panelAlt), 0);
  lv_obj_set_style_pad_hor(qLabel, 8, 0);
  lv_obj_set_style_pad_ver(qLabel, 2, 0);
  lv_obj_remove_flag(qLabel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(qLabel, LV_OBJ_FLAG_HIDDEN);
  graphVisual->qLabel = qLabel;

  // Nodes/marks/grips are created above with no position or style beyond
  // their defaults -- refreshEqGraphCurve() is what actually places and
  // colours them from `params`. Without this call the first render shows
  // untouched default-themed objects (e.g. an unstyled white mark square)
  // until the next drag or slider tweak happens to repaint the graph.
  refreshEqGraphCurve(graph, params, false);

  // Create the header actions after the graph so they remain topmost even if
  // a future layout adjustment accidentally brings the two regions close.
  parameter_widgets::renderCloseButton(panelObject, context);
  parameter_widgets::renderBlockActions(panelObject, state, context, bypassOut);

  const auto selectedStage = context->ui->selectedEqStage();
  const bool selectedIsPass = isPassStage(selectedStage);

  for (std::size_t i = 0; i < kEqStageCount; ++i) {
    const bool selected = i == selectedStage;
    lv_obj_t* stageChip = button(panelObject, "");
    lv_obj_set_size(stageChip, kEqStageChipWidth, 50);
    lv_obj_set_pos(stageChip, 28 + static_cast<int>(i) * (kEqStageChipWidth + kEqStageChipGap),
                   kEqStageStripY);
    styleSurface(stageChip, selected ? panel : panelAlt);
    lv_obj_set_style_border_color(stageChip, lv_color_hex(selected ? lamp : rule), 0);
    lv_label_set_text(lv_obj_get_child(stageChip, 0), "");
    lv_obj_t* dot = lv_obj_create(stageChip);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 12, 0);
    styleSurface(dot, stageEnabled(params, i) && selected ? lamp : disabled);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_t* chipLabel = label(
      stageChip, stageName(i) + "  " + eqFrequencyCompact(stageFrequency(params, i)),
      LV_ALIGN_LEFT_MID, 28, 0, &ardor_font_saira_cond_medium_18, selected ? text : muted);
    lv_obj_set_width(chipLabel, kEqStageChipWidth - 34);
    lv_label_set_long_mode(chipLabel, LV_LABEL_LONG_CLIP);
    lv_obj_add_event_cb(stageChip, onEqStageSelected, LV_EVENT_CLICKED,
                        context->ui->remember(state, i));
    if (bandButtonsOut) (*bandButtonsOut)[i] = stageChip;
  }

  graphVisual->editorTitle = label(
    panelObject, stageEditorTitle(selectedStage), LV_ALIGN_TOP_LEFT, 28, kEqEditorHeadingY,
    &ardor_font_saira_cond_medium_18, lamp);

  lv_obj_t* bandeditBox = lv_obj_create(panelObject);
  lv_obj_remove_flag(bandeditBox, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(bandeditBox, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(bandeditBox, parameter_widgets::panelWidth - 2 * parameter_widgets::panelEdgeInset + 12,
                 132 + 12);
  lv_obj_set_pos(bandeditBox, parameter_widgets::panelEdgeInset - 6, kEqSlidersY - 6);
  lv_obj_set_style_bg_opa(bandeditBox, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bandeditBox, 1, 0);
  lv_obj_set_style_border_color(bandeditBox, lv_color_hex(lamp), 0);
  lv_obj_set_style_radius(bandeditBox, 0, 0);
  lv_obj_move_background(bandeditBox);

  const bool selectedEnabled = stageEnabled(params, selectedStage);
  lv_obj_t* enabled = button(panelObject, selectedEnabled
    ? (selectedIsPass ? "Filter On" : "Band On")
    : (selectedIsPass ? "Filter Off" : "Band Off"));
  lv_obj_set_size(enabled, 130, 44);
  lv_obj_set_pos(enabled, 930, kEqEditorHeadingY - 8);
  styleSurface(enabled, selectedEnabled ? panel : panelAlt);
  lv_obj_set_style_text_color(lv_obj_get_child(enabled, 0),
                              lv_color_hex(selectedEnabled ? lamp : danger), 0);
  auto* enabledContext = context->ui->remember(state, selectedStage);
  lv_obj_add_event_cb(enabled, onEqStageEnabled, LV_EVENT_CLICKED, enabledContext);
  if (enabledOut) *enabledOut = enabled;
  if (enabledContextOut) *enabledContextOut = enabledContext;

  lv_obj_t* reset = button(panelObject, selectedIsPass ? "Reset Filter" : "Reset Band");
  graphVisual->resetButton = reset;
  lv_obj_set_size(reset, 148, 44);
  lv_obj_set_pos(reset, 1074, kEqEditorHeadingY - 8);
  styleSurface(reset, panelAlt);
  auto* resetContext = context->ui->remember(state, selectedStage);
  lv_obj_add_event_cb(reset, onEqStageReset, LV_EVENT_CLICKED, resetContext);
  if (resetContextOut) *resetContextOut = resetContext;

  constexpr std::array<EqBandField, 3> bandSliderFields = {
    EqBandField::Frequency, EqBandField::Q, EqBandField::Gain,
  };
  constexpr std::array<EqBandField, 3> passSliderFields = {
    EqBandField::Frequency, EqBandField::Q, EqBandField::Slope,
  };
  const auto& eqSliderFields = selectedIsPass ? passSliderFields : bandSliderFields;
  for (std::size_t i = 0; i < eqSliderFields.size(); ++i) {
    const auto field = eqSliderFields[i];
    auto* sliderContext = context->ui->remember(state, selectedStage,
                                                 field == EqBandField::Frequency ? "frequency"
                                                 : field == EqBandField::Q ? "q"
                                                 : field == EqBandField::Slope ? "slope" : "gain");
    sliderContext->controlledObject = graph;
    lv_obj_t* slider = parameter_widgets::createSlider(
      panelObject, selectedIsPass
        ? eqSliderControl(field, passFilterForStage(params, selectedStage))
        : eqSliderControl(field, params.bands[selectedStage - kEqFirstBandStage]),
      parameter_widgets::sliderGridX + static_cast<int>(i) * (parameter_widgets::sliderWidth + parameter_widgets::sliderColumnGap),
      kEqSlidersY, context->ui->isEqBandFieldFocused(field), sliderContext,
      onEqSliderPressed, onEqSliderPressing, i);
    if (slidersOut) (*slidersOut)[i] = slider;
    if (sliderContextsOut) (*sliderContextsOut)[i] = sliderContext;
  }
}

} // namespace

namespace parameter_view {

ParameterControl eqControl(EqBandField field, const EqBandParams& band)
{
  return eqSliderControl(field, band);
}

ParameterControl eqControl(EqBandField field, const EqPassFilterParams& filter)
{
  return eqSliderControl(field, filter);
}

void syncEqGraph(lv_obj_t* graph, const ParametricEqParams& params, bool throttle)
{
  refreshEqGraphCurve(graph, params, throttle);
}

void syncEqBandSelection(
  lv_obj_t* graph,
  const std::array<lv_obj_t*, kEqStageCount>& bandButtons,
  const ParametricEqParams& params, std::size_t selectedStage)
{
  auto* graphVisual = graph ? static_cast<EqGraphVisual*>(lv_obj_get_user_data(graph)) : nullptr;
  if (graphVisual) {
    graphVisual->selectedStage = selectedStage;
    if (graphVisual->editorTitle) {
      lv_label_set_text(graphVisual->editorTitle, stageEditorTitle(selectedStage).c_str());
    }
    if (graphVisual->resetButton) {
      lv_label_set_text(lv_obj_get_child(graphVisual->resetButton, 0),
                        isPassStage(selectedStage) ? "Reset Filter" : "Reset Band");
    }
  }
  refreshEqGraphCurve(graph, params, false);
  for (std::size_t i = 0; i < kEqStageCount; ++i) {
    const bool selected = selectedStage == i;
    if (bandButtons[i]) {
      styleSurface(bandButtons[i], selected ? panel : panelAlt);
      lv_obj_set_style_border_color(bandButtons[i], lv_color_hex(selected ? lamp : rule), 0);
      if (lv_obj_t* dot = lv_obj_get_child(bandButtons[i], 1)) {
        styleSurface(dot, stageEnabled(params, i) && selected ? lamp : disabled);
        lv_obj_set_style_border_width(dot, 0, 0);
      }
      if (lv_obj_t* chipLabel = lv_obj_get_child(bandButtons[i], 2)) {
        lv_obj_set_style_text_color(chipLabel, lv_color_hex(selected ? text : muted), 0);
        const auto chipText = stageName(i) + "  " + eqFrequencyCompact(stageFrequency(params, i));
        lv_label_set_text(chipLabel, chipText.c_str());
      }
    }
  }
}

void buildEqPanel(
  lv_obj_t* root, UiState& state, UiEventContext* context,
  lv_obj_t** graphOut,
  std::array<lv_obj_t*, kEqStageCount>* bandButtonsOut,
  lv_obj_t** enabledOut, UiEventContext** enabledContextOut,
  UiEventContext** resetContextOut,
  std::array<lv_obj_t*, 3>* slidersOut,
  std::array<UiEventContext*, 3>* sliderContextsOut,
  lv_obj_t** bypassOut)
{
  renderParametricEqPanel(root, state, context, graphOut, bandButtonsOut,
                          enabledOut, enabledContextOut, resetContextOut,
                          slidersOut, sliderContextsOut, bypassOut);
}

} // namespace parameter_view
} // namespace ardor
