#include "ui/LvglUi.h"

#include "ui/LvglChainLayout.h"
#include "ui/LvglUiDrag.h"
#include "ui/LvglUiNavigation.h"
#include "ui/LvglUiStyle.h"
#include "ui/fonts/OpenSansRegular.h"
#include "ui/fonts/OpenSansSemibold.h"

#include <algorithm>
#include <string>

namespace ardor {
namespace {

using namespace chain_layout;
using namespace lvgl_drag;
using namespace lvgl_navigation;
using namespace lvgl_ui;

void redraw(UiEventContext* context)
{
  context->ui->invalidate(UiChange::None);
}

void onOpenBlockDrawer(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  openBlockDrawer(*context->state);
  redraw(context);
}

void onOpenBlockDrawerAt(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  openBlockDrawerAt(*context->state, context->index);
  redraw(context);
}

void onOpenLaneBlockDrawer(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  openLaneBlockDrawer(*context->state, context->parentIndex, context->laneIndex, context->index);
  redraw(context);
}

void onChainScroll(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->state->activePreset < context->state->chainScrollOffsets.size()) {
    context->state->chainScrollOffsets[context->state->activePreset]
      = lv_obj_get_scroll_x(lv_event_get_target_obj(event));
  }
}

void onChainScrollEnd(lv_event_t* event)
{
  onChainScroll(event);
}

void onChainStartClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->scrollChainToStart(*context->state);
}

void onChainEndClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->scrollChainToEnd(*context->state);
}

void onGlobalParamsClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectGlobalParams(*context->state);
  redraw(context);
}

void onBlockClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->suppressClick) {
    context->suppressClick = false;
    return;
  }
  context->ui->scrollChainBlockIntoView(context->index);
  context->ui->selectBlock(*context->state, context->index);
  redraw(context);
}

void onLaneBlockClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->suppressClick) {
    context->suppressClick = false;
    return;
  }
  context->ui->scrollChainBlockIntoView(context->parentIndex);
  context->ui->selectLaneBlock(*context->state, context->parentIndex,
                               context->laneIndex, context->index);
  redraw(context);
}


void onBlockPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->dragging = false;
  context->suppressClick = false;
  context->ui->setChainDragActive(true);
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
  context->ui->setChainDragActive(false);
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
  const auto target = context->ui->chainSlotAtPoint(point);
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
  context->ui->setChainDragActive(false);
  context->suppressClick = context->dragging;
  const bool wasDragging = context->dragging;
  clearDragVisuals(context);
  if (wasDragging) {
    context->ui->endInteraction();
  }
}

void placeLaneDragIndicator(UiEventContext* context, const UiLaneDropTarget& target)
{
  if (!context->indicator) {
    context->indicator = lv_obj_create(context->ui->canvas());
    lv_obj_set_size(context->indicator, 5, kLaneTileHeight);
    lv_obj_set_style_border_width(context->indicator, 0, 0);
    lv_obj_set_style_radius(context->indicator, 2, 0);
  }
  const auto position = context->ui->laneIndicatorForTarget(target);
  lv_obj_set_pos(context->indicator, position.x, position.y);
  lv_obj_set_style_bg_color(context->indicator,
                            lv_color_hex(target.laneIndex == 0 ? accent : rigRight), 0);
  moveToFront(context->indicator);
}

void onLaneBlockPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->dragging = false;
  context->suppressClick = false;
  context->ui->setChainDragActive(true);
  const auto& blocks = context->state->bank.presets[context->state->activePreset].blocks;
  if (context->parentIndex < blocks.size()
      && context->laneIndex < blocks[context->parentIndex].lanes.size()
      && context->index < blocks[context->parentIndex].lanes[context->laneIndex].size()) {
    const auto& child = blocks[context->parentIndex].lanes[context->laneIndex][context->index];
    context->dragText = laneToken(child) + "\n" + child.assetName;
  }
  lv_indev_t* input = lv_event_get_indev(event);
  if (input) {
    lv_indev_get_point(input, &context->pressPoint);
    context->pressPoint = context->ui->toCanvas(context->pressPoint);
  }
}

void onLaneBlockPressing(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) return;
  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  const int dx = point.x - context->pressPoint.x;
  const int dy = point.y - context->pressPoint.y;
  if (!context->dragging && dx * dx + dy * dy < 64) return;
  if (!context->dragging) {
    context->dragging = true;
    context->ui->beginInteraction();
    if (context->controlledObject) lv_obj_set_style_opa(context->controlledObject, LV_OPA_TRANSP, 0);
  }
  context->ui->autoScrollChainForDrag(*context->state, point);
  placeDragGhost(context, point);
  if (const auto target = context->ui->laneDropTargetAtPoint(point)) {
    placeLaneDragIndicator(context, *target);
  } else if (context->indicator) {
    lv_obj_delete(context->indicator);
    context->indicator = nullptr;
  }
}

void onLaneBlockReleased(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto& blocks = context->state->bank.presets[context->state->activePreset].blocks;
  const bool enabled = context->parentIndex < blocks.size()
    && context->laneIndex < blocks[context->parentIndex].lanes.size()
    && context->index < blocks[context->parentIndex].lanes[context->laneIndex].size()
    && blocks[context->parentIndex].lanes[context->laneIndex][context->index].enabled;
  if (context->controlledObject) {
    lv_obj_set_style_opa(context->controlledObject, enabled ? LV_OPA_COVER : LV_OPA_70, 0);
  }
  context->ui->setChainDragActive(false);
  if (!context->dragging) return;
  lv_indev_t* input = lv_event_get_indev(event);
  std::optional<UiLaneDropTarget> target;
  if (input) {
    lv_point_t point{};
    lv_indev_get_point(input, &point);
    point = context->ui->toCanvas(point);
    target = context->ui->laneDropTargetAtPoint(point);
  }
  clearDragVisuals(context);
  if (target) {
    moveLaneBlock(*context->state, context->parentIndex, context->laneIndex,
                  context->index, target->laneIndex, target->blockIndex);
    redraw(context);
  }
  context->ui->endInteraction();
}

void onLaneBlockPressLost(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto& blocks = context->state->bank.presets[context->state->activePreset].blocks;
  const bool enabled = context->parentIndex < blocks.size()
    && context->laneIndex < blocks[context->parentIndex].lanes.size()
    && context->index < blocks[context->parentIndex].lanes[context->laneIndex].size()
    && blocks[context->parentIndex].lanes[context->laneIndex][context->index].enabled;
  if (context->controlledObject) {
    lv_obj_set_style_opa(context->controlledObject, enabled ? LV_OPA_COVER : LV_OPA_70, 0);
  }
  context->ui->setChainDragActive(false);
  const bool wasDragging = context->dragging;
  clearDragVisuals(context);
  if (wasDragging) context->ui->endInteraction();
}

} // namespace

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
  const auto* selectedEffect = selectedUiBlock(state);
  const bool editingEq = state.paramDrawerOpen && state.paramTarget == UiParamTarget::Block
    && selectedEffect && selectedEffect->type == "eq"
    && isParametricEqMode(selectedEffect->params);
  if (editingEq) {
    // The retained parameter layer owns the EQ editor.
  }

  chainViewport_ = lv_obj_create(root);
  lv_obj_set_size(chainViewport_, kChainWidth, kChainHeight);
  lv_obj_set_pos(chainViewport_, kChainLeft, kChainTop);
  styleSurface(chainViewport_, bg);
  lv_obj_set_style_pad_all(chainViewport_, 0, 0);
  lv_obj_set_style_radius(chainViewport_, 0, 0);
  lv_obj_set_scroll_dir(chainViewport_, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(chainViewport_, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_remove_flag(chainViewport_, LV_OBJ_FLAG_SCROLL_ELASTIC);

  chainWorld_ = lv_obj_create(chainViewport_);
  lv_obj_set_size(chainWorld_, kChainWidth, kChainWorldHeight);
  lv_obj_set_pos(chainWorld_, 0, 0);
  lv_obj_set_style_bg_opa(chainWorld_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chainWorld_, 0, 0);
  lv_obj_set_style_pad_all(chainWorld_, 0, 0);
  lv_obj_set_style_radius(chainWorld_, 0, 0);
  lv_obj_remove_flag(chainWorld_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(chainWorld_, LV_OBJ_FLAG_CLICKABLE);

  auto* scrollContext = remember(state);
  scrollContext->controlledObject = chainViewport_;
  lv_obj_add_event_cb(chainViewport_, onChainScroll, LV_EVENT_SCROLL, scrollContext);
  lv_obj_add_event_cb(chainViewport_, onChainScrollEnd, LV_EVENT_SCROLL_END, scrollContext);

  const auto rail = [&](int x, int y, int width, int color = muted) {
    if (width <= 0) return static_cast<lv_obj_t*>(nullptr);
    lv_obj_t* line = lv_obj_create(chainWorld_);
    lv_obj_set_size(line, width, 3);
    lv_obj_set_pos(line, x, y - 1);
    styleSurface(line, color);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_opa(line, LV_OPA_70, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    return line;
  };
  const auto terminal = [&](int x, const char* title, const char* detail) {
    lv_obj_t* object = lv_obj_create(chainWorld_);
    lv_obj_set_size(object, kChainTerminalWidth, 64);
    lv_obj_set_pos(object, x, kChainRailY - 32);
    styleSurface(object, 0x171717);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    label(object, title, LV_ALIGN_TOP_MID, 0, 8, &ardor_font_open_sans_semibold_22, text);
    label(object, detail, LV_ALIGN_BOTTOM_MID, 0, -8, &ardor_font_open_sans_regular_18, muted);
    return object;
  };
  const auto topInsert = [&](int x, std::size_t index) {
    rail(x - kChainGap, kChainRailY, kChainInsertWidth + 2 * kChainGap, muted);
    lv_obj_t* add = button(chainWorld_, "+");
    lv_obj_set_size(add, 46, 46);
    lv_obj_set_pos(add, x + (kChainInsertWidth - 46) / 2, kChainRailY - 23);
    styleSurface(add, 0x1b1b1b);
    lv_obj_set_style_border_color(add, lv_color_hex(0x4b4b4b), 0);
    lv_obj_set_style_border_width(add, 1, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(add, 0), lv_color_hex(accent), 0);
    auto* context = remember(state, index);
    lv_obj_add_event_cb(add, onOpenBlockDrawerAt, LV_EVENT_CLICKED, context);
    chainInsertionXs_.push_back(x + kChainInsertWidth / 2);
  };
  const auto laneInsert = [&](int x, int y, std::size_t rigIndex,
                              std::size_t laneIndex, std::size_t index, int color, bool disabled) {
    lv_obj_t* add = button(chainWorld_, "+");
    lv_obj_set_size(add, 42, 42);
    lv_obj_set_pos(add, x, y - 21);
    styleSurface(add, 0x1b1b1b);
    lv_obj_set_style_border_color(add, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(add, 1, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(add, 0), lv_color_hex(color), 0);
    if (disabled) lv_obj_add_state(add, LV_STATE_DISABLED);
    auto* context = remember(state, index);
    context->parentIndex = rigIndex;
    context->laneIndex = laneIndex;
    lv_obj_add_event_cb(add, onOpenLaneBlockDrawer, LV_EVENT_CLICKED, context);
    laneInsertionXs_[laneIndex].push_back(x + 21);
  };
  const auto dragHandle = [&](lv_obj_t* parent, lv_obj_t* controlled, std::size_t index) {
    lv_obj_t* handle = button(parent, "|||");
    lv_obj_set_size(handle, kChainHandleWidth, 52);
    lv_obj_align(handle, LV_ALIGN_RIGHT_MID, -6, 0);
    styleSurface(handle, 0x333333);
    lv_obj_set_style_text_color(lv_obj_get_child(handle, 0), lv_color_hex(muted), 0);
    auto* context = remember(state, index);
    context->controlledObject = controlled;
    lv_obj_add_event_cb(handle, onBlockPressed, LV_EVENT_PRESSED, context);
    lv_obj_add_event_cb(handle, onBlockPressing, LV_EVENT_PRESSING, context);
    lv_obj_add_event_cb(handle, onBlockReleased, LV_EVENT_RELEASED, context);
    lv_obj_add_event_cb(handle, onBlockPressLost, LV_EVENT_PRESS_LOST, context);
    chainDragContexts_[index] = context;
  };
  const auto laneEnd = [](std::size_t count) {
    return static_cast<int>(count + 1) * (kLaneInsertWidth + 8)
      + static_cast<int>(count) * (kLaneTileWidth + 8);
  };

  chainItemStarts_.clear();
  chainItemEnds_.clear();
  chainInsertionXs_.clear();
  renderedBlockIds_.clear();
  int x = kChainStartX;
  terminal(x, "INPUT", "Mono");
  x += kChainTerminalWidth + kChainGap;
  topInsert(x, 0);
  x += kChainInsertWidth + kChainGap;

  for (std::size_t i = 0; i < blocks.size() && i < kMaxEffectBlocks; ++i) {
    const auto& block = blocks[i];
    const bool selected = state.paramTarget == UiParamTarget::Block
      && state.selectedBlock == i && !selectedBlockIsLaneChild(state);
    const int itemStart = x;
    chainItemStarts_.push_back(itemStart);
    renderedBlockIds_.push_back(block.id);

    if (block.type != "dualRig") {
      rail(x - kChainGap, kChainRailY, kChainTileWidth + 2 * kChainGap, muted);
      lv_obj_t* object = button(chainWorld_, "");
      lv_obj_set_size(object, kChainTileWidth, kChainTileHeight);
      lv_obj_set_pos(object, x, kChainTileTop);
      styleSurface(object, block.enabled ? panel : 0x171717);
      lv_obj_set_style_pad_all(object, 0, 0);
      if (!block.enabled) lv_obj_set_style_opa(object, LV_OPA_70, 0);
      if (isBlockHighlighted(block.id)) {
        lv_obj_set_style_border_color(object, lv_color_hex(accent), 0);
        lv_obj_set_style_border_width(object, 3, 0);
      }
      const int catColor = categoryColor(block.type);
      // Permanent left accent bar marks the block's family.
      lv_obj_t* categoryBar = lv_obj_create(object);
      lv_obj_set_size(categoryBar, 4, kChainTileHeight - 26);
      lv_obj_align(categoryBar, LV_ALIGN_LEFT_MID, 3, 0);
      styleSurface(categoryBar, catColor);
      lv_obj_set_style_border_width(categoryBar, 0, 0);
      lv_obj_set_style_shadow_width(categoryBar, 0, 0);
      lv_obj_set_style_radius(categoryBar, 2, 0);
      lv_obj_remove_flag(categoryBar, LV_OBJ_FLAG_CLICKABLE);

      std::string category = block.label;
      std::transform(category.begin(), category.end(), category.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
      });
      lv_obj_t* categoryLabel = label(object, category, LV_ALIGN_TOP_LEFT, kChainTextX, 9,
                                      &ardor_font_open_sans_regular_18, catColor);
      lv_obj_set_width(categoryLabel, kChainTextWidth);
      lv_label_set_long_mode(categoryLabel, LV_LABEL_LONG_CLIP);
      lv_obj_t* assetName = label(object, block.assetName, LV_ALIGN_TOP_LEFT, kChainTextX, 38,
                                  &ardor_font_open_sans_semibold_22);
      lv_obj_set_width(assetName, kChainTextWidth);
      lv_label_set_long_mode(assetName, LV_LABEL_LONG_CLIP);
      lv_obj_t* bypassed = label(object, "BYPASSED", LV_ALIGN_BOTTOM_LEFT, kChainTextX, -7,
                                 &ardor_font_open_sans_regular_18, danger);
      if (block.enabled) lv_obj_add_flag(bypassed, LV_OBJ_FLAG_HIDDEN);
      lv_obj_t* indicator = lv_obj_create(object);
      lv_obj_set_size(indicator, 42, 4);
      lv_obj_align(indicator, LV_ALIGN_BOTTOM_MID, -kChainHandleWidth / 2, -3);
      styleSurface(indicator, accent);
      lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
      if (!selected) lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
      auto* clickContext = remember(state, i);
      lv_obj_add_event_cb(object, onBlockClicked, LV_EVENT_CLICKED, clickContext);
      dragHandle(object, object, i);

      chainCards_[i] = object;
      chainCategoryLabels_[i] = categoryLabel;
      chainAssetLabels_[i] = assetName;
      chainBypassLabels_[i] = bypassed;
      chainSelectionIndicators_[i] = indicator;
      chainClickContexts_[i] = clickContext;
      x += kChainTileWidth;
    } else {
      renderedRigIndex_ = i;
      const std::size_t longest = std::max(block.lanes[0].size(), block.lanes[1].size());
      const int laneWidth = std::max(laneEnd(longest), laneEnd(1));
      const int splitX = x;
      const int laneStart = splitX + kChainJunctionWidth + 26;
      const int joinX = laneStart + laneWidth + 22;
      rail(splitX + kChainJunctionWidth / 2, kChainLeftRailY,
           joinX - splitX, accent);
      rail(splitX + kChainJunctionWidth / 2, kChainRightRailY,
           joinX - splitX, rigRight);
      lv_obj_t* splitStem = lv_obj_create(chainWorld_);
      lv_obj_set_size(splitStem, 3, kChainRightRailY - kChainLeftRailY);
      lv_obj_set_pos(splitStem, splitX + kChainJunctionWidth / 2 - 1, kChainLeftRailY);
      styleSurface(splitStem, muted);
      lv_obj_remove_flag(splitStem, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_t* joinStem = lv_obj_create(chainWorld_);
      lv_obj_set_size(joinStem, 3, kChainRightRailY - kChainLeftRailY);
      lv_obj_set_pos(joinStem, joinX + kChainJunctionWidth / 2 - 1, kChainLeftRailY);
      styleSurface(joinStem, muted);
      lv_obj_remove_flag(joinStem, LV_OBJ_FLAG_CLICKABLE);

      lv_obj_t* split = button(chainWorld_, "SPLIT");
      lv_obj_set_size(split, kChainJunctionWidth, 82);
      lv_obj_set_pos(split, splitX, kChainRailY - 41);
      styleSurface(split, 0x1b1b1b);
      lv_obj_set_style_pad_all(split, 0, 0);
      lv_obj_set_style_border_color(split, lv_color_hex(selected ? accent : 0x4b4b4b), 0);
      lv_obj_set_style_border_width(split, selected ? 3 : 1, 0);
      lv_obj_t* splitLabel = lv_obj_get_child(split, 0);
      lv_obj_set_width(splitLabel, kChainJunctionWidth - kChainHandleWidth - 24);
      lv_obj_align(splitLabel, LV_ALIGN_LEFT_MID, 12, 0);
      lv_obj_set_style_text_align(splitLabel, LV_TEXT_ALIGN_LEFT, 0);
      auto* clickContext = remember(state, i);
      lv_obj_add_event_cb(split, onBlockClicked, LV_EVENT_CLICKED, clickContext);
      dragHandle(split, split, i);
      chainCards_[i] = split;
      chainClickContexts_[i] = clickContext;

      lv_obj_t* join = lv_obj_create(chainWorld_);
      lv_obj_set_size(join, kChainJunctionWidth, 64);
      lv_obj_set_pos(join, joinX, kChainRailY - 32);
      styleSurface(join, 0x171717);
      lv_obj_set_style_border_color(join, lv_color_hex(0x4b4b4b), 0);
      lv_obj_set_style_border_width(join, 1, 0);
      lv_obj_set_style_pad_all(join, 0, 0);
      lv_obj_remove_flag(join, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_remove_flag(join, LV_OBJ_FLAG_CLICKABLE);
      label(join, "JOIN", LV_ALIGN_CENTER, 0, 0, &ardor_font_open_sans_semibold_22, text);

      for (std::size_t laneIndex = 0; laneIndex < block.lanes.size(); ++laneIndex) {
        const int laneY = laneIndex == 0 ? kChainLeftRailY : kChainRightRailY;
        const int laneColor = laneIndex == 0 ? accent : rigRight;
        label(chainWorld_, laneIndex == 0 ? "LEFT" : "RIGHT", LV_ALIGN_TOP_LEFT,
              splitX + kChainJunctionWidth / 2 + 12, laneY - 54,
              &ardor_font_open_sans_semibold_22, laneColor);
        int laneX = laneStart;
        laneInsert(laneX, laneY, i, laneIndex, 0, laneColor,
                   block.lanes[laneIndex].size() >= kMaxEffectBlocks);
        laneX += kLaneInsertWidth + 8;
        for (std::size_t childIndex = 0; childIndex < block.lanes[laneIndex].size(); ++childIndex) {
          const auto& child = block.lanes[laneIndex][childIndex];
          lv_obj_t* childObject = button(chainWorld_, "");
          lv_obj_set_size(childObject, kLaneTileWidth, kLaneTileHeight);
          lv_obj_set_pos(childObject, laneX, laneY - kLaneTileHeight / 2);
          styleSurface(childObject, child.enabled ? panel : 0x171717);
          lv_obj_set_style_pad_all(childObject, 0, 0);
          lv_obj_set_style_border_color(childObject, lv_color_hex(laneColor), 0);
          const bool childSelected = state.paramTarget == UiParamTarget::Block
            && state.selectedBlockId == child.id;
          lv_obj_set_style_border_width(childObject, childSelected ? 3 : 1, 0);
          if (!child.enabled) lv_obj_set_style_opa(childObject, LV_OPA_70, 0);
          label(childObject, laneToken(child), LV_ALIGN_TOP_LEFT, 10, 8,
                &ardor_font_open_sans_semibold_22, laneColor);
          lv_obj_t* childAsset = label(childObject, child.assetName, LV_ALIGN_BOTTOM_LEFT, 10, -9,
                                       &ardor_font_open_sans_regular_18,
                                       child.enabled ? text : muted);
          lv_obj_set_width(childAsset, kLaneTileWidth - 62);
          lv_label_set_long_mode(childAsset, LV_LABEL_LONG_CLIP);
          auto* childClickContext = remember(state, childIndex);
          childClickContext->parentIndex = i;
          childClickContext->laneIndex = laneIndex;
          lv_obj_add_event_cb(childObject, onLaneBlockClicked, LV_EVENT_CLICKED, childClickContext);
          lv_obj_t* childHandle = button(childObject, "||");
          lv_obj_set_size(childHandle, 36, 44);
          lv_obj_align(childHandle, LV_ALIGN_RIGHT_MID, -6, 0);
          styleSurface(childHandle, 0x333333);
          lv_obj_set_style_text_color(lv_obj_get_child(childHandle, 0), lv_color_hex(muted), 0);
          auto* childDragContext = remember(state, childIndex);
          childDragContext->parentIndex = i;
          childDragContext->laneIndex = laneIndex;
          childDragContext->controlledObject = childObject;
          childDragContext->dragText = laneToken(child) + "\n" + child.assetName;
          lv_obj_add_event_cb(childHandle, onLaneBlockPressed, LV_EVENT_PRESSED, childDragContext);
          lv_obj_add_event_cb(childHandle, onLaneBlockPressing, LV_EVENT_PRESSING, childDragContext);
          lv_obj_add_event_cb(childHandle, onLaneBlockReleased, LV_EVENT_RELEASED, childDragContext);
          lv_obj_add_event_cb(childHandle, onLaneBlockPressLost, LV_EVENT_PRESS_LOST, childDragContext);
          laneX += kLaneTileWidth + 8;
          laneInsert(laneX, laneY, i, laneIndex, childIndex + 1, laneColor,
                     block.lanes[laneIndex].size() >= kMaxEffectBlocks);
          laneX += kLaneInsertWidth + 8;
        }
      }
      x = joinX + kChainJunctionWidth;
    }

    chainItemEnds_.push_back(x);
    x += kChainGap;
    topInsert(x, i + 1);
    x += kChainInsertWidth + kChainGap;
  }

  rail(x - kChainGap, kChainRailY, kChainGap, muted);
  terminal(x, "OUTPUT", "Stereo");
  x += kChainTerminalWidth + kChainStartX;
  lv_obj_set_width(chainWorld_, std::max(x, kChainWidth));
  lv_obj_update_layout(chainViewport_);
  const int32_t savedScroll = state.activePreset < state.chainScrollOffsets.size()
    ? state.chainScrollOffsets[state.activePreset] : 0;
  lv_obj_scroll_to_x(chainViewport_, savedScroll, LV_ANIM_OFF);

  lv_obj_t* inputJump = button(root, "<  Input");
  lv_obj_set_size(inputJump, 144, 52);
  lv_obj_set_pos(inputJump, 28, 604);
  styleSurface(inputJump, 0x171717);
  lv_obj_add_event_cb(inputJump, onChainStartClicked, LV_EVENT_CLICKED, remember(state));
  label(root, "Swipe the canvas to move  |  + inserts an effect or Split  |  drag ||| to reorder",
        LV_ALIGN_TOP_MID, 0, 620, &ardor_font_open_sans_regular_18, muted);
  lv_obj_t* outputJump = button(root, "Output  >");
  lv_obj_set_size(outputJump, 144, 52);
  lv_obj_set_pos(outputJump, 1108, 604);
  styleSurface(outputJump, 0x171717);
  lv_obj_add_event_cb(outputJump, onChainEndClicked, LV_EVENT_CLICKED, remember(state));

}

} // namespace ardor
