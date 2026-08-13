#include "ui/LvglUi.h"

#include "ui/LvglChainLayout.h"
#include "ui/LvglUiStyle.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace ardor {

using namespace chain_layout;
using namespace lvgl_ui;

std::size_t LvglUi::chainSlotForPoint(std::size_t blockCount, lv_point_t canvasPoint)
{
  return chain_layout::slotForPoint(blockCount, canvasPoint);
}

std::size_t LvglUi::chainInsertionSlotForPoint(std::size_t blockCount, lv_point_t canvasPoint)
{
  return chain_layout::insertionSlotForPoint(blockCount, canvasPoint);
}

lv_point_t LvglUi::chainIndicatorPosition(std::size_t blockCount, std::size_t slot)
{
  return chain_layout::indicatorPosition(blockCount, slot);
}

lv_point_t LvglUi::chainReorderIndicatorPosition(std::size_t blockCount, std::size_t source,
                                                  std::size_t target)
{
  return chain_layout::reorderIndicatorPosition(blockCount, source, target);
}

std::size_t LvglUi::chainSlotAtPoint(lv_point_t canvasPoint) const
{
  if (chainItemStarts_.empty()) return 0;
  const int32_t scroll = chainViewport_ ? lv_obj_get_scroll_x(chainViewport_) : 0;
  const int32_t contentX = canvasPoint.x - kChainLeft + scroll;
  std::size_t nearest = 0;
  int32_t nearestDistance = std::numeric_limits<int32_t>::max();
  for (std::size_t i = 0; i < chainItemStarts_.size(); ++i) {
    const int32_t center = (chainItemStarts_[i] + chainItemEnds_[i]) / 2;
    const int32_t distance = std::abs(contentX - center);
    if (distance < nearestDistance) {
      nearest = i;
      nearestDistance = distance;
    }
  }
  return nearest;
}

std::size_t LvglUi::chainInsertionSlotAtPoint(lv_point_t canvasPoint) const
{
  if (chainInsertionXs_.empty()) return 0;
  const int32_t scroll = chainViewport_ ? lv_obj_get_scroll_x(chainViewport_) : 0;
  const int32_t contentX = canvasPoint.x - kChainLeft + scroll;
  std::size_t nearest = 0;
  int32_t nearestDistance = std::numeric_limits<int32_t>::max();
  for (std::size_t i = 0; i < chainInsertionXs_.size(); ++i) {
    const int32_t distance = std::abs(contentX - chainInsertionXs_[i]);
    if (distance < nearestDistance) {
      nearest = i;
      nearestDistance = distance;
    }
  }
  return nearest;
}

lv_point_t LvglUi::chainIndicatorForSlot(std::size_t slot) const
{
  if (chainInsertionXs_.empty()) {
    return {kChainLeft + kChainStartX, kChainTop + kChainTileTop};
  }
  slot = std::min(slot, chainInsertionXs_.size() - 1);
  const int32_t scroll = chainViewport_ ? lv_obj_get_scroll_x(chainViewport_) : 0;
  return {kChainLeft + chainInsertionXs_[slot] - scroll,
          kChainTop + kChainTileTop};
}

void LvglUi::scrollChainToStart(UiState& state)
{
  if (!chainViewport_) return;
  lv_obj_scroll_to_x(chainViewport_, 0, LV_ANIM_ON);
  state.chainScrollOffsets[state.activePreset] = 0;
}

void LvglUi::scrollChainToEnd(UiState& state)
{
  if (!chainViewport_) return;
  lv_obj_update_layout(chainViewport_);
  const int32_t end = lv_obj_get_scroll_left(chainViewport_)
    + lv_obj_get_scroll_right(chainViewport_);
  lv_obj_scroll_to_x(chainViewport_, end, LV_ANIM_ON);
  state.chainScrollOffsets[state.activePreset] = end;
}

void LvglUi::scrollChainBlockIntoView(std::size_t blockIndex)
{
  if (!chainViewport_ || blockIndex >= chainCards_.size() || !chainCards_[blockIndex]) return;
  lv_obj_scroll_to_view(chainCards_[blockIndex], LV_ANIM_ON);
}

void LvglUi::setChainDragActive(bool active)
{
  chainDragActive_ = active;
  if (!chainViewport_) return;
  if (active) {
    lv_obj_remove_flag(chainViewport_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(chainViewport_, LV_SCROLLBAR_MODE_OFF);
  } else {
    lv_obj_add_flag(chainViewport_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(chainViewport_, LV_SCROLLBAR_MODE_AUTO);
  }
}

void LvglUi::autoScrollChainForDrag(UiState& state, lv_point_t canvasPoint)
{
  if (!chainViewport_ || canvasPoint.y < kChainTop
      || canvasPoint.y > kChainTop + kChainHeight) {
    return;
  }
  constexpr int edgeZone = 88;
  constexpr int step = 28;
  if (chainDragActive_) lv_obj_add_flag(chainViewport_, LV_OBJ_FLAG_SCROLLABLE);
  if (canvasPoint.x < kChainLeft + edgeZone && lv_obj_get_scroll_left(chainViewport_) > 0) {
    lv_obj_scroll_by_bounded(chainViewport_, step, 0, LV_ANIM_OFF);
  } else if (canvasPoint.x > kChainLeft + kChainWidth - edgeZone
             && lv_obj_get_scroll_right(chainViewport_) > 0) {
    lv_obj_scroll_by_bounded(chainViewport_, -step, 0, LV_ANIM_OFF);
  }
  if (chainDragActive_) lv_obj_remove_flag(chainViewport_, LV_OBJ_FLAG_SCROLLABLE);
  state.chainScrollOffsets[state.activePreset] = lv_obj_get_scroll_x(chainViewport_);
}

std::optional<UiLaneDropTarget> LvglUi::laneDropTargetAtPoint(lv_point_t canvasPoint) const
{
  if (!renderedRigIndex_ || !chainViewport_) return std::nullopt;
  const int32_t relativeY = canvasPoint.y - kChainTop;
  const int32_t leftDistance = std::abs(relativeY - kChainLeftRailY);
  const int32_t rightDistance = std::abs(relativeY - kChainRightRailY);
  const std::size_t laneIndex = leftDistance <= rightDistance ? 0 : 1;
  if (std::min(leftDistance, rightDistance) > 72 || laneInsertionXs_[laneIndex].empty()) {
    return std::nullopt;
  }
  const int32_t contentX = canvasPoint.x - kChainLeft
    + lv_obj_get_scroll_x(chainViewport_);
  std::size_t insertion = 0;
  int32_t nearestDistance = std::numeric_limits<int32_t>::max();
  for (std::size_t i = 0; i < laneInsertionXs_[laneIndex].size(); ++i) {
    const int32_t distance = std::abs(contentX - laneInsertionXs_[laneIndex][i]);
    if (distance < nearestDistance) {
      insertion = i;
      nearestDistance = distance;
    }
  }
  return UiLaneDropTarget{*renderedRigIndex_, laneIndex, insertion};
}

lv_point_t LvglUi::laneIndicatorForTarget(const UiLaneDropTarget& target) const
{
  if (!chainViewport_ || target.laneIndex >= laneInsertionXs_.size()
      || laneInsertionXs_[target.laneIndex].empty()) {
    return {kChainLeft, kChainTop};
  }
  const auto& insertions = laneInsertionXs_[target.laneIndex];
  const std::size_t index = std::min(target.blockIndex, insertions.size() - 1);
  const int32_t scroll = lv_obj_get_scroll_x(chainViewport_);
  const int32_t y = target.laneIndex == 0 ? kChainLeftRailY : kChainRightRailY;
  return {kChainLeft + insertions[index] - scroll,
          kChainTop + y - kLaneTileHeight / 2};
}

void LvglUi::selectBlock(UiState& state, std::size_t blockIndex)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (blockIndex >= blocks.size()) {
    return;
  }
  ardor::selectBlock(state, blockIndex);
  highlightedBlockId_.clear();
  selectedEqStage_ = kEqFirstBandStage;
  resetParameterPage();
}

void LvglUi::selectLaneBlock(UiState& state, std::size_t rigIndex,
                             std::size_t laneIndex, std::size_t blockIndex)
{
  ardor::selectLaneBlock(state, rigIndex, laneIndex, blockIndex);
  highlightedBlockId_.clear();
  selectedEqStage_ = kEqFirstBandStage;
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
  return highlightedBlockId_ == blockId
    && std::chrono::steady_clock::now() < highlightUntil_;
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
  editModifiedLabel_ = nullptr;
  editModuleCountLabel_ = nullptr;
  chainCards_.fill(nullptr);
  chainCategoryLabels_.fill(nullptr);
  chainAssetLabels_.fill(nullptr);
  chainBypassLabels_.fill(nullptr);
  chainClickContexts_.fill(nullptr);
  chainDragContexts_.fill(nullptr);
  renderedBlockIds_.clear();
  chainViewport_ = nullptr;
  chainWorld_ = nullptr;
  chainDragActive_ = false;
  chainItemStarts_.clear();
  chainItemEnds_.clear();
  chainInsertionXs_.clear();
  renderedRigIndex_.reset();
  for (auto& insertions : laneInsertionXs_) insertions.clear();
  contextRegion_ = UiContextRegion::Edit;
  renderEditMode(editLayer_, state);
  contextRegion_ = UiContextRegion::None;
}

void LvglUi::syncChainCards(UiState& state)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  const bool sameSerialTopology = !renderedRigIndex_.has_value()
    && blocks.size() == renderedBlockIds_.size()
    && blocks.size() <= kMaxEffectBlocks
    && std::equal(blocks.begin(), blocks.end(), renderedBlockIds_.begin(),
                  [](const UiBlock& block, const std::string& renderedId) {
                    return block.type != "dualRig" && block.id == renderedId;
                  });
  if (!sameSerialTopology) {
    rebuildEditView(state);
    return;
  }

  for (std::size_t i = 0; i < blocks.size(); ++i) {
    const auto& block = blocks[i];
    if (!chainCards_[i] || !chainCategoryLabels_[i] || !chainAssetLabels_[i]
        || !chainBypassLabels_[i]
        || !chainClickContexts_[i] || !chainDragContexts_[i]) {
      rebuildEditView(state);
      return;
    }

    auto* card = chainCards_[i];
    styleSurface(card, block.enabled ? panel : panelAlt);
    lv_obj_set_style_opa(card, block.enabled ? LV_OPA_COVER : LV_OPA_70, 0);
    const bool selected = state.paramTarget == UiParamTarget::Block
      && state.selectedBlock == i && !selectedBlockIsLaneChild(state);
    lv_obj_set_style_border_color(card, lv_color_hex(selected ? text : rule), 0);
    if (isBlockHighlighted(block.id)) {
      lv_obj_set_style_border_color(card, lv_color_hex(text), 0);
      lv_obj_set_style_border_width(card, 3, 0);
    }

    lv_label_set_text(chainCategoryLabels_[i], uppercase(block.label).c_str());
    setText(chainCategoryLabels_[i], block.enabled ? bg : muted, &ardor_font_saira_cond_medium_18);
    auto* categoryHeader = lv_obj_get_parent(chainCategoryLabels_[i]);
    styleSurface(categoryHeader, block.enabled ? categoryColor(block.type) : rule);
    lv_obj_set_style_border_width(categoryHeader, 0, 0);
    lv_label_set_text(chainAssetLabels_[i], uppercase(block.assetName).c_str());
    setText(chainAssetLabels_[i], block.enabled ? text : disabled, &ardor_font_saira_cond_semibold_28);
    if (block.enabled) {
      lv_obj_add_flag(chainBypassLabels_[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_flag(chainBypassLabels_[i], LV_OBJ_FLAG_HIDDEN);
    }

    chainClickContexts_[i]->index = i;
    chainDragContexts_[i]->index = i;
    chainDragContexts_[i]->controlledObject = card;
  }
}

} // namespace ardor
