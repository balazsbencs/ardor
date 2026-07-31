#include "ui/LvglUi.h"

#include "ui/LvglUiParameterView.h"
#include "ui/LvglUiParameterWidgets.h"
#include "ui/LvglUiStyle.h"
#include "ui/fonts/OpenSansRegular.h"
#include "ui/fonts/OpenSansSemibold.h"

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
constexpr std::array<int, kParametricEqBandCount> eqBandColors = {
  0x56c7ff, 0x8be28b, 0xf5d76e, 0xff8c69, 0xc792ea,
};

constexpr int kEqPanelTop = 94;
constexpr int kEqPanelHeight = 578;
constexpr int kEqGraphX = parameter_widgets::panelEdgeInset;
constexpr int kEqGraphY = 80;
constexpr int kEqGraphWidth = parameter_widgets::panelWidth - 2 * parameter_widgets::panelEdgeInset;
constexpr int kEqGraphHeight = 236;
constexpr int kEqBandControlsY = 330;
constexpr int kEqSlidersY = 398;
constexpr int kEqNodeSize = 48;
constexpr int kEqNodeRadius = kEqNodeSize / 2;
constexpr uint32_t kEqCurveRefreshIntervalMs = 33;

struct EqGraphVisual {
  std::array<lv_obj_t*, kParametricEqBandCount + 1> responseLines{};
  std::array<lv_obj_t*, kParametricEqBandCount> nodes{};
  uint32_t lastCurveRefresh = 0;
};

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
  context->ui->invalidate(UiChange::None);
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
  const float ratio = parameter_widgets::sliderRatioForInput(slider, input);
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
  parameter_view::syncSlider(lv_event_get_target_obj(event),
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
  if (!selectedUiBlock(state)) return;

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
  parameter_widgets::renderCloseButton(panelObject, context);
  parameter_widgets::renderBlockActions(panelObject, state, context, bypassOut);

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
    lv_obj_t* slider = parameter_widgets::createSlider(
      panelObject, eqSliderControl(field, band),
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

void syncEqGraph(lv_obj_t* graph, const ParametricEqParams& params, bool throttle)
{
  refreshEqGraphCurve(graph, params, throttle);
}

void syncEqBandSelection(
  lv_obj_t* graph,
  const std::array<lv_obj_t*, kParametricEqBandCount>& bandButtons,
  const ParametricEqParams& params, std::size_t selectedBand)
{
  auto* graphVisual = graph ? static_cast<EqGraphVisual*>(lv_obj_get_user_data(graph)) : nullptr;
  for (std::size_t i = 0; i < kParametricEqBandCount; ++i) {
    const bool selected = selectedBand == i;
    if (graphVisual && graphVisual->nodes[i]) {
      lv_obj_set_style_opa(graphVisual->nodes[i],
                           params.bands[i].enabled ? LV_OPA_COVER : LV_OPA_50, 0);
      lv_obj_set_style_border_width(graphVisual->nodes[i], selected ? 2 : 0, 0);
      if (selected) {
        lv_obj_set_style_border_color(graphVisual->nodes[i], lv_color_hex(text), 0);
      }
    }
    if (bandButtons[i]) {
      styleSurface(bandButtons[i], selected ? eqBandColors[i] : 0x171717);
      lv_obj_set_style_text_color(lv_obj_get_child(bandButtons[i], 0),
                                  lv_color_hex(selected ? bg : text), 0);
    }
  }
}

void buildEqPanel(
  lv_obj_t* root, UiState& state, UiEventContext* context,
  lv_obj_t** graphOut,
  std::array<lv_obj_t*, kParametricEqBandCount>* bandButtonsOut,
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
