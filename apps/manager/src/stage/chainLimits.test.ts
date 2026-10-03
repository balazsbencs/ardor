import { describe, expect, it } from "vitest";

import type { Preset } from "../api/types";
import { blockOf } from "../test/blocks";
import { addBlockedReason, CHAIN_FULL, LANE_FULL, WDW_FULL } from "./chainLimits";

const global = { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 };
const chorus = (count: number, prefix: string) => Array.from({ length: count }, (_, i) => blockOf("mod:chorus", `${prefix}${i}`));

describe("addBlockedReason", () => {
  it("blocks the serial chain at 10 top-level blocks", () => {
    const preset: Preset = { version: 1, name: "S", routing: "serial", global, blocks: chorus(9, "m") };
    expect(addBlockedReason(preset, { kind: "top", index: 9 })).toBeUndefined();
    expect(addBlockedReason({ ...preset, blocks: chorus(10, "m") }, { kind: "top", index: 10 })).toBe(CHAIN_FULL);
  });

  it("blocks a full Dual Rig lane", () => {
    const rig = blockOf("dualRig", "r1");
    const full = { ...rig, lanes: { left: { blocks: chorus(10, "l") }, right: rig.lanes!.right } };
    const preset: Preset = { version: 2, name: "R", routing: "serial", global, blocks: [full] };
    expect(addBlockedReason(preset, { kind: "lane", rigId: "r1", lane: "left", index: 0 })).toBe(LANE_FULL);
    expect(addBlockedReason(preset, { kind: "lane", rigId: "r1", lane: "right", index: 0 })).toBeUndefined();
  });

  it("blocks a WDW lane at 10 blocks and the WDW chain at 20", () => {
    const wdw = (dry: number, wet: number) => ({
      version: 3, name: "W", routing: "wdw", global, blocks: [],
      wdw: { dry: { enabled: true, levelDb: 0, blocks: chorus(dry, "d") }, wet: { enabled: true, levelDb: 0, blocks: chorus(wet, "w") } },
    }) as Preset;
    expect(addBlockedReason(wdw(10, 2), { kind: "wdw", lane: "dry", index: 0 })).toBe(LANE_FULL);
    expect(addBlockedReason(wdw(10, 2), { kind: "wdw", lane: "wet", index: 0 })).toBeUndefined();
    expect(addBlockedReason(wdw(10, 10), { kind: "wdw", lane: "wet", index: 0 })).toBe(WDW_FULL);
  });
});
