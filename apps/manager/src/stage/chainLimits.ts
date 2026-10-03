import type { Preset } from "../api/types";
import { allPresetBlocksInPreset, findPresetBlockInPreset } from "../presets/editor/editorReducer";
import type { AddTarget } from "../presets/editor/usePresetEditor";

/** Block limits as before the redesign (ruling R4); the reducer enforces the same caps. */
export const MAX_SERIAL_BLOCKS = 10;
export const MAX_WDW_BLOCKS = 20;
export const MAX_LANE_BLOCKS = 10;

export const CHAIN_FULL = `The chain holds ${MAX_SERIAL_BLOCKS} blocks.`;
export const WDW_FULL = `The chain holds ${MAX_WDW_BLOCKS} blocks.`;
export const LANE_FULL = `The lane holds ${MAX_LANE_BLOCKS} blocks.`;

/** Why a block cannot go to this insert point, or undefined when it can. */
export function addBlockedReason(preset: Preset, target: AddTarget): string | undefined {
  if (target.kind === "wdw") {
    if (allPresetBlocksInPreset(preset).length >= MAX_WDW_BLOCKS) return WDW_FULL;
    return (preset.wdw?.[target.lane].blocks.length ?? 0) >= MAX_LANE_BLOCKS ? LANE_FULL : undefined;
  }
  if (target.kind === "lane") {
    const lane = findPresetBlockInPreset(preset, target.rigId)?.lanes?.[target.lane];
    return (lane?.blocks.length ?? 0) >= MAX_LANE_BLOCKS ? LANE_FULL : undefined;
  }
  return preset.blocks.length >= MAX_SERIAL_BLOCKS ? CHAIN_FULL : undefined;
}
