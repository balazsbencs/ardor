import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import type { Preset } from "../api/types";
import { renderWithEditor } from "./renderWithEditor";
import { ScenesDrawer } from "./ScenesDrawer";

const basePreset: Preset = {
  version: 1, name: "Scenes", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [{ id: "d1", type: "delay", enabled: true, asset: "", params: { mode: "tape", time_ms: 300, feedback: 0.3, mix: 0.25, filter: 0.5 } }],
};
const session = {
  status: "connected" as const, current: { location: { bank: 0, slot: 0 }, preset: structuredClone(basePreset), exists: true },
  device: { active: { bank: 0, slot: 0, liveSceneId: "scene-1" as string | undefined }, capabilities: {} }, presets: [], irs: [], reverbIrs: [], models: [],
  busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(), applyCurrent: vi.fn(), refreshPresets: vi.fn(), selectLocation: vi.fn(async () => undefined),
};
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

describe("ScenesDrawer", () => {
  it("offers to create four scenes, then shows the scene bar", async () => {
    const { editor } = renderWithEditor(<ScenesDrawer onClose={vi.fn()} />);
    expect(screen.getByText("Scenes change blocks and values inside one preset. FS 1 to 4 pick them on the pedal.")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "Create four scenes" }));
    expect(editor().present.sceneSet?.scenes).toHaveLength(4);
  });

  it("marks a live scene only when the open slot is the live slot", async () => {
    session.device.active = { bank: 0, slot: 0, liveSceneId: "scene-1" };
    renderWithEditor(<ScenesDrawer onClose={vi.fn()} />);
    await userEvent.click(screen.getByRole("button", { name: "Create four scenes" }));
    expect(screen.getByRole("tab", { name: /Scene 1/ })).toHaveTextContent("Live");
    session.device.active = { bank: 0, slot: 2, liveSceneId: "scene-1" };
    await userEvent.click(screen.getByRole("tab", { name: /Scene 2/ }));
    expect(screen.getByRole("tab", { name: /Scene 1/ })).not.toHaveTextContent("Live");
  });
});
