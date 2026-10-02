import { fireEvent, render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { Preset } from "../api/types";
import { createBlockFromDefinition } from "../effects/catalog";
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
  device: { active: { bank: 0, slot: 0 }, capabilities: {} }, presets: [],
  irs: [] as Array<Record<string, unknown>>, reverbIrs: [] as Array<Record<string, unknown>>,
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
  return <><output data-testid="params">{JSON.stringify(editor.present.blocks.find((b) => b.id === id)?.params)}</output>{editor.present.sceneSet && <output data-testid="scene-owners">{editor.present.sceneSet.scenes.filter((s) => s.targets.some((t) => t.target === "blockEnabled" && t.blockId === id)).length}</output>}<BlockDrawer block={block} issues={[]} onFocusKey={vi.fn()} onClose={onClose} onManageFiles={onManage} /></>;
}

beforeEach(() => {
  session.current.preset = structuredClone(basePreset);
  session.irs = [];
  session.reverbIrs = [];
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

  it("offers Create four scenes beside a scene-set-required issue", async () => {
    const issue = { severity: "error" as const, code: "scene-set-required", message: "Preset version 4 requires a scene set." };
    function IssueHarness() {
      const editor = usePresetEditorContext();
      return <>{editor.present.sceneSet && <output data-testid="has-scenes" />}<BlockDrawer block={editor.present.blocks[0]} issues={[issue]} onFocusKey={vi.fn()} onClose={onClose} onManageFiles={onManage} /></>;
    }
    render(<EditorProvider><IssueHarness /></EditorProvider>);
    await userEvent.click(screen.getByRole("button", { name: "Create four scenes" }));
    expect(screen.getByTestId("has-scenes")).toBeInTheDocument();
  });
});

describe("BlockDrawer, ported from the old inspector", () => {
  const params = () => JSON.parse(screen.getByTestId("params").textContent ?? "{}") as Record<string, unknown>;
  const withBlock = (block: Preset["blocks"][number]) => { session.current.preset = { ...structuredClone(basePreset), blocks: [block] }; };

  it("lists reverb IRs, plus the cab IR the block already uses", () => {
    session.reverbIrs = [{ id: "r", kind: "ir", filename: "Chapel.wav", path: "reverb-irs/Chapel.wav", sizeBytes: 1 }];
    session.irs = [{ id: "c", kind: "ir", filename: "Room.wav", path: "irs/Room.wav", sizeBytes: 1 }, { id: "o", kind: "ir", filename: "Other.wav", path: "irs/Other.wav", sizeBytes: 1 }];
    withBlock({ ...createBlockFromDefinition("irreverb", [], "irs/Room.wav"), id: "v1" });
    render(<EditorProvider><Harness id="v1" /></EditorProvider>);
    expect(screen.getByRole("radio", { name: "Chapel" })).toBeInTheDocument();
    expect(screen.getByRole("radio", { name: "Room" })).toHaveAttribute("aria-checked", "true");
    expect(screen.queryByRole("radio", { name: "Other" })).not.toBeInTheDocument();
    expect(screen.queryByText(/is not on the pedal/)).not.toBeInTheDocument();
  });

  it("names an unknown block type and still offers Block on", async () => {
    withBlock({ id: "x1", type: "mystery" as never, enabled: true, asset: "", params: {} });
    render(<EditorProvider><Harness id="x1" /></EditorProvider>);
    expect(screen.getByText(/unknown to this manager/)).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "Block on" }));
    expect(screen.getByRole("button", { name: "Block off" })).toBeInTheDocument();
  });

  it("changes the mode of a delay through the Type strip", async () => {
    render(<EditorProvider><Harness id="d1" /></EditorProvider>);
    await userEvent.click(within(screen.getByRole("radiogroup", { name: "Type" })).getByRole("radio", { name: "Digital Delay" }));
    expect(screen.getByRole("heading", { name: "Digital Delay" })).toBeInTheDocument();
  });

  it("offers Cut and Let ring for IR reverb on a version 4 preset", async () => {
    session.current.preset = { ...structuredClone(scenePreset), blocks: [{ ...createBlockFromDefinition("irreverb", []), id: "v1" }] };
    render(<EditorProvider><Harness id="v1" /></EditorProvider>);
    expect(screen.getByRole("radio", { name: "Let ring" })).toHaveAttribute("aria-checked", "true");
    await userEvent.click(screen.getByRole("radio", { name: "Cut" }));
    expect(screen.getByRole("radio", { name: "Cut" })).toHaveAttribute("aria-checked", "true");
  });

  it("keeps IR reverb time shared across scenes and explains its range", async () => {
    session.current.preset = { ...structuredClone(scenePreset), blocks: [{ ...createBlockFromDefinition("irreverb", []), id: "v1" }] };
    render(<EditorProvider><Harness id="v1" /></EditorProvider>);
    const time = screen.getByRole("slider", { name: "Reverb time" });
    expect(time).toHaveAttribute("aria-valuemin", "0.25");
    expect(time).toHaveAttribute("aria-valuemax", "1");
    expect(time).toHaveAccessibleDescription(/Short or non-decaying IRs keep their original response\. Shared across scenes\./);
    fireEvent.keyDown(time, { key: "ArrowLeft" });
    expect(params().reverbTimeRatio).toBeLessThan(1);
    expect(time.closest(".lb-ctl")).not.toHaveTextContent("SCENE");
  });

  it("offers the nano model and the input source as choices", async () => {
    render(<EditorProvider><Harness id="n1" /></EditorProvider>);
    const nano = screen.getByRole("radiogroup", { name: "Use nano model" });
    expect(within(nano).getByRole("radio", { name: "Off" })).toHaveAttribute("aria-checked", "true");
    await userEvent.click(within(nano).getByRole("radio", { name: "On" }));
    expect(params().useNano).toBe(true);
    await userEvent.click(within(screen.getByRole("radiogroup", { name: "Input source" })).getByRole("radio", { name: "Left / Mono" }));
    expect(params().inputMode).toBe("left");
  });

  it("renders the complete compressor control surface", async () => {
    withBlock({ ...createBlockFromDefinition("dynamics:compressor", []), id: "c1" });
    render(<EditorProvider><Harness id="c1" /></EditorProvider>);
    for (const label of ["Threshold", "Ratio", "Attack", "Release", "Knee", "Makeup", "Input", "Mix", "Sidechain HPF"]) {
      expect(screen.getByRole("slider", { name: label })).toBeInTheDocument();
    }
    await userEvent.click(within(screen.getByRole("radiogroup", { name: "Detector" })).getByRole("radio", { name: "RMS" }));
    expect(params().detector).toBe("rms");
  });

  it("edits Dual Amp lane assets independently", async () => {
    session.models = [{ id: "m", kind: "model", filename: "amp.nam", path: "models/amp.nam", sizeBytes: 1 }];
    session.irs = [{ id: "c", kind: "ir", filename: "cab.wav", path: "irs/cab.wav", sizeBytes: 1 }];
    withBlock({ ...createBlockFromDefinition("dualAmp", []), id: "a1" });
    render(<EditorProvider><Harness id="a1" /></EditorProvider>);
    expect(screen.getByRole("heading", { name: "Lane A" })).toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "Lane B" })).toBeInTheDocument();
    await userEvent.click(within(screen.getByRole("radiogroup", { name: "Left NAM model" })).getByRole("radio", { name: "amp" }));
    expect(params()).toMatchObject({ leftNamAsset: "models/amp.nam", rightNamAsset: "" });
    await userEvent.click(within(screen.getByRole("radiogroup", { name: "Right cabinet IR" })).getByRole("radio", { name: "cab" }));
    expect(params()).toMatchObject({ rightIrAsset: "irs/cab.wav", leftIrAsset: "" });
  });

  it("shows Daisy values in physical units", () => {
    withBlock({ ...createBlockFromDefinition("delay:tape", []), id: "t1" });
    render(<EditorProvider><Harness id="t1" /></EditorProvider>);
    expect(screen.getByText("98.1")).toBeInTheDocument();
    expect(screen.getByRole("slider", { name: "Time" })).toHaveAttribute("aria-valuetext", "98.1 ms");
  });

  it("moves categorical sliders only between valid options", () => {
    withBlock({ ...createBlockFromDefinition("mod:chorus", []), id: "m1" });
    render(<EditorProvider><Harness id="m1" /></EditorProvider>);
    const type = screen.getByRole("slider", { name: "Type" });
    fireEvent.keyDown(type, { key: "ArrowRight" });
    expect(params().p2).toBe(0.25);
  });

  it("edits the high-pass and low-pass stages around the five EQ bands", async () => {
    withBlock({ ...createBlockFromDefinition("eq:parametric_eq_5", []), id: "e1" });
    render(<EditorProvider><Harness id="e1" /></EditorProvider>);
    expect(screen.getAllByRole("tab")).toHaveLength(7);
    await userEvent.click(screen.getByRole("tab", { name: /HP/ }));
    expect(screen.getByText("High-pass filter")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("checkbox", { name: "High-pass enabled" }));
    expect(params().high_pass).toMatchObject({ enabled: true, frequency_hz: 40 });
    await userEvent.click(screen.getByRole("button", { name: "24 dB/oct" }));
    expect(params().high_pass).toMatchObject({ slope_db_per_octave: 24 });
    await userEvent.click(screen.getByRole("tab", { name: /LP/ }));
    expect(screen.getByRole("slider", { name: "Cutoff" })).toHaveAttribute("aria-valuenow", "16000");
  });

  it("edits an EQ band", async () => {
    withBlock({ ...createBlockFromDefinition("eq:parametric_eq_5", []), id: "e1" });
    render(<EditorProvider><Harness id="e1" /></EditorProvider>);
    await userEvent.click(screen.getAllByRole("tab")[2]);
    fireEvent.keyDown(screen.getByRole("slider", { name: "Gain" }), { key: "ArrowRight" });
    expect((params().bands as Array<{ gain_db: number }>)[1].gain_db).toBeCloseTo(0.1);
  });

  it("hides resonance for a first-order pass filter", async () => {
    const block = createBlockFromDefinition("eq:parametric_eq_5", []);
    withBlock({ ...block, id: "e1", params: { ...block.params, high_pass: { ...(block.params.high_pass as object), slope_db_per_octave: 6 } } });
    render(<EditorProvider><Harness id="e1" /></EditorProvider>);
    await userEvent.click(screen.getByRole("tab", { name: /HP/ }));
    expect(screen.queryByRole("slider", { name: "Resonance" })).not.toBeInTheDocument();
  });

  it("marks a scene-owned value and lets it share again", async () => {
    session.current.preset = { ...structuredClone(scenePreset), blocks: [{ ...createBlockFromDefinition("delay:tape", []), id: "t1" }] };
    render(<EditorProvider><Harness id="t1" /></EditorProvider>);
    await userEvent.click(screen.getByRole("button", { name: /^1 Scene 1/ }));
    fireEvent.keyDown(screen.getByRole("slider", { name: "Time" }), { key: "ArrowRight" });
    expect(screen.getAllByText("SCENE").length).toBeGreaterThan(0);
    await userEvent.click(screen.getAllByRole("button", { name: "Share" })[0]);
    expect(screen.queryByText("SCENE")).not.toBeInTheDocument();
  });
});
