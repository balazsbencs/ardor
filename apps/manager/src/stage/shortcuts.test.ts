import { describe, expect, it } from "vitest";

import type { Preset } from "../api/types";
import { blockOf } from "../test/blocks";
import { addTargetAfter, shortcutFor } from "./shortcuts";

const key = (init: Partial<KeyboardEvent> & { target?: EventTarget | null }) =>
  ({ key: "", metaKey: false, ctrlKey: false, shiftKey: false, altKey: false, target: document.body, ...init });

describe("shortcutFor", () => {
  it("maps the edit keys", () => {
    expect(shortcutFor(key({ key: "z", metaKey: true }))).toBe("undo");
    expect(shortcutFor(key({ key: "Z", ctrlKey: true, shiftKey: true }))).toBe("redo");
    expect(shortcutFor(key({ key: "s", metaKey: true }))).toBe("save");
    expect(shortcutFor(key({ key: "b" }))).toBe("toggle");
    expect(shortcutFor(key({ key: "Backspace" }))).toBe("remove");
    expect(shortcutFor(key({ key: "A" }))).toBe("add");
    expect(shortcutFor(key({ key: "Escape" }))).toBe("close");
    expect(shortcutFor(key({ key: "b", altKey: true }))).toBeUndefined();
  });

  it("leaves text fields and dialogs alone and lets a slider keep all but undo, redo and save", () => {
    const input = document.createElement("input");
    const slider = document.createElement("div");
    slider.setAttribute("role", "slider");
    const dialog = document.createElement("div");
    dialog.setAttribute("role", "dialog");
    const inside = dialog.appendChild(document.createElement("button"));
    expect(shortcutFor(key({ key: "z", metaKey: true, target: input }))).toBeUndefined();
    expect(shortcutFor(key({ key: "s", metaKey: true, target: input }))).toBe("save");
    expect(shortcutFor(key({ key: "s", ctrlKey: true, target: inside }))).toBe("save");
    expect(shortcutFor(key({ key: "Escape", target: inside }))).toBeUndefined();
    expect(shortcutFor(key({ key: "b", target: slider }))).toBeUndefined();
    expect(shortcutFor(key({ key: "s", metaKey: true, target: slider }))).toBe("save");
  });
});

describe("shortcutFor on a focused slider", () => {
  it("closes the drawer with Escape, while text fields keep their own Escape", () => {
    const slider = document.createElement("div");
    slider.setAttribute("role", "slider");
    expect(shortcutFor(key({ key: "Escape", target: slider }))).toBe("close");
    expect(shortcutFor(key({ key: "Escape", target: document.createElement("input") }))).toBeUndefined();
    expect(shortcutFor(key({ key: "Delete", target: slider }))).toBeUndefined();
  });
});

describe("addTargetAfter", () => {
  const global = { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 };

  it("inserts after a top-level or lane block, else at the end", () => {
    const rig = blockOf("dualRig", "r1");
    const preset: Preset = { version: 2, name: "R", routing: "serial", global, blocks: [blockOf("delay:tape", "d1"), rig] };
    expect(addTargetAfter(preset, "d1")).toEqual({ kind: "top", index: 1 });
    expect(addTargetAfter(preset, rig.lanes!.right.blocks[0].id)).toEqual({ kind: "lane", rigId: "r1", lane: "right", index: 1 });
    expect(addTargetAfter(preset)).toEqual({ kind: "top", index: 2 });
  });

  it("uses the WDW lanes, ending on the wet lane", () => {
    const preset = {
      version: 3, name: "W", routing: "wdw", global, blocks: [],
      wdw: { dry: { enabled: true, levelDb: 0, blocks: [blockOf("delay:tape", "x1")] }, wet: { enabled: true, levelDb: 0, blocks: [] } },
    } as unknown as Preset;
    expect(addTargetAfter(preset, "x1")).toEqual({ kind: "wdw", lane: "dry", index: 1 });
    expect(addTargetAfter(preset)).toEqual({ kind: "wdw", lane: "wet", index: 0 });
  });
});
