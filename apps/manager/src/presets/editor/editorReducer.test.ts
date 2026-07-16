import { describe, expect, it } from "vitest";

import type { Preset } from "../../api/types";
import { createEditorState, editorReducer, isEditorDirty } from "./editorReducer";
import type { EditorAction, EditorState } from "./editorTypes";
import { createEmptyPreset } from "./presetFactory";

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
