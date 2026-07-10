#include "ui/LvglUi.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace ardor {

namespace {

// The layout below is authored against this fixed design grid; build() scales
// the whole canvas to the real display resolution.
constexpr int32_t kDesignWidth = 800;
constexpr int32_t kDesignHeight = 480;

constexpr auto bg = 0x101214;
constexpr auto panel = 0x171b22;
constexpr auto panelAlt = 0x1b2028;
constexpr auto text = 0xedf2f7;
constexpr auto muted = 0xa8b3c1;
constexpr auto accent = 0x6ee7b7;

void setText(lv_obj_t* object, int color = text, const lv_font_t* font = &lv_font_montserrat_18)
{
  lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(object, font, 0);
}

void stylePanel(lv_obj_t* object, int color)
{
  lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_border_color(object, lv_color_hex(0x303744), 0);
  lv_obj_set_style_border_width(object, 1, 0);
  lv_obj_set_style_radius(object, 8, 0);
}

void label(lv_obj_t* parent, const std::string& value, lv_align_t align, int x, int y,
           const lv_font_t* font = &lv_font_montserrat_18, int color = text)
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
  selectGlobalParams(*context->state);
  redraw(context);
}

enum class GlobalParam {
  Input,
  Output
};

void stepGlobalParam(UiState& state, GlobalParam param, float delta)
{
  auto& global = state.bank.presets[state.activePreset].global;
  if (param == GlobalParam::Input) {
    setActiveInputGainDb(state, global.inputGainDb + delta);
  } else {
    setActiveOutputGainDb(state, global.outputGainDb + delta);
  }
}

void onInputGainDown(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepGlobalParam(*context->state, GlobalParam::Input, -1.0f);
  redraw(context);
}

void onInputGainUp(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepGlobalParam(*context->state, GlobalParam::Input, 1.0f);
  redraw(context);
}

void onOutputGainDown(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepGlobalParam(*context->state, GlobalParam::Output, -1.0f);
  redraw(context);
}

void onOutputGainUp(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepGlobalParam(*context->state, GlobalParam::Output, 1.0f);
  redraw(context);
}

float selectedParamValue(const UiState& state, const std::string& key, float fallback)
{
  const auto& block = state.bank.presets[state.activePreset].blocks[state.selectedBlock];
  return block.params.value(key, fallback);
}

void stepSelectedParam(UiState& state, const std::string& key, float delta, float low, float high, float fallback)
{
  setSelectedBlockParam(state, key, std::clamp(selectedParamValue(state, key, fallback) + delta, low, high));
}

void onCabLevelDown(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepSelectedParam(*context->state, "levelDb", -1.0f, -60.0f, 12.0f, 0.0f);
  redraw(context);
}

void onCabLevelUp(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepSelectedParam(*context->state, "levelDb", 1.0f, -60.0f, 12.0f, 0.0f);
  redraw(context);
}

void onCabMixDown(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepSelectedParam(*context->state, "mix", -0.05f, 0.0f, 1.0f, 1.0f);
  redraw(context);
}

void onCabMixUp(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  stepSelectedParam(*context->state, "mix", 0.05f, 0.0f, 1.0f, 1.0f);
  redraw(context);
}

void onBlockClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  selectBlock(*context->state, context->index);
  redraw(context);
}

std::size_t chainSlotFromPoint(const UiState& state, const lv_point_t& point)
{
  const auto blockCount = state.bank.presets[state.activePreset].blocks.size();
  if (blockCount == 0) {
    return 0;
  }

  const int row = point.y >= 218 ? 1 : 0;
  const int col = std::clamp((point.x - 20) / 172, 0, 3);
  return std::min<std::size_t>(static_cast<std::size_t>(row * 4 + col), blockCount - 1);
}

std::size_t insertSlotFromPoint(const UiState& state, const lv_point_t& point)
{
  const auto blockCount = state.bank.presets[state.activePreset].blocks.size();
  const int row = point.y >= 218 ? 1 : 0;
  const int col = std::clamp((point.x - 20) / 172, 0, 3);
  return std::min<std::size_t>(static_cast<std::size_t>(row * 4 + col), blockCount);
}

bool pointInChain(const lv_point_t& point)
{
  const bool insideX = point.x >= 20 && point.x <= 780;
  const bool insideTop = point.y >= 86 && point.y <= 204;
  const bool insideBottom = point.y >= 232 && point.y <= 350;
  return insideX && (insideTop || insideBottom);
}

void moveToFront(lv_obj_t* object)
{
  lv_obj_t* parent = lv_obj_get_parent(object);
  lv_obj_move_to_index(object, static_cast<int32_t>(lv_obj_get_child_count(parent)) - 1);
}

void placeDragIndicatorAtSlot(UiEventContext* context, std::size_t slot)
{
  const auto row = static_cast<int>(slot / 4);
  const auto col = static_cast<int>(slot % 4);

  if (!context->indicator) {
    context->indicator = lv_obj_create(context->ui->canvas());
    lv_obj_set_size(context->indicator, 5, 104);
    lv_obj_set_style_bg_color(context->indicator, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(context->indicator, 0, 0);
    lv_obj_set_style_radius(context->indicator, 3, 0);
  }

  lv_obj_set_pos(context->indicator, 28 + col * 172, row == 0 ? 93 : 239);
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

void globalControl(lv_obj_t* parent, const std::string& name, float value, int x,
                   lv_event_cb_t down, lv_event_cb_t up, UiEventContext* context)
{
  label(parent, name + " " + std::to_string(static_cast<int>(value)) + " dB",
        LV_ALIGN_TOP_LEFT, x, 0, &lv_font_montserrat_18, muted);
  lv_obj_t* minus = button(parent, "-");
  lv_obj_set_size(minus, 36, 32);
  lv_obj_align(minus, LV_ALIGN_TOP_LEFT, x, 28);
  lv_obj_add_event_cb(minus, down, LV_EVENT_CLICKED, context);

  lv_obj_t* plus = button(parent, "+");
  lv_obj_set_size(plus, 36, 32);
  lv_obj_align(plus, LV_ALIGN_TOP_LEFT, x + 42, 28);
  lv_obj_add_event_cb(plus, up, LV_EVENT_CLICKED, context);
}

lv_obj_t* button(lv_obj_t* parent, const std::string& value)
{
  lv_obj_t* object = lv_button_create(parent);
  lv_obj_set_style_radius(object, 8, 0);
  lv_obj_t* buttonLabel = lv_label_create(object);
  lv_label_set_text(buttonLabel, value.c_str());
  setText(buttonLabel);
  lv_obj_center(buttonLabel);
  return object;
}

} // namespace

LvglUi::LvglUi(UiActions actions)
  : actions_(std::move(actions))
{
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

  // The UI is authored on an 800x480 design grid. Rather than re-flow every
  // widget for the panel, build it on a fixed 800x480 canvas and scale that
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
        LV_ALIGN_BOTTOM_LEFT, 18, -14, &lv_font_montserrat_18, color);
}

void LvglUi::renderPresetMode(lv_obj_t* root, UiState& state)
{
  label(root, state.bank.name, LV_ALIGN_TOP_MID, 0, 18, &lv_font_montserrat_28);
  label(root, "Master " + std::to_string(state.masterVolume) + "%", LV_ALIGN_TOP_LEFT, 18, 18,
        &lv_font_montserrat_18, muted);

  lv_obj_t* edit = button(root, "Edit");
  lv_obj_set_size(edit, 96, 44);
  lv_obj_align(edit, LV_ALIGN_TOP_RIGHT, -18, 14);
  lv_obj_add_event_cb(edit, onEditModeClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* grid = lv_obj_create(root);
  lv_obj_set_size(grid, 760, 360);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -18);
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
    lv_obj_set_style_bg_color(preset, lv_color_hex(i == state.activePreset ? 0x143a32 : 0x212733), 0);
    lv_obj_set_style_border_color(preset, lv_color_hex(i == state.activePreset ? accent : 0x303744), 0);
    lv_obj_set_style_border_width(preset, i == state.activePreset ? 3 : 1, 0);
    lv_obj_add_event_cb(preset, onPresetClicked, LV_EVENT_CLICKED, remember(state, i));
  }
  telemetryLine(root, state.telemetry, state.effectsBypassed);
}

void LvglUi::renderEditMode(lv_obj_t* root, UiState& state)
{
  label(root, state.bank.presets[state.activePreset].name, LV_ALIGN_TOP_MID, 0, 18, &lv_font_montserrat_28);

  lv_obj_t* presets = button(root, "Presets");
  lv_obj_set_size(presets, 112, 44);
  lv_obj_align(presets, LV_ALIGN_TOP_LEFT, 18, 14);
  lv_obj_add_event_cb(presets, onPresetModeClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* globalButton = button(root, "Global");
  lv_obj_set_size(globalButton, 112, 44);
  lv_obj_align(globalButton, LV_ALIGN_TOP_RIGHT, -264, 14);
  lv_obj_add_event_cb(globalButton, onGlobalParamsClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* save = button(root, state.dirty ? "Save*" : "Save");
  lv_obj_set_size(save, 96, 44);
  lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -140, 14);
  lv_obj_set_style_bg_color(save, lv_color_hex(state.dirty ? 0x25634f : 0x2b3442), 0);
  lv_obj_add_event_cb(save, onSaveClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* blocksButton = button(root, "Blocks");
  lv_obj_set_size(blocksButton, 112, 44);
  lv_obj_align(blocksButton, LV_ALIGN_TOP_RIGHT, -18, 14);
  lv_obj_add_event_cb(blocksButton, onOpenBlockDrawer, LV_EVENT_CLICKED, remember(state));

  const auto& blocks = state.bank.presets[state.activePreset].blocks;

  lv_obj_t* top = lv_obj_create(root);
  lv_obj_t* bottom = lv_obj_create(root);
  for (lv_obj_t* row : {top, bottom}) {
    lv_obj_set_size(row, 760, 118);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    stylePanel(row, 0x141820);
    lv_obj_set_style_pad_all(row, 14, 0);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  }
  lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 86);
  lv_obj_align(bottom, LV_ALIGN_TOP_MID, 0, 232);
  label(root, "Input", LV_ALIGN_TOP_LEFT, 28, 64, &lv_font_montserrat_18, muted);
  label(root, "Output", LV_ALIGN_TOP_RIGHT, -28, 360, &lv_font_montserrat_18, muted);

  for (std::size_t i = 0; i < blocks.size(); ++i) {
    const auto& block = blocks[i];
    lv_obj_t* row = i < 4 ? top : bottom;
    lv_obj_t* object = button(row, block.label + "\n" + block.assetName);
    lv_obj_set_size(object, 160, 84);
    lv_obj_set_style_bg_color(object, lv_color_hex(block.enabled ? 0x243044 : 0x262626), 0);
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
    renderParamDrawer(root, state);
  }
}

void LvglUi::renderBlockDrawer(lv_obj_t* root, UiState& state)
{
  lv_obj_t* drawer = lv_obj_create(root);
  lv_obj_set_size(drawer, 280, 480);
  lv_obj_align(drawer, LV_ALIGN_LEFT_MID, 0, 0);
  stylePanel(drawer, panel);
  lv_obj_set_style_radius(drawer, 0, 0);
  lv_obj_set_style_border_side(drawer, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_pad_all(drawer, 14, 0);
  // Content fits; the inner list scrolls on its own. A scrollable drawer would
  // steal taps on the close button on a jittery finger touch.
  lv_obj_remove_flag(drawer, LV_OBJ_FLAG_SCROLLABLE);

  label(drawer, "Blocks", LV_ALIGN_TOP_LEFT, 0, 0, &lv_font_montserrat_22);
  lv_obj_t* close = button(drawer, "X");
  lv_obj_set_size(close, 40, 36);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -4);
  lv_obj_add_event_cb(close, onCloseBlockDrawer, LV_EVENT_CLICKED, remember(state));

  static constexpr std::array filters = {
    std::pair{"All", "all"}, std::pair{"Amps", "amps"}, std::pair{"Cabs", "cabs"},
    std::pair{"Dyn", "dynamics"}, std::pair{"Mod", "modulation"}, std::pair{"Time", "time"},
  };

  lv_obj_t* filterRow = lv_obj_create(drawer);
  lv_obj_set_size(filterRow, 244, 92);
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
    lv_obj_set_style_bg_color(filterButton,
                              lv_color_hex(state.categoryFilter == filter ? 0x25634f : 0x2b3442), 0);
    lv_obj_add_event_cb(filterButton, onFilterClicked, LV_EVENT_CLICKED, remember(state, 0, filter));
  }

  lv_obj_t* list = lv_obj_create(drawer);
  lv_obj_set_size(list, 244, 250);
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
    lv_obj_set_width(item, 230);
    lv_obj_set_height(item, 38);
    lv_obj_set_style_bg_color(item, lv_color_hex(0x242b36), 0);
    auto* context = remember(state, i);
    lv_obj_add_event_cb(item, onAssetPressed, LV_EVENT_PRESSED, context);
    lv_obj_add_event_cb(item, onAssetPressing, LV_EVENT_PRESSING, context);
    lv_obj_add_event_cb(item, onAssetReleased, LV_EVENT_RELEASED, context);
    lv_obj_add_event_cb(item, onAssetClicked, LV_EVENT_CLICKED, context);
  }
}

void LvglUi::renderParamDrawer(lv_obj_t* root, UiState& state)
{
  lv_obj_t* drawer = lv_obj_create(root);
  lv_obj_set_size(drawer, 800, 142);
  lv_obj_remove_flag(drawer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(drawer, LV_ALIGN_BOTTOM_MID, 0, 0);
  stylePanel(drawer, panelAlt);
  lv_obj_set_style_radius(drawer, 0, 0);
  lv_obj_set_style_border_side(drawer, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_pad_all(drawer, 16, 0);

  if (state.paramTarget == UiParamTarget::Globals) {
    const auto& global = state.bank.presets[state.activePreset].global;
    label(drawer, "Global", LV_ALIGN_TOP_LEFT, 0, 0, &lv_font_montserrat_22);
    auto* context = remember(state);
    globalControl(drawer, "In", global.inputGainDb, 0, onInputGainDown, onInputGainUp, context);
    globalControl(drawer, "Out", global.outputGainDb, 150, onOutputGainDown, onOutputGainUp, context);
    label(drawer, "Limit " + std::to_string(static_cast<int>(global.safetyLimitDb)) + " dB (fixed)",
          LV_ALIGN_TOP_LEFT, 300, 0, &lv_font_montserrat_18, muted);
    lv_obj_t* close = button(drawer, "X");
    lv_obj_set_size(close, 42, 36);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_add_event_cb(close, onCloseParamDrawer, LV_EVENT_CLICKED, remember(state));
    return;
  }

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    lv_obj_delete(drawer);
    return;
  }

  const auto& block = blocks[state.selectedBlock];
  label(drawer, block.label + " - " + block.assetName, LV_ALIGN_TOP_LEFT, 0, 0, &lv_font_montserrat_22);
  label(drawer, "Enabled", LV_ALIGN_BOTTOM_LEFT, 0, -8, &lv_font_montserrat_18, muted);
  label(drawer, block.enabled ? "On" : "Off", LV_ALIGN_BOTTOM_LEFT, 102, -8, &lv_font_montserrat_18, accent);

  if (block.type == "cab") {
    const float levelDb = block.params.value("levelDb", 0.0f);
    const float mix = block.params.value("mix", 1.0f);
    auto* context = remember(state);
    globalControl(drawer, "Level", levelDb, 190, onCabLevelDown, onCabLevelUp, context);
    label(drawer, "Mix " + std::to_string(static_cast<int>(mix * 100.0f)) + "%",
          LV_ALIGN_BOTTOM_LEFT, 330, -8, &lv_font_montserrat_18, muted);
    lv_obj_t* mixMinus = button(drawer, "-");
    lv_obj_set_size(mixMinus, 36, 32);
    lv_obj_align(mixMinus, LV_ALIGN_BOTTOM_LEFT, 430, -4);
    lv_obj_add_event_cb(mixMinus, onCabMixDown, LV_EVENT_CLICKED, context);
    lv_obj_t* mixPlus = button(drawer, "+");
    lv_obj_set_size(mixPlus, 36, 32);
    lv_obj_align(mixPlus, LV_ALIGN_BOTTOM_LEFT, 472, -4);
    lv_obj_add_event_cb(mixPlus, onCabMixUp, LV_EVENT_CLICKED, context);
  }

  lv_obj_t* close = button(drawer, "X");
  lv_obj_set_size(close, 42, 36);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -4);
  lv_obj_add_event_cb(close, onCloseParamDrawer, LV_EVENT_CLICKED, remember(state));
}

} // namespace ardor
