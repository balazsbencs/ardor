import sceneParameters from "../../../../../services/managerd/internal/presets/scene-parameters.json";

const ranges: Readonly<Record<string, Record<string, Record<string, number[]>>>> = sceneParameters;

export function sceneParameterRange(blockType: string, parameter: string, mode?: unknown): number[] | undefined {
  const modeIndependent = ["cab", "irreverb", "stereo"].includes(blockType);
  return ranges[blockType]?.[modeIndependent || typeof mode !== "string" ? "" : mode]?.[parameter];
}

// Keep bypass eligibility aligned with bypassCapabilityFor in ScenePlan.cpp.
const sceneBypassTypes = new Set(["mod", "delay", "reverb", "irreverb", "stereo", "dynamics", "distortion", "wah", "eq"]);

export function sceneCanOwnBlockEnabled(blockType: string): boolean {
  return sceneBypassTypes.has(blockType);
}

export function isSharedOnlyParameter(blockType: string, parameter: string, mode?: unknown): boolean {
  return !sceneParameterRange(blockType, parameter, mode);
}

/** Help text shown under a control, or undefined when the control has none. */
export function parameterHelp(blockType: string, parameter: string, scenesEnabled: boolean): string | undefined {
  if (blockType !== "irreverb" || parameter !== "reverbTimeRatio") return undefined;
  return "25–100% of the original decay. Short or non-decaying IRs keep their original response."
    + (scenesEnabled ? " Shared across scenes." : "");
}
