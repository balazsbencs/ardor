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

  it("contains the complete unique set of 46 definitions", () => {
    expect(definitions).toHaveLength(46);
    expect(new Set(definitions.map(({ id }) => id)).size).toBe(46);
    expect(new Set(definitions.map(({ blockType, mode }) => `${blockType}:${mode ?? ""}`)).size).toBe(46);
    expect(new Set(definitions.map(({ name }) => name)).size).toBe(46);
    expect(definitions.every(({ controls }) => controls.length > 0)).toBe(true);
    expect(definitions.filter(({ blockType }) => blockType === "mod")).toHaveLength(15);
    expect(definitions.filter(({ blockType }) => blockType === "delay")).toHaveLength(10);
    expect(definitions.filter(({ blockType }) => blockType === "reverb")).toHaveLength(12);
  });

  it("groups compressor, noise gate, EQ, and wah under Utility", () => {
    expect(getEffectDefinition("dynamics:compressor").category).toBe("utility");
    expect(getEffectDefinition("dynamics:noise_gate").category).toBe("utility");
    expect(getEffectDefinition("eq:parametric_eq_5").category).toBe("utility");
    expect(getEffectDefinition("wah:gcb95").category).toBe("utility");
    expect(definitions.filter(({ category }) => category === "utility")).toHaveLength(4);
  });

  it("keeps Daisy presets normalized while attaching physical UI displays", () => {
    for (const definition of definitions.filter(({ blockType }) => ["mod", "delay", "reverb"].includes(blockType))) {
      expect(definition.controls).toHaveLength(7);
      for (const control of definition.controls) {
        expect(control).toMatchObject({ kind: "number", minimum: 0, maximum: 1, unit: "percent", display: expect.any(Object) });
        if (control.kind === "number") {
          for (const value of [0, control.defaultValue, 1]) {
            expect(control.display?.format(value)).not.toMatch(/NaN|undefined/);
          }
          if (control.display?.choices) {
            expect(control.step).toBe(1);
            expect(new Set(control.display.choices.map(({ label }) => label)).size).toBe(control.display.choices.length);
          } else {
            expect(control.step).toBeLessThanOrEqual(0.01);
          }
        }
      }
    }

    const tape = getEffectDefinition("delay:tape");
    const time = tape.controls.find((control) => control.kind === "number" && control.key === "time");
    const repeats = tape.controls.find((control) => control.kind === "number" && control.key === "repeats");
    const flutter = tape.controls.find((control) => control.kind === "number" && control.key === "mod_spd");
    expect(time).toMatchObject({ kind: "number", label: "Time" });
    expect(time?.kind === "number" && time.display?.format(0.25)).toBe("98.1 ms");
    expect(time).toMatchObject({ kind: "number", step: 0.001 });
    expect(time?.kind === "number" && time.display?.fromInput(2500)).toBe(1);
    expect(repeats?.kind === "number" && repeats.display?.format(1)).toBe("98%");
    expect(flutter).toMatchObject({ kind: "number", label: "Flutter Rate" });

    const shimmer = getEffectDefinition("reverb:shimmer");
    const pitch = shimmer.controls.find((control) => control.kind === "number" && control.key === "param1");
    expect(pitch?.kind === "number" && pitch.display?.format(24 / 36)).toBe("+12 st");

    const chorus = getEffectDefinition("mod:chorus");
    const type = chorus.controls.find((control) => control.kind === "number" && control.key === "p2");
    expect(type?.kind === "number" && type.display?.format(0.65)).toBe("Detune");
    expect(type).toMatchObject({ kind: "number", step: 1, display: { choices: expect.arrayContaining([
      expect.objectContaining({ value: 0.25, label: "Multi" }),
    ]) } });
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

  it("matches the noise gate controls and runtime defaults", () => {
    const noiseGate = getEffectDefinition("dynamics:noise_gate");
    expect(noiseGate.controls).toHaveLength(7);
    expect(defaultsForDefinition(noiseGate.id)).toEqual({
      mode: "noise_gate",
      threshold_db: -55,
      reduction_db: 80,
      attack_ms: 2,
      hold_ms: 50,
      release_ms: 150,
      hysteresis_db: 6,
      sidechain_hpf_hz: 80,
    });
  });

  it("defines five canonical EQ bands", () => {
    expect(defaultsForDefinition("eq:parametric_eq_5")).toEqual({
      mode: "parametric_eq_5",
      high_pass: { enabled: false, frequency_hz: 40, q: 0.70710678, slope_db_per_octave: 12 },
      bands: [80, 250, 800, 2500, 8000].map((frequency_hz) => ({
        enabled: true,
        frequency_hz,
        q: 1,
        gain_db: 0,
      })),
      low_pass: { enabled: false, frequency_hz: 16000, q: 0.70710678, slope_db_per_octave: 12 },
    });
  });

  it("defines the GCB-95 wah block and its runtime defaults", () => {
    expect(defaultsForDefinition("wah:gcb95")).toEqual({
      mode: "gcb95",
      position: 0,
      level: 0,
    });
    expect(getEffectDefinition("wah:gcb95").controls).toEqual([
      expect.objectContaining({ kind: "number", key: "position", minimum: 0, maximum: 1 }),
      expect.objectContaining({ kind: "number", key: "level", minimum: -24, maximum: 24 }),
    ]);
  });

  it("defines the NAM and cabinet asset contracts", () => {
    expect(getEffectDefinition("nam").controls).toEqual([
      { kind: "asset", label: "NAM model", assetKind: "models" },
      {
        kind: "choice",
        key: "inputMode",
        label: "Input source",
        choices: [
          { value: "sum", label: "L+R Average" },
          { value: "left", label: "Left / Mono" },
          { value: "right", label: "Right" },
        ],
        defaultValue: "sum",
      },
      { kind: "toggle", key: "useNano", label: "Use nano model", defaultValue: false },
    ]);
    expect(defaultsForDefinition("nam")).toEqual({ inputMode: "sum", useNano: false });
    expect(getEffectDefinition("cab").controls).toEqual([
      { kind: "asset", label: "Cabinet IR", assetKind: "irs" },
      expect.objectContaining({ kind: "number", key: "levelDb", defaultValue: 0 }),
      expect.objectContaining({ kind: "number", key: "mix", defaultValue: 1 }),
    ]);
  });

  it("defines the fixed two-lane Dual Amp contract", () => {
    expect(defaultsForDefinition("dualAmp")).toEqual({
      inputMode: "sum",
      leftNamAsset: "",
      leftUseNano: false,
      leftIrAsset: "",
      leftCabLevelDb: 0,
      leftCabMix: 1,
      leftPolarityInvert: false,
      rightNamAsset: "",
      rightUseNano: false,
      rightIrAsset: "",
      rightCabLevelDb: 0,
      rightCabMix: 1,
      rightPolarityInvert: false,
    });
    expect(getEffectDefinition("dualAmp").controls.filter(({ kind }) => kind === "asset"))
      .toEqual([
        expect.objectContaining({ key: "leftNamAsset", assetKind: "models" }),
        expect.objectContaining({ key: "leftIrAsset", assetKind: "irs" }),
        expect.objectContaining({ key: "rightNamAsset", assetKind: "models" }),
        expect.objectContaining({ key: "rightIrAsset", assetKind: "irs" }),
      ]);
  });

  it("creates Dual Rig with two independent NAM-to-cab lanes and unique ids", () => {
    const rig = createBlockFromDefinition("dualRig", []);
    expect(rig).toMatchObject({
      type: "dualRig",
      params: {
        inputMode: "sum",
        leftLevelDb: 0,
        leftPolarityInvert: false,
        rightLevelDb: 0,
        rightPolarityInvert: false,
      },
    });
    expect(rig.lanes?.left.blocks.map(({ type }) => type)).toEqual(["nam", "cab"]);
    expect(rig.lanes?.right.blocks.map(({ type }) => type)).toEqual(["nam", "cab"]);
    const ids = [
      rig.id,
      ...(rig.lanes?.left.blocks.map(({ id }) => id) ?? []),
      ...(rig.lanes?.right.blocks.map(({ id }) => id) ?? []),
    ];
    expect(new Set(ids).size).toBe(ids.length);
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
    expect(blocks).toHaveLength(46);
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
