import type { EditorAction } from "../presets/editor/editorTypes";

export type ListId = "top" | `lane:${string}:left` | `lane:${string}:right` | "wdw:dry" | "wdw:wet";
export type DropPoint = { listId: ListId; index: number };

function laneOf(listId: ListId): { rigId: string; lane: "left" | "right" } | undefined {
  const match = /^lane:(.+):(left|right)$/.exec(listId);
  return match ? { rigId: match[1], lane: match[2] as "left" | "right" } : undefined;
}

/** Maps a drag from one list position to another to the reducer move, or undefined when it is a no-op or not supported. */
export function resolveDrop(from: DropPoint, to: DropPoint, blockId: string): EditorAction | undefined {
  if (from.listId === to.listId && from.index === to.index) return undefined;
  if (from.listId === "top" && to.listId === "top") return { type: "move-block", blockId, index: to.index };
  const fromLane = laneOf(from.listId);
  const toLane = laneOf(to.listId);
  if (fromLane && toLane) {
    return fromLane.rigId === toLane.rigId ? { type: "move-lane-block", rigId: toLane.rigId, blockId, lane: toLane.lane, index: to.index } : undefined;
  }
  if (from.listId.startsWith("wdw:") && to.listId.startsWith("wdw:")) {
    return { type: "move-wdw-block", lane: to.listId === "wdw:dry" ? "dry" : "wet", blockId, index: to.index };
  }
  return undefined;
}
