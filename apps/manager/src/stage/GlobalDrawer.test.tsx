import { act, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { Preset } from "../api/types";
import { GlobalDrawer } from "./GlobalDrawer";
import { renderWithEditor } from "./renderWithEditor";

const basePreset: Preset = {
  version: 1, name: "Global", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [{ id: "d1", type: "delay", enabled: true, asset: "", params: { mode: "tape", time_ms: 300, feedback: 0.3, mix: 0.25, filter: 0.5 } }],
};
const session = {
  status: "connected" as const, current: { location: { bank: 0, slot: 0 }, preset: structuredClone(basePreset), exists: true },
  device: { active: { bank: 0, slot: 0 }, capabilities: {} }, presets: [], irs: [], reverbIrs: [], models: [],
  busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(), applyCurrent: vi.fn(), refreshPresets: vi.fn(), selectLocation: vi.fn(async () => undefined),
};
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

beforeEach(() => { session.current.preset = structuredClone(basePreset); });

describe("GlobalDrawer", () => {
  it("shows input, output, the fixed limiter and the topology", () => {
    renderWithEditor(<GlobalDrawer onClose={vi.fn()} />);
    expect(screen.getByRole("slider", { name: "Input gain" })).toBeInTheDocument();
    expect(screen.getByRole("slider", { name: "Output level" })).toBeInTheDocument();
    expect(screen.getByText("Protection, not a tone control. It cannot be changed.")).toBeInTheDocument();
    expect(screen.queryByRole("slider", { name: /limit/i })).not.toBeInTheDocument();
    expect(screen.getByRole("radio", { name: "Serial" })).toHaveAttribute("aria-checked", "true");
  });

  it("edits input gain and switches to wet dry wet with lane controls", async () => {
    const { editor } = renderWithEditor(<GlobalDrawer onClose={vi.fn()} />);
    act(() => screen.getByRole("slider", { name: "Input gain" }).focus());
    await userEvent.keyboard("{ArrowRight}");
    expect(editor().present.global.inputGainDb).toBeCloseTo(0.5);
    await userEvent.click(screen.getByRole("radio", { name: "Wet dry wet" }));
    expect(editor().present.routing).toBe("wdw");
    expect(screen.getByRole("slider", { name: "Dry level" })).toBeInTheDocument();
    expect(screen.getByRole("slider", { name: "Wet width" })).toBeInTheDocument();
  });

  it("enables the expression pedal on a block parameter", async () => {
    const { editor } = renderWithEditor(<GlobalDrawer onClose={vi.fn()} />);
    await userEvent.click(screen.getByRole("checkbox", { name: "Expression pedal" }));
    expect(editor().present.expression).toBeDefined();
    expect(screen.getByRole("slider", { name: "Heel" })).toBeInTheDocument();
    await userEvent.click(screen.getByRole("checkbox", { name: "Invert" }));
    expect(editor().present.expression?.inverted).toBe(true);
  });
});
