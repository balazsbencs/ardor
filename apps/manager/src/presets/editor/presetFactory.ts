import type { Preset, PresetBlock, WdwRouting } from "../../api/types";
import { createBlockFromDefinition } from "../../effects/catalog";

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

export function createEmptyWdwRouting(): WdwRouting {
  const dryNam = createBlockFromDefinition("nam", []);
  const dryCab = createBlockFromDefinition("cab", [dryNam]);
  const wetNam = createBlockFromDefinition("nam", [dryNam, dryCab]);
  const wetCab = createBlockFromDefinition("cab", [dryNam, dryCab, wetNam]);
  return {
    dry: { blocks: [dryNam, dryCab], levelDb: 0, pan: 0, enabled: true },
    wet: { blocks: [wetNam, wetCab], levelDb: 0, width: 1, enabled: true },
  };
}

export function createWdwPreset(name = "New WDW Preset"): Preset {
  return {
    version: 3,
    name,
    routing: "wdw",
    global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
    blocks: [],
    wdw: createEmptyWdwRouting(),
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
