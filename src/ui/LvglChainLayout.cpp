#include "ui/LvglChainLayout.h"

#include <algorithm>
#include <cctype>

namespace ardor::chain_layout {

std::size_t slotForPoint(std::size_t blockCount, lv_point_t canvasPoint)
{
  if (blockCount == 0) return 0;
  const int contentX = std::max(0, static_cast<int>(canvasPoint.x) - kChainLeft - kChainStartX
                                   - kChainTerminalWidth - kChainInsertWidth);
  return std::min(blockCount - 1, static_cast<std::size_t>(contentX / kChainSlotWidth));
}

std::size_t insertionSlotForPoint(std::size_t blockCount, lv_point_t canvasPoint)
{
  const int contentX = std::max(0, static_cast<int>(canvasPoint.x) - kChainLeft - kChainStartX
                                   - kChainTerminalWidth - kChainInsertWidth
                                   + kChainSlotWidth / 2);
  return std::min(blockCount, static_cast<std::size_t>(contentX / kChainSlotWidth));
}

lv_point_t indicatorPosition(std::size_t blockCount, std::size_t slot)
{
  slot = std::min(slot, std::min(blockCount, kMaxEffectBlocks));
  return {kChainLeft + kChainStartX + kChainTerminalWidth + kChainInsertWidth
            + static_cast<int>(slot) * kChainSlotWidth,
          kChainTop + kChainTileTop};
}

lv_point_t reorderIndicatorPosition(std::size_t blockCount, std::size_t source,
                                    std::size_t target)
{
  if (blockCount == 0) {
    return indicatorPosition(0, 0);
  }
  source = std::min(source, blockCount - 1);
  target = std::min(target, blockCount - 1);
  auto position = indicatorPosition(blockCount, target);
  if (target > source) {
    position.x += kChainSlotWidth;
  }
  return position;
}

std::string laneToken(const UiBlock& block)
{
  if (block.type == "nam") return "NAM";
  if (block.type == "cab") return "IR";
  if (block.type == "dynamics") {
    const auto mode = block.params.value("mode", std::string{});
    if (mode == "noise_gate") return "GATE";
    if (mode == "transient_shaper") return "TRAN";
    return "CMP";
  }
  if (block.type == "distortion") {
    return block.params.value("mode", std::string{}) == "big_cheese" ? "FUZZ" : "RAT";
  }
  if (block.type == "irreverb") return "CONV";
  if (block.type == "stereo") return "WIDE";
  if (block.type == "eq") return "EQ";
  if (block.type == "wah") return "WAH";
  if (block.type == "delay") return "DLY";
  if (block.type == "reverb") return "REV";
  if (block.type == "mod") {
    const auto mode = block.params.value("mode", std::string{});
    if (mode == "chorus") return "CHO";
    if (mode == "vintage_trem") return "TREM";
    if (mode == "phaser") return "PHA";
    if (mode == "flanger") return "FLG";
    return "MOD";
  }
  std::string token = block.type.empty() ? std::string{"?"} : block.type.substr(0, 3);
  std::transform(token.begin(), token.end(), token.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return token;
}

} // namespace ardor::chain_layout
