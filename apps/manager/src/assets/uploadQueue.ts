import type { AssetKind } from "../api/types";

export type QueueItem = { id: number; file: File; kind: AssetKind; state: "waiting" | "uploading" | "conflict" | "rejected"; replace: boolean };
export type QueueAction =
  | { type: "enqueue"; files: File[]; openKind: AssetKind; existing: Record<AssetKind, string[]>; replace?: boolean }
  | { type: "start"; id: number }
  | { type: "resolve"; id: number; choice: "replace" | "skip" }
  | { type: "done"; id: number }
  | { type: "failed"; id: number }
  | { type: "dismiss"; id: number };

let nextId = 0;

/** .nam goes to models; .wav goes to the open IR tab, else cabinet IRs. */
export function routeKind(filename: string, openKind: AssetKind): AssetKind | undefined {
  const name = filename.toLowerCase();
  if (name.endsWith(".nam")) return "models";
  if (name.endsWith(".wav")) return openKind === "reverb-irs" ? "reverb-irs" : "irs";
  return undefined;
}

export function uploadQueue(state: QueueItem[], action: QueueAction): QueueItem[] {
  switch (action.type) {
    case "enqueue":
      return [...state, ...action.files.map((file): QueueItem => {
        const kind = routeKind(file.name, action.openKind);
        const exists = kind !== undefined && action.existing[kind].some((name) => name.toLowerCase() === file.name.toLowerCase());
        return { id: ++nextId, file, kind: kind ?? action.openKind, state: !kind ? "rejected" : exists && !action.replace ? "conflict" : "waiting", replace: action.replace === true };
      })];
    case "start":
      return state.map((item) => (item.id === action.id ? { ...item, state: "uploading" } : item));
    case "resolve":
      return action.choice === "skip"
        ? state.filter((item) => item.id !== action.id)
        : state.map((item) => (item.id === action.id ? { ...item, state: "waiting", replace: true } : item));
    case "done":
    case "dismiss":
      return state.filter((item) => item.id !== action.id);
    case "failed":
      return state.map((item) => (item.id === action.id ? { ...item, state: "rejected" } : item));
  }
}

export const nextUpload = (state: QueueItem[]): QueueItem | undefined => state.find((item) => item.state === "waiting");
