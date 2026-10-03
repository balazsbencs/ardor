import { act, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";

import type { Preset } from "../api/types";
import { getEffectDefinition } from "../effects/catalog";
import type { NumberControl } from "../effects/types";
import { EditRail } from "./EditRail";
import { renderWithEditor } from "./renderWithEditor";

const basePreset: Preset = {
  version: 1, name: "Rail", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [{ id: "d1", type: "delay", enabled: true, asset: "", params: { mode: "tape", time_ms: 300, feedback: 0.3, mix: 0.25, filter: 0.5 } }],
};
const session = {
  status: "connected" as const, current: { location: { bank: 0, slot: 0 }, preset: structuredClone(basePreset), exists: true },
  device: { active: { bank: 0, slot: 0 }, capabilities: {} }, presets: [], irs: [], reverbIrs: [], models: [],
  busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(), applyCurrent: vi.fn(), refreshPresets: vi.fn(), selectLocation: vi.fn(async () => undefined),
};
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

const mixControl = () => getEffectDefinition("delay:tape").controls.find((c) => c.kind === "number" && c.key === "mix") as NumberControl;
const noop = { onOpen: vi.fn(), onAdd: vi.fn(), onDone: vi.fn() };

beforeEach(() => { session.current.preset = structuredClone(basePreset); });
afterEach(() => { vi.useRealTimers(); });

describe("EditRail", () => {
  it("keeps Save quiet until there is a change, then makes it primary", () => {
    const { editor } = renderWithEditor(<EditRail drawer="none" {...noop} />);
    expect(screen.getByRole("button", { name: "Save" })).toBeDisabled();
    act(() => editor().editParameter("d1", "mix", 0.5));
    expect(screen.getByRole("button", { name: "Save" })).toHaveClass("button--primary");
  });

  it("shows the focused control with fine steps and Assign EXP", async () => {
    const { editor } = renderWithEditor(<EditRail drawer="block" focused={{ blockId: "d1", control: mixControl() }} {...noop} />);
    expect(screen.getByText("Mix")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "Fine increase" }));
    expect(editor().present.blocks[0].params.mix).toBeCloseTo(0.255);
    await userEvent.click(screen.getByRole("button", { name: "Assign EXP" }));
    expect(editor().present.expression).toMatchObject({ blockId: "d1", parameter: "mix" });
  });

  it("groups fine steps within 900 ms into one undo step and starts a new one after", () => {
    vi.useFakeTimers();
    vi.setSystemTime(1_000_000);
    const { editor } = renderWithEditor(<EditRail drawer="block" focused={{ blockId: "d1", control: mixControl() }} {...noop} />);
    const up = () => act(() => screen.getByRole("button", { name: "Fine increase" }).click());
    up();
    vi.setSystemTime(1_000_400);
    up();
    expect(editor().editor.history.past).toHaveLength(1);
    vi.setSystemTime(1_002_000);
    up();
    expect(editor().editor.history.past).toHaveLength(2);
  });

  it("shows Done only while a drawer is open", async () => {
    const onDone = vi.fn();
    renderWithEditor(<EditRail drawer="global" onOpen={vi.fn()} onAdd={vi.fn()} onDone={onDone} />);
    await userEvent.click(screen.getByRole("button", { name: "Done" }));
    expect(onDone).toHaveBeenCalled();
  });
});
