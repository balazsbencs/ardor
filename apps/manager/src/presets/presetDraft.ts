import type { Preset, PresetBlock } from "../api/types";

export function clonePreset(preset: Preset): Preset {
  return structuredClone(preset);
}

export function setPresetName(preset: Preset, name: string): Preset {
  return { ...clonePreset(preset), name };
}

export function setGlobalParam(preset: Preset, key: "inputGainDb" | "outputGainDb", value: number): Preset {
  const draft = clonePreset(preset);
  draft.global[key] = value;
  return draft;
}

export function setBlockAsset(preset: Preset, blockId: string, asset: string): Preset {
  const draft = clonePreset(preset);
  draft.blocks = draft.blocks.map((block) => (block.id === blockId ? { ...block, asset } : block));
  return draft;
}

export function setBlockParam(preset: Preset, blockId: string, key: string, value: unknown): Preset {
  const draft = clonePreset(preset);
  draft.blocks = draft.blocks.map((block) =>
    block.id === blockId ? { ...block, params: { ...block.params, [key]: value } } : block,
  );
  return draft;
}

export function addAssetBlock(preset: Preset, type: "nam" | "cab", asset: string): Preset {
  const draft = clonePreset(preset);
  const prefix = type === "nam" ? "nam" : "cab";
  let number = 1;
  while (draft.blocks.some((block) => block.id === `${prefix}-${number}`)) {
    number += 1;
  }
  draft.blocks.push({
    id: `${prefix}-${number}`,
    type,
    enabled: true,
    asset,
    params: type === "cab" ? { levelDb: 0, mix: 1 } : { useNano: false },
  });
  return draft;
}

export function isKnownEditableBlock(block: PresetBlock): boolean {
  return ["nam", "cab", "mod", "delay", "reverb"].includes(block.type);
}
