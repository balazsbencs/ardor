#include "ui/LvglUi.h"

#include "ui/LvglUiDrag.h"
#include "ui/LvglUiStyle.h"
#include "ui/fonts/OpenSansRegular.h"
#include "ui/fonts/OpenSansSemibold.h"

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ardor {
namespace {

using namespace lvgl_drag;
using namespace lvgl_ui;

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
constexpr std::array<std::pair<const char*, const char*>, 7> kDrawerFilters = {{
  {"All", "all"}, {"Amps", "amps"}, {"Cabs", "cabs"}, {"Utility", "utility"},
  {"Mod", "modulation"}, {"Delays", "delay"}, {"Reverbs", "reverb"},
}};

std::string assetRenderKey(const UiAsset& asset)
{
  return asset.type + "\x1f" + asset.blockType + "\x1f" + asset.mode + "\x1f"
    + asset.path + "\x1f" + asset.name;
}

void redraw(UiEventContext* context)
{
  context->ui->invalidate(UiChange::None);
}

void onCloseBlockDrawer(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  closeBlockDrawer(*context->state);
  redraw(context);
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
  if (context->state->blockInsertRig.has_value() && context->state->blockInsertLane.has_value()) {
    insertLaneAssetBlock(*context->state, context->index,
                         *context->state->blockInsertRig, *context->state->blockInsertLane,
                         context->state->blockInsertIndex);
  } else {
    insertAssetBlock(*context->state, context->index, context->state->blockInsertIndex);
  }
  const auto& blocks = context->state->bank.presets[context->state->activePreset].blocks;
  if (blocks.size() > before && context->state->selectedBlock < blocks.size()) {
    context->ui->highlightBlock(blocks[context->state->selectedBlock].id);
    context->ui->resetParameterPage();
  }
  redraw(context);
}

std::string assetDragText(const UiAsset& asset)
{
  if (asset.blockType == "dualRig") {
    return "Split\nLeft / Right";
  }
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
  context->ui->autoScrollChainForDrag(*context->state, point);
  placeDragGhost(context, point);
  if (pointInVisibleChain(*context->state, point)) {
    placeDragIndicatorAtSlot(context, context->ui->chainInsertionSlotAtPoint(point));
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
  const auto target = context->ui->chainInsertionSlotAtPoint(point);
  clearDragVisuals(context);
  if (!droppedOnChain) {
    context->ui->endInteraction();
    return;
  }

  const auto before = context->state->bank.presets[context->state->activePreset].blocks.size();
  if (context->state->blockInsertRig.has_value() && context->state->blockInsertLane.has_value()) {
    insertLaneAssetBlock(*context->state, context->index,
                         *context->state->blockInsertRig, *context->state->blockInsertLane,
                         context->state->blockInsertIndex);
  } else {
    insertAssetBlock(*context->state, context->index, target);
  }
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

} // namespace

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
  std::unordered_map<std::string, std::vector<std::size_t>> oldIndicesByKey;
  oldIndicesByKey.reserve(oldKeys.size());
  for (std::size_t i = 0; i < oldKeys.size(); ++i) {
    oldIndicesByKey[oldKeys[i]].push_back(i);
  }
  std::unordered_map<std::string, std::size_t> nextIndexByKey;
  nextIndexByKey.reserve(oldIndicesByKey.size());
  std::vector<bool> used(oldKeys.size(), false);
  std::vector<lv_obj_t*> buttons(state.assets.size(), nullptr);
  std::vector<UiEventContext*> contexts(state.assets.size(), nullptr);
  std::vector<std::string> keys;
  keys.reserve(state.assets.size());

  for (std::size_t i = 0; i < state.assets.size(); ++i) {
    const auto key = assetRenderKey(state.assets[i]);
    keys.push_back(key);
    const auto oldIndices = oldIndicesByKey.find(key);
    if (oldIndices != oldIndicesByKey.end()) {
      auto& next = nextIndexByKey[key];
      if (next < oldIndices->second.size()) {
        const auto oldIndex = oldIndices->second[next++];
        used[oldIndex] = true;
        buttons[i] = oldButtons[oldIndex];
        contexts[i] = oldContexts[oldIndex];
      }
    }
    if (buttons[i]) continue;

    lv_obj_t* item = button(drawerAssetList_, state.assets[i].name);
    lv_obj_set_width(item, kBlockDrawerContentWidth - 14);
    lv_obj_set_height(item, kDrawerAssetButtonHeight);
    lv_obj_set_style_min_height(item, kDrawerAssetButtonHeight, 0);
    styleSurface(item, panel);
    // Category accent bar (child index 1); colour is kept in sync below when
    // rows are recycled across filter changes.
    lv_obj_t* assetBar = lv_obj_create(item);
    lv_obj_set_size(assetBar, 5, kDrawerAssetButtonHeight - 22);
    lv_obj_align(assetBar, LV_ALIGN_LEFT_MID, 6, 0);
    styleSurface(assetBar, categoryColor(state.assets[i].type));
    lv_obj_set_style_border_width(assetBar, 0, 0);
    lv_obj_set_style_shadow_width(assetBar, 0, 0);
    lv_obj_set_style_radius(assetBar, 2, 0);
    lv_obj_remove_flag(assetBar, LV_OBJ_FLAG_CLICKABLE);
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
    lv_obj_set_style_bg_color(lv_obj_get_child(buttons[i], 1),
                              lv_color_hex(categoryColor(state.assets[i].type)), 0);
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
                                lv_color_hex(selected ? categoryColor(kDrawerFilters[i].second) : text), 0);
  }

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  const bool insertingLane = state.blockInsertRig.has_value() && state.blockInsertLane.has_value();
  bool chainFull = blocks.size() >= kMaxEffectBlocks;
  if (insertingLane && *state.blockInsertRig < blocks.size()
      && *state.blockInsertLane < blocks[*state.blockInsertRig].lanes.size()) {
    chainFull = blocks[*state.blockInsertRig].lanes[*state.blockInsertLane].size() >= kMaxEffectBlocks;
  }
  const bool alreadySplit = std::any_of(blocks.begin(), blocks.end(), [](const UiBlock& block) {
    return block.enabled && (block.type == "dualRig" || block.type == "dualAmp");
  });
  const bool standaloneAmp = std::any_of(blocks.begin(), blocks.end(), [](const UiBlock& block) {
    return block.enabled && (block.type == "nam" || block.type == "cab");
  });
  if (drawerInstructionLabel_) {
    lv_label_set_text(drawerInstructionLabel_, chainFull
      ? "Chain full - delete a block to add"
      : (insertingLane ? "Choose an effect for this lane"
                       : "Choose an effect or Split Left / Right"));
    lv_obj_set_style_text_color(drawerInstructionLabel_,
                                lv_color_hex(chainFull ? 0xf97373 : muted), 0);
  }
  for (std::size_t i = 0; i < drawerAssetButtons_.size() && i < state.assets.size(); ++i) {
    lv_obj_t* item = drawerAssetButtons_[i];
    const bool visible = state.categoryFilter == "all" || state.assets[i].type == state.categoryFilter;
    if (visible) lv_obj_remove_flag(item, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
    const bool splitUnavailable = state.assets[i].blockType == "dualRig"
      && (insertingLane || alreadySplit || standaloneAmp);
    if (chainFull || splitUnavailable) lv_obj_add_state(item, LV_STATE_DISABLED);
    else lv_obj_remove_state(item, LV_STATE_DISABLED);
  }
  if (drawerAssetList_) {
    lv_obj_update_layout(drawerAssetList_);
    lv_obj_scroll_to_y(drawerAssetList_, state.assetScrollOffset, LV_ANIM_OFF);
    state.assetScrollOffset = lv_obj_get_scroll_y(drawerAssetList_);
  }
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

  const bool insertingLane = state.blockInsertRig.has_value() && state.blockInsertLane.has_value();
  const std::string drawerTitle = insertingLane
    ? std::string{"Add to "} + (*state.blockInsertLane == 0 ? "Left" : "Right")
    : "Insert block";
  label(drawer, drawerTitle, LV_ALIGN_TOP_LEFT, 0, 0, &ardor_font_open_sans_semibold_22);
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
                                lv_color_hex(state.categoryFilter == filter ? categoryColor(filter) : text), 0);
    lv_obj_add_event_cb(filterButton, onFilterClicked, LV_EVENT_CLICKED, remember(state, 0, filter));
    drawerCategoryButtons_[i] = filterButton;
  }

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  bool chainFull = blocks.size() >= kMaxEffectBlocks;
  if (insertingLane && *state.blockInsertRig < blocks.size()
      && *state.blockInsertLane < blocks[*state.blockInsertRig].lanes.size()) {
    chainFull = blocks[*state.blockInsertRig].lanes[*state.blockInsertLane].size() >= kMaxEffectBlocks;
  }
  const bool alreadySplit = std::any_of(blocks.begin(), blocks.end(), [](const UiBlock& block) {
    return block.enabled && (block.type == "dualRig" || block.type == "dualAmp");
  });
  const bool standaloneAmp = std::any_of(blocks.begin(), blocks.end(), [](const UiBlock& block) {
    return block.enabled && (block.type == "nam" || block.type == "cab");
  });

  lv_obj_t* separator = lv_obj_create(drawer);
  lv_obj_set_size(separator, kBlockDrawerContentWidth, 1);
  lv_obj_align(separator, LV_ALIGN_TOP_LEFT, 0, kDrawerSeparatorY);
  styleSurface(separator, 0x3a3a3a);
  lv_obj_set_style_radius(separator, 0, 0);
  lv_obj_remove_flag(separator, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(separator, LV_OBJ_FLAG_CLICKABLE);

  drawerInstructionLabel_ = label(drawer,
    chainFull ? "Chain full - delete a block to add"
              : (insertingLane ? "Choose an effect for this lane"
                               : "Choose an effect or Split Left / Right"),
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
    lv_obj_t* itemTitle = lv_obj_get_child(item, 0);
    lv_obj_set_width(item, kBlockDrawerContentWidth - 14);
    lv_obj_set_height(item, kDrawerAssetButtonHeight);
    lv_obj_set_style_min_height(item, kDrawerAssetButtonHeight, 0);
    styleSurface(item, panel);
    // Category accent bar (kept at child index 1 in both build and sync paths).
    lv_obj_t* assetBar = lv_obj_create(item);
    lv_obj_set_size(assetBar, 5, kDrawerAssetButtonHeight - 22);
    lv_obj_align(assetBar, LV_ALIGN_LEFT_MID, 6, 0);
    styleSurface(assetBar, categoryColor(asset.type));
    lv_obj_set_style_border_width(assetBar, 0, 0);
    lv_obj_set_style_shadow_width(assetBar, 0, 0);
    lv_obj_set_style_radius(assetBar, 2, 0);
    lv_obj_remove_flag(assetBar, LV_OBJ_FLAG_CLICKABLE);
    const bool splitUnavailable = asset.blockType == "dualRig"
      && (insertingLane || alreadySplit || standaloneAmp);
    if (asset.blockType == "dualRig") {
      lv_obj_set_style_border_color(item, lv_color_hex(accent), 0);
      lv_obj_set_style_border_width(item, 1, 0);
      lv_obj_set_style_text_color(itemTitle, lv_color_hex(accent), 0);
      if (splitUnavailable) {
        const char* reason = insertingLane ? "No nested Split"
          : alreadySplit ? "A Split already exists" : "Remove standalone NAM / IR first";
        lv_obj_set_width(itemTitle, kBlockDrawerContentWidth - 56);
        lv_label_set_long_mode(itemTitle, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(itemTitle, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(itemTitle, LV_ALIGN_CENTER, 0, -14);
        lv_obj_t* reasonLabel = label(item, reason, LV_ALIGN_CENTER, 0, 15,
                                      &ardor_font_open_sans_regular_18, warning);
        lv_obj_set_width(reasonLabel, kBlockDrawerContentWidth - 56);
        lv_label_set_long_mode(reasonLabel, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(reasonLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(reasonLabel, LV_ALIGN_CENTER, 0, 15);
      }
    }
    if (chainFull || splitUnavailable) lv_obj_add_state(item, LV_STATE_DISABLED);
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
