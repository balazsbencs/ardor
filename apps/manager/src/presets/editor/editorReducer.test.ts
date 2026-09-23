import { describe, expect, it } from "vitest";

import type { Preset } from "../../api/types";
import { createEditorState, editorReducer, isEditorDirty } from "./editorReducer";
import type { EditorAction, EditorState } from "./editorTypes";
import { createEmptyPreset, createWdwPreset } from "./presetFactory";

function preset(): Preset {
  return {
    version: 1,
    name: "Studio",
    routing: "serial",
    global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1, futureGlobal: "keep" },
    blocks: [
      {
        id: "block-1",
        type: "dynamics",
        enabled: true,
        asset: "",
        params: { mode: "compressor", threshold_db: -18, futureParam: 42 },
        futureBlock: { keep: true },
      },
      {
        id: "block-2",
        type: "mod",
        enabled: true,
        asset: "",
        params: { mode: "vintage_trem", speed: 0.82, p1: 0.24, futureParam: "keep" },
      },
      { id: "custom", type: "future", enabled: false, asset: "", params: { opaque: true } },
    ],
    futureRoot: ["keep"],
  };
}

function state(source = preset()): EditorState {
  return createEditorState({ bank: 2, slot: 1 }, source);
}

function reduce(source: EditorState, ...actions: EditorAction[]): EditorState {
  return actions.reduce(editorReducer, source);
}

describe("editorReducer", () => {
  it("loads canonical snapshots and changes selection without history", () => {
    const loaded = editorReducer(state(), {
      type: "load",
      location: { bank: 8, slot: 3 },
      preset: createEmptyPreset("Loaded"),
    });
    const selected = editorReducer(loaded, { type: "select-block", blockId: undefined });
    expect(loaded.location).toEqual({ bank: 8, slot: 3 });
    expect(loaded.history).toMatchObject({ past: [], future: [], present: { name: "Loaded" } });
    expect(selected.history).toBe(loaded.history);
  });

  it("edits name and globals immutably, clamps finite values, and preserves future fields", () => {
    const before = state();
    const edited = reduce(
      before,
      { type: "set-name", name: "Changed" },
      { type: "set-global", key: "inputGainDb", value: 99 },
    );
    expect(edited.history.present.name).toBe("Changed");
    expect(edited.history.present.global.inputGainDb).toBe(24);
    expect(edited.history.present.futureRoot).toEqual(["keep"]);
    expect(edited.history.present.global.futureGlobal).toBe("keep");
    expect(before.history.present.name).toBe("Studio");
    expect(editorReducer(edited, { type: "set-global", key: "outputGainDb", value: Number.NaN })).toBe(edited);
  });

  it("stores and clears an expression assignment as undoable preset data", () => {
    const assigned = editorReducer(state(), {
      type: "set-expression",
      expression: {
        blockId: "block-2",
        parameter: "speed",
        minimum: 0.1,
        maximum: 0.9,
        inverted: true,
      },
    });
    expect(assigned.history.present.expression).toEqual({
      blockId: "block-2",
      parameter: "speed",
      minimum: 0.1,
      maximum: 0.9,
      inverted: true,
    });
    expect(assigned.history.past).toHaveLength(1);
    expect(editorReducer(assigned, { type: "set-expression" }).history.present.expression).toBeUndefined();
  });

  it.each([
    [0, ["nam", "dynamics", "mod", "future"]],
    [1, ["dynamics", "nam", "mod", "future"]],
    [99, ["dynamics", "mod", "future", "nam"]],
  ])("adds a complete block at index %s", (index, expectedTypes) => {
    const edited = editorReducer(state(), { type: "add-block", definitionId: "nam", index });
    expect(edited.history.present.blocks.map(({ type }) => type)).toEqual(expectedTypes);
    const added = edited.history.present.blocks.find(({ type }) => type === "nam");
    expect(added).toMatchObject({ id: "block-3", enabled: true, asset: "", params: {} });
    expect(edited.selectedBlockId).toBe("block-3");
  });

  it("moves with final-index semantics and keeps the moved block selected", () => {
    const edited = editorReducer(state(), { type: "move-block", blockId: "block-1", index: 2 });
    expect(edited.history.present.blocks.map(({ id }) => id)).toEqual(["block-2", "custom", "block-1"]);
    expect(edited.selectedBlockId).toBe("block-1");
  });

  it("adds a version-2 Dual Rig and edits both child chains recursively", () => {
    const added = editorReducer(state(), { type: "add-block", definitionId: "dualRig", index: 1 });
    const rig = added.history.present.blocks.find(({ type }) => type === "dualRig");
    expect(added.history.present.version).toBe(2);
    expect(rig?.lanes?.left.blocks.map(({ type }) => type)).toEqual(["nam", "cab"]);
    expect(rig?.lanes?.right.blocks.map(({ type }) => type)).toEqual(["nam", "cab"]);

    const leftNam = rig?.lanes?.left.blocks[0];
    expect(leftNam).toBeDefined();
    const edited = reduce(
      added,
      { type: "set-block-asset", blockId: leftNam!.id, asset: "models/left.nam" },
      { type: "set-block-param", blockId: leftNam!.id, key: "useNano", value: true },
      { type: "add-lane-block", rigId: rig!.id, lane: "right", definitionId: "delay:digital", index: 2 },
    );
    const editedRig = edited.history.present.blocks.find(({ id }) => id === rig!.id);
    expect(editedRig?.lanes?.left.blocks[0]).toMatchObject({
      asset: "models/left.nam",
      params: expect.objectContaining({ useNano: true }),
    });
    expect(editedRig?.lanes?.right.blocks.map(({ type }) => type)).toEqual(["nam", "cab", "delay"]);

    const delay = editedRig!.lanes!.right.blocks[2];
    const reordered = editorReducer(edited, {
      type: "move-lane-block", rigId: rig!.id, blockId: delay.id, lane: "right", index: 0,
    });
    const reorderedRig = reordered.history.present.blocks.find(({ id }) => id === rig!.id);
    expect(reorderedRig?.lanes?.right.blocks.map(({ type }) => type)).toEqual(["delay", "nam", "cab"]);

    const moved = editorReducer(reordered, {
      type: "move-lane-block", rigId: rig!.id, blockId: delay.id, lane: "left", index: 2,
    });
    const movedRig = moved.history.present.blocks.find(({ id }) => id === rig!.id);
    expect(movedRig?.lanes?.left.blocks.map(({ type }) => type)).toEqual(["nam", "cab", "delay"]);
    expect(movedRig?.lanes?.right.blocks.map(({ type }) => type)).toEqual(["nam", "cab"]);

    const removed = editorReducer(moved, { type: "remove-block", blockId: delay.id });
    expect(removed.selectedBlockId).toBe(rig!.id);
  });

  it("edits version-3 WDW lanes and keeps lane mix values bounded", () => {
    const source = createWdwPreset("WDW");
    const dryId = source.wdw!.dry.blocks[0].id;
    const wetId = source.wdw!.wet.blocks[1].id;
    const loaded = createEditorState({ bank: 1, slot: 2 }, source);
    const edited = reduce(
      loaded,
      { type: "set-wdw-mix", lane: "dry", key: "pan", value: 4 },
      { type: "set-wdw-mix", lane: "wet", key: "width", value: -1 },
      { type: "add-wdw-block", lane: "wet", definitionId: "delay:digital", index: 2 },
      { type: "move-wdw-block", blockId: wetId, lane: "dry", index: 0 },
      { type: "toggle-block", blockId: dryId, enabled: false },
    );
    expect(edited.history.present.routing).toBe("wdw");
    expect(edited.history.present.wdw?.dry.pan).toBe(1);
    expect(edited.history.present.wdw?.wet.width).toBe(0);
    expect(edited.history.present.wdw?.wet.blocks.some(({ type }) => type === "delay")).toBe(true);
    expect(edited.history.present.wdw?.dry.blocks.some(({ id }) => id === wetId)).toBe(true);
    expect(edited.history.present.wdw?.dry.blocks.find(({ id }) => id === dryId)?.enabled).toBe(false);
    expect(edited.selectedBlockId).toBe(wetId);

    const wetDelay = edited.history.present.wdw?.wet.blocks.find(({ type }) => type === "delay");
    expect(wetDelay).toBeDefined();
    const rejected = editorReducer(edited, {
      type: "move-wdw-block", blockId: wetDelay!.id, lane: "dry", index: 0,
    });
    expect(rejected).toBe(edited);
  });

  it("allows WDW drafts to remove the last NAM or optional cab", () => {
    const source = createWdwPreset("Draft WDW");
    const namId = source.wdw!.dry.blocks[0].id;
    const cabId = source.wdw!.wet.blocks[1].id;
    const loaded = createEditorState({ bank: 1, slot: 1 }, source);
    const withoutNam = editorReducer(loaded, { type: "remove-block", blockId: namId });
    expect(withoutNam.history.present.wdw!.dry.blocks.map(({ id }) => id)).not.toContain(namId);
    const withoutCab = editorReducer(withoutNam, { type: "remove-block", blockId: cabId });
    expect(withoutCab.history.present.wdw!.wet.blocks.map(({ id }) => id)).not.toContain(cabId);
  });

  it("keeps serial blocks when switching topology and flattens WDW explicitly", () => {
    const serial = state();
    const wdw = editorReducer(serial, { type: "set-routing", routing: "wdw" });
    expect(wdw.history.present).toMatchObject({ version: 3, routing: "wdw", blocks: [] });
    expect(wdw.history.present.wdw?.dry.blocks.map(({ id }) => id)).toEqual(
      serial.history.present.blocks.map(({ id }) => id),
    );
    expect(new Set(wdw.history.present.wdw?.wet.blocks.map(({ id }) => id))).toHaveProperty("size", 2);

    const flattened = editorReducer(wdw, { type: "set-routing", routing: "serial" });
    expect(flattened.history.present.routing).toBe("serial");
    expect(flattened.history.present.version).toBe(2);
    expect(flattened.history.present.blocks).toHaveLength(5);
    expect(flattened.history.present.wdw).toBeUndefined();
  });

  it("toggles, updates assets, and clamps known parameters", () => {
    const edited = reduce(
      state(),
      { type: "toggle-block", blockId: "block-1", enabled: false },
      { type: "set-block-asset", blockId: "block-1", asset: "models/x.nam" },
      { type: "set-block-param", blockId: "block-1", key: "threshold_db", value: -100 },
    );
    expect(edited.history.present.blocks[0]).toMatchObject({ enabled: false, asset: "models/x.nam" });
    expect(edited.history.present.blocks[0].params.threshold_db).toBe(-60);
    expect(edited.history.present.blocks[0].params.futureParam).toBe(42);
    expect(editorReducer(edited, {
      type: "set-block-param", blockId: "block-1", key: "ratio", value: Infinity,
    })).toBe(edited);
  });

  it("updates let-ring policy only on version 4 IR reverb blocks", () => {
    const source = preset();
    source.version = 4;
    source.blocks[0] = {
      id: "block-1", type: "irreverb", enabled: true,
      asset: "reverb-irs/room.wav", params: {},
    };
    const edited = editorReducer(state(source), {
      type: "set-scene-bypass", blockId: "block-1", policy: "letRing",
    });
    expect(edited.history.present.blocks[0].sceneBypass).toBe("letRing");

    source.version = 1;
    expect(editorReducer(state(source), {
      type: "set-scene-bypass", blockId: "block-1", policy: "letRing",
    })).toEqual(state(source));
  });

  it("duplicates immediately after the source with a collision-free id and selects it", () => {
    const edited = editorReducer(state(), { type: "duplicate-block", blockId: "block-1" });
    expect(edited.history.present.blocks.map(({ id }) => id)).toEqual(["block-1", "block-3", "block-2", "custom"]);
    expect(edited.history.present.blocks[1].params).toEqual(edited.history.present.blocks[0].params);
    expect(edited.history.present.blocks[1].params).not.toBe(edited.history.present.blocks[0].params);
    expect(edited.selectedBlockId).toBe("block-3");
  });

  it("removes a block and selects its nearest surviving neighbor", () => {
    const middle = editorReducer(state(), { type: "remove-block", blockId: "block-2" });
    expect(middle.selectedBlockId).toBe("custom");
    const last = editorReducer(middle, { type: "remove-block", blockId: "custom" });
    expect(last.selectedBlockId).toBe("block-1");
  });

  it("changes modes within a family while preserving shared and unknown parameters", () => {
    const edited = editorReducer(state(), {
      type: "change-definition", blockId: "block-2", definitionId: "mod:flanger",
    });
    expect(edited.history.present.blocks[1].params).toMatchObject({
      mode: "flanger",
      speed: 0.82,
      p1: 0.24,
      futureParam: "keep",
      depth: 0.7,
    });
    expect(editorReducer(edited, {
      type: "change-definition", blockId: "block-2", definitionId: "delay:digital",
    })).toBe(edited);
  });

  it("resets known parameters while preserving unknown block data and parameters", () => {
    const edited = editorReducer(state(), { type: "reset-block", blockId: "block-2" });
    expect(edited.history.present.blocks[1].params).toMatchObject({
      mode: "vintage_trem",
      speed: 0.35,
      p1: 0,
      futureParam: "keep",
    });
    expect(edited.history.present.blocks[0].futureBlock).toEqual({ keep: true });
  });

  it("edits nested EQ bands immutably and restores a canonical five-band shape", () => {
    const eqPreset = preset();
    eqPreset.blocks[0] = {
      id: "block-1",
      type: "eq",
      enabled: true,
      asset: "",
      params: { mode: "parametric_eq_5", bands: [{ enabled: true, frequency_hz: 100, q: 1, gain_db: 0, future: 1 }] },
    };
    const originalBands = eqPreset.blocks[0].params.bands;
    const edited = editorReducer(state(eqPreset), {
      type: "set-eq-band", blockId: "block-1", band: 0, patch: { frequency_hz: 50000, gain_db: 6 },
    });
    const bands = edited.history.present.blocks[0].params.bands as Array<Record<string, unknown>>;
    expect(bands).toHaveLength(5);
    expect(bands[0]).toMatchObject({ frequency_hz: 20000, gain_db: 6, future: 1 });
    expect(bands).not.toBe(originalBands);
    expect(eqPreset.blocks[0].params.bands).toHaveLength(1);
  });

  it("replaces the present and marks the daemon response as canonical", () => {
    const replacement = createEmptyPreset("Replacement");
    const replaced = editorReducer(state(), { type: "replace-present", preset: replacement });
    expect(replaced.history.past).toHaveLength(1);
    expect(replaced.history.present.name).toBe("Replacement");
    const canonical = { ...replacement, name: "Canonical" };
    const saved = editorReducer(replaced, { type: "mark-saved", preset: canonical });
    expect(saved.saved).toEqual(canonical);
    expect(saved.history.present).toEqual(canonical);
    expect(isEditorDirty(saved)).toBe(false);
  });

  it("edits scene identity and transition settings without recalling audio", () => {
    const preset = createEmptyPreset("Scenes");
    preset.version = 4;
    preset.sceneSet = {
      defaultSceneId: "scene-1", openIn: "presets",
      scenes: [
        { id: "scene-1", name: "Scene 1", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
        { id: "scene-2", name: "Scene 2", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
        { id: "scene-3", name: "Scene 3", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
        { id: "scene-4", name: "Scene 4", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
      ],
    };
    const selected = editorReducer(state(preset), { type: "select-scene", sceneId: "scene-3" });
    expect(selected.editingSceneId).toBe("scene-3");
    expect(selected.history.past).toHaveLength(0);

    const edited = reduce(selected,
      { type: "set-scene-name", sceneId: "scene-3", name: "Lead" },
      { type: "set-scene-enter-time", sceneId: "scene-3", value: 550 },
      { type: "set-scene-trim", sceneId: "scene-3", value: 2.5 },
      { type: "set-default-scene", sceneId: "scene-3" },
      { type: "set-scene-open-in", value: "scenes" },
    );
    expect(edited.history.present.sceneSet?.scenes[2]).toMatchObject({ name: "Lead", enterTimeMs: 550, outputTrimDb: 2.5 });
    expect(edited.history.present.sceneSet).toMatchObject({ defaultSceneId: "scene-3", openIn: "scenes" });
    expect(editorReducer(edited, { type: "undo" }).history.present.sceneSet?.openIn).toBe("presets");
  });

  it("starts four editable scenes and routes owned input gain to the selected scene", () => {
    const enabled = editorReducer(state(), { type: "enable-scenes" });
    expect(enabled.history.present.version).toBe(4);
    expect(enabled.history.present.sceneSet?.scenes).toHaveLength(4);
    expect(enabled.editingSceneId).toBe("scene-1");
    expect(editorReducer(enabled, { type: "undo" }).history.present.sceneSet).toBeUndefined();

    const owned = editorReducer(enabled, {
      type: "set-scene-input-scope", sceneId: "scene-1", scope: "scene",
    });
    expect(owned.history.present.sceneSet?.scenes.every((scene) =>
      scene.targets.some((target) => target.target === "inputGainDb"))).toBe(true);
    const changed = editorReducer(owned, {
      type: "set-scene-input-gain", sceneId: "scene-1", value: 6,
    });
    expect(changed.history.present.global.inputGainDb).toBe(0);
    expect(changed.history.present.sceneSet?.scenes[0].targets[0]).toEqual({ target: "inputGainDb", value: 6 });
    expect(changed.history.present.sceneSet?.scenes[1].targets[0]).toEqual({ target: "inputGainDb", value: 0 });
    const shared = editorReducer(changed, {
      type: "set-scene-input-scope", sceneId: "scene-1", scope: "shared",
    });
    expect(shared.history.present.global.inputGainDb).toBe(6);
    expect(shared.history.present.sceneSet?.scenes.every((scene) => scene.targets.length === 0)).toBe(true);
  });

  it("edits an owned wet/dry/wet lane value without changing the shared lane", () => {
    const source = createWdwPreset();
    const enabled = editorReducer(state(source), { type: "enable-scenes" });
    const withTargets = structuredClone(enabled.history.present);
    for (const scene of withTargets.sceneSet!.scenes) {
      scene.targets.push({ target: "wdwLane", lane: "wet", parameter: "levelDb", value: 0 });
    }
    const changed = editorReducer(state(withTargets), {
      type: "set-scene-wdw-mix", sceneId: "scene-1", lane: "wet", key: "levelDb", value: -6,
    });
    expect(changed.history.present.wdw?.wet.levelDb).toBe(0);
    expect(changed.history.present.sceneSet?.scenes[0].targets[0]).toMatchObject({ value: -6 });
    expect(changed.history.present.sceneSet?.scenes[1].targets[0]).toMatchObject({ value: 0 });
  });

  it("converts parameter ownership across all scenes and commits the editing value when shared", () => {
    const source = preset();
    source.version = 4;
    source.sceneSet = {
      defaultSceneId: "scene-1", openIn: "scenes", scenes: [
        { id: "scene-1", name: "One", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
        { id: "scene-2", name: "Two", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
        { id: "scene-3", name: "Three", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
        { id: "scene-4", name: "Four", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
      ],
    };
    let editor = editorReducer(state(source), {
      type: "set-scene-scope", sceneId: "scene-1", blockId: "block-1",
      parameter: "threshold_db", scope: "scene", value: -18,
    });
    expect(editor.history.present.sceneSet?.scenes.every((scene) => scene.targets.some((target) =>
      target.target === "parameter" && target.parameter === "threshold_db" && target.value === -18))).toBe(true);

    editor = editorReducer(editor, {
      type: "set-scene-parameter", sceneId: "scene-1", blockId: "block-1",
      parameter: "threshold_db", value: -9,
    });
    editor = editorReducer(editor, {
      type: "set-scene-scope", sceneId: "scene-1", blockId: "block-1",
      parameter: "threshold_db", scope: "shared", value: -9,
    });
    expect(editor.history.present.blocks[0].params.threshold_db).toBe(-9);
    expect(editor.history.present.sceneSet?.scenes.every((scene) => scene.targets.length === 0)).toBe(true);
  });

  it("copies scene sound data without replacing destination identity and swaps complete identities", () => {
    const source = createEmptyPreset("Scenes");
    source.version = 4;
    source.sceneSet = {
      defaultSceneId: "one", openIn: "scenes", scenes: [
        { id: "one", name: "One", enterTimeMs: 500, outputTrimDb: 2, targets: [{ target: "inputGainDb", value: 3 }] },
        { id: "two", name: "Two", enterTimeMs: 0, outputTrimDb: 0, targets: [{ target: "inputGainDb", value: -2 }] },
        { id: "three", name: "Three", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
        { id: "four", name: "Four", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
      ],
    };
    const copied = editorReducer(state(source), { type: "copy-scene", sourceSceneId: "one", destinationSceneId: "two" });
    expect(copied.history.present.sceneSet?.scenes[1]).toMatchObject({
      id: "two", name: "Two", enterTimeMs: 500, outputTrimDb: 2, targets: [{ target: "inputGainDb", value: 3 }],
    });
    expect(editorReducer(copied, { type: "undo" }).history.present.sceneSet?.scenes[1].targets[0]).toMatchObject({ value: -2 });

    const swapped = editorReducer(copied, { type: "swap-scenes", firstSceneId: "one", secondSceneId: "three" });
    expect(swapped.history.present.sceneSet?.scenes.map(({ id }) => id)).toEqual(["three", "two", "one", "four"]);
    expect(swapped.history.present.sceneSet?.defaultSceneId).toBe("one");

    const rowCopied = editorReducer(state(source), { type: "copy-scene-row-across", sourceSceneId: "one", rowKey: "inputGainDb" });
    expect(rowCopied.history.present.sceneSet?.scenes.map((scene) => scene.targets[0])).toEqual([
      { target: "inputGainDb", value: 3 },
      { target: "inputGainDb", value: 3 },
      { target: "inputGainDb", value: 3 },
      { target: "inputGainDb", value: 3 },
    ]);
    expect(editorReducer(rowCopied, { type: "undo" }).history.present.sceneSet?.scenes[1].targets[0]).toMatchObject({ value: -2 });
  });

  it("supports undo, redo, branch truncation, and a 100-snapshot cap", () => {
    const edited = reduce(state(), { type: "set-name", name: "One" }, { type: "set-name", name: "Two" });
    const undone = editorReducer(edited, { type: "undo" });
    expect(undone.history.present.name).toBe("One");
    expect(undone.history.future).toHaveLength(1);
    expect(editorReducer(undone, { type: "redo" }).history.present.name).toBe("Two");
    expect(editorReducer(undone, { type: "set-name", name: "Branch" }).history.future).toEqual([]);

    let capped = state();
    for (let index = 0; index < 105; index += 1) {
      capped = editorReducer(capped, { type: "set-name", name: `Name ${index}` });
    }
    expect(capped.history.past).toHaveLength(100);
  });
});
