#include "ui/PresetChainStrip.h"

#include <iostream>
#include <string>

namespace {

int require(bool ok, const std::string& message)
{
  if (!ok) {
    std::cerr << message << "\n";
    return 1;
  }
  return 0;
}

ardor::UiBlock block(std::string type, std::string assetName, bool enabled = true)
{
  ardor::UiBlock value;
  value.type = std::move(type);
  value.assetName = std::move(assetName);
  value.enabled = enabled;
  return value;
}

} // namespace

int main()
{
  using ardor::chainStripCode;

  if (require(chainStripCode(block("nam", "Clean Twin")) == "TWIN",
              "named blocks should use the last word of the asset name")) return 1;
  if (require(chainStripCode(block("cab", "Open Back 2x12")) == "2X12",
              "a word with a digit should win, so cabs read as their speaker layout")) return 1;
  if (require(chainStripCode(block("dynamics", "Compressor")) == "COMPRESSOR",
              "single-word names should stay whole")) return 1;
  if (require(chainStripCode(block("reverb", "Extraordinarily")) == "EXTRAORDIN",
              "codes should be capped at ten characters")) return 1;
  if (require(chainStripCode(block("delay", "")) == "DLY",
              "blocks without an asset name should use a type code")) return 1;
  if (require(chainStripCode(block("nam", "nam")) == "AMP",
              "an asset name equal to the type should fall back to the type code")) return 1;
  if (require(chainStripCode(block("dualRig", "Left 2 blocks  /  Right 1 blocks")) == "RIG",
              "Dual Rig summaries are not names and should use the type code")) return 1;
  if (require(chainStripCode(block("dualAmp", "L a / R b")) == "2 AMP",
              "Dual Amp summaries should use the type code")) return 1;
  if (require(chainStripCode(block("custom", "")) == "CUSTOM",
              "unknown types should fall back to the upper-case type")) return 1;

  if (require(ardor::blockTypeCode("delay") == "DLY" && ardor::blockTypeCode("nam") == "AMP"
                && ardor::blockTypeCode("dualRig") == "RIG"
                && ardor::blockTypeCode("custom") == "CUSTOM",
              "type codes should be available on their own for drawer rows")) return 1;

  ardor::UiPreset preset;
  preset.blocks = {block("dynamics", "Compressor"), block("nam", "Clean Twin"),
                   block("mod", "Chorus", false)};
  const auto segments = ardor::presetChainStrip(preset);
  if (require(segments.size() == 3, "every top-level block should produce one segment")) return 1;
  if (require(segments[0].code == "COMPRESSOR" && segments[0].type == "dynamics"
                && segments[0].enabled,
              "segments should keep chain order, type and enabled state")) return 1;
  if (require(segments[2].code == "CHORUS" && !segments[2].enabled,
              "bypassed blocks should stay in the strip as disabled segments")) return 1;
  if (require(ardor::presetChainStrip(ardor::UiPreset{}).empty(),
              "an empty preset should produce an empty strip")) return 1;

  return 0;
}
