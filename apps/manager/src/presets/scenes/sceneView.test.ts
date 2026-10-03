import { describe, expect, it } from "vitest";

import type { PresetBlock, PresetScene } from "../../api/types";
import { applySceneToBlock, applySceneToBlocks, sceneOwns } from "./sceneView";

const delay: PresetBlock = { id: "d1", type: "delay", enabled: true, asset: "", params: { mode: "tape", mix: 0.25 } };
const scene: PresetScene = {
  id: "solo", name: "Solo", enterTimeMs: 0, outputTrimDb: 0,
  targets: [
    { target: "parameter", blockId: "d1", parameter: "mix", value: 0.4 },
    { target: "blockEnabled", blockId: "c1", value: false },
  ],
};

describe("sceneView", () => {
  it("applies scene values to one block without changing the source", () => {
    const shown = applySceneToBlock(delay, scene);
    expect(shown.params.mix).toBe(0.4);
    expect(delay.params.mix).toBe(0.25);
  });

  it("returns the same block when the scene does not touch it", () => {
    const other: PresetBlock = { ...delay, id: "x" };
    expect(applySceneToBlock(other, scene)).toBe(other);
    expect(applySceneToBlock(delay, undefined)).toBe(delay);
  });

  it("applies block enabled inside Dual Rig lanes", () => {
    const chorus: PresetBlock = { id: "c1", type: "mod", enabled: true, asset: "", params: {} };
    const rig: PresetBlock = { id: "r1", type: "dualRig", enabled: true, asset: "", params: {}, lanes: { left: { blocks: [chorus] }, right: { blocks: [] } } };
    const [shown] = applySceneToBlocks([rig], scene);
    expect(shown.lanes?.left.blocks[0].enabled).toBe(false);
  });

  it("tells which addresses a scene owns", () => {
    expect(sceneOwns(scene, "d1", "mix")).toBe(true);
    expect(sceneOwns(scene, "d1", "time")).toBe(false);
    expect(sceneOwns(scene, "c1")).toBe(true);
    expect(sceneOwns(undefined, "d1", "mix")).toBe(false);
  });
});
