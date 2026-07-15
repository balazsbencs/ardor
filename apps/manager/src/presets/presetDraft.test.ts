import { describe, expect, it } from "vitest";
import type { Preset } from "../api/types";
import { addAssetBlock, isKnownEditableBlock, setBlockAsset, setBlockParam, setGlobalParam, setPresetName } from "./presetDraft";

const basePreset: Preset = {
  version: 1,
  name: "Clean",
  routing: "serial",
  global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1, future: "keep" },
  blocks: [
    { id: "nam-1", type: "nam", enabled: true, asset: "models/a.nam", params: {}, future: "keep" },
    { id: "cab-1", type: "cab", enabled: true, asset: "irs/a.wav", params: { mix: 1 } },
    { id: "future-1", type: "future", enabled: true, asset: "", params: { value: 3 } },
  ],
  futureRoot: true,
};

describe("preset drafts", () => {
  it("edits name and globals immutably", () => {
    const renamed = setPresetName(basePreset, "Lead");
    expect(renamed.name).toBe("Lead");
    expect(basePreset.name).toBe("Clean");

    const gained = setGlobalParam(basePreset, "inputGainDb", -6);
    expect(gained.global.inputGainDb).toBe(-6);
    expect(gained.global.safetyLimitDb).toBe(-1);
    expect(gained.global.future).toBe("keep");
  });

  it("edits known block assets and params while preserving future fields", () => {
    const withAsset = setBlockAsset(basePreset, "nam-1", "models/b.nam");
    expect(withAsset.blocks[0].asset).toBe("models/b.nam");
    expect(withAsset.blocks[0].future).toBe("keep");

    const withMix = setBlockParam(basePreset, "cab-1", "mix", 0.5);
    expect(withMix.blocks[1].params.mix).toBe(0.5);
    expect(withMix.blocks[2].params.value).toBe(3);
  });

  it("classifies editable blocks", () => {
    expect(isKnownEditableBlock(basePreset.blocks[0])).toBe(true);
    expect(isKnownEditableBlock(basePreset.blocks[1])).toBe(true);
    expect(isKnownEditableBlock(basePreset.blocks[2])).toBe(false);
  });

  it("adds NAM and cabinet blocks with usable defaults", () => {
    const withNam = addAssetBlock(basePreset, "nam", "models/new.nam");
    expect(withNam.blocks[withNam.blocks.length - 1]).toMatchObject({ id: "nam-2", type: "nam", asset: "models/new.nam", params: {} });

    const withCab = addAssetBlock(withNam, "cab", "irs/new.wav");
    expect(withCab.blocks[withCab.blocks.length - 1]).toMatchObject({
      id: "cab-2",
      type: "cab",
      asset: "irs/new.wav",
      params: { levelDb: 0, mix: 1 },
    });
    expect(basePreset.blocks).toHaveLength(3);
  });
});
