import { fireEvent, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { createBlockFromDefinition } from "../../effects/catalog";
import { renderWithProviders } from "../../test/render";
import { BlockInspector } from "./BlockInspector";

describe("BlockInspector", () => {
  it("offers the nano model as an opt-in switch", async () => {
    const user = userEvent.setup();
    const block = createBlockFromDefinition("nam", [], "models/amp.nam");
    const onParam = vi.fn();
    renderWithProviders(<BlockInspector block={block} issues={[]} models={[]} irs={[]} onToggle={() => undefined} onParam={onParam} onAsset={() => undefined} onMode={() => undefined} onEqBand={() => undefined} onReset={() => undefined} onDuplicate={() => undefined} onDelete={() => undefined} onAssets={() => undefined} />);

    const nanoSwitch = screen.getByRole("checkbox", { name: "Use nano model" });
    expect(nanoSwitch).not.toBeChecked();
    await user.click(nanoSwitch);
    expect(onParam).toHaveBeenCalledWith(block.id, "useNano", true);
  });

  it("renders the complete compressor control surface", async () => {
    const user = userEvent.setup();
    const block = createBlockFromDefinition("dynamics:compressor", []);
    const onParam = vi.fn();
    renderWithProviders(<BlockInspector block={block} issues={[]} models={[]} irs={[]} onToggle={() => undefined} onParam={onParam} onAsset={() => undefined} onMode={() => undefined} onEqBand={() => undefined} onReset={() => undefined} onDuplicate={() => undefined} onDelete={() => undefined} onAssets={() => undefined} />);

    for (const label of ["Threshold", "Ratio", "Attack", "Release", "Knee", "Makeup", "Input", "Mix", "Sidechain HPF"]) {
      expect(screen.getByRole("slider", { name: label })).toBeInTheDocument();
    }
    expect(screen.getByRole("combobox", { name: "Detector" })).toBeInTheDocument();

    await user.selectOptions(screen.getByRole("combobox", { name: "Detector" }), "rms");
    expect(onParam).toHaveBeenCalledWith(block.id, "detector", "rms");
  });

  it("shows Daisy values in physical units and discrete selectors", () => {
    const block = createBlockFromDefinition("delay:tape", []);
    renderWithProviders(<BlockInspector block={block} issues={[]} models={[]} irs={[]} onToggle={() => undefined} onParam={() => undefined} onAsset={() => undefined} onMode={() => undefined} onEqBand={() => undefined} onReset={() => undefined} onDuplicate={() => undefined} onDelete={() => undefined} onAssets={() => undefined} />);

    expect(screen.getByText("98.1 ms")).toBeInTheDocument();
    expect(screen.getByRole("slider", { name: "Time" })).toHaveAttribute("step", "0.001");
    expect(screen.getByRole("slider", { name: "Flutter Rate" })).toHaveAttribute("step", "0.001");
  });

  it("moves categorical Daisy sliders only between valid options", () => {
    const block = createBlockFromDefinition("mod:chorus", []);
    const onParam = vi.fn();
    renderWithProviders(<BlockInspector block={block} issues={[]} models={[]} irs={[]} onToggle={() => undefined} onParam={onParam} onAsset={() => undefined} onMode={() => undefined} onEqBand={() => undefined} onReset={() => undefined} onDuplicate={() => undefined} onDelete={() => undefined} onAssets={() => undefined} />);

    const type = screen.getByRole("slider", { name: "Type" });
    expect(type).toHaveAttribute("min", "0");
    expect(type).toHaveAttribute("max", "4");
    expect(type).toHaveAttribute("step", "1");
    fireEvent.change(type, { target: { value: "1" } });
    expect(onParam).toHaveBeenCalledWith(block.id, "p2", 0.25);
  });
});
