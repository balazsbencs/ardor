import type { PresetBlock } from "../../api/types";

export type WdwLaneName = "dry" | "wet";

const dryBlockTypes = new Set(["nam", "cab", "dynamics", "eq", "distortion", "wah"]);
const wetBlockTypes = new Set(["nam", "cab", "mod", "delay", "reverb", "irreverb", "stereo"]);

/** The fixed WDW topology admits drive/amp work on Dry and stereo time work on Wet. */
export function isWdwBlockAllowed(lane: WdwLaneName, blockOrType: PresetBlock | string): boolean {
  const type = typeof blockOrType === "string" ? blockOrType : blockOrType.type;
  return (lane === "dry" ? dryBlockTypes : wetBlockTypes).has(type);
}

export function wdwLaneLabel(lane: WdwLaneName): string {
  return lane === "dry" ? "Dry" : "Wet";
}
