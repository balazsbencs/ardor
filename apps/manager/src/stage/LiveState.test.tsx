import { act, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { Preset } from "../api/types";
import { LiveState } from "./LiveState";
import { renderWithEditor } from "./renderWithEditor";

const basePreset: Preset = {
  version: 1, name: "Live", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [{ id: "d1", type: "delay", enabled: true, asset: "", params: { mode: "tape", time_ms: 300, feedback: 0.3, mix: 0.25, filter: 0.5 } }],
};
const session = {
  status: "connected" as const, current: { location: { bank: 0, slot: 0 }, preset: structuredClone(basePreset), exists: true },
  device: { active: {} as Record<string, unknown>, capabilities: {} }, presets: [], irs: [], reverbIrs: [], models: [],
  busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(async (preset: Preset) => ({ location: { bank: 0, slot: 0 }, preset, exists: true })), applyCurrent: vi.fn(async () => undefined), refreshPresets: vi.fn(), selectLocation: vi.fn(async () => undefined),
};
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

beforeEach(() => {
  session.current.preset = structuredClone(basePreset);
  session.busy.apply = false;
  session.saveCurrent.mockClear();
  session.applyCurrent.mockClear();
});

describe("LiveState", () => {
  it("says LIVE ON PEDAL when the saved slot is live", () => {
    session.device.active = { bank: 0, slot: 0, generation: 3, storedRevisionMatches: true };
    renderWithEditor(<LiveState />);
    expect(screen.getByText("LIVE ON PEDAL")).toBeInTheDocument();
  });

  it("offers Load on pedal for a clean preset that is not live", async () => {
    session.device.active = { bank: 5, slot: 0, generation: 3, storedRevisionMatches: true };
    renderWithEditor(<LiveState />);
    await userEvent.click(screen.getByRole("button", { name: "Load on pedal" }));
    expect(session.applyCurrent).toHaveBeenCalled();
  });

  it("offers Save and load when the draft has changes", async () => {
    session.device.active = { bank: 0, slot: 0, generation: 3, storedRevisionMatches: true };
    const { editor } = renderWithEditor(<LiveState />);
    act(() => editor().editParameter("d1", "mix", 0.5));
    await userEvent.click(screen.getByRole("button", { name: "Save and load" }));
    expect(session.saveCurrent).toHaveBeenCalled();
  });

  it("shows a busy label while sending", () => {
    session.busy.apply = true;
    renderWithEditor(<LiveState />);
    expect(screen.getByText("Sending to the pedal")).toBeInTheDocument();
  });
});
