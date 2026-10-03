import type { PresetBlock, PresetScene } from "../../api/types";

/** The block as the scene plays it: scene-owned params and enabled replace the preset values. */
export function applySceneToBlock(block: PresetBlock, scene?: PresetScene): PresetBlock {
  if (!scene) return block;
  let params = block.params;
  let enabled = block.enabled;
  let touched = false;
  for (const target of scene.targets) {
    if (target.target === "parameter" && target.blockId === block.id) {
      params = { ...params, [target.parameter]: target.value };
      touched = true;
    } else if (target.target === "blockEnabled" && target.blockId === block.id) {
      enabled = target.value;
      touched = true;
    }
  }
  return touched ? { ...block, params, enabled } : block;
}

export function applySceneToBlocks(blocks: PresetBlock[], scene?: PresetScene): PresetBlock[] {
  if (!scene) return blocks;
  return blocks.map((block) => {
    const shown = applySceneToBlock(block, scene);
    if (!shown.lanes) return shown;
    return {
      ...shown,
      lanes: {
        left: { ...shown.lanes.left, blocks: applySceneToBlocks(shown.lanes.left.blocks, scene) },
        right: { ...shown.lanes.right, blocks: applySceneToBlocks(shown.lanes.right.blocks, scene) },
      },
    };
  });
}

export function sceneOwns(scene: PresetScene | undefined, blockId: string, parameter?: string): boolean {
  return scene?.targets.some((target) => (parameter !== undefined
    ? target.target === "parameter" && target.blockId === blockId && target.parameter === parameter
    : target.target === "blockEnabled" && target.blockId === blockId)) ?? false;
}
