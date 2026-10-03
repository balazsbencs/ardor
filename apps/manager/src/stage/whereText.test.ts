import { describe, expect, it } from "vitest";

import type { Preset } from "../api/types";
import { blockOf } from "../test/blocks";
import { whereText } from "./whereText";

const base: Preset = {
  version: 1, name: "Where", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [blockOf("delay:tape", "d1")],
};

describe("whereText", () => {
  it("names the start and the block before the insert point", () => {
    expect(whereText({ kind: "top", index: 0 }, base)).toBe("Insert at the start");
    expect(whereText({ kind: "top", index: 1 }, base)).toBe("Insert after Tape Delay, position 2");
    expect(whereText(undefined, base)).toBe("");
  });

  it("names Dual Rig and WDW lanes", () => {
    const rig = blockOf("dualRig", "r1");
    const withRig: Preset = { ...base, blocks: [rig] };
    expect(whereText({ kind: "lane", rigId: "r1", lane: "left", index: 0 }, withRig)).toBe("Insert at the start in lane A");
    expect(whereText({ kind: "lane", rigId: "r1", lane: "right", index: 1 }, withRig)).toBe("Insert after NAM Model, position 2 in lane B");
    const wdw: Preset = {
      ...base, routing: "wdw", blocks: [],
      wdw: { dry: { enabled: true, levelDb: 0, blocks: [] }, wet: { enabled: true, levelDb: 0, blocks: [blockOf("delay:tape", "w1")] } },
    } as Preset;
    expect(whereText({ kind: "wdw", lane: "dry", index: 0 }, wdw)).toBe("Insert at the start on the dry lane");
    expect(whereText({ kind: "wdw", lane: "wet", index: 1 }, wdw)).toBe("Insert after Tape Delay, position 2 on the wet lane");
  });
});
