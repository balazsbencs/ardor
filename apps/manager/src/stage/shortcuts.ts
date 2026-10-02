import type { Preset } from "../api/types";
import type { AddTarget } from "../presets/editor/usePresetEditor";

export type Shortcut = "undo" | "redo" | "save" | "toggle" | "remove" | "add" | "close";

type KeyInput = Pick<KeyboardEvent, "key" | "metaKey" | "ctrlKey" | "shiftKey" | "altKey"> & { target: EventTarget | null };

const TEXT_FIELDS = "input, select, textarea, [contenteditable=''], [contenteditable='true']";
const PLAIN_KEYS: Record<string, Shortcut> = {
  b: "toggle", B: "toggle", a: "add", A: "add", Delete: "remove", Backspace: "remove", Escape: "close",
};

const isSave = (event: KeyInput) =>
  (event.metaKey || event.ctrlKey) && !event.altKey && !event.shiftKey && event.key.toLowerCase() === "s";

/**
 * The edit screen's key map: Cmd/Ctrl+Z, Shift+Cmd/Ctrl+Z, Cmd/Ctrl+S, B, Delete, A and Escape.
 * Cmd/Ctrl+S always saves, so the browser never opens its own Save dialog. Otherwise text fields
 * and open dialogs keep their keys, and a focused slider keeps all but undo, redo and Escape.
 */
export function shortcutFor(event: KeyInput): Shortcut | undefined {
  if (isSave(event)) return "save";
  const element = event.target instanceof Element ? event.target : undefined;
  if (element?.closest(`${TEXT_FIELDS}, [role=dialog], [role=alertdialog]`)) return undefined;
  if (event.metaKey || event.ctrlKey) {
    return !event.altKey && event.key.toLowerCase() === "z" ? (event.shiftKey ? "redo" : "undo") : undefined;
  }
  if (event.altKey) return undefined;
  if (element?.closest("[role=slider]")) return event.key === "Escape" ? "close" : undefined;
  return PLAIN_KEYS[event.key];
}

/** The insert point right after a block, or the end of the chain (the wet lane for WDW). */
export function addTargetAfter(preset: Preset, blockId?: string): AddTarget {
  if (preset.routing === "wdw" && preset.wdw) {
    for (const lane of ["dry", "wet"] as const) {
      const index = preset.wdw[lane].blocks.findIndex(({ id }) => id === blockId);
      if (index >= 0) return { kind: "wdw", lane, index: index + 1 };
    }
    return { kind: "wdw", lane: "wet", index: preset.wdw.wet.blocks.length };
  }
  const top = preset.blocks.findIndex(({ id }) => id === blockId);
  if (top >= 0) return { kind: "top", index: top + 1 };
  for (const rig of preset.blocks) {
    for (const lane of ["left", "right"] as const) {
      const index = rig.lanes?.[lane].blocks.findIndex(({ id }) => id === blockId) ?? -1;
      if (index >= 0) return { kind: "lane", rigId: rig.id, lane, index: index + 1 };
    }
  }
  return { kind: "top", index: preset.blocks.length };
}
