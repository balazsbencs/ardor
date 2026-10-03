import { screen } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { Preset } from "../api/types";
import { enableScenes } from "../presets/editor/presetFactory";
import { renderWithEditor } from "./renderWithEditor";
import { SceneScope } from "./SceneScope";

const preset: Preset = enableScenes({
  version: 1, name: "Scenes", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [],
});
const session = {
  status: "connected" as const, current: { location: { bank: 0, slot: 0 }, preset: structuredClone(preset), exists: true },
  device: { active: { bank: 0, slot: 0, liveSceneId: "scene-2" }, capabilities: {} },
  presets: [], irs: [], reverbIrs: [], models: [], busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(), applyCurrent: vi.fn(), refreshPresets: vi.fn(), selectLocation: vi.fn(async () => undefined),
};
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

beforeEach(() => { session.device.active = { bank: 0, slot: 0, liveSceneId: "scene-2" }; });

describe("SceneScope", () => {
  it("marks the live scene of the live preset", () => {
    renderWithEditor(<SceneScope />);
    expect(screen.getByRole("button", { name: "2 Scene 2" }).querySelector(".lampdot")).not.toBeNull();
    expect(document.querySelectorAll(".lampdot")).toHaveLength(1);
  });

  it("shows no live scene when another slot is live", () => {
    session.device.active = { bank: 0, slot: 1, liveSceneId: "scene-2" };
    const { editor } = renderWithEditor(<SceneScope />);
    expect(document.querySelector(".lampdot")).toBeNull();
    expect(editor().liveSceneId).toBeUndefined();
  });
});
