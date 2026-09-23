import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { Preset } from "../../api/types";
import { createBlockFromDefinition } from "../../effects/catalog";
import { PresetWorkspace } from "./PresetWorkspace";

const preset: Preset = {
  version: 4, name: "Afterglow", routing: "serial",
  global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [],
  sceneSet: {
    defaultSceneId: "verse", openIn: "scenes", scenes: [
      { id: "verse", name: "Verse", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
      { id: "chorus", name: "Chorus", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
      { id: "solo", name: "Solo", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
      { id: "outro", name: "Outro", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
    ],
  },
};

const session = {
  status: "connected" as const,
  current: { location: { bank: 0, slot: 0 }, preset, exists: true },
  device: {
    active: { bank: 0, slot: 0, generation: 42, storedRevisionMatches: true, liveSceneId: "verse" },
    capabilities: { sceneRecall: true },
  },
  models: [], irs: [], reverbIrs: [], presets: [], busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(async (_saved: Preset) => ({ bank: 0, slot: 0, preset })),
  applyCurrent: vi.fn(async () => { throw new Error("DSP preparation failed"); }),
  refreshPresets: vi.fn(async () => undefined),
  selectLocation: vi.fn(async () => undefined),
  recallScene: vi.fn(async () => true),
};

vi.mock("../../connection/deviceSession", () => ({ useDeviceSession: () => session }));

describe("PresetWorkspace scene save/apply semantics", () => {
  beforeEach(() => {
    vi.clearAllMocks();
    session.current = { location: { bank: 0, slot: 0 }, preset: structuredClone(preset), exists: true };
  });

  it("reports when save succeeds but the pedal keeps playing the previous revision", async () => {
    render(<PresetWorkspace onAssets={vi.fn()} onConnection={vi.fn()} />);
    await userEvent.click(screen.getByRole("button", { name: "Save & Apply" }));
    expect(await screen.findByText(/Saved; pedal still playing the previous version/)).toHaveTextContent("DSP preparation failed");
    expect(session.saveCurrent).toHaveBeenCalled();
    expect(session.applyCurrent).toHaveBeenCalledWith("verse");
  });

  it("shows physically formatted preset values in the shared comparison", async () => {
    render(<PresetWorkspace onAssets={vi.fn()} onConnection={vi.fn()} />);
    await userEvent.click(screen.getByRole("button", { name: "Compare" }));
    await userEvent.click(screen.getByRole("button", { name: "Shared" }));
    expect(screen.getByText("Preset · output gain")).toBeInTheDocument();
    expect(screen.getByText("Preset · safety limit").nextElementSibling).toHaveTextContent("-1.0 dB");
  });

  it("lets a normal preset start scene authoring in the Manager", async () => {
    session.current.preset = { ...structuredClone(preset), version: 1, sceneSet: undefined };
    render(<PresetWorkspace onAssets={vi.fn()} onConnection={vi.fn()} />);
    await userEvent.click(screen.getByRole("button", { name: "Enable scenes" }));
    expect(screen.getByRole("tab", { name: /Scene 1/ })).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: /^Save$/ }));
    expect(session.saveCurrent).toHaveBeenCalledWith(expect.objectContaining({
      version: 4,
      sceneSet: expect.objectContaining({ defaultSceneId: "scene-1" }),
    }));
  });

  it("edits the selected scene's input gain without changing the shared preset gain", async () => {
    for (const scene of session.current.preset.sceneSet!.scenes) {
      scene.targets = [{ target: "inputGainDb", value: -6 }];
    }
    render(<PresetWorkspace onAssets={vi.fn()} onConnection={vi.fn()} />);
    const input = screen.getByRole("spinbutton", { name: "Input gain" });
    expect(input).toHaveValue(-6);
    await userEvent.clear(input);
    await userEvent.type(input, "3");
    await userEvent.click(screen.getByRole("button", { name: /^Save$/ }));
    const saved = session.saveCurrent.mock.lastCall?.[0] as Preset;
    expect(saved.global.inputGainDb).toBe(0);
    expect(saved.sceneSet?.scenes[0].targets[0]).toMatchObject({ value: 3 });
    expect(saved.sceneSet?.scenes[1].targets[0]).toMatchObject({ value: -6 });
  });

  it("shows and toggles the selected scene's bypass state on the chain canvas", async () => {
    const block = createBlockFromDefinition("mod:chorus", []);
    session.current.preset.blocks = [block];
    for (const scene of session.current.preset.sceneSet!.scenes) {
      scene.targets = [{ target: "blockEnabled", blockId: block.id, value: false }];
    }
    render(<PresetWorkspace onAssets={vi.fn()} onConnection={vi.fn()} />);
    const chain = screen.getByRole("region", { name: "Signal chain" });
    const toggle = chain.querySelector('input[type="checkbox"]') as HTMLInputElement;
    expect(toggle).not.toBeChecked();
    await userEvent.click(toggle);
    await userEvent.click(screen.getByRole("button", { name: /^Save$/ }));
    const saved = session.saveCurrent.mock.lastCall?.[0] as Preset;
    expect(saved.blocks[0].enabled).toBe(true);
    expect(saved.sceneSet?.scenes[0].targets[0]).toMatchObject({ value: true });
    expect(saved.sceneSet?.scenes[1].targets[0]).toMatchObject({ value: false });
  });
});
