#include "ui/PresetChainStrip.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <string_view>
#include <utility>

namespace ardor {
namespace {

constexpr std::size_t kMaxCodeLength = 10;

// Type codes for blocks whose asset name is missing or is a generated
// summary rather than a name a player chose.
constexpr std::array<std::pair<std::string_view, std::string_view>, 15> kTypeCodes = {{
  {"nam", "AMP"},
  {"dualAmp", "2 AMP"},
  {"dualRig", "RIG"},
  {"cab", "CAB"},
  {"distortion", "DRV"},
  {"dynamics", "DYN"},
  {"eq", "EQ"},
  {"wah", "WAH"},
  {"mod", "MOD"},
  {"modulation", "MOD"},
  {"delay", "DLY"},
  {"time", "DLY"},
  {"reverb", "REV"},
  {"irreverb", "REV"},
  {"stereo", "ST"},
}};

std::string upper(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return value;
}

std::string typeCode(const std::string& type)
{
  const auto match = std::find_if(kTypeCodes.begin(), kTypeCodes.end(),
                                  [&](const auto& entry) { return entry.first == type; });
  return match != kTypeCodes.end() ? std::string(match->second) : upper(type);
}

bool hasDigit(const std::string& word)
{
  return std::any_of(word.begin(), word.end(),
                     [](unsigned char character) { return std::isdigit(character) != 0; });
}

bool usesSummaryName(const std::string& type)
{
  return type == "dualRig" || type == "dualAmp";
}

} // namespace

std::string blockTypeCode(const std::string& type)
{
  return typeCode(type);
}

std::string chainStripCode(const UiBlock& block)
{
  if (block.assetName.empty() || block.assetName == block.type || usesSummaryName(block.type)) {
    return typeCode(block.type);
  }
  std::istringstream words(block.assetName);
  std::string word;
  std::string last;
  std::string withDigit;
  while (words >> word) {
    last = word;
    if (withDigit.empty() && hasDigit(word)) withDigit = word;
  }
  const std::string& chosen = withDigit.empty() ? last : withDigit;
  if (chosen.empty()) return typeCode(block.type);
  return upper(chosen.substr(0, kMaxCodeLength));
}

std::vector<ChainStripSegment> presetChainStrip(const UiPreset& preset)
{
  std::vector<ChainStripSegment> segments;
  segments.reserve(preset.blocks.size());
  for (const auto& block : preset.blocks) {
    segments.push_back({chainStripCode(block), block.type, block.enabled});
  }
  return segments;
}

} // namespace ardor
