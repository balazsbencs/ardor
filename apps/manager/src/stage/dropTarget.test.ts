import { describe, expect, it } from "vitest";

import { resolveDrop } from "./dropTarget";

describe("resolveDrop", () => {
  it("moves inside the top chain", () => {
    expect(resolveDrop({ listId: "top", index: 1 }, { listId: "top", index: 4 }, "b2"))
      .toEqual({ type: "move-block", blockId: "b2", index: 4 });
  });

  it("moves inside or across the lanes of one Dual Rig", () => {
    expect(resolveDrop({ listId: "lane:r1:left", index: 0 }, { listId: "lane:r1:right", index: 2 }, "c1"))
      .toEqual({ type: "move-lane-block", rigId: "r1", blockId: "c1", lane: "right", index: 2 });
  });

  it("moves between WDW lanes", () => {
    expect(resolveDrop({ listId: "wdw:dry", index: 0 }, { listId: "wdw:wet", index: 1 }, "n1"))
      .toEqual({ type: "move-wdw-block", lane: "wet", blockId: "n1", index: 1 });
  });

  it("moves to the tail of the same list and from wet to dry", () => {
    expect(resolveDrop({ listId: "top", index: 0 }, { listId: "top", index: 3 }, "b1"))
      .toEqual({ type: "move-block", blockId: "b1", index: 3 });
    expect(resolveDrop({ listId: "wdw:wet", index: 1 }, { listId: "wdw:dry", index: 0 }, "n1"))
      .toEqual({ type: "move-wdw-block", lane: "dry", blockId: "n1", index: 0 });
  });

  it("refuses moves the reducer cannot do, and no-op drops", () => {
    expect(resolveDrop({ listId: "top", index: 0 }, { listId: "lane:r1:left", index: 0 }, "b1")).toBeUndefined();
    expect(resolveDrop({ listId: "lane:r1:left", index: 0 }, { listId: "top", index: 0 }, "c1")).toBeUndefined();
    expect(resolveDrop({ listId: "lane:r1:left", index: 0 }, { listId: "lane:r2:left", index: 0 }, "c1")).toBeUndefined();
    expect(resolveDrop({ listId: "top", index: 2 }, { listId: "top", index: 2 }, "b1")).toBeUndefined();
  });
});
