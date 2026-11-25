// These controls require shared preparation or lack a live scene transition.
const SHARED_ONLY: Readonly<Record<string, readonly string[]>> = {
  delay: ["time"],
  irreverb: ["reverbTimeRatio"],
};

// Keep bypass eligibility aligned with bypassCapabilityFor in ScenePlan.cpp.
const sceneBypassTypes = new Set(["mod", "delay", "reverb", "irreverb", "stereo", "dynamics", "distortion", "wah", "eq"]);

export function sceneCanOwnBlockEnabled(blockType: string): boolean {
  return sceneBypassTypes.has(blockType);
}

export function isSharedOnlyParameter(blockType: string, parameter: string, mode?: unknown): boolean {
  return blockType === "dualAmp" || blockType === "dualRig"
    || (blockType === "mod" && mode === "pog3" && parameter === "expression_mode")
    || (SHARED_ONLY[blockType]?.includes(parameter) ?? false);
}

/** Help text shown under a control, or undefined when the control has none. */
export function parameterHelp(blockType: string, parameter: string, scenesEnabled: boolean): string | undefined {
  if (blockType !== "irreverb" || parameter !== "reverbTimeRatio") return undefined;
  return "25–100% of the original decay. Short or non-decaying IRs keep their original response."
    + (scenesEnabled ? " Shared across scenes." : "");
}
