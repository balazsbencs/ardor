import type { Preset, PresetBlock, PresetSceneSet, PresetSceneTarget, WdwRouting } from "../../api/types";
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

export function createSceneSet(targets: PresetSceneTarget[] = []): PresetSceneSet {
  const scenes = [0, 1, 2, 3].map((index) => ({
    id: `scene-${index + 1}`,
    name: `Scene ${index + 1}`,
    enterTimeMs: 0,
    outputTrimDb: 0,
    targets: structuredClone(targets),
  })) as PresetSceneSet["scenes"];
  return { defaultSceneId: scenes[0].id, openIn: "scenes", scenes };
}

// Target eligibility belongs to the runtime capability registry. This helper
// only performs the versioned document conversion with a caller-supplied,
// already-qualified target set.
export function enableScenes(preset: Preset, targets: PresetSceneTarget[] = []): Preset {
  const next = clonePreset(preset);
  next.version = 4;
  next.sceneSet = createSceneSet(targets);
  return next;
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
