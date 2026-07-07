#include "preset/Preset.h"

#include <cassert>
#include <string>

int main()
{
  const auto json = nlohmann::json::parse(R"({
    "version": 1,
    "name": "Clean Lead",
    "routing": "serial",
    "global": {
      "inputGainDb": -12.0,
      "outputGainDb": -6.0,
      "safetyLimitDb": -1.0
    },
    "blocks": [
      {
        "id": "block-1",
        "type": "nam",
        "enabled": true,
        "asset": "models/clean.nam",
        "params": { "levelDb": 0.0 }
      },
      {
        "id": "block-2",
        "type": "cab",
        "enabled": false,
        "asset": "irs/open-back.wav",
        "params": { "mix": 1.0, "levelDb": -3.0 }
      }
    ]
  })");

  const ardor::Preset preset = ardor::presetFromJson(json);
  assert(preset.version == 1);
  assert(preset.name == "Clean Lead");
  assert(preset.routing == "serial");
  assert(preset.global.inputGainDb == -12.0f);
  assert(preset.global.outputGainDb == -6.0f);
  assert(preset.global.safetyLimitDb == -1.0f);
  assert(preset.blocks.size() == 2);
  assert(preset.blocks[0].id == "block-1");
  assert(preset.blocks[0].type == "nam");
  assert(preset.blocks[0].enabled);
  assert(preset.blocks[0].asset == "models/clean.nam");
  assert(preset.blocks[1].type == "cab");
  assert(!preset.blocks[1].enabled);
  assert(preset.blocks[1].params.at("mix").get<float>() == 1.0f);

  const ardor::Preset roundTrip = ardor::presetFromJson(ardor::toJson(preset));
  assert(roundTrip.blocks.size() == 2);
  assert(roundTrip.blocks[1].id == "block-2");
  assert(roundTrip.blocks[1].params.at("levelDb").get<float>() == -3.0f);
  return 0;
}
