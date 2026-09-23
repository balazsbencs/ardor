import { describe, expect, it } from "vitest";

import type { Asset, Preset, PresetBlock } from "../../api/types";
import { createBlockFromDefinition } from "../../effects/catalog";
import { createEmptyPreset, createSceneSet, createWdwPreset, enableScenes } from "./presetFactory";
import {
  firstBlockingIssue,
  issuesForBlock,
  validatePreset,
  type AssetInventory,
} from "./presetValidation";

const assets: AssetInventory = {
  models: [{ id: "amp.nam", kind: "model", filename: "amp.nam", path: "models/amp.nam", sizeBytes: 1 }],
  irs: [{ id: "cab.wav", kind: "ir", filename: "cab.wav", path: "irs/cab.wav", sizeBytes: 1 }],
  reverbIrs: [{ id: "room.wav", kind: "ir", filename: "room.wav", path: "reverb-irs/room.wav", sizeBytes: 1 }],
};

function validPreset(blocks: PresetBlock[] = []): Preset {
  return { ...createEmptyPreset("Valid"), blocks };
}

function codes(preset: Preset): string[] {
  return validatePreset(preset, assets).issues.map(({ code }) => code);
}

function scenePreset(): Preset {
  const block = createBlockFromDefinition("delay:digital", []);
  return {
    ...validPreset([block]),
    version: 4,
    sceneSet: {
      defaultSceneId: "scene-1",
      openIn: "scenes",
      scenes: [0, 1, 2, 3].map((index) => ({
        id: `scene-${index + 1}`,
        name: `Scene ${index + 1}`,
        enterTimeMs: index === 1 ? 500 : 0,
        outputTrimDb: index === 2 ? 2 : 0,
        targets: [
          { target: "inputGainDb" as const, value: index },
          { target: "blockEnabled" as const, blockId: block.id, value: index !== 0 },
          { target: "parameter" as const, blockId: block.id, parameter: "mix", value: index / 4 },
        ],
      })) as NonNullable<Preset["sceneSet"]>["scenes"],
    },
  };
}

describe("preset validation", () => {
  it("validates convolution reverb assets against the reverb IR inventory", () => {
    const reverb = createBlockFromDefinition("irreverb", []);
    reverb.asset = "reverb-irs/room.wav";
    expect(codes(validPreset([reverb]))).not.toContain("asset-missing");

    reverb.asset = "irs/cab.wav";
    expect(codes(validPreset([reverb]))).not.toContain("asset-missing");

    reverb.asset = "irs/missing.wav";
    expect(codes(validPreset([reverb]))).toContain("asset-missing");
  });

  it.each([
    ["version", (preset: Preset) => Object.assign(preset, { version: 3 })],
    ["routing", (preset: Preset) => Object.assign(preset, { routing: "parallel" })],
    ["name-length", (preset: Preset) => Object.assign(preset, { name: "x".repeat(121) })],
  ])("rejects invalid preset-level %s", (code, mutate) => {
    const preset = validPreset();
    mutate(preset);
    const result = validatePreset(preset, assets);
    expect(result).toMatchObject({ canSave: false, canApply: false });
    expect(result.issues[0].code).toBe(code);
  });

  it("accepts a complete version 4 scene set", () => {
    expect(codes(scenePreset())).not.toEqual(expect.arrayContaining([
      "version", "scene-set-required", "scene-target-shape", "scene-target-mismatch",
    ]));
  });

  it("enables four independent scene drafts without changing the rig", () => {
    const source = validPreset();
    const converted = enableScenes(source, [{ target: "inputGainDb", value: source.global.inputGainDb }]);
    expect(converted).toMatchObject({ version: 4, routing: "serial", blocks: source.blocks });
    expect(converted.sceneSet).toEqual(createSceneSet([{ target: "inputGainDb", value: 0 }]));
    converted.sceneSet!.scenes[0].targets[0].value = -12;
    expect(converted.sceneSet!.scenes[1].targets[0].value).toBe(0);
  });

  it("requires scenes for version 4 and rejects scenes on legacy versions", () => {
    const missing = validPreset();
    missing.version = 4;
    expect(codes(missing)).toContain("scene-set-required");

    const legacy = scenePreset();
    legacy.version = 1;
    expect(codes(legacy)).toContain("scene-version");
  });

  it("validates scene identity, timing, default, and complete targets", () => {
    const preset = scenePreset();
    preset.sceneSet!.scenes[1].id = "scene-1";
    preset.sceneSet!.scenes[2].enterTimeMs = 150;
    preset.sceneSet!.scenes[3].targets.pop();
    preset.sceneSet!.defaultSceneId = "missing";
    expect(codes(preset)).toEqual(expect.arrayContaining([
      "scene-id-duplicate", "scene-enter-time", "scene-target-mismatch", "scene-default",
    ]));
  });

  it("validates scene target value types and existing block references", () => {
    const preset = scenePreset();
    const first = preset.sceneSet!.scenes[0].targets;
    first[1] = { target: "blockEnabled", blockId: "missing", value: true };
    (first[2] as { value: unknown }).value = false;
    expect(codes(preset)).toContain("scene-target-shape");
  });

  it("keeps structural block enables shared across scenes", () => {
    const preset = scenePreset();
    const cab = createBlockFromDefinition("cab", []);
    preset.blocks.push(cab);
    preset.sceneSet!.scenes.forEach((scene) => {
      scene.targets = scene.targets.map((target) => target.target === "blockEnabled"
        ? { target: "blockEnabled", blockId: cab.id, value: true }
        : target);
    });
    expect(codes(preset)).toContain("scene-bypass-shared");
  });

  it("allows let ring only for version 4 delay and reverb blocks", () => {
    const reverb = createBlockFromDefinition("irreverb", []);
    reverb.asset = "reverb-irs/room.wav";
    reverb.sceneBypass = "letRing";
    const preset = enableScenes(validPreset([reverb]), []);
    expect(codes(preset)).not.toContain("scene-bypass-policy");

    const legacy = { ...preset, version: 1 as const, sceneSet: undefined };
    expect(codes(legacy)).toContain("scene-bypass-policy");

    const delay = createBlockFromDefinition("delay:digital", []);
    delay.sceneBypass = "letRing";
    expect(codes(enableScenes(validPreset([delay]), []))).not.toContain("scene-bypass-policy");

    const modulation = createBlockFromDefinition("mod:chorus", []);
    modulation.sceneBypass = "letRing";
    expect(codes(enableScenes(validPreset([modulation]), []))).toContain("scene-bypass-policy");

    reverb.sceneBypass = "future" as "cut";
    expect(codes(enableScenes(validPreset([reverb]), []))).toContain("scene-bypass-policy");
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

  it("validates expression assignments against stable block IDs", () => {
    const compressor = createBlockFromDefinition("dynamics:compressor", []);
    const preset = validPreset([compressor]);
    preset.expression = {
      blockId: compressor.id,
      parameter: "threshold_db",
      minimum: -60,
      maximum: -10,
      inverted: false,
    };
    expect(codes(preset)).not.toContain("expression-block");

    preset.expression.blockId = "missing";
    expect(codes(preset)).toContain("expression-block");

    preset.expression.blockId = compressor.id;
    preset.expression.minimum = 1;
    preset.expression.maximum = 0;
    expect(codes(preset)).toContain("expression-range");
  });

  it("validates learned MIDI ranges and multi-action scenes", () => {
    const delay = createBlockFromDefinition("delay:digital", []);
    const chorus = createBlockFromDefinition("mod:chorus", [delay]);
    const preset = validPreset([delay, chorus]);
    preset.midiMappings = [{
      channel: 0,
      controlChange: 64,
      mode: "toggle",
      actions: [
        { target: "blockEnabled", blockId: chorus.id, value1: 1, value2: 0 },
        { target: "parameter", blockId: delay.id, parameter: "feedback", value1: 0.2, value2: 0.7 },
      ],
    }];
    expect(codes(preset)).not.toEqual(expect.arrayContaining([
      "midi-binding-shape", "midi-action-shape",
    ]));

    preset.midiMappings.push({
      channel: -1,
      controlChange: 64,
      mode: "continuous",
      actions: [{ target: "parameter", blockId: "missing", parameter: "mix", value1: 0, value2: 1 }],
    });
    expect(codes(preset)).toEqual(expect.arrayContaining([
      "midi-binding-overlap", "midi-action-shape",
    ]));
  });

  it("validates named scene MIDI actions and controller conflicts", () => {
    const preset = scenePreset();
    preset.sceneMidiMappings = [
      { channel: 0, controlChange: 70, action: "selectScene", sceneId: "scene-3" },
      { channel: 0, controlChange: 71, action: "sceneNumber" },
      { channel: 0, controlChange: 72, action: "showPresets" },
      { channel: 0, controlChange: 73, action: "showScenes" },
    ];
    expect(codes(preset)).not.toEqual(expect.arrayContaining([
      "scene-midi-shape", "scene-midi-binding-shape", "scene-midi-scene", "midi-binding-overlap",
    ]));

    preset.midiMappings = [{
      channel: -1,
      controlChange: 70,
      mode: "continuous",
      actions: [{
        target: "parameter", blockId: preset.blocks[0].id, parameter: "mix", value1: 0, value2: 1,
      }],
    }];
    preset.sceneMidiMappings[0].sceneId = "missing";
    expect(codes(preset)).toEqual(expect.arrayContaining([
      "midi-binding-overlap", "scene-midi-scene",
    ]));
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

  it("validates noise gate parameter ranges from the catalog", () => {
    const noiseGate = createBlockFromDefinition("dynamics:noise_gate", []);
    noiseGate.params.threshold_db = -81;
    noiseGate.params.hold_ms = "long";
    const result = validatePreset(validPreset([noiseGate]), assets);
    expect(result.issues).toEqual(expect.arrayContaining([
      expect.objectContaining({
        code: "parameter-range", blockId: "block-1", field: "params.threshold_db",
      }),
      expect.objectContaining({
        code: "parameter-type", blockId: "block-1", field: "params.hold_ms",
      }),
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

  it("allows NAM to fold stereo to mono while still rejecting cabinet directly after stereo", () => {
    const delay = createBlockFromDefinition("delay:digital", []);
    const nam = createBlockFromDefinition("nam", [delay], "models/amp.nam");
    expect(validatePreset(validPreset([delay, nam]), assets).issues.map(({ code }) => code)).not.toContain("mono-after-stereo");

    const cab = createBlockFromDefinition("cab", [delay, nam], "irs/cab.wav");
    expect(validatePreset(validPreset([delay, nam, cab]), assets).issues.map(({ code }) => code)).not.toContain("mono-after-stereo");
    expect(validatePreset(validPreset([delay, cab]), assets).issues).toContainEqual(
      expect.objectContaining({ code: "mono-after-stereo", blockId: cab.id }),
    );
  });

  it("validates all Dual Amp lane assets and conflicts with standalone amp blocks", () => {
    const dual = createBlockFromDefinition("dualAmp", []);
    dual.params.leftNamAsset = "models/amp.nam";
    dual.params.leftIrAsset = "irs/cab.wav";
    dual.params.rightNamAsset = "models/amp.nam";
    dual.params.rightIrAsset = "irs/cab.wav";
    expect(validatePreset(validPreset([dual]), assets)).toMatchObject({ canSave: true, canApply: true });

    dual.params.rightIrAsset = "../escape.wav";
    expect(validatePreset(validPreset([dual]), assets).issues).toContainEqual(
      expect.objectContaining({ code: "asset-path", field: "params.rightIrAsset" }),
    );
    dual.params.rightIrAsset = "irs/cab.wav";

    const nam = createBlockFromDefinition("nam", [dual], "models/amp.nam");
    expect(validatePreset(validPreset([dual, nam]), assets).issues).toContainEqual(
      expect.objectContaining({ code: "dual-amp-conflict", blockId: nam.id }),
    );
  });

  it("validates version-2 Dual Rig child chains and forbids nested split regions", () => {
    const rig = createBlockFromDefinition("dualRig", []);
    const left = rig.lanes!.left.blocks;
    const right = rig.lanes!.right.blocks;
    left[0].asset = "models/amp.nam";
    left[1].asset = "irs/cab.wav";
    right[0].asset = "models/amp.nam";
    right[1].asset = "irs/cab.wav";
    const preset = validPreset([rig]);
    preset.version = 2;
    expect(validatePreset(preset, assets)).toMatchObject({ canSave: true, canApply: true });

    right[0].asset = "models/missing.nam";
    expect(validatePreset(preset, assets).issues).toContainEqual(
      expect.objectContaining({ code: "asset-missing", blockId: right[0].id }),
    );
    right[0].asset = "models/amp.nam";

    const nested = createBlockFromDefinition("dualRig", [rig]);
    rig.lanes!.left.blocks.push(nested);
    expect(validatePreset(preset, assets).issues).toContainEqual(
      expect.objectContaining({ code: "nested-split", blockId: nested.id }),
    );
  });

  it("validates version-3 WDW lanes and resolves lane-scoped expression/MIDI targets", () => {
    const preset = createWdwPreset("Wet Dry Wet");
    const { dry, wet } = preset.wdw!;
    dry.blocks[0].asset = "models/amp.nam";
    dry.blocks[1].asset = "irs/cab.wav";
    wet.blocks[0].asset = "models/amp.nam";
    wet.blocks[1].asset = "irs/cab.wav";
    const delay = createBlockFromDefinition("delay:digital", wet.blocks);
    wet.blocks.push(delay);
    preset.expression = {
      blockId: delay.id, parameter: "mix", minimum: 0, maximum: 1, inverted: false,
    };
    preset.midiMappings = [{
      channel: 0, controlChange: 22, mode: "toggle",
      actions: [{ target: "blockEnabled", blockId: delay.id, value1: 0, value2: 1 }],
    }];
    expect(validatePreset(preset, assets)).toMatchObject({ canSave: true, canApply: true });

    const namOnly = structuredClone(preset);
    namOnly.wdw!.dry.blocks.splice(1, 1);
    namOnly.wdw!.wet.blocks.splice(1, 1);
    expect(validatePreset(namOnly, assets)).toMatchObject({ canSave: true, canApply: true });

    const bypassedCab = structuredClone(preset);
    bypassedCab.wdw!.dry.blocks[1].enabled = false;
    expect(validatePreset(bypassedCab, assets)).toMatchObject({ canSave: true, canApply: true });

    wet.blocks[1].asset = "irs/missing.wav";
    expect(codes(preset)).toContain("asset-missing");
    wet.blocks[1].asset = "irs/cab.wav";
    preset.wdw!.dry.pan = 2;
    expect(codes(preset)).toContain("wdw-pan-range");
    preset.wdw!.dry.pan = 0;
    preset.wdw!.wet.pan = 0.25;
    expect(codes(preset)).toContain("wdw-wet-pan");
    preset.wdw!.wet.pan = 0;
    preset.wdw!.dry.blocks[0].enabled = false;
    expect(codes(preset)).toContain("wdw-required-disabled");
  });

  it("flags a time effect placed on Dry and keeps the lane rule singular", () => {
    const preset = createWdwPreset("WDW placement");
    const dry = preset.wdw!.dry;
    dry.blocks[0].asset = "models/amp.nam";
    dry.blocks[1].asset = "irs/cab.wav";
    const delay = createBlockFromDefinition("delay:digital", dry.blocks);
    dry.blocks.push(delay);
    const placementIssues = validatePreset(preset, assets).issues.filter(({ code, blockId }) =>
      code === "wdw-placement" && blockId === delay.id,
    );
    expect(placementIssues).toHaveLength(1);
  });

  it("requires the canonical complete five-band EQ shape", () => {
    const eq = createBlockFromDefinition("eq:parametric_eq_5", []);
    (eq.params.bands as unknown[]).pop();
    expect(codes(validPreset([eq]))).toContain("eq-band-count");
    eq.params.bands = Array.from({ length: 5 }, (_, index) => ({
      enabled: true, frequency_hz: index === 0 ? 10 : 1000, q: 1, gain_db: 0,
    }));
    expect(codes(validPreset([eq]))).toContain("parameter-range");
    (eq.params.high_pass as Record<string, unknown>).slope_db_per_octave = 16;
    expect(validatePreset(validPreset([eq]), assets).issues).toContainEqual(
      expect.objectContaining({
        code: "parameter-range",
        field: "params.high_pass.slope_db_per_octave",
      }),
    );
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
