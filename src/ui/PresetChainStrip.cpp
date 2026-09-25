#include "ui/PresetChainStrip.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <string_view>
#include <utility>

namespace ardor {
namespace {

constexpr std::size_t kMaxCodeLength = 8;

// Built-in modules carry fixed codes of at most five characters, so they fit
// the 52 px code square in the module drawer as well as the chain strip.
constexpr std::array<std::pair<std::string_view, std::string_view>, 37> kAssetCodes = {{
  {"RAT Distortion", "RAT"}, {"Big Cheese Fuzz", "FUZZ"}, {"Tape Machine", "TAPE"},
  {"Compressor", "CMP"}, {"Noise Gate", "GATE"}, {"Transient Shaper", "TRANS"},
  {"Five Band EQ", "EQ"}, {"GCB-95 Wah", "WAH"}, {"Stereo Widener", "WIDE"},
  {"Chorus", "CHO"}, {"Flanger", "FLNG"}, {"Rotary", "ROTRY"}, {"Vibe", "VIBE"},
  {"Phaser", "PHASE"}, {"Vintage Trem", "TREM"}, {"Poly Octave", "OCT"},
  {"Pattern Trem", "PTRN"}, {"Auto Swell", "SWELL"}, {"Filter", "FLTR"},
  {"Ladder Sweep", "LADDR"}, {"Formant", "FRMNT"}, {"Quadrature", "QUAD"},
  {"Destroyer", "DSTRY"}, {"Whammy", "WHAMY"}, {"Harmonizer", "HARM"},
  {"Room Reverb", "ROOM"}, {"Hall Reverb", "HALL"}, {"Plate Reverb", "PLATE"},
  {"Spring Reverb", "SPRNG"}, {"Bloom Reverb", "BLOOM"}, {"Cloud Reverb", "CLOUD"},
  {"Shimmer Reverb", "SHIMR"}, {"Chorale Reverb", "CHORL"}, {"Nonlinear Reverb", "NONLN"},
  {"Swell Reverb", "SWELL"}, {"Magneto Reverb", "MAGNT"}, {"Reflections Reverb", "REFL"},
}};

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

std::string assetCode(const std::string& assetName, const std::string& type)
{
  if (assetName.empty() || assetName == type || usesSummaryName(type)) return typeCode(type);
  const auto known = std::find_if(kAssetCodes.begin(), kAssetCodes.end(),
                                  [&](const auto& entry) { return entry.first == assetName; });
  if (known != kAssetCodes.end()) return std::string(known->second);
  // Every delay reads the same on a strip: the time family is one colour.
  if (type == "delay" || type == "time") return typeCode(type);
  std::istringstream words(assetName);
  std::string word;
  std::string last;
  std::string withDigit;
  while (words >> word) {
    last = word;
    if (withDigit.empty() && hasDigit(word)) withDigit = word;
  }
  const std::string& chosen = withDigit.empty() ? last : withDigit;
  if (chosen.empty()) return typeCode(type);
  return upper(chosen.substr(0, kMaxCodeLength));
}

std::string chainStripCode(const UiBlock& block)
{
  return assetCode(block.assetName, block.type);
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
