#include "ui/LvglUi.h"

#include "ui/LvglUiDrag.h"
#include "ui/LampBlack.h"
#include "ui/LvglUiStyle.h"
#include "ui/PresetChainStrip.h"
#include "ui/LvglChainLayout.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ardor {
namespace {

using namespace lvgl_drag;
using namespace lvgl_ui;

// Lamp Black module drawer (mockups/lvgl-taste/1-lamp-black.html): 480 px
// from the right with a bone-3 left rule. Positions are drawer-relative; the
// drawer's content starts 25 px in from its left edge.
constexpr int kDrawerX = kDesignWidth - kBlockDrawerWidth;
constexpr int kDrawerInset = 25;
constexpr int kBlockDrawerContentWidth = kBlockDrawerWidth - kDrawerInset - lb::kGutter;
constexpr int kDrawerTitleTop = 18;
constexpr int kDrawerCountTop = 28;
constexpr int kDrawerCloseY = 14;
constexpr int kDrawerSubtitleTop = 79;
constexpr int kCategoryColumns = 4;
constexpr int kCategoryButtonHeight = 52;
constexpr int kCategoryButtonGap = 8;
constexpr int kDrawerCategoryTop = 116;
constexpr int kDrawerFilterBarHeight = 4;
constexpr int kDrawerListTop = 250;
constexpr int kDrawerFooterHeight = 64;
constexpr int kDrawerListHeight = kDesignHeight - kDrawerFooterHeight - kDrawerListTop;
// Rows run 12 px past the content column on both sides so the pressed
// highlight can bleed, as the mockup's .item.hl does.
constexpr int kDrawerRowBleed = 12;
constexpr int kDrawerRowWidth = kBlockDrawerContentWidth + 2 * kDrawerRowBleed;
constexpr int kDrawerAssetButtonHeight = 72;
constexpr int kDrawerGroupHeight = 43;
constexpr int kDrawerGroupTextTop = 11;
constexpr int kDrawerCodeSize = 52;
constexpr int kDrawerCodeTop = 9;
constexpr int kDrawerItemTextX = kDrawerCodeSize + 14;
constexpr int kDrawerItemTitleTop = 4;
constexpr int kDrawerItemSubtitleTop = 36;
constexpr int kDrawerAddSize = 52;
constexpr int kDrawerScrollbarWidth = 4;
constexpr int kDrawerFooterTextTop = 20;
// Scrim: rgba(8, 9, 10, .72) over the chain.
constexpr std::uint32_t kScrimColor = 0x08090a;
constexpr lv_opa_t kScrimOpa = 184;
// Insert marker: a 44 px bone disc inside a 6 px ground ring and a 2 px
// bone ring, centred on the wire at the insertion point.
constexpr int kMarkerSize = 44;
constexpr int kMarkerRing = 6;
constexpr int kMarkerOuterRing = 2;
constexpr int kMarkerLabelTop = 383;
constexpr std::array<std::pair<const char*, const char*>, 8> kDrawerFilters = {{
  {"All", "all"}, {"Amps", "amps"}, {"Cabs", "cabs"}, {"Drive", "drive"},
  {"Utility", "utility"}, {"Mod", "modulation"}, {"Delays", "delay"},
  {"Reverbs", "reverb"},
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
std::string wdwAssetBlockType(const UiAsset& asset)
{
  if (!asset.blockType.empty()) return asset.blockType;
  if (asset.type == "amps") return "nam";
  if (asset.type == "cabs") return "cab";
  return asset.type;
}

bool wdwAssetAllowedOnLane(const UiAsset& asset, std::size_t lane)
{
  const auto blockType = wdwAssetBlockType(asset);
  if (lane == 0) {
    return blockType == "nam" || blockType == "cab"
      || blockType == "dynamics" || blockType == "eq"
      || blockType == "distortion" || blockType == "wah";
  }
  return blockType == "nam" || blockType == "cab"
    || blockType == "mod" || blockType == "delay"
    || blockType == "reverb" || blockType == "irreverb"
    || blockType == "stereo";
}

const UiBlock* laneTarget(const UiState& state)
{
  if (!state.blockInsertRig.has_value() || !state.blockInsertLane.has_value()) return nullptr;
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (*state.blockInsertRig >= blocks.size()) return nullptr;
  const auto& rig = blocks[*state.blockInsertRig];
  if (rig.type != "dualRig" || rig.params.value("routing", std::string{}) != "wdw") return nullptr;
  return &rig;
}

std::string laneAssetReason(const UiAsset& asset, const UiBlock* rig, std::size_t lane)
{
  if (!rig) return {};
  if (lane >= rig->lanes.size()) return "Invalid lane";
  const auto blockType = wdwAssetBlockType(asset);
  if (blockType == "dualRig" || blockType == "dualAmp") {
    return "No nested Split";
  }
  if (!wdwAssetAllowedOnLane(asset, lane)) {
    return std::string{"Not admitted on the "} + (lane == 0 ? "Dry" : "Wet") + " lane";
  }
  if ((blockType == "nam" || blockType == "cab")
      && std::any_of(rig->lanes[lane].begin(), rig->lanes[lane].end(), [&](const UiBlock& block) {
        return block.type == blockType;
      })) {
    return std::string{"Each lane keeps one "} + (blockType == "nam" ? "NAM" : "CAB");
  }
  return {};
}

std::string drawerInstructionText(const UiState& state, bool chainFull, bool insertingLane)
{
  if (chainFull) return uppercase("Chain full - delete a block to add");
  if (insertingLane) return uppercase("Choose an effect for this lane");
  if (state.bank.presets[state.activePreset].routing == "wdw") {
    return uppercase("Use a Dry or Wet lane + to add modules");
  }
  return uppercase(drawerFilterDisplayName(state.categoryFilter));
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

// Builds a drawer row. Child order is fixed: 0 title, 1 code square,
// 2 subtitle, 3 the + target, 4 the bottom rule. The recycle path and the
// press handlers index into it by position.
lv_obj_t* createDrawerItem(lv_obj_t* list, const UiAsset& asset)
{
  lv_obj_t* item = lv_button_create(list);
  lv_obj_remove_style_all(item);
  lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(item, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(item, kDrawerRowWidth, kDrawerAssetButtonHeight);
  lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(item, lv_color_hex(panel), 0);
  lv_obj_set_style_bg_color(item, lv_color_hex(plateHi), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_opa(item, LV_OPA_40, LV_STATE_DISABLED);
  const int x = kDrawerRowBleed;
  lv_obj_t* title = lb::textLabel(item, lb::type::itemTitle, uppercase(asset.name), text,
                                  x + kDrawerItemTextX, kDrawerItemTitleTop);
  const int columnWidth = kBlockDrawerContentWidth - kDrawerItemTextX - kDrawerAddSize - 14;
  lv_obj_set_width(title, columnWidth);
  lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
  // Family square with the module's code; it matches the chain strip on the
  // preset tiles, so a module reads the same in both places.
  lv_obj_t* square = lb::box(item, x, kDrawerCodeTop, kDrawerCodeSize, kDrawerCodeSize,
                             categoryColor(asset.type));
  const std::string type = asset.blockType.empty()
    ? (asset.type == "amps" ? "nam" : asset.type == "cabs" ? "cab" : asset.type)
    : asset.blockType;
  // The square fits five mono characters; longer codes fall back to the type.
  std::string code = assetCode(asset.name, type);
  if (code.size() > 5) code = blockTypeCode(type);
  lb::centeredText(square, lb::type::code, code, bg, 0, 0, kDrawerCodeSize, kDrawerCodeSize);
  lv_obj_t* subtitle = lb::textLabel(item, lb::type::itemSubtitle, asset.subtitle, muted,
                                     x + kDrawerItemTextX, kDrawerItemSubtitleTop);
  lv_obj_set_width(subtitle, columnWidth);
  lv_label_set_long_mode(subtitle, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_t* add = lb::box(item, x + kBlockDrawerContentWidth - kDrawerAddSize, kDrawerCodeTop,
                          kDrawerAddSize, kDrawerAddSize, panel, disabled, 1);
  lv_obj_set_style_bg_opa(add, LV_OPA_TRANSP, 0);
  lb::centeredText(add, lb::type::add, "+", text, -1, -1, kDrawerAddSize, kDrawerAddSize);
  lb::box(item, x, kDrawerAssetButtonHeight - 1, kBlockDrawerContentWidth, 1, rule);
  for (uint32_t i = 0; i < lv_obj_get_child_count(item); ++i) {
    lv_obj_remove_flag(lv_obj_get_child(item, static_cast<int32_t>(i)), LV_OBJ_FLAG_CLICKABLE);
  }
  return item;
}

// The pressed row inverts its + target to bone, as the mockup's highlight.
void styleDrawerItemPressed(lv_obj_t* item, bool pressed)
{
  if (lv_obj_get_child_count(item) < 4) return;
  lv_obj_t* add = lv_obj_get_child(item, 3);
  lv_obj_set_style_bg_opa(add, pressed ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(add, lv_color_hex(text), 0);
  lv_obj_set_style_border_color(add, lv_color_hex(pressed ? text : disabled), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(add, 0), lv_color_hex(pressed ? bg : text), 0);
}

lv_obj_t* createDrawerGroupHeader(lv_obj_t* list, const std::string& title)
{
  lv_obj_t* header = lv_obj_create(list);
  lv_obj_remove_style_all(header);
  lv_obj_set_size(header, kDrawerRowWidth, kDrawerGroupHeight);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);
  lb::textLabel(header, lb::type::group, uppercase(title), disabled, kDrawerRowBleed,
                kDrawerGroupTextTop);
  lv_obj_t* count = lb::textLabel(header, lb::type::group, "0", disabled, 0, kDrawerGroupTextTop);
  lv_obj_set_x(count, kDrawerRowBleed + kBlockDrawerContentWidth - lb::textWidth(lb::type::group, "0"));
  lb::box(header, kDrawerRowBleed, kDrawerGroupHeight - 1, kBlockDrawerContentWidth, 1, rule);
  return header;
}

std::string drawerGroupTitle(const std::string& filter)
{
  if (filter == "modulation") return "Mod";
  for (const auto& [name, key] : kDrawerFilters) {
    if (key == filter) return name;
  }
  return filter;
}

std::string assetDragText(const UiAsset& asset)
{
  if (asset.blockType == "dualRig") {
    if (asset.mode == "wdw") return "Wet / Dry / Wet\nDry + Wet lanes";
    return "Split\nLeft / Right";
  }
  if (asset.type == "amps") {
    return "Neural Amp\n" + asset.name;
  }
  if (asset.type == "cabs") {
    return "Cab\n" + asset.name;
  }
  if (asset.blockType == "irreverb") {
    return "Reverb\n" + asset.name;
  }
  return asset.name;
}

void onAssetPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->dragging = false;
  context->suppressClick = false;
  styleDrawerItemPressed(lv_event_get_target_obj(event), true);
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
  styleDrawerItemPressed(lv_event_get_target_obj(event), false);

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
  styleDrawerItemPressed(lv_event_get_target_obj(event), false);
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

// Filters rest on the raised plate with a family bar along the top; the
// chosen one inverts to bone with dark lettering.
void styleDrawerFilter(lv_obj_t* filterButton, bool selected)
{
  lv_obj_set_style_bg_color(filterButton, lv_color_hex(selected ? text : bg), 0);
  lv_obj_set_style_border_color(filterButton, lv_color_hex(selected ? text : rule), 0);
  lv_obj_set_style_text_color(lv_obj_get_child(filterButton, 0),
                              lv_color_hex(selected ? bg : text), 0);
  if (lv_obj_t* bar = lv_obj_get_child(filterButton, 1)) {
    if (selected) lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
  }
}

// Filter order sets the list's group order; unknown types follow.
std::size_t drawerGroupRank(const std::string& type)
{
  for (std::size_t i = 1; i < kDrawerFilters.size(); ++i) {
    if (kDrawerFilters[i].second == type) return i;
  }
  return kDrawerFilters.size();
}

std::vector<std::string> drawerGroupOrder(const UiState& state)
{
  std::vector<std::string> groups;
  for (const auto& asset : state.assets) {
    if (std::find(groups.begin(), groups.end(), asset.type) == groups.end()) {
      groups.push_back(asset.type);
    }
  }
  std::stable_sort(groups.begin(), groups.end(), [](const std::string& a, const std::string& b) {
    return drawerGroupRank(a) < drawerGroupRank(b);
  });
  return groups;
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
  drawerGroupHeaders_.clear();
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

    lv_obj_t* item = createDrawerItem(drawerAssetList_, state.assets[i]);
    lv_obj_t* subtitle = lv_obj_get_child(item, 2);
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
    lv_label_set_text(lv_obj_get_child(buttons[i], 0), uppercase(state.assets[i].name).c_str());
    lv_obj_set_style_bg_color(lv_obj_get_child(buttons[i], 1),
                              lv_color_hex(categoryColor(state.assets[i].type)), 0);
    lv_label_set_text(subtitles[i], state.assets[i].subtitle.c_str());
    contexts[i]->index = i;
    contexts[i]->controlledObject = drawerAssetList_;
  }
  orderDrawerList(state, buttons);
  drawerAssetButtons_ = std::move(buttons);
  drawerAssetContexts_ = std::move(contexts);
  drawerAssetSubtitleLabels_ = std::move(subtitles);
  renderedAssetKeys_ = std::move(keys);
}

void LvglUi::orderDrawerList(const UiState& state, const std::vector<lv_obj_t*>& buttons)
{
  if (!drawerAssetList_) return;
  // Each family gets a header row followed by its modules, in filter order.
  int32_t index = 0;
  for (const auto& group : drawerGroupOrder(state)) {
    auto header = std::find_if(drawerGroupHeaders_.begin(), drawerGroupHeaders_.end(),
                               [&](const auto& entry) { return entry.first == group; });
    if (header == drawerGroupHeaders_.end()) {
      drawerGroupHeaders_.emplace_back(group, createDrawerGroupHeader(drawerAssetList_,
                                                                      drawerGroupTitle(group)));
      header = std::prev(drawerGroupHeaders_.end());
    }
    lv_obj_move_to_index(header->second, index++);
    for (std::size_t i = 0; i < buttons.size() && i < state.assets.size(); ++i) {
      if (state.assets[i].type == group) lv_obj_move_to_index(buttons[i], index++);
    }
  }
}

void LvglUi::syncDrawerView(UiState& state)
{
  // The button array is sized in the header and the labels live here, so the
  // two have to be kept in step. They are indexed against each other below.
  static_assert(kDrawerFilters.size() == kDrawerCategoryCount,
                "kDrawerCategoryCount must match the filter table");
  for (std::size_t i = 0; i < drawerCategoryButtons_.size(); ++i) {
    lv_obj_t* category = drawerCategoryButtons_[i];
    if (!category) continue;
    styleDrawerFilter(category, state.categoryFilter == kDrawerFilters[i].second);
  }

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  const bool insertingLane = state.blockInsertRig.has_value() && state.blockInsertLane.has_value();
  const UiBlock* targetWdwRig = laneTarget(state);
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
    // The insert line names where the module goes; a full chain or a WDW
    // route replaces it with the reason nothing can be added here.
    const bool blocked = chainFull
      || (!insertingLane && state.bank.presets[state.activePreset].routing == "wdw");
    lv_obj_t* afterLabel = lv_obj_get_child(lv_obj_get_parent(drawerInstructionLabel_),
      static_cast<int32_t>(lv_obj_get_index(drawerInstructionLabel_)) + 1);
    lv_label_set_text(drawerInstructionLabel_, blocked
      ? drawerInstructionText(state, chainFull, insertingLane).c_str()
      : (insertingLane ? "INSERT INTO " : "INSERT AFTER "));
    lv_obj_set_style_text_color(drawerInstructionLabel_,
                                lv_color_hex(chainFull ? danger : muted), 0);
    if (afterLabel) {
      if (blocked) lv_obj_add_flag(afterLabel, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_remove_flag(afterLabel, LV_OBJ_FLAG_HIDDEN);
    }
  }
  const std::size_t visibleCount = visibleAssetCount(state);
  if (drawerCountLabel_) {
    lv_label_set_text(drawerCountLabel_, std::to_string(state.assets.size()).c_str());
  }
  if (drawerFooterCountLabel_) {
    const auto count = std::to_string(visibleCount) + " OF " + std::to_string(state.assets.size());
    lv_label_set_text(drawerFooterCountLabel_, count.c_str());
    lv_obj_set_x(drawerFooterCountLabel_, kBlockDrawerContentWidth - lb::textWidth(lb::type::footer, count));
  }
  for (const auto& [group, header] : drawerGroupHeaders_) {
    const auto members = static_cast<std::size_t>(std::count_if(
      state.assets.begin(), state.assets.end(),
      [&group](const UiAsset& asset) { return asset.type == group; }));
    const bool shown = members > 0 && (state.categoryFilter == "all" || state.categoryFilter == group);
    if (shown) lv_obj_remove_flag(header, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* countLabel = lv_obj_get_child(header, 1);
    const auto countText = std::to_string(members);
    lv_label_set_text(countLabel, countText.c_str());
    lv_obj_set_x(countLabel, kDrawerRowBleed + kBlockDrawerContentWidth
                 - lb::textWidth(lb::type::group, countText));
  }
  for (std::size_t i = 0; i < drawerAssetButtons_.size() && i < state.assets.size(); ++i) {
    lv_obj_t* item = drawerAssetButtons_[i];
    const bool visible = state.categoryFilter == "all" || state.assets[i].type == state.categoryFilter;
    if (visible) lv_obj_remove_flag(item, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
    const bool splitUnavailable = state.assets[i].blockType == "dualRig"
      && (insertingLane || alreadySplit || standaloneAmp);
    const std::string laneReason = insertingLane && state.blockInsertLane.has_value()
      ? laneAssetReason(state.assets[i], targetWdwRig, *state.blockInsertLane) : std::string{};
    // The subtitle line carries the reason a row is unavailable, in warn.
    std::string note = state.assets[i].subtitle;
    bool warn = false;
    if (splitUnavailable) {
      note = insertingLane ? "No nested Split"
        : alreadySplit ? "A Split already exists" : "Remove standalone NAM / IR first";
      warn = true;
    } else if (insertingLane && targetWdwRig && !laneReason.empty()) {
      note = laneReason;
      warn = true;
    }
    lv_label_set_text(drawerAssetSubtitleLabels_[i], note.c_str());
    lv_obj_set_style_text_color(drawerAssetSubtitleLabels_[i], lv_color_hex(warn ? warning : muted), 0);
    const bool routeWdwTopLevel = !insertingLane
      && state.bank.presets[state.activePreset].routing == "wdw";
    if (chainFull || splitUnavailable || !laneReason.empty() || routeWdwTopLevel) {
      lv_obj_add_state(item, LV_STATE_DISABLED);
    }
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
  // The scrim dims the chain rather than covering it, so the insertion point
  // stays visible; tapping it closes the drawer.
  lv_obj_t* scrim = lb::box(root, 0, 0, kDrawerX, kDesignHeight, kScrimColor);
  lv_obj_set_style_bg_opa(scrim, kScrimOpa, 0);
  lv_obj_add_flag(scrim, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scrim, onCloseBlockDrawer, LV_EVENT_PRESSED, remember(state));

  const bool insertingLane = state.blockInsertRig.has_value() && state.blockInsertLane.has_value();
  const UiBlock* targetWdwRig = laneTarget(state);
  const auto& blocks = state.bank.presets[state.activePreset].blocks;

  // Insert marker on the dimmed wire at the chosen slot.
  if (!insertingLane && !chainInsertionXs_.empty() && chainViewport_) {
    const auto slot = std::min(state.blockInsertIndex, chainInsertionXs_.size() - 1);
    const int centerX = chain_layout::kChainLeft + chainInsertionXs_[slot] - lv_obj_get_scroll_x(chainViewport_);
    const int centerY = chain_layout::kChainTop + chain_layout::kChainRailY;
    if (centerX > 0 && centerX < kDrawerX) {
      const int outer = kMarkerSize + 2 * (kMarkerRing + kMarkerOuterRing);
      lv_obj_t* ring = lb::box(scrim, centerX - outer / 2, centerY - outer / 2, outer, outer, bg,
                               text, kMarkerOuterRing);
      lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
      lv_obj_t* disc = lb::box(ring, kMarkerRing, kMarkerRing, kMarkerSize, kMarkerSize, text);
      lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
      lb::centeredText(disc, lb::type::marker, "+", bg, 0, 0, kMarkerSize, kMarkerSize);
      const std::string legend = "INSERT HERE";
      lb::textLabel(scrim, lb::type::markerLabel, legend, text,
                    centerX - lb::textWidth(lb::type::markerLabel, legend) / 2, kMarkerLabelTop);
    }
  }

  lv_obj_t* drawer = lb::box(root, kDrawerX, 0, kBlockDrawerWidth, kDesignHeight, panel);
  lb::setBorder(drawer, disabled, 1, LV_BORDER_SIDE_LEFT);
  // Drawer children sit inside the 1 px left rule.
  const int x = kDrawerInset - 1;

  const std::string drawerTitle = insertingLane
    ? std::string{"Add to "} + (targetWdwRig
        ? (*state.blockInsertLane == 0 ? "Dry" : "Wet")
        : (*state.blockInsertLane == 0 ? "Left" : "Right"))
    : "Modules";
  const std::string title = uppercase(drawerTitle);
  lb::textLabel(drawer, lb::type::drawerTitle, title, text, x, kDrawerTitleTop);
  drawerCountLabel_ = lb::textLabel(drawer, lb::type::count, std::to_string(state.assets.size()),
                                    disabled, x + lb::textWidth(lb::type::drawerTitle, title) + 12,
                                    kDrawerCountTop);
  lv_obj_t* close = lb::button(drawer, "CLOSE", lb::ButtonKind::Normal, 0, kDrawerCloseY,
                               std::max(100, lb::textWidth(lb::type::button, "CLOSE") + 46));
  lv_obj_set_x(close, kBlockDrawerWidth - 1 - lb::kGutter - lv_obj_get_style_width(close, LV_PART_MAIN));
  lv_obj_add_event_cb(close, onCloseBlockDrawer, LV_EVENT_PRESSED, remember(state));

  // "INSERT AFTER CLEAN TWIN": the block the new module follows.
  std::string after;
  if (insertingLane) {
    after = targetWdwRig ? (*state.blockInsertLane == 0 ? "DRY LANE" : "WET LANE")
                         : (*state.blockInsertLane == 0 ? "LEFT LANE" : "RIGHT LANE");
  } else if (state.blockInsertIndex == 0 || blocks.empty()) {
    after = "INPUT";
  } else {
    after = uppercase(blocks[std::min(state.blockInsertIndex, blocks.size()) - 1].assetName);
  }
  const std::string lead = insertingLane ? "INSERT INTO " : "INSERT AFTER ";
  drawerInstructionLabel_ = lb::textLabel(drawer, lb::type::subtitle, lead, muted, x,
                                          kDrawerSubtitleTop);
  lv_obj_t* afterLabel = lb::textLabel(drawer, lb::type::subtitleBold, after, text,
                                       x + lb::textWidth(lb::type::subtitle, lead),
                                       kDrawerSubtitleTop);
  lv_obj_set_width(afterLabel, kBlockDrawerContentWidth - lb::textWidth(lb::type::subtitle, lead));
  lv_label_set_long_mode(afterLabel, LV_LABEL_LONG_MODE_DOTS);

  lv_obj_t* filterRow = lv_obj_create(drawer);
  lv_obj_remove_style_all(filterRow);
  lv_obj_set_pos(filterRow, x, kDrawerCategoryTop);
  lv_obj_set_size(filterRow, kBlockDrawerContentWidth, 2 * kCategoryButtonHeight + kCategoryButtonGap);
  lv_obj_remove_flag(filterRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(filterRow, LV_OBJ_FLAG_CLICKABLE);
  // CSS grid: four 101.75 px columns with 8 px gaps.
  const double pitch = (kBlockDrawerContentWidth + kCategoryButtonGap) / static_cast<double>(kCategoryColumns);
  for (std::size_t i = 0; i < kDrawerFilters.size(); ++i) {
    const auto& [name, filter] = kDrawerFilters[i];
    const int column = static_cast<int>(i % kCategoryColumns);
    const int row = static_cast<int>(i / kCategoryColumns);
    const int left = static_cast<int>(std::lround(column * pitch));
    const int right = static_cast<int>(std::lround((column + 1) * pitch)) - kCategoryButtonGap;
    lv_obj_t* filterButton = lb::button(filterRow, "", lb::ButtonKind::Normal, left,
                                        row * (kCategoryButtonHeight + kCategoryButtonGap),
                                        right - left, kCategoryButtonHeight, lb::type::filter);
    const std::string printed = uppercase(name);
    lv_label_set_text(lb::buttonLabel(filterButton), printed.c_str());
    lv_obj_set_x(lb::buttonLabel(filterButton),
                 (right - left - lb::textWidth(lb::type::filter, printed)) / 2 - 1);
    lv_obj_set_y(lb::buttonLabel(filterButton),
                 lb::centeredTextTop(lb::type::filter, kDrawerFilterBarHeight - 1,
                                     kCategoryButtonHeight - kDrawerFilterBarHeight) - 1);
    lv_obj_t* familyBar = lb::box(filterButton, -1, -1, right - left, kDrawerFilterBarHeight,
      std::string_view{filter} == "all" ? text : static_cast<std::uint32_t>(categoryColor(filter)));
    (void) familyBar;
    styleDrawerFilter(filterButton, state.categoryFilter == filter);
    lv_obj_add_event_cb(filterButton, onFilterClicked, LV_EVENT_CLICKED, remember(state, 0, filter));
    drawerCategoryButtons_[i] = filterButton;
  }

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

  // A full chain or a WDW route replaces the insert line with its reason.
  const std::string instruction = drawerInstructionText(state, chainFull, insertingLane);
  if (chainFull || (!insertingLane && state.bank.presets[state.activePreset].routing == "wdw")) {
    lv_label_set_text(drawerInstructionLabel_, instruction.c_str());
    lv_obj_set_style_text_color(drawerInstructionLabel_, lv_color_hex(chainFull ? danger : muted), 0);
    lv_obj_add_flag(afterLabel, LV_OBJ_FLAG_HIDDEN);
  }

  const std::size_t visibleCount = visibleAssetCount(state);
  lv_obj_t* list = lv_obj_create(drawer);
  lv_obj_remove_style_all(list);
  lv_obj_set_pos(list, x - kDrawerRowBleed, kDrawerListTop);
  lv_obj_set_size(list, kDrawerRowWidth + kDrawerScrollbarWidth, kDrawerListHeight);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_style_bg_color(list, lv_color_hex(disabled), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(list, kDrawerScrollbarWidth, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(list, 0, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_top(list, 6, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(list, 0, LV_PART_SCROLLBAR);
  lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  drawerAssetList_ = list;

  for (std::size_t i = 0; i < state.assets.size(); ++i) {
    const auto& asset = state.assets[i];
    lv_obj_t* item = createDrawerItem(list, asset);
    lv_obj_t* subtitle = lv_obj_get_child(item, 2);
    const bool splitUnavailable = asset.blockType == "dualRig"
      && (insertingLane || alreadySplit || standaloneAmp);
    const std::string laneReason = insertingLane && state.blockInsertLane.has_value()
      ? laneAssetReason(asset, targetWdwRig, *state.blockInsertLane) : std::string{};
    const bool routeWdwTopLevel = !insertingLane
      && state.bank.presets[state.activePreset].routing == "wdw";
    if (splitUnavailable) {
      const char* reason = insertingLane ? "No nested Split"
        : alreadySplit ? "A Split already exists" : "Remove standalone NAM / IR first";
      lv_label_set_text(subtitle, reason);
      lv_obj_set_style_text_color(subtitle, lv_color_hex(warning), 0);
    }
    if (!laneReason.empty() && asset.blockType != "dualRig") {
      lv_label_set_text(subtitle, laneReason.c_str());
      lv_obj_set_style_text_color(subtitle, lv_color_hex(warning), 0);
    }
    if (chainFull || splitUnavailable || !laneReason.empty() || routeWdwTopLevel) {
      lv_obj_add_state(item, LV_STATE_DISABLED);
    }
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
  orderDrawerList(state, drawerAssetButtons_);
  for (const auto& [group, header] : drawerGroupHeaders_) {
    const auto members = std::count_if(state.assets.begin(), state.assets.end(),
                                       [&](const UiAsset& asset) { return asset.type == group; });
    lv_obj_t* countLabel = lv_obj_get_child(header, 1);
    lv_label_set_text(countLabel, std::to_string(members).c_str());
    lv_obj_set_x(countLabel, kDrawerRowBleed + kBlockDrawerContentWidth
                 - lb::textWidth(lb::type::group, std::to_string(members)));
    if (state.categoryFilter != "all" && state.categoryFilter != group) {
      lv_obj_add_flag(header, LV_OBJ_FLAG_HIDDEN);
    }
  }

  lv_obj_update_layout(list);
  lv_obj_scroll_to_y(list, state.assetScrollOffset, LV_ANIM_OFF);
  state.assetScrollOffset = lv_obj_get_scroll_y(list);
  auto* scrollContext = remember(state);
  lv_obj_add_event_cb(list, onAssetListScrollBegin, LV_EVENT_SCROLL_BEGIN, scrollContext);
  lv_obj_add_event_cb(list, onAssetListScroll, LV_EVENT_SCROLL, scrollContext);
  lv_obj_add_event_cb(list, onAssetListScrollEnd, LV_EVENT_SCROLL_END, scrollContext);

  lv_obj_t* footer = lb::box(drawer, x, kDesignHeight - kDrawerFooterHeight,
                             kBlockDrawerContentWidth, kDrawerFooterHeight, panel);
  lb::setBorder(footer, rule, 1, LV_BORDER_SIDE_TOP);
  lb::textLabel(footer, lb::type::footer, "TAP + TO INSERT  /  HOLD TO DRAG", disabled, 0,
                kDrawerFooterTextTop - 1);
  const auto count = std::to_string(visibleCount) + " OF " + std::to_string(state.assets.size());
  drawerFooterCountLabel_ = lb::textLabel(footer, lb::type::footer, count, disabled,
    kBlockDrawerContentWidth - lb::textWidth(lb::type::footer, count), kDrawerFooterTextTop - 1);
}

} // namespace ardor
