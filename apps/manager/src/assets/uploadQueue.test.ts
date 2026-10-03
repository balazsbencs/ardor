import { describe, expect, it } from "vitest";

import { nextUpload, routeKind, uploadQueue } from "./uploadQueue";

const file = (name: string) => new File(["x"], name);
const existing = { models: ["Brown Sound.nam"], irs: [], "reverb-irs": [] };

describe("uploadQueue", () => {
  it("routes by extension and the open tab", () => {
    expect(routeKind("A.NAM", "irs")).toBe("models");
    expect(routeKind("room.wav", "models")).toBe("irs");
    expect(routeKind("room.wav", "reverb-irs")).toBe("reverb-irs");
    expect(routeKind("notes.txt", "models")).toBeUndefined();
  });

  it("holds a name conflict and rejects other file types", () => {
    const state = uploadQueue([], { type: "enqueue", files: [file("Brown Sound.nam"), file("New.nam"), file("notes.txt")], openKind: "models", existing });
    expect(state.map(({ file: f, state: s }) => [f.name, s])).toEqual([["Brown Sound.nam", "conflict"], ["New.nam", "waiting"], ["notes.txt", "rejected"]]);
    expect(nextUpload(state)?.file.name).toBe("New.nam");
  });

  it("uploads a conflict only after Replace, and drops it on Skip", () => {
    const [conflict] = uploadQueue([], { type: "enqueue", files: [file("Brown Sound.nam")], openKind: "models", existing });
    const replaced = uploadQueue([conflict], { type: "resolve", id: conflict.id, choice: "replace" });
    expect(replaced[0]).toMatchObject({ state: "waiting", replace: true });
    expect(uploadQueue([conflict], { type: "resolve", id: conflict.id, choice: "skip" })).toEqual([]);
  });

  it("removes an item when it is done", () => {
    const state = uploadQueue([], { type: "enqueue", files: [file("New.nam")], openKind: "models", existing });
    const started = uploadQueue(state, { type: "start", id: state[0].id });
    expect(started[0].state).toBe("uploading");
    expect(uploadQueue(started, { type: "done", id: state[0].id })).toEqual([]);
  });

  it("shows a failed upload as rejected until it is dismissed", () => {
    const state = uploadQueue([], { type: "enqueue", files: [file("New.nam")], openKind: "models", existing });
    const failed = uploadQueue(state, { type: "failed", id: state[0].id });
    expect(failed[0].state).toBe("rejected");
    expect(uploadQueue(failed, { type: "dismiss", id: state[0].id })).toEqual([]);
  });
});
