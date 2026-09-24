#pragma once

#include "ui/UiModel.h"

#include <string>
#include <vector>

namespace ardor {

// One segment of the chain strip on a preset tile: a short, glanceable code
// plus the block type that picks its family colour.
struct ChainStripSegment {
  std::string code;
  std::string type;
  bool enabled = true;

  bool operator==(const ChainStripSegment&) const = default;
};

// Short upper-case code for a block. Built-in modules have fixed codes such
// as "CMP" or "PLATE", and every delay reads "DLY". For other assets a word
// that contains a digit wins (cabs read as "4X12"), otherwise the last word
// of the asset name, capped at eight characters. Blocks without a real asset
// name fall back to a type code such as "AMP".
std::string chainStripCode(const UiBlock& block);
// The same code for a module that is not in a chain yet.
std::string assetCode(const std::string& assetName, const std::string& type);

// Type code alone ("AMP", "DLY", "RIG"); unknown types upper-case as-is.
std::string blockTypeCode(const std::string& type);

// Top-level blocks of a preset, in chain order. Dual Rig lanes collapse into
// their parent block's single segment.
std::vector<ChainStripSegment> presetChainStrip(const UiPreset& preset);

} // namespace ardor
