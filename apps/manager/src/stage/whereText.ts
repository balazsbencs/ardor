import type { Preset, PresetBlock } from "../api/types";
import { findPresetBlockInPreset } from "../presets/editor/editorReducer";
import type { AddTarget } from "../presets/editor/usePresetEditor";
import { blockTitle } from "./BlockCard";

function listFor(target: AddTarget, preset: Preset): PresetBlock[] {
  if (target.kind === "top") return preset.blocks;
  if (target.kind === "wdw") return preset.wdw?.[target.lane].blocks ?? [];
  return findPresetBlockInPreset(preset, target.rigId)?.lanes?.[target.lane].blocks ?? [];
}

function laneSuffix(target: AddTarget): string {
  if (target.kind === "lane") return target.lane === "left" ? " in lane A" : " in lane B";
  if (target.kind === "wdw") return target.lane === "dry" ? " on the dry lane" : " on the wet lane";
  return "";
}

/** Where the module drawer will insert, in words: "Insert after Tape Delay, position 2". */
export function whereText(target: AddTarget | undefined, preset: Preset): string {
  if (!target) return "";
  const previous = target.index > 0 ? listFor(target, preset)[target.index - 1] : undefined;
  const base = previous ? `Insert after ${blockTitle(previous)}, position ${target.index + 1}` : "Insert at the start";
  return `${base}${laneSuffix(target)}`;
}
