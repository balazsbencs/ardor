import { describe, expect, it } from "vitest";

import {
  allEffectDefinitions,
  createBlockFromDefinition,
  defaultsForDefinition,
  findEffectDefinition,
  getEffectDefinition,
  validateEffectCatalog,
} from "./catalog";

describe("effect catalog", () => {
  const definitions = allEffectDefinitions();

  it("contains the complete unique set of 39 definitions", () => {
    expect(definitions).toHaveLength(39);
    expect(new Set(definitions.map(({ id }) => id)).size).toBe(39);
    expect(new Set(definitions.map(({ blockType, mode }) => `${blockType}:${mode ?? ""}`)).size).toBe(39);
    expect(definitions.filter(({ blockType }) => blockType === "mod")).toHaveLength(13);
    expect(definitions.filter(({ blockType }) => blockType === "delay")).toHaveLength(10);
    expect(definitions.filter(({ blockType }) => blockType === "reverb")).toHaveLength(12);
  });

  it("describes all Daisy parameters as seven normalized numeric controls", () => {
    for (const definition of definitions.filter(({ blockType }) => ["mod", "delay", "reverb"].includes(blockType))) {
      expect(definition.controls).toHaveLength(7);
      for (const control of definition.controls) {
        expect(control).toMatchObject({ kind: "number", minimum: 0, maximum: 1, unit: "percent" });
      }
    }
  });

  it("matches the compressor controls and runtime defaults", () => {
    const compressor = getEffectDefinition("dynamics:compressor");
    expect(compressor.controls).toHaveLength(11);
    expect(defaultsForDefinition(compressor.id)).toEqual({
      mode: "compressor",
      threshold_db: -24,
      ratio: 4,
      attack_ms: 10,
      release_ms: 150,
      knee_db: 6,
      makeup_db: 0,
      input_gain_db: 0,
      mix: 1,
      sidechain_hpf_hz: 80,
      detector: "peak",
      auto_makeup: false,
    });
  });

  it("defines five canonical EQ bands", () => {
    expect(defaultsForDefinition("eq:parametric_eq_5")).toEqual({
      mode: "parametric_eq_5",
      bands: [80, 250, 800, 2500, 8000].map((frequency_hz) => ({
        enabled: true,
        frequency_hz,
        q: 1,
        gain_db: 0,
      })),
    });
  });

  it("defines the NAM and cabinet asset contracts", () => {
    expect(getEffectDefinition("nam").controls).toEqual([
      { kind: "asset", label: "NAM model", assetKind: "models" },
    ]);
    expect(getEffectDefinition("cab").controls).toEqual([
      { kind: "asset", label: "Cabinet IR", assetKind: "irs" },
      expect.objectContaining({ kind: "number", key: "levelDb", defaultValue: 0 }),
      expect.objectContaining({ kind: "number", key: "mix", defaultValue: 1 }),
    ]);
  });

  it("creates a complete block for every definition", () => {
    const blocks = definitions.map(({ id }, index) => {
      const block = createBlockFromDefinition(id, [], id === "nam" ? "models/amp.nam" : undefined);
      expect(block.id).toBe("block-1");
      expect(block.enabled).toBe(true);
      expect(block.params).toEqual(defaultsForDefinition(id));
      expect(findEffectDefinition(block)?.id).toBe(id);
      return { ...block, id: `block-${index + 1}` };
    });
    expect(blocks).toHaveLength(39);
  });

  it("chooses the next numeric block id and handles nonstandard collisions", () => {
    expect(createBlockFromDefinition("nam", [
      { id: "block-2", type: "future", enabled: true, asset: "", params: {} },
      { id: "custom", type: "future", enabled: true, asset: "", params: {} },
    ]).id).toBe("block-3");

    expect(createBlockFromDefinition("nam", [
      { id: "block-x", type: "future", enabled: true, asset: "", params: {} },
      { id: "block-1", type: "future", enabled: true, asset: "", params: {} },
    ]).id).toBe("block-2");
  });

  it("rejects malformed catalog data with a descriptive path", () => {
    expect(() => validateEffectCatalog({ version: 1, definitions: [{ id: "broken" }] }))
      .toThrow(/definitions\[0\]\.blockType/);
  });
});
