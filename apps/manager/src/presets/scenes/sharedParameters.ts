/**
 * Parameters a scene cannot own. IR reverb time sets how the pedal prepares the kernel,
 * which never changes on the audio thread, so the pedal ignores scene targets for it.
 */
const SHARED_ONLY: Readonly<Record<string, readonly string[]>> = {
  irreverb: ["reverbTimeRatio"],
};

export function isSharedOnlyParameter(blockType: string, parameter: string): boolean {
  return SHARED_ONLY[blockType]?.includes(parameter) ?? false;
}

/** Help text shown under a control, or undefined when the control has none. */
export function parameterHelp(blockType: string, parameter: string, scenesEnabled: boolean): string | undefined {
  if (blockType !== "irreverb" || parameter !== "reverbTimeRatio") return undefined;
  return "25–100% of the original decay. Short or non-decaying IRs keep their original response."
    + (scenesEnabled ? " Shared across scenes." : "");
}
