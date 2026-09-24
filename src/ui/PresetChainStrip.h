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

// Short upper-case code for a block. A word that contains a digit wins (cabs
// read as "4X12"), otherwise the last word of the asset name. Blocks without
// a real asset name fall back to a type code such as "AMP" or "DLY".
std::string chainStripCode(const UiBlock& block);

// Top-level blocks of a preset, in chain order. Dual Rig lanes collapse into
// their parent block's single segment.
std::vector<ChainStripSegment> presetChainStrip(const UiPreset& preset);

} // namespace ardor
