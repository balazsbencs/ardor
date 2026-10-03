import { describe, expect, it } from "vitest";

import type { Preset, PresetBlock } from "../api/types";
import { allPresetBlocksInPreset, createEditorState, editorReducer } from "../presets/editor/editorReducer";
import { blockOf } from "../test/blocks";
import { renameDraftActions } from "./renameDraft";

const global = { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 };
const OLD = "models/Clean.nam";
const NEW = "models/Clean v2.nam";
const nam = (id: string, asset: string): PresetBlock => ({ ...blockOf("nam", id), asset });

/** Applies the rename actions to a dirty draft, as useAssetLibrary does. */
function followRename(preset: Preset): Preset {
  const dirty = editorReducer(createEditorState({ bank: 0, slot: 0 }, preset), { type: "set-name", name: "Edited" });
  const actions = renameDraftActions(dirty.history.present, allPresetBlocksInPreset(dirty.history.present), OLD, NEW);
  return actions.reduce(editorReducer, dirty).history.present;
}

describe("renameDraftActions", () => {
  it("follows a rename into a Dual Rig lane", () => {
    const rig = blockOf("dualRig", "r1");
    const lanes = { left: { blocks: [nam("l1", OLD)] }, right: { blocks: [nam("r2", "models/Other.nam")] } };
    const next = followRename({ version: 2, name: "Rig", routing: "serial", global, blocks: [{ ...rig, lanes }] });
    expect(next.blocks[0].lanes!.left.blocks[0].asset).toBe(NEW);
    expect(next.blocks[0].lanes!.right.blocks[0].asset).toBe("models/Other.nam");
    expect(next.name).toBe("Edited");
  });

  it("follows a rename into a WDW lane", () => {
    const next = followRename({
      version: 3, name: "Wdw", routing: "wdw", global, blocks: [],
      wdw: { dry: { enabled: true, levelDb: 0, blocks: [] }, wet: { enabled: true, levelDb: 0, blocks: [nam("w1", OLD)] } },
    });
    expect(next.wdw!.wet.blocks[0].asset).toBe(NEW);
  });

  it("follows a rename into the Dual Amp file params", () => {
    const dual: PresetBlock = {
      ...blockOf("dualAmp", "a1"),
      params: { ...blockOf("dualAmp", "a1").params, leftNamAsset: OLD, rightNamAsset: OLD, leftIrAsset: "irs/Room.wav" },
    };
    const next = followRename({ version: 1, name: "Dual", routing: "serial", global, blocks: [dual] });
    expect(next.blocks[0].params).toMatchObject({ leftNamAsset: NEW, rightNamAsset: NEW, leftIrAsset: "irs/Room.wav" });
  });
});
