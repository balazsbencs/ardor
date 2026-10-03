import type { Preset, PresetBlock } from "../api/types";
import type { EditorAction } from "../presets/editor/editorTypes";
import { assetRefs } from "./assetRefs";

const DUAL_AMP_KEYS = ["leftNamAsset", "leftIrAsset", "rightNamAsset", "rightIrAsset"] as const;

/** Editor actions that make an unsaved draft follow a renamed file. */
export function renameDraftActions(preset: Preset, allBlocks: PresetBlock[], oldPath: string, newPath: string): EditorAction[] {
  const blockIds = new Set(assetRefs(preset).filter(({ path }) => path === oldPath).map(({ blockId }) => blockId));
  return allBlocks.filter((block) => blockIds.has(block.id)).flatMap((block): EditorAction[] => {
    if (block.type !== "dualAmp") return block.asset === oldPath ? [{ type: "set-block-asset", blockId: block.id, asset: newPath }] : [];
    return DUAL_AMP_KEYS
      .filter((key) => block.params[key] === oldPath)
      .map((key): EditorAction => ({ type: "set-block-param", blockId: block.id, key, value: newPath }));
  });
}
