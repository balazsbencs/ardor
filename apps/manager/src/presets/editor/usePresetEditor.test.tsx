import { act, renderHook } from "@testing-library/react";
import type { ReactNode } from "react";
import { describe, expect, it, vi } from "vitest";

import type { Preset } from "../../api/types";
import { createBlockFromDefinition, findEffectDefinition } from "../../effects/catalog";
import { EditorProvider, usePresetEditorContext } from "./EditorContext";
import { createWdwPreset, enableScenes } from "./presetFactory";

const scene = (id: string, name: string) => ({ id, name, enterTimeMs: 0, outputTrimDb: 0, targets: [] });
const preset: Preset = {
  version: 4, name: "Scenes", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [
    { id: "d1", type: "delay", enabled: true, asset: "", params: { mode: "tape", mix: 0.25, time: 0.25 } },
    { id: "amp", type: "nam", enabled: true, asset: "models/test.nam", params: {} },
  ],
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
  recallScene: vi.fn(async () => true),
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

  it("uses the effect default in untouched scenes when the preset omits a parameter", () => {
    const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
    const sparse = structuredClone(preset);
    delete sparse.blocks[0].params.mix;
    const control = findEffectDefinition(sparse.blocks[0])?.controls.find((item) =>
      item.kind === "number" && item.key === "mix");
    if (control?.kind !== "number") throw new Error("Mix control is missing");
    act(() => result.current.dispatch({ type: "replace-present", preset: sparse }));
    act(() => result.current.editParameter("d1", "mix", 0.75));
    const [editedScene, otherScene] = result.current.present.sceneSet!.scenes;
    expect(editedScene.targets[0]).toMatchObject({ value: 0.75 });
    expect(otherScene.targets[0]).toMatchObject({ value: control.defaultValue });
    act(() => result.current.dispatch({ type: "undo" }));
    expect(result.current.present).toEqual(sparse);
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

  it("keeps amp bypass and delay time shared while editing a scene", () => {
    const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
    act(() => result.current.editBlockEnabled("amp", false));
    act(() => result.current.editParameter("d1", "time", 0.5));
    expect(result.current.present.blocks[1].enabled).toBe(false);
    expect(result.current.present.blocks[0].params.time).toBe(0.5);
    expect(result.current.present.sceneSet!.scenes.every((scene) => scene.targets.length === 0)).toBe(true);
  });

  it("preserves edits made while a save is in flight", async () => {
    let finish!: (response: { preset: Preset }) => void;
    session.saveCurrent.mockImplementationOnce(() => new Promise((resolve) => { finish = resolve; }));
    const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
    act(() => result.current.dispatch({ type: "set-scene-name", sceneId: "verse", name: "Submitted" }));
    const submitted = structuredClone(result.current.present);
    let saving!: Promise<boolean>;
    act(() => { saving = result.current.save(); });
    act(() => result.current.dispatch({ type: "set-scene-name", sceneId: "verse", name: "Still editing" }));
    await act(async () => {
      finish({ preset: submitted });
      expect(await saving).toBe(true);
    });
    expect(result.current.editingScene?.name).toBe("Still editing");
    expect(result.current.dirty).toBe(true);
  });

  it("rejects imported delay time as an unsupported scene target", () => {
    const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
    const imported = structuredClone(preset);
    for (const scene of imported.sceneSet!.scenes)
      scene.targets = [{ target: "parameter", blockId: "d1", parameter: "time", value: 0.5 }];
    act(() => result.current.dispatch({ type: "replace-present", preset: imported }));
    expect(result.current.validation.canSave).toBe(false);
    expect(result.current.validation.issues).toEqual(expect.arrayContaining([
      expect.objectContaining({ code: "scene-target-unsupported" }),
    ]));
  });

  it("copies scene parameter values when duplicating a bypassed effect", () => {
    const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
    const source = structuredClone(preset);
    const block = createBlockFromDefinition("delay:digital", []);
    source.blocks = [block];
    source.sceneSet!.scenes.forEach((scene, index) => {
      scene.targets.push({ target: "parameter", blockId: block.id, parameter: "mix", value: index / 4 });
    });
    act(() => result.current.dispatch({ type: "replace-present", preset: source }));
    act(() => result.current.dispatch({ type: "duplicate-block", blockId: block.id }));
    const duplicate = result.current.present.blocks[1];
    expect(duplicate.enabled).toBe(false);
    for (const [index, scene] of result.current.present.sceneSet!.scenes.entries()) {
      expect(scene.targets).toContainEqual({ target: "parameter", blockId: duplicate.id, parameter: "mix", value: index / 4 });
    }
  });

  it("makes a WDW lane value scene-owned on the first scene edit and can share it again", () => {
    const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
    const source = enableScenes(createWdwPreset());
    act(() => result.current.dispatch({ type: "replace-present", preset: source }));
    act(() => result.current.dispatch({ type: "select-scene", sceneId: "scene-2" }));
    act(() => result.current.editWdwMix("wet", "width", 0.5));
    const scenes = result.current.present.sceneSet!.scenes;
    expect(scenes[0].targets).toContainEqual({ target: "wdwLane", lane: "wet", parameter: "width", value: 1 });
    expect(scenes[1].targets).toContainEqual({ target: "wdwLane", lane: "wet", parameter: "width", value: 0.5 });
    act(() => result.current.dispatch({ type: "share-wdw-mix", sceneId: "scene-2", lane: "wet", key: "width" }));
    expect(result.current.present.wdw?.wet.width).toBe(0.5);
    expect(result.current.present.sceneSet?.scenes.every((scene) => scene.targets.length === 0)).toBe(true);
  });

  it("recalls the default scene shown in the drawer when editing shared values", async () => {
    const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
    act(() => result.current.dispatch({ type: "clear-scene" }));
    await act(() => result.current.recallScene());
    expect(session.recallScene).toHaveBeenCalledWith("verse");
  });
});
