#include "ui/LvglUi.h"

#include <algorithm>
#include <array>
#include <string>

namespace ardor {

namespace {

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
  context->ui->build(lv_screen_active(), *context->state);
}

void onPresetClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  selectPreset(*context->state, context->index);
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

void onBlockReleased(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }

  lv_point_t point{};
  lv_indev_get_point(input, &point);
  const auto target = chainSlotFromPoint(*context->state, point);
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
  appendAssetBlock(*context->state, context->index);
  redraw(context);
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

UiEventContext* LvglUi::remember(UiState& state, std::size_t index, std::string filter)
{
  contexts_.push_back({this, &state, index, std::move(filter)});
  return &contexts_.back();
}

void LvglUi::build(lv_obj_t* root, UiState& state)
{
  contexts_.clear();
  contexts_.reserve(128);
  lv_obj_clean(root);
  lv_obj_set_style_bg_color(root, lv_color_hex(bg), 0);

  if (state.mode == UiMode::Preset) {
    renderPresetMode(root, state);
  } else {
    renderEditMode(root, state);
  }
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
}

void LvglUi::renderEditMode(lv_obj_t* root, UiState& state)
{
  label(root, state.bank.presets[state.activePreset].name, LV_ALIGN_TOP_MID, 0, 18, &lv_font_montserrat_28);

  lv_obj_t* presets = button(root, "Presets");
  lv_obj_set_size(presets, 112, 44);
  lv_obj_align(presets, LV_ALIGN_TOP_LEFT, 18, 14);
  lv_obj_add_event_cb(presets, onPresetModeClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* blocksButton = button(root, "Blocks");
  lv_obj_set_size(blocksButton, 112, 44);
  lv_obj_align(blocksButton, LV_ALIGN_TOP_RIGHT, -18, 14);
  lv_obj_add_event_cb(blocksButton, onOpenBlockDrawer, LV_EVENT_CLICKED, remember(state));

  const auto& blocks = state.bank.presets[state.activePreset].blocks;

  lv_obj_t* top = lv_obj_create(root);
  lv_obj_t* bottom = lv_obj_create(root);
  for (lv_obj_t* row : {top, bottom}) {
    lv_obj_set_size(row, 760, 118);
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
    lv_obj_add_event_cb(object, onBlockClicked, LV_EVENT_CLICKED, remember(state, i));
    lv_obj_add_event_cb(object, onBlockReleased, LV_EVENT_RELEASED, remember(state, i));
  }

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
  lv_obj_set_size(drawer, 280, 420);
  lv_obj_align(drawer, LV_ALIGN_LEFT_MID, 14, 0);
  stylePanel(drawer, panel);
  lv_obj_set_style_pad_all(drawer, 14, 0);
  lv_obj_set_scroll_dir(drawer, LV_DIR_VER);

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
    lv_obj_add_event_cb(item, onAssetClicked, LV_EVENT_CLICKED, remember(state, i));
  }
}

void LvglUi::renderParamDrawer(lv_obj_t* root, UiState& state)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (state.selectedBlock >= blocks.size()) {
    return;
  }

  const auto& block = blocks[state.selectedBlock];
  lv_obj_t* drawer = lv_obj_create(root);
  lv_obj_set_size(drawer, 720, 128);
  lv_obj_align(drawer, LV_ALIGN_BOTTOM_MID, 0, -14);
  stylePanel(drawer, panelAlt);
  lv_obj_set_style_pad_all(drawer, 16, 0);

  label(drawer, block.label + " - " + block.assetName, LV_ALIGN_TOP_LEFT, 0, 0, &lv_font_montserrat_22);
  label(drawer, "Enabled", LV_ALIGN_BOTTOM_LEFT, 0, -8, &lv_font_montserrat_18, muted);
  label(drawer, block.enabled ? "On" : "Off", LV_ALIGN_BOTTOM_LEFT, 102, -8, &lv_font_montserrat_18, accent);

  lv_obj_t* close = button(drawer, "X");
  lv_obj_set_size(close, 42, 36);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, -4);
  lv_obj_add_event_cb(close, onCloseParamDrawer, LV_EVENT_CLICKED, remember(state));
}

} // namespace ardor
