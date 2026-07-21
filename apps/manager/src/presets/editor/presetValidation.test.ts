import { describe, expect, it } from "vitest";

import type { Asset, Preset, PresetBlock } from "../../api/types";
import { createBlockFromDefinition } from "../../effects/catalog";
import { createEmptyPreset } from "./presetFactory";
import {
  firstBlockingIssue,
  issuesForBlock,
  validatePreset,
  type AssetInventory,
} from "./presetValidation";

const assets: AssetInventory = {
  models: [{ id: "amp.nam", kind: "model", filename: "amp.nam", path: "models/amp.nam", sizeBytes: 1 }],
  irs: [{ id: "cab.wav", kind: "ir", filename: "cab.wav", path: "irs/cab.wav", sizeBytes: 1 }],
};

function validPreset(blocks: PresetBlock[] = []): Preset {
  return { ...createEmptyPreset("Valid"), blocks };
}

function codes(preset: Preset): string[] {
  return validatePreset(preset, assets).issues.map(({ code }) => code);
}

describe("preset validation", () => {
  it.each([
    ["version", (preset: Preset) => Object.assign(preset, { version: 2 })],
    ["routing", (preset: Preset) => Object.assign(preset, { routing: "parallel" })],
    ["name-length", (preset: Preset) => Object.assign(preset, { name: "x".repeat(121) })],
  ])("rejects invalid preset-level %s", (code, mutate) => {
    const preset = validPreset();
    mutate(preset);
    const result = validatePreset(preset, assets);
    expect(result).toMatchObject({ canSave: false, canApply: false });
    expect(result.issues[0].code).toBe(code);
  });

  it.each([
    ["inputGainDb", Number.NaN, "global-non-finite"],
    ["outputGainDb", 25, "global-range"],
    ["safetyLimitDb", 1, "global-range"],
  ])("validates global %s", (key, value, code) => {
    const preset = validPreset();
    (preset.global as Record<string, unknown>)[key] = value;
    expect(codes(preset)).toContain(code);
  });

  it.each([
    ["", "block-id-empty"],
    ["x".repeat(81), "block-id-length"],
  ])("rejects invalid block ids", (id, code) => {
    const block = createBlockFromDefinition("dynamics:compressor", []);
    block.id = id;
    expect(codes(validPreset([block]))).toContain(code);
  });

  it("rejects duplicate ids and more than ten blocks", () => {
    const blocks = Array.from({ length: 11 }, (_, index) => ({
      ...createBlockFromDefinition("dynamics:compressor", []), id: index < 2 ? "same" : `block-${index}`,
    }));
    expect(codes(validPreset(blocks))).toEqual(expect.arrayContaining(["block-limit", "block-id-duplicate"]));
  });

  it.each(["/tmp/a.nam", "models\\a.nam", "models/./a.nam", "models/../a.nam", "C:/a.nam"])(
    "rejects invalid relative asset path %s",
    (asset) => {
      const block = createBlockFromDefinition("nam", [], asset);
      expect(codes(validPreset([block]))).toContain("asset-path");
    },
  );

  it("allows missing assets to be saved but not applied", () => {
    const empty = validatePreset(validPreset([createBlockFromDefinition("nam", [])]), assets);
    const missing = validatePreset(validPreset([
      createBlockFromDefinition("cab", [], "irs/missing.wav"),
    ]), assets);
    expect(empty).toMatchObject({ canSave: true, canApply: false });
    expect(empty.issues[0]).toMatchObject({ code: "asset-required", blockId: "block-1" });
    expect(missing.issues[0]).toMatchObject({ code: "asset-missing", blockId: "block-1" });
  });

  it("rejects wrong known parameter types and ranges", () => {
    const compressor = createBlockFromDefinition("dynamics:compressor", []);
    compressor.params.ratio = 21;
    compressor.params.auto_makeup = "yes";
    const result = validatePreset(validPreset([compressor]), assets);
    expect(result.issues).toEqual(expect.arrayContaining([
      expect.objectContaining({ code: "parameter-range", blockId: "block-1", field: "params.ratio" }),
      expect.objectContaining({ code: "parameter-type", blockId: "block-1", field: "params.auto_makeup" }),
    ]));
    expect(result.canSave).toBe(false);
  });

  it("warns for unknown block types and unsupported known-family modes", () => {
    const future = { id: "future", type: "future", enabled: false, asset: "", params: {} };
    const unsupported = { id: "mod-x", type: "mod", enabled: false, asset: "", params: { mode: "future" } };
    const result = validatePreset(validPreset([future, unsupported]), assets);
    expect(result).toMatchObject({ canSave: true, canApply: false });
    expect(result.issues).toEqual([
      expect.objectContaining({ code: "block-unsupported", blockId: "future" }),
      expect.objectContaining({ code: "mode-unsupported", blockId: "mod-x" }),
    ]);
  });

  it.each(["nam", "cab", "mod:vintage_trem", "delay:digital", "reverb:room"])(
    "allows at most one enabled constrained %s block",
    (definitionId) => {
      const first = createBlockFromDefinition(definitionId, [], definitionId === "nam" ? "models/amp.nam" : definitionId === "cab" ? "irs/cab.wav" : undefined);
      const second = { ...structuredClone(first), id: "block-2", enabled: true };
      const duplicate = validatePreset(validPreset([first, second]), assets);
      expect(duplicate.issues).toContainEqual(expect.objectContaining({ code: "constraint-duplicate", blockId: "block-2" }));
      second.enabled = false;
      expect(validatePreset(validPreset([first, second]), assets).issues.map(({ code }) => code)).not.toContain("constraint-duplicate");
    },
  );

  it("rejects enabled mono asset blocks after the first enabled stereo Daisy block", () => {
    const delay = createBlockFromDefinition("delay:digital", []);
    const nam = createBlockFromDefinition("nam", [delay], "models/amp.nam");
    const result = validatePreset(validPreset([delay, nam]), assets);
    expect(result.issues).toContainEqual(expect.objectContaining({ code: "mono-after-stereo", blockId: nam.id }));
    nam.enabled = false;
    expect(validatePreset(validPreset([delay, nam]), assets).issues.map(({ code }) => code)).not.toContain("mono-after-stereo");
  });

  it("requires the canonical complete five-band EQ shape", () => {
    const eq = createBlockFromDefinition("eq:parametric_eq_5", []);
    (eq.params.bands as unknown[]).pop();
    expect(codes(validPreset([eq]))).toContain("eq-band-count");
    eq.params.bands = Array.from({ length: 5 }, (_, index) => ({
      enabled: true, frequency_hz: index === 0 ? 10 : 1000, q: 1, gain_db: 0,
    }));
    expect(codes(validPreset([eq]))).toContain("parameter-range");
  });

  it("sorts preset issues first and block issues in chain order without mutating input", () => {
    const preset = validPreset([
      { id: "first", type: "future", enabled: true, asset: "", params: {} },
      createBlockFromDefinition("nam", []),
    ]);
    preset.name = "x".repeat(121);
    const snapshot = structuredClone(preset);
    const result = validatePreset(preset, assets);
    expect(result.issues.map(({ blockId }) => blockId)).toEqual([undefined, "first", "block-1"]);
    expect(preset).toEqual(snapshot);
    expect(issuesForBlock(result, "block-1")).toHaveLength(1);
    expect(firstBlockingIssue(result)).toBe(result.issues[0]);
  });
});
