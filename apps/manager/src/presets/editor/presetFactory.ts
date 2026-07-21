import type { Preset, PresetBlock } from "../../api/types";

export function clonePreset(preset: Preset): Preset {
  return structuredClone(preset);
}

export function createEmptyPreset(name = "New Preset"): Preset {
  return {
    version: 1,
    name,
    routing: "serial",
    global: {
      inputGainDb: 0,
      outputGainDb: 0,
      safetyLimitDb: -1,
    },
    blocks: [],
  };
}

export function nextPresetBlockId(blocks: PresetBlock[]): string {
  const used = new Set(blocks.map(({ id }) => id));
  let greatest = 0;
  for (const id of used) {
    const match = /^block-([1-9]\d*)$/.exec(id);
    if (match) greatest = Math.max(greatest, Number(match[1]));
  }
  if (greatest > 0) return `block-${greatest + 1}`;
  let number = 1;
  while (used.has(`block-${number}`)) number += 1;
  return `block-${number}`;
}
