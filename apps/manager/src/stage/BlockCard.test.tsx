import { fireEvent, render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { blockOf } from "../test/blocks";
import { BlockCard } from "./BlockCard";

const delay = blockOf("delay:tape", "d1");
const props = { selected: false, issues: [], missingFile: false, sceneOwnsEnabled: false, onSelect: vi.fn(), onToggle: vi.fn(), onNudge: vi.fn() };

describe("BlockCard", () => {
  it("shows the device cap, name and two main values", () => {
    render(<BlockCard block={delay} {...props} />);
    expect(screen.getByText("Delay")).toBeInTheDocument();
    expect(screen.getByText("Tape Delay")).toBeInTheDocument();
    expect(screen.getByText("Time")).toBeInTheDocument();
    expect(screen.getByText("Repeats")).toBeInTheDocument();
  });

  it("shows OFF and hides values when bypassed", () => {
    render(<BlockCard block={{ ...delay, enabled: false }} {...props} />);
    expect(screen.getByText("OFF")).toBeInTheDocument();
    expect(screen.queryByText("Time")).not.toBeInTheDocument();
  });

  it("selects on click and toggles with the power button without selecting", async () => {
    const onSelect = vi.fn();
    const onToggle = vi.fn();
    render(<BlockCard block={delay} {...props} onSelect={onSelect} onToggle={onToggle} />);
    await userEvent.click(screen.getByRole("button", { name: "Bypass Tape Delay" }));
    expect(onToggle).toHaveBeenCalled();
    expect(onSelect).not.toHaveBeenCalled();
    await userEvent.click(screen.getByText("Tape Delay"));
    expect(onSelect).toHaveBeenCalled();
  });

  it("moves with Alt and the arrow keys", () => {
    const onNudge = vi.fn();
    render(<BlockCard block={delay} {...props} onNudge={onNudge} />);
    fireEvent.keyDown(screen.getByRole("group", { name: /Tape Delay/ }), { key: "ArrowRight", altKey: true });
    expect(onNudge).toHaveBeenCalledWith(1);
  });

  it("warns about a missing file and a validation error", () => {
    const nam = { ...blockOf("nam", "n1"), asset: "models/Fuzz Stack.nam" };
    render(<BlockCard block={nam} {...props} missingFile issues={[{ severity: "error", code: "x", message: "Pick a model." }]} />);
    expect(screen.getByText("FILE MISSING")).toBeInTheDocument();
    expect(screen.getByText("FIX")).toHaveAttribute("title", "Pick a model.");
  });

  it("shows Source and Nano on a NAM card, like the mockup", () => {
    const nam = { ...blockOf("nam", "n1"), asset: "models/Plexi.nam", params: { inputMode: "left", useNano: true } };
    render(<BlockCard block={nam} {...props} />);
    expect(screen.getByText("Source").parentElement).toHaveTextContent("Source Left / Mono");
    expect(screen.getByText("Nano").parentElement).toHaveTextContent("Nano On");
  });

  it("falls back to the NAM defaults", () => {
    render(<BlockCard block={{ ...blockOf("nam", "n1"), params: {} }} {...props} />);
    expect(screen.getByText("Source").parentElement).toHaveTextContent("Source L+R Average");
    expect(screen.getByText("Nano").parentElement).toHaveTextContent("Nano Off");
  });
});
