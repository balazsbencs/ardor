#include "ui/LvglUi.h"

#include "ui/LvglUiDrag.h"
#include "ui/LvglUiStyle.h"

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
constexpr int kDrawerHeaderHeight = 46;
constexpr int kDrawerCloseSize = 40;
constexpr int kCategoryColumns = 4;
constexpr int kCategoryButtonWidth = 105;
constexpr int kCategoryButtonHeight = 58;
constexpr int kCategoryButtonGap = 8;
constexpr int kDrawerCategoryTop = 60;
constexpr int kDrawerCategoryHeight = 2 * kCategoryButtonHeight + kCategoryButtonGap;
constexpr int kDrawerSeparatorY = kDrawerCategoryTop + kDrawerCategoryHeight + 14;
// The count row replaces the old plain instruction line: it keeps the same
// chain-state guidance on the left (chain full / lane target) but now pairs
// it with a "N available" readout on the right, per the drawer mockup.
constexpr int kDrawerCountY = kDrawerSeparatorY + 14;
constexpr int kDrawerCountHeight = 22;
constexpr int kDrawerListTop = kDrawerCountY + kDrawerCountHeight + 10;
constexpr int kDrawerFooterHeight = 40;
constexpr int kDrawerListHeight =
  kBlockDrawerContentHeight - kDrawerListTop - kDrawerFooterHeight - 10;
constexpr int kDrawerAssetButtonHeight = 72;
// Family tick (13 left inset + 26 wide) then a 14 px gutter to the text column.
constexpr int kDrawerItemTextX = 53;
constexpr int kDrawerGripBarWidth = 16;
constexpr std::array<std::pair<const char*, const char*>, 7> kDrawerFilters = {{
  {"All", "all"}, {"Amps", "amps"}, {"Cabs", "cabs"}, {"Utility", "utility"},
  {"Mod", "modulation"}, {"Delays", "delay"}, {"Reverbs", "reverb"},
}};

std::size_t visibleAssetCount(const UiState& state)
{
  return static_cast<std::size_t>(std::count_if(state.assets.begin(), state.assets.end(),
    [&state](const UiAsset& asset) {
      return state.categoryFilter == "all" || asset.type == state.categoryFilter;
    }));
}

std::string drawerFilterDisplayName(const std::string& filter)
{
  if (filter == "all") return "All modules";
  for (const auto& [name, key] : kDrawerFilters) {
    if (key == filter) return std::string{name} + " modules";
  }
  return "Modules";
}

// The count row's left side: chain-state guidance takes priority when there
// is any (chain full / lane target); otherwise it names the active filter.
// Uppercased to match the drawer's other engraved legend text.
std::string drawerInstructionText(const UiState& state, bool chainFull, bool insertingLane)
{
  return uppercase(chainFull ? "Chain full - delete a block to add"
    : (insertingLane ? "Choose an effect for this lane"
                     : drawerFilterDisplayName(state.categoryFilter)));
}

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

// Builds the family tick, subtitle line and trailing drag-grip glyph shared
// by both the initial drawer build and the recycle path below. Child order on
// `item` is fixed: 0 title (from button()), 1 tick, 2 subtitle, 3 grip -- the
// recycle path indexes into this by position, so it must stay in lockstep.
lv_obj_t* decorateDrawerItem(lv_obj_t* item, const UiAsset& asset)
{
  const int columnWidth = kBlockDrawerContentWidth - 14 - kDrawerItemTextX - kDrawerGripBarWidth - 27;

  // Name and subtitle sit inline on one line, per the mockup ("CHORUS
  // Modulation · 6 controls..."), not stacked -- the title auto-sizes to its
  // text and the subtitle picks up right where it ends.
  lv_obj_t* itemTitle = lv_obj_get_child(item, 0);
  lv_label_set_text(itemTitle, uppercase(asset.name).c_str());
  lv_obj_set_width(itemTitle, LV_SIZE_CONTENT);
  lv_obj_align(itemTitle, LV_ALIGN_LEFT_MID, kDrawerItemTextX, 0);

  lv_obj_t* tick = lv_obj_create(item);
  lv_obj_set_size(tick, 26, 3);
  lv_obj_align(tick, LV_ALIGN_LEFT_MID, 13, 0);
  styleSurface(tick, categoryColor(asset.type));
  lv_obj_set_style_border_width(tick, 0, 0);
  lv_obj_set_style_shadow_width(tick, 0, 0);
  lv_obj_set_style_radius(tick, 0, 0);
  lv_obj_remove_flag(tick, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_update_layout(item);
  const int titleWidth = lv_obj_get_width(itemTitle);
  const int subtitleX = kDrawerItemTextX + titleWidth + 8;
  const int subtitleWidth = std::max(0, columnWidth - titleWidth - 8);

  lv_obj_t* subtitle = label(item, asset.subtitle, LV_ALIGN_LEFT_MID, subtitleX, 1,
                             &ardor_font_saira_light_12, muted);
  lv_obj_set_width(subtitle, subtitleWidth);
  lv_label_set_long_mode(subtitle, LV_LABEL_LONG_CLIP);
  lv_obj_remove_flag(subtitle, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t* grip = lv_obj_create(item);
  lv_obj_set_size(grip, kDrawerGripBarWidth, 12);
  lv_obj_align(grip, LV_ALIGN_RIGHT_MID, -13, 0);
  lv_obj_set_style_bg_opa(grip, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grip, 0, 0);
  lv_obj_set_style_pad_all(grip, 0, 0);
  lv_obj_remove_flag(grip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(grip, LV_OBJ_FLAG_SCROLLABLE);
  for (int bar = 0; bar < 3; ++bar) {
    lv_obj_t* gripBar = lv_obj_create(grip);
    lv_obj_set_size(gripBar, kDrawerGripBarWidth, 2);
    lv_obj_set_pos(gripBar, 0, bar * 5);
    styleSurface(gripBar, rule);
    lv_obj_set_style_border_width(gripBar, 0, 0);
    lv_obj_set_style_radius(gripBar, 0, 0);
    lv_obj_remove_flag(gripBar, LV_OBJ_FLAG_CLICKABLE);
  }
  return subtitle;
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
  drawerAssetSubtitleLabels_.clear();
  renderedAssetKeys_.clear();
  drawerAssetList_ = nullptr;
  drawerInstructionLabel_ = nullptr;
  drawerCountLabel_ = nullptr;
  drawerFooterCountLabel_ = nullptr;
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
  const auto oldSubtitles = drawerAssetSubtitleLabels_;
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
  std::vector<lv_obj_t*> subtitles(state.assets.size(), nullptr);
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
        subtitles[i] = oldSubtitles[oldIndex];
      }
    }
    if (buttons[i]) continue;

    lv_obj_t* item = button(drawerAssetList_, state.assets[i].name);
    lv_obj_set_width(item, kBlockDrawerContentWidth - 14);
    lv_obj_set_height(item, kDrawerAssetButtonHeight);
    lv_obj_set_style_min_height(item, kDrawerAssetButtonHeight, 0);
    styleSurface(item, panel);
    lv_obj_t* subtitle = decorateDrawerItem(item, state.assets[i]);
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
    subtitles[i] = subtitle;
  }

  for (std::size_t j = 0; j < oldButtons.size(); ++j) {
    if (used[j]) continue;
    lv_obj_delete(oldButtons[j]);
    UiEventContext* removed = oldContexts[j];
    contexts_.remove_if([removed](const UiEventContext& context) { return &context == removed; });
  }

  for (std::size_t i = 0; i < buttons.size(); ++i) {
    lv_obj_move_to_index(buttons[i], static_cast<int32_t>(i));
    lv_label_set_text(lv_obj_get_child(buttons[i], 0), uppercase(state.assets[i].name).c_str());
    lv_obj_set_style_bg_color(lv_obj_get_child(buttons[i], 1),
                              lv_color_hex(categoryColor(state.assets[i].type)), 0);
    lv_label_set_text(subtitles[i], state.assets[i].subtitle.c_str());
    contexts[i]->index = i;
    contexts[i]->controlledObject = drawerAssetList_;
  }
  drawerAssetButtons_ = std::move(buttons);
  drawerAssetContexts_ = std::move(contexts);
  drawerAssetSubtitleLabels_ = std::move(subtitles);
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
    lv_label_set_text(drawerInstructionLabel_,
      drawerInstructionText(state, chainFull, insertingLane).c_str());
    lv_obj_set_style_text_color(drawerInstructionLabel_,
                                lv_color_hex(chainFull ? danger : muted), 0);
  }
  const std::size_t visibleCount = visibleAssetCount(state);
  if (drawerCountLabel_) {
    lv_label_set_text(drawerCountLabel_, uppercase(std::to_string(visibleCount) + " available").c_str());
  }
  if (drawerFooterCountLabel_) {
    lv_label_set_text(drawerFooterCountLabel_,
      (std::to_string(visibleCount) + " / " + std::to_string(state.assets.size())).c_str());
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
  lv_obj_set_style_bg_color(scrim, lv_color_hex(panelAlt), 0);
  // ~55% -- the chain stays legible behind the drawer so the chosen insertion
  // point is never hidden while a module is being picked.
  lv_obj_set_style_bg_opa(scrim, 140, 0);
  lv_obj_set_style_border_width(scrim, 0, 0);
  lv_obj_set_style_radius(scrim, 0, 0);
  lv_obj_remove_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scrim, onCloseBlockDrawer, LV_EVENT_PRESSED, remember(state));

  lv_obj_t* drawer = lv_obj_create(root);
  lv_obj_set_size(drawer, kBlockDrawerWidth, kDesignHeight - kStatusBarHeight);
  lv_obj_align(drawer, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(drawer, lv_color_hex(panelAlt), 0);
  lv_obj_set_style_border_color(drawer, lv_color_hex(rule), 0);
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
    : "Modules";
  label(drawer, uppercase(drawerTitle), LV_ALIGN_TOP_LEFT, 0, 6, &ardor_font_saira_cond_semibold_22);

  // Close is a bare glyph, not a chrome button -- lettering-first per
  // docs/lvgl-ui-redesign-spec.md §8.11, the only other one being the rail's
  // "+" insertion point. `button()` still gives it a real hit box and press
  // feedback; only the visible chrome is stripped. Saira Condensed SemiBold
  // has no U+2715 (✕) glyph, so the compiled font's × (U+00D7) stands in --
  // same close-mark reading, already in the subset every other label uses.
  lv_obj_t* close = button(drawer, "\xC3\x97" /* × */);
  lv_obj_set_size(close, kDrawerCloseSize, kDrawerCloseSize);
  lv_obj_align(close, LV_ALIGN_TOP_RIGHT, 0, 8);
  lv_obj_set_style_bg_opa(close, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(close, 0, 0);
  lv_obj_set_style_border_width(close, 2, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(lv_obj_get_child(close, 0), lv_color_hex(muted), 0);
  lv_obj_add_event_cb(close, onCloseBlockDrawer, LV_EVENT_PRESSED, remember(state));

  label(drawer, "Tap to insert", LV_ALIGN_TOP_RIGHT, -(kDrawerCloseSize + 10), 12,
       &ardor_font_saira_cond_medium_18, muted);

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
    styleSurface(filterButton, state.categoryFilter == filter ? panel : panelAlt);
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
  styleSurface(separator, rule);
  lv_obj_set_style_radius(separator, 0, 0);
  lv_obj_remove_flag(separator, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(separator, LV_OBJ_FLAG_CLICKABLE);

  const std::size_t visibleCount = visibleAssetCount(state);
  drawerInstructionLabel_ = label(drawer, drawerInstructionText(state, chainFull, insertingLane),
    LV_ALIGN_TOP_LEFT, 0, kDrawerCountY, &ardor_font_saira_cond_medium_18,
    chainFull ? danger : muted);
  lv_obj_set_width(drawerInstructionLabel_, kBlockDrawerContentWidth - 120);
  lv_label_set_long_mode(drawerInstructionLabel_, LV_LABEL_LONG_CLIP);
  drawerCountLabel_ = label(drawer, uppercase(std::to_string(visibleCount) + " available"),
    LV_ALIGN_TOP_RIGHT, 0, kDrawerCountY, &ardor_font_saira_cond_semibold_11, disabled);

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
    lv_obj_t* subtitle = decorateDrawerItem(item, asset);
    lv_obj_t* itemTitle = lv_obj_get_child(item, 0);
    const bool splitUnavailable = asset.blockType == "dualRig"
      && (insertingLane || alreadySplit || standaloneAmp);
    if (asset.blockType == "dualRig") {
      lv_obj_set_style_border_color(item, lv_color_hex(text), 0);
      lv_obj_set_style_border_width(item, 1, 0);
      lv_obj_set_style_text_color(itemTitle, lv_color_hex(text), 0);
      if (splitUnavailable) {
        const char* reason = insertingLane ? "No nested Split"
          : alreadySplit ? "A Split already exists" : "Remove standalone NAM / IR first";
        lv_obj_add_flag(subtitle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(itemTitle, kBlockDrawerContentWidth - 56);
        lv_label_set_long_mode(itemTitle, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(itemTitle, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(itemTitle, LV_ALIGN_CENTER, 0, -14);
        lv_obj_t* reasonLabel = label(item, reason, LV_ALIGN_CENTER, 0, 15,
                                      &ardor_font_saira_cond_medium_18, warning);
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
    drawerAssetSubtitleLabels_.push_back(subtitle);
    renderedAssetKeys_.push_back(assetRenderKey(asset));
  }

  lv_obj_update_layout(list);
  lv_obj_scroll_to_y(list, state.assetScrollOffset, LV_ANIM_OFF);
  state.assetScrollOffset = lv_obj_get_scroll_y(list);
  auto* scrollContext = remember(state);
  lv_obj_add_event_cb(list, onAssetListScrollBegin, LV_EVENT_SCROLL_BEGIN, scrollContext);
  lv_obj_add_event_cb(list, onAssetListScroll, LV_EVENT_SCROLL, scrollContext);
  lv_obj_add_event_cb(list, onAssetListScrollEnd, LV_EVENT_SCROLL_END, scrollContext);

  lv_obj_t* footer = lv_obj_create(drawer);
  lv_obj_set_size(footer, kBlockDrawerContentWidth, kDrawerFooterHeight);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_opa(footer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(footer, 1, 0);
  lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(footer, lv_color_hex(rule), 0);
  lv_obj_set_style_radius(footer, 0, 0);
  lv_obj_set_style_pad_all(footer, 0, 0);
  lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(footer, LV_OBJ_FLAG_CLICKABLE);
  label(footer, "DRAG ONTO THE RAIL TO PLACE EXACTLY", LV_ALIGN_LEFT_MID, 0, 0,
       &ardor_font_saira_cond_semibold_11, disabled);
  drawerFooterCountLabel_ = label(footer,
    std::to_string(visibleCount) + " / " + std::to_string(state.assets.size()),
    LV_ALIGN_RIGHT_MID, 0, 0, &ardor_font_saira_cond_semibold_11, disabled);
}

} // namespace ardor
