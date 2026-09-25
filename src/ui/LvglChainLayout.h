#pragma once

#include "ui/UiModel.h"

#include <cstddef>
#include <string>

#include <lvgl.h>

namespace ardor::chain_layout {

// Lamp Black edit stage (mockups/lvgl-taste/1-lamp-black.html): the chain
// fills the band between the 64 px header and the 108 px rail. The 4 px wire
// runs through y = 328 on screen; cards are 172 x 296 with 36 px insert
// circles 10 px either side of them.
inline constexpr int kChainLeft = 0;
inline constexpr int kChainWidth = 1280;
inline constexpr int kChainTop = 64;
inline constexpr int kChainHeight = 548;
inline constexpr int kChainWorldHeight = 548;
inline constexpr int kChainRailY = 264;
inline constexpr int kChainWireHeight = 4;
inline constexpr int kChainLeftRailY = 162;
inline constexpr int kChainRightRailY = 366;
inline constexpr int kChainStartX = 20;
inline constexpr int kChainTerminalWidth = 88;
inline constexpr int kChainTerminalHeight = 56;
inline constexpr int kChainJunctionWidth = 132;
// The full-width 64 px card header is the drag surface; the body is a tap
// target that opens the block's parameters.
inline constexpr int kChainTileHeight = 296;
inline constexpr int kChainTileWidth = 172;
inline constexpr int kChainHeaderHeight = 64;
inline constexpr int kChainHandleWidth = 48;
inline constexpr int kChainInsertWidth = 36;
inline constexpr int kChainGap = 10;
inline constexpr int kLaneTileWidth = 200;
inline constexpr int kLaneTileHeight = 92;
inline constexpr int kLaneHeaderHeight = 52;
inline constexpr int kLaneInsertWidth = 48;
inline constexpr int kChainSlotWidth = kChainTileWidth + kChainInsertWidth + 2 * kChainGap;
inline constexpr int kChainTileTop = kChainRailY - kChainTileHeight / 2;
// Card text sits 14 px inside the 1 px border.
inline constexpr int kChainTextX = 14;
inline constexpr int kChainTextWidth = kChainTileWidth - 2 - 2 * kChainTextX;

std::size_t slotForPoint(std::size_t blockCount, lv_point_t canvasPoint);
std::size_t insertionSlotForPoint(std::size_t blockCount, lv_point_t canvasPoint);
lv_point_t indicatorPosition(std::size_t blockCount, std::size_t slot);
lv_point_t reorderIndicatorPosition(std::size_t blockCount, std::size_t source,
                                    std::size_t target);
std::string laneToken(const UiBlock& block);

} // namespace ardor::chain_layout
