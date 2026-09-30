import { fireEvent, render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { blockOf } from "../test/blocks";
import { ChipStrip } from "./ChipStrip";

describe("ChipStrip", () => {
  it("lists chips in chain order, selects one, and moves it with Alt and an arrow", async () => {
    const onSelect = vi.fn();
    const onMove = vi.fn();
    const blocks = [blockOf("dynamics:compressor", "b1"), blockOf("delay:tape", "b2")];
    render(<ChipStrip blocks={blocks} selectedId="b2" onSelect={onSelect} onMove={onMove} onAdd={vi.fn()} />);
    expect(screen.getByRole("button", { name: /Tape Delay/ })).toHaveAttribute("aria-current", "true");
    await userEvent.click(screen.getByRole("button", { name: /Compressor/ }));
    expect(onSelect).toHaveBeenCalledWith("b1");
    fireEvent.keyDown(screen.getByRole("button", { name: /Compressor/ }), { key: "ArrowRight", altKey: true });
    expect(onMove).toHaveBeenCalledWith({ type: "move-block", blockId: "b1", index: 1 });
  });

  it("adds at the end and marks a switched off block", async () => {
    const onAdd = vi.fn();
    const off = { ...blockOf("delay:tape", "b2"), enabled: false };
    render(<ChipStrip blocks={[off]} onSelect={vi.fn()} onMove={vi.fn()} onAdd={onAdd} />);
    expect(screen.getByRole("button", { name: /Tape Delay/ })).toHaveTextContent("off");
    await userEvent.click(screen.getByRole("button", { name: "Add a block at the end" }));
    expect(onAdd).toHaveBeenCalled();
  });

  it("shows the WDW dry and wet lanes and moves a chip within its lane with Alt and an arrow", () => {
    const onMove = vi.fn();
    const wdw = {
      dry: { enabled: true, levelDb: 0, blocks: [blockOf("dynamics:compressor", "x1")] },
      wet: { enabled: true, levelDb: 0, blocks: [blockOf("delay:tape", "w1"), blockOf("mod:chorus", "w2")] },
    };
    render(<ChipStrip blocks={[]} wdw={wdw} onSelect={vi.fn()} onMove={onMove} onAdd={vi.fn()} />);
    expect(screen.getByText("DRY")).toBeInTheDocument();
    expect(screen.getByText("WET")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /Compressor/ })).toBeInTheDocument();
    fireEvent.keyDown(screen.getByRole("button", { name: /Tape Delay/ }), { key: "ArrowRight", altKey: true });
    expect(onMove).toHaveBeenCalledWith({ type: "move-wdw-block", lane: "wet", blockId: "w1", index: 1 });
  });
});
