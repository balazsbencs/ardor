import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { allEffectDefinitions } from "../effects/catalog";
import { familyOf } from "../ui/family";
import { ModuleDrawer } from "./ModuleDrawer";

describe("ModuleDrawer", () => {
  it("groups blocks by family with code squares and says where they go", () => {
    render(<ModuleDrawer open where="Insert after Tape Delay, position 9" disabledIds={new Map()} onOpenChange={vi.fn()} onChoose={vi.fn()} />);
    expect(screen.getByText("Insert after Tape Delay, position 9")).toBeInTheDocument();
    const delay = screen.getByRole("group", { name: "Delay" });
    expect(within(delay).getByRole("button", { name: /Tape Delay/ })).toBeInTheDocument();
    expect(within(delay).getByText("TAPE")).toBeInTheDocument();
  });

  it("lists every supported definition", () => {
    render(<ModuleDrawer open where="" disabledIds={new Map()} onOpenChange={vi.fn()} onChoose={vi.fn()} />);
    for (const definition of allEffectDefinitions()) {
      expect(screen.getAllByRole("button", { name: new RegExp(definition.name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")) }).length).toBeGreaterThan(0);
    }
  });

  it("groups dynamics, EQ, wah and the widener under Dynamics and tone", () => {
    render(<ModuleDrawer open where="" disabledIds={new Map()} onOpenChange={vi.fn()} onChoose={vi.fn()} />);
    const utility = within(screen.getByRole("group", { name: "Dynamics and tone" }));
    for (const name of [/Compressor/, /Noise Gate/, /Five Band Parametric EQ/, /GCB-95 Wah/, /Stereo Widener/, /Transient Shaper/, /1073 EQ/]) {
      expect(utility.getByRole("button", { name })).toBeInTheDocument();
    }
  });

  it("filters by search and picks the first match with Enter", async () => {
    const onChoose = vi.fn();
    render(<ModuleDrawer open where="" disabledIds={new Map()} onOpenChange={vi.fn()} onChoose={onChoose} />);
    await userEvent.type(screen.getByRole("searchbox", { name: "Search blocks" }), "shimmer{Enter}");
    expect(onChoose).toHaveBeenCalledWith(expect.objectContaining({ id: "reverb:shimmer" }));
  });

  it("disables a block with the reason", () => {
    render(<ModuleDrawer open where="" disabledIds={new Map([["delay:digital", "Disable Tape Delay first"]])} onOpenChange={vi.fn()} onChoose={vi.fn()} />);
    const row = screen.getByRole("button", { name: /Digital Delay/ });
    expect(row).toBeDisabled();
    expect(row).toHaveTextContent("Disable Tape Delay first");
  });

  it("reopens with an empty search and the All filter", async () => {
    const props = { where: "", disabledIds: new Map<string, string>(), onOpenChange: vi.fn(), onChoose: vi.fn() };
    const { rerender } = render(<ModuleDrawer open {...props} />);
    await userEvent.type(screen.getByRole("searchbox", { name: "Search blocks" }), "shim");
    await userEvent.click(within(screen.getByRole("group", { name: "Families" })).getByRole("button", { name: "Delay" }));
    rerender(<ModuleDrawer open={false} {...props} />);
    rerender(<ModuleDrawer open {...props} />);
    expect(screen.getByRole("searchbox", { name: "Search blocks" })).toHaveValue("");
    expect(within(screen.getByRole("group", { name: "Families" })).getByRole("button", { name: "All" })).toHaveAttribute("aria-pressed", "true");
    expect(screen.getByRole("group", { name: "Reverb" })).toBeInTheDocument();
  });

  it("skips a disabled first match when Enter is pressed", async () => {
    const delays = allEffectDefinitions().filter((d) => familyOf(d.blockType) === "dly" && d.name.toLowerCase().includes("delay"));
    const onChoose = vi.fn();
    render(<ModuleDrawer open where="" disabledIds={new Map([[delays[0].id, "Disable it first"]])} onOpenChange={vi.fn()} onChoose={onChoose} />);
    await userEvent.click(within(screen.getByRole("group", { name: "Families" })).getByRole("button", { name: "Delay" }));
    await userEvent.type(screen.getByRole("searchbox", { name: "Search blocks" }), "delay{Enter}");
    expect(onChoose).toHaveBeenCalledTimes(1);
    expect(onChoose).toHaveBeenCalledWith(expect.objectContaining({ id: delays[1].id }));
  });
});


it("lists uploaded parametric models by name and selects the model asset", async () => {
  const onChoose = vi.fn();
  render(<ModuleDrawer open where="" disabledIds={new Map()} onOpenChange={vi.fn()} onChoose={onChoose}
    models={[{ id: "od.nam", kind: "model", filename: "Open OD.nam", path: "models/od.nam", sizeBytes: 1,
      nam: { architecture: "ConcatLSTM", version: "1.0.0", gearType: "pedal", supported: true,
        parameters: [{ name: "Drive", min: 0, max: 10, default: 5, type: "continuous" }] } }]} />);
  await userEvent.type(screen.getByRole("searchbox", { name: "Search blocks" }), "Open OD{Enter}");
  expect(onChoose).toHaveBeenCalledWith(expect.objectContaining({ id: "namPedal", assetPath: "models/od.nam" }));
});

it("classifies named amps and pedals, respects the amp slot, and excludes unsupported models", () => {
  const onChoose = vi.fn();
  const makeModel = (name: string, gearType: "amp" | "pedal", supported = true) => ({
    id: name + ".nam", kind: "model" as const, filename: name + ".nam", path: `models/${name}.nam`, sizeBytes: 1,
    nam: { architecture: "ParametricWaveNet", version: "0.7.0", gearType, supported, slimmable: true,
      parameters: [{ name: "Gain", min: 0, max: 1, default: 0.5, type: "continuous" as const }] },
  });
  render(<ModuleDrawer open where="" disabledIds={new Map([["nam", "Disable the existing amp first"]])}
    onOpenChange={vi.fn()} onChoose={onChoose}
    models={[makeModel("Public Amp", "amp"), makeModel("Public Pedal", "pedal"), makeModel("Unsupported Model", "pedal", false)]} />);
  expect(screen.getByRole("button", { name: /Public Amp/ })).toBeDisabled();
  expect(screen.getByRole("button", { name: /Public Pedal/ })).toBeEnabled();
  expect(screen.queryByRole("button", { name: /Unsupported Model/ })).not.toBeInTheDocument();
});
