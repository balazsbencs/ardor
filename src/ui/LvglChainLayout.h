#pragma once

#include "ui/UiModel.h"

#include <cstddef>
#include <string>

#include <lvgl.h>

namespace ardor::chain_layout {

inline constexpr int kChainLeft = 20;
inline constexpr int kChainWidth = 1240;
inline constexpr int kChainTop = 96;
inline constexpr int kChainHeight = 492;
inline constexpr int kChainWorldHeight = 456;
inline constexpr int kChainRailY = 228;
inline constexpr int kChainLeftRailY = 126;
inline constexpr int kChainRightRailY = 330;
inline constexpr int kChainStartX = 24;
inline constexpr int kChainTerminalWidth = 92;
inline constexpr int kChainJunctionWidth = 132;
// 168x326 (24 px category header + 302 px body) matches the mockup's tall
// engraved module card (mockups/lvgl-redesign/panel.html .blkm/.hd/.bd) 1:1 --
// panel.html's .scr is a 1280x720 canvas, the same px scale as kDesignWidth.
inline constexpr int kChainTileHeight = 326;
inline constexpr int kChainTileWidth = 168;
inline constexpr int kChainHeaderHeight = 24;
inline constexpr int kChainHandleWidth = 48;
inline constexpr int kChainInsertWidth = 52;
inline constexpr int kChainGap = 14;
inline constexpr int kLaneTileWidth = 200;
inline constexpr int kLaneTileHeight = 92;
inline constexpr int kLaneInsertWidth = 48;
inline constexpr int kChainSlotWidth = kChainTileWidth + kChainInsertWidth + 2 * kChainGap;
inline constexpr int kChainTileTop = kChainRailY - kChainTileHeight / 2;
inline constexpr int kChainTextX = 13;
inline constexpr int kChainTextWidth = kChainTileWidth - 2 * kChainTextX;

std::size_t slotForPoint(std::size_t blockCount, lv_point_t canvasPoint);
std::size_t insertionSlotForPoint(std::size_t blockCount, lv_point_t canvasPoint);
lv_point_t indicatorPosition(std::size_t blockCount, std::size_t slot);
lv_point_t reorderIndicatorPosition(std::size_t blockCount, std::size_t source,
                                    std::size_t target);
std::string laneToken(const UiBlock& block);

} // namespace ardor::chain_layout
