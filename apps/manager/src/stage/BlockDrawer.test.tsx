import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { Preset } from "../api/types";
import { EditorProvider, usePresetEditorContext } from "../presets/editor/EditorContext";
import { createSceneSet } from "../presets/editor/presetFactory";
import { applySceneToBlock } from "../presets/scenes/sceneView";
import { BlockDrawer } from "./BlockDrawer";

const basePreset: Preset = {
  version: 1, name: "Drawer", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [
    { id: "d1", type: "delay", enabled: true, asset: "", params: { mode: "tape", time_ms: 300, feedback: 0.3, mix: 0.25, filter: 0.5 } },
    { id: "n1", type: "nam", enabled: true, asset: "models/Clean.nam", params: { inputMode: "sum", useNano: false } },
  ],
};
const scenePreset: Preset = { ...basePreset, version: 4, sceneSet: createSceneSet() };
const session = {
  status: "connected" as const, current: { location: { bank: 0, slot: 0 }, preset: structuredClone(basePreset), exists: true },
  device: { active: { bank: 0, slot: 0 }, capabilities: {} }, presets: [], irs: [], reverbIrs: [],
  models: [
    { id: "m1", kind: "model", filename: "Clean.nam", path: "models/Clean.nam", sizeBytes: 1 },
    { id: "m2", kind: "model", filename: "Lead.nam", path: "models/Lead.nam", sizeBytes: 1 },
  ],
  busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(), applyCurrent: vi.fn(), refreshPresets: vi.fn(), selectLocation: vi.fn(async () => undefined),
};
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

const onClose = vi.fn();
const onManage = vi.fn();

function Harness({ id }: { id: string }) {
  const editor = usePresetEditorContext();
  const block = applySceneToBlock(editor.present.blocks.find((b) => b.id === id)!, editor.editingScene);
  return <>{editor.present.sceneSet && <output data-testid="scene-owners">{editor.present.sceneSet.scenes.filter((s) => s.targets.some((t) => t.target === "blockEnabled" && t.blockId === id)).length}</output>}<BlockDrawer block={block} issues={[]} onFocusKey={vi.fn()} onClose={onClose} onManageFiles={onManage} /></>;
}

beforeEach(() => {
  session.current.preset = structuredClone(basePreset);
  onClose.mockReset();
  onManage.mockReset();
});

describe("BlockDrawer", () => {
  it("shows every control of the block at drawer scale", () => {
    render(<EditorProvider><Harness id="d1" /></EditorProvider>);
    expect(screen.getByRole("heading", { name: "Tape Delay" })).toBeInTheDocument();
    for (const name of ["Time", "Repeats", "Mix", "Filter"]) expect(screen.getByRole("slider", { name })).toBeInTheDocument();
  });

  it("turns the block off with BLOCK ON", async () => {
    render(<EditorProvider><Harness id="d1" /></EditorProvider>);
    await userEvent.click(screen.getByRole("button", { name: "Block on" }));
    expect(screen.getByRole("button", { name: "Block off" })).toBeInTheDocument();
  });

  it("picks a NAM file and links to Assets", async () => {
    render(<EditorProvider><Harness id="n1" /></EditorProvider>);
    await userEvent.click(screen.getByRole("radio", { name: "Lead" }));
    expect(screen.getByRole("radio", { name: "Lead" })).toHaveAttribute("aria-checked", "true");
    await userEvent.click(screen.getByRole("button", { name: "Manage files" }));
    expect(onManage).toHaveBeenCalledWith("models");
  });

  it("says when the file is not on the pedal", () => {
    session.current.preset.blocks[1].asset = "models/Gone.nam";
    render(<EditorProvider><Harness id="n1" /></EditorProvider>);
    expect(screen.getByText(/Gone.nam is not on the pedal/)).toBeInTheDocument();
  });

  it("closes and deletes through the head actions", async () => {
    render(<EditorProvider><Harness id="d1" /></EditorProvider>);
    await userEvent.click(screen.getByRole("button", { name: "Close" }));
    expect(onClose).toHaveBeenCalledTimes(1);
  });

  it("shows no SCENE tag or Share beside BLOCK ON when the open scene does not own it", async () => {
    session.current.preset = structuredClone(scenePreset);
    render(<EditorProvider><Harness id="d1" /></EditorProvider>);
    await userEvent.click(screen.getByRole("button", { name: /^1 Scene 1/ }));
    expect(screen.queryByText("SCENE")).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "Share" })).not.toBeInTheDocument();
  });

  it("lets BLOCK ON share its scene value again", async () => {
    session.current.preset = structuredClone(scenePreset);
    render(<EditorProvider><Harness id="d1" /></EditorProvider>);
    await userEvent.click(screen.getByRole("button", { name: /^1 Scene 1/ }));
    await userEvent.click(screen.getByRole("button", { name: "Block on" }));
    expect(screen.getByRole("button", { name: "Block off" })).toBeInTheDocument();
    expect(screen.getByText("SCENE")).toBeInTheDocument();
    expect(screen.getByTestId("scene-owners")).toHaveTextContent("4");
    await userEvent.click(screen.getByRole("button", { name: "Share" }));
    expect(screen.getByTestId("scene-owners")).toHaveTextContent("0");
    expect(screen.queryByText("SCENE")).not.toBeInTheDocument();
  });
});
