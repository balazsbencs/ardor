import { fireEvent, render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { blockOf } from "../test/blocks";
import { ChainStage } from "./ChainStage";

const blocks = [blockOf("dynamics:compressor", "b1"), blockOf("delay:tape", "b2")];
const base = {
  issuesFor: () => [], missingFile: () => false, sceneOwnsEnabled: () => false, maxed: false,
  onSelect: vi.fn(), onToggle: vi.fn(), onAdd: vi.fn(), onMove: vi.fn(),
};

describe("ChainStage", () => {
  it("draws IN, the cards in order, insert points and OUT", () => {
    render(<ChainStage blocks={blocks} {...base} />);
    const stage = screen.getByRole("region", { name: "Signal chain" });
    expect(within(stage).getByText("IN")).toBeInTheDocument();
    expect(within(stage).getByText("OUT")).toBeInTheDocument();
    expect(within(stage).getAllByRole("group").map((el) => el.getAttribute("aria-label")?.split(",")[0])).toEqual(["Compressor", "Tape Delay"]);
    expect(within(stage).getAllByRole("button", { name: /Add a block at position/ })).toHaveLength(3);
  });

  it("inserts at the chosen point", async () => {
    const onAdd = vi.fn();
    render(<ChainStage blocks={blocks} {...base} onAdd={onAdd} />);
    await userEvent.click(screen.getByRole("button", { name: "Add a block at position 2" }));
    expect(onAdd).toHaveBeenCalledWith({ kind: "top", index: 1 });
  });

  it("moves a card with Alt and the arrow keys", () => {
    const onMove = vi.fn();
    render(<ChainStage blocks={blocks} {...base} onMove={onMove} />);
    screen.getByRole("group", { name: /Compressor/ }).focus();
    fireEvent.keyDown(document.activeElement!, { key: "ArrowRight", altKey: true });
    expect(onMove).toHaveBeenCalledWith({ type: "move-block", blockId: "b1", index: 1 });
  });

  it("draws Dual Rig lanes with their own insert points", () => {
    const rig = { ...blockOf("dualRig", "r1"), lanes: { left: { blocks: [blockOf("mod:chorus", "c1")] }, right: { blocks: [] } } };
    render(<ChainStage blocks={[rig]} {...base} />);
    expect(screen.getByText("SPLIT")).toBeInTheDocument();
    expect(screen.getByText("JOIN")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Add a block to lane B" })).toBeInTheDocument();
  });

  it("disables the insert points when the chain is full", () => {
    render(<ChainStage blocks={blocks} {...base} maxed />);
    for (const button of screen.getAllByRole("button", { name: /Add a block at position/ })) expect(button).toBeDisabled();
  });

  it("disables a Dual Rig lane's insert points when that lane holds 10 blocks", () => {
    const full = Array.from({ length: 10 }, (_, i) => blockOf("mod:chorus", `c${i}`));
    const rig = { ...blockOf("dualRig", "r1"), lanes: { left: { blocks: full }, right: { blocks: [] } } };
    render(<ChainStage blocks={[rig]} {...base} maxed={false} />);
    const laneA = screen.getAllByRole("button", { name: /Add a block to lane A/ });
    expect(laneA).toHaveLength(11);
    for (const button of laneA) expect(button).toBeDisabled();
    expect(screen.getByRole("button", { name: "Add a block to lane B" })).toBeEnabled();
  });
});
