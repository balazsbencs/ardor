import { act, renderHook } from "@testing-library/react";
import type { ReactNode } from "react";
import { describe, expect, it, vi } from "vitest";

import type { Preset } from "../../api/types";
import { EditorProvider, usePresetEditorContext } from "./EditorContext";

const scene = (id: string, name: string) => ({ id, name, enterTimeMs: 0, outputTrimDb: 0, targets: [] });
const preset: Preset = {
  version: 4, name: "Scenes", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [{ id: "d1", type: "delay", enabled: true, asset: "", params: { mode: "tape", mix: 0.25 } }],
  sceneSet: {
    defaultSceneId: "verse", openIn: "presets",
    scenes: [scene("verse", "Verse"), scene("chorus", "Chorus"), scene("solo", "Solo"), scene("outro", "Outro")],
  },
};
const session = {
  status: "connected" as const, current: { location: { bank: 0, slot: 0 }, preset, exists: true },
  device: { active: { bank: 0, slot: 0 }, capabilities: {} }, models: [], irs: [], reverbIrs: [], presets: [],
  busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(), applyCurrent: vi.fn(), refreshPresets: vi.fn(), selectLocation: vi.fn(async () => undefined),
};
vi.mock("../../connection/deviceSession", () => ({ useDeviceSession: () => session }));

const wrapper = ({ children }: { children: ReactNode }) => <EditorProvider>{children}</EditorProvider>;

describe("usePresetEditor scene rule", () => {
  it("makes all scenes own a value on the first scene edit and changes only the open scene", () => {
    const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
    act(() => result.current.dispatch({ type: "select-scene", sceneId: "solo" }));
    act(() => result.current.editParameter("d1", "mix", 0.5, "g1"));
    const scenes = result.current.present.sceneSet!.scenes;
    const mixOf = (id: string) => scenes.find((s) => s.id === id)!.targets.find((t) => t.target === "parameter" && t.parameter === "mix");
    expect(mixOf("solo")).toMatchObject({ value: 0.5 });
    expect(mixOf("verse")).toMatchObject({ value: 0.25 });
    expect(result.current.present.blocks[0].params.mix).toBe(0.25);
    act(() => result.current.dispatch({ type: "undo" }));
    expect(result.current.present.sceneSet!.scenes.every((s) => s.targets.length === 0)).toBe(true);
  });

  it("edits the shared value with no scene open", () => {
    const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
    act(() => result.current.dispatch({ type: "clear-scene" }));
    act(() => result.current.editParameter("d1", "mix", 0.6));
    expect(result.current.present.blocks[0].params.mix).toBe(0.6);
  });

  it("makes the scenes own the enabled state on the first scene toggle", () => {
    const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
    act(() => result.current.dispatch({ type: "select-scene", sceneId: "solo" }));
    act(() => result.current.editBlockEnabled("d1", false));
    const scenes = result.current.present.sceneSet!.scenes;
    const enabledOf = (id: string) => scenes.find((s) => s.id === id)!.targets.find((t) => t.target === "blockEnabled");
    expect(enabledOf("solo")).toMatchObject({ value: false });
    expect(enabledOf("verse")).toMatchObject({ value: true });
    expect(result.current.present.blocks[0].enabled).toBe(true);
  });
});
