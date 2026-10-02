import { act, fireEvent, render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import type { ReactNode } from "react";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { Preset } from "../api/types";
import { createBlockFromDefinition } from "../effects/catalog";
import { EditorProvider } from "../presets/editor/EditorContext";
import { blockOf } from "../test/blocks";
import { renderWithEditor } from "./renderWithEditor";
import { StageWorkspace } from "./StageWorkspace";

const scenePreset: Preset = {
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

const delayPreset: Preset = {
  version: 1, name: "Glass", routing: "serial",
  global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [blockOf("delay:tape", "d1")],
};

const session = {
  status: "connected" as "connected" | "disconnected",
  baseUrl: "http://192.168.88.12:8080",
  current: { location: { bank: 0, slot: 0 }, preset: structuredClone(scenePreset), exists: true },
  device: {
    active: { bank: 0, slot: 0, generation: 42, storedRevisionMatches: true, liveSceneId: "verse" },
    capabilities: { sceneRecall: true },
  },
  models: [], irs: [], reverbIrs: [], presets: [], busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(async (_saved: Preset) => ({ bank: 0, slot: 0, preset: scenePreset })),
  applyCurrent: vi.fn(async () => { throw new Error("DSP preparation failed"); }),
  refreshPresets: vi.fn(async () => undefined),
  selectLocation: vi.fn(async () => undefined),
  recallScene: vi.fn(async () => true),
};

vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

function AppProviders({ children }: { children: ReactNode }) {
  return <EditorProvider>{children}</EditorProvider>;
}

const stage = (onConnection = vi.fn()) => <StageWorkspace onManageFiles={vi.fn()} onConnection={onConnection} />;
const lastSaved = () => session.saveCurrent.mock.lastCall?.[0] as Preset;

beforeEach(() => {
  vi.clearAllMocks();
  session.status = "connected";
  session.current = { location: { bank: 0, slot: 0 }, preset: structuredClone(scenePreset), exists: true };
});

describe("StageWorkspace scene save and load semantics", () => {
  it("reports when save succeeds but the pedal keeps playing the previous revision", async () => {
    render(<AppProviders>{stage()}</AppProviders>);
    await userEvent.type(screen.getByRole("textbox", { name: "Preset name" }), " II");
    await userEvent.click(screen.getByRole("button", { name: "Save and load" }));
    expect(await screen.findByText(/Saved; pedal still playing the previous version/)).toHaveTextContent("DSP preparation failed");
    expect(session.saveCurrent).toHaveBeenCalled();
    expect(session.applyCurrent).toHaveBeenCalledWith("verse");
  });

  it("shows physically formatted preset values in the shared comparison", async () => {
    render(<AppProviders>{stage()}</AppProviders>);
    await userEvent.click(screen.getByRole("button", { name: "Scenes" }));
    await userEvent.click(screen.getByRole("button", { name: "Compare" }));
    await userEvent.click(screen.getByRole("button", { name: "Shared" }));
    expect(screen.getByText("Preset · output gain")).toBeInTheDocument();
    expect(screen.getByText("Preset · safety limit").nextElementSibling).toHaveTextContent("-1.0 dB");
  });

  it("lets a normal preset start scene authoring in the Manager", async () => {
    session.current.preset = { ...structuredClone(scenePreset), version: 1, sceneSet: undefined };
    render(<AppProviders>{stage()}</AppProviders>);
    await userEvent.click(screen.getByRole("button", { name: "Create scenes" }));
    await userEvent.click(screen.getByRole("button", { name: "Create four scenes" }));
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
    render(<AppProviders>{stage()}</AppProviders>);
    await userEvent.click(screen.getByRole("button", { name: "Global" }));
    const input = screen.getByRole("slider", { name: "Input gain" });
    expect(input).toHaveAttribute("aria-valuenow", "-6");
    act(() => input.focus());
    await userEvent.keyboard("{ArrowRight}");
    await userEvent.click(screen.getByRole("button", { name: /^Save$/ }));
    const saved = lastSaved();
    expect(saved.global.inputGainDb).toBe(0);
    expect(saved.sceneSet?.scenes[0].targets[0]).toMatchObject({ value: -5.5 });
    expect(saved.sceneSet?.scenes[1].targets[0]).toMatchObject({ value: -6 });
  });

  it("keeps the drawer, the scene and the undo history after Save", async () => {
    session.current.preset.blocks = [blockOf("delay:tape", "d1")];
    session.saveCurrent.mockImplementationOnce(async (saved: Preset) => {
      session.current = { ...session.current, preset: structuredClone(saved) };
      return { bank: 0, slot: 0, preset: structuredClone(saved) };
    });
    render(<AppProviders>{stage()}</AppProviders>);
    await userEvent.click(screen.getByRole("group", { name: /Tape Delay/ }));
    await userEvent.click(screen.getByRole("button", { name: "2 Chorus" }));
    act(() => screen.getByRole("slider", { name: "Mix" }).focus());
    await userEvent.keyboard("{ArrowRight}");
    await userEvent.click(screen.getByRole("button", { name: /^Save$/ }));
    expect(session.saveCurrent).toHaveBeenCalledTimes(1);
    expect(screen.getByRole("region", { name: "Tape Delay parameters" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Undo" })).toBeEnabled();
    expect(screen.getByRole("button", { name: "2 Chorus" })).toHaveAttribute("aria-pressed", "true");
  });

  it("undoes the first scene BLOCK ON edit in one step", async () => {
    session.current.preset.blocks = [blockOf("delay:tape", "d1")];
    const { editor } = renderWithEditor(stage());
    await userEvent.click(screen.getByRole("group", { name: /Tape Delay/ }));
    await userEvent.click(screen.getByRole("button", { name: "2 Chorus" }));
    await userEvent.click(screen.getByRole("button", { name: "Block on" }));
    expect(editor().present.sceneSet!.scenes[1].targets).toEqual([{ target: "blockEnabled", blockId: "d1", value: false }]);
    await userEvent.click(screen.getByRole("button", { name: "Undo" }));
    expect(editor().present.sceneSet!.scenes.flatMap(({ targets }) => targets)).toEqual([]);
  });

  it("undoes the first scene input gain edit in one step", async () => {
    const { editor } = renderWithEditor(stage());
    await userEvent.click(screen.getByRole("button", { name: "Global" }));
    act(() => screen.getByRole("slider", { name: "Input gain" }).focus());
    await userEvent.keyboard("{ArrowRight}");
    expect(editor().present.sceneSet!.scenes[0].targets).toEqual([{ target: "inputGainDb", value: 0.5 }]);
    await userEvent.click(screen.getByRole("button", { name: "Undo" }));
    expect(editor().present.sceneSet!.scenes.flatMap(({ targets }) => targets)).toEqual([]);
  });

  it("shows and toggles the selected scene's bypass state on the chain stage", async () => {
    const block = createBlockFromDefinition("mod:chorus", []);
    session.current.preset.blocks = [block];
    for (const scene of session.current.preset.sceneSet!.scenes) {
      scene.targets = [{ target: "blockEnabled", blockId: block.id, value: false }];
    }
    render(<AppProviders>{stage()}</AppProviders>);
    const chain = screen.getByRole("region", { name: "Signal chain" });
    expect(within(chain).getByRole("group", { name: /Chorus, .*bypassed/ })).toBeInTheDocument();
    await userEvent.click(within(chain).getByRole("button", { name: "Turn on Chorus" }));
    await userEvent.click(screen.getByRole("button", { name: /^Save$/ }));
    const saved = lastSaved();
    expect(saved.blocks[0].enabled).toBe(true);
    expect(saved.sceneSet?.scenes[0].targets[0]).toMatchObject({ value: true });
    expect(saved.sceneSet?.scenes[1].targets[0]).toMatchObject({ value: false });
  });
});

describe("StageWorkspace stage and drawer", () => {
  beforeEach(() => { session.current.preset = structuredClone(delayPreset); });

  it("keeps an unsaved edit when the Assets view opens and closes", async () => {
    const { rerender } = render(<AppProviders><StageWorkspace onManageFiles={vi.fn()} onConnection={vi.fn()} /></AppProviders>);
    await userEvent.click(screen.getByRole("group", { name: /Tape Delay/ }));
    await userEvent.type(screen.getByRole("slider", { name: "Mix" }), "{ArrowRight}");
    rerender(<AppProviders><p>Assets</p></AppProviders>);
    rerender(<AppProviders><StageWorkspace onManageFiles={vi.fn()} onConnection={vi.fn()} /></AppProviders>);
    expect(screen.getByText("MODIFIED")).toBeInTheDocument();
  });

  it("opens the drawer on a card and folds the chain into chips", async () => {
    render(<AppProviders><StageWorkspace onManageFiles={vi.fn()} onConnection={vi.fn()} /></AppProviders>);
    await userEvent.click(screen.getByRole("group", { name: /Tape Delay/ }));
    expect(screen.getByRole("region", { name: "Tape Delay parameters" })).toBeInTheDocument();
    expect(screen.queryByRole("region", { name: "Signal chain" })).not.toBeInTheDocument();
    await userEvent.keyboard("{Escape}");
    expect(screen.getByRole("region", { name: "Signal chain" })).toBeInTheDocument();
  });

  it("shows the connect card while offline", async () => {
    session.status = "disconnected";
    const onConnection = vi.fn();
    render(<AppProviders>{stage(onConnection)}</AppProviders>);
    expect(screen.getByRole("heading", { name: "Connect to your pedal" })).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "Connect to device" }));
    expect(onConnection).toHaveBeenCalled();
  });

  it("counts the blocks and renames the preset", async () => {
    const { editor } = renderWithEditor(stage());
    expect(screen.getByText("1 block")).toBeInTheDocument();
    await userEvent.clear(screen.getByRole("textbox", { name: "Preset name" }));
    await userEvent.type(screen.getByRole("textbox", { name: "Preset name" }), "Night");
    expect(editor().present.name).toBe("Night");
  });

  it("adds a block from the module drawer after the selected block", async () => {
    const { editor } = renderWithEditor(stage());
    await userEvent.click(screen.getByRole("group", { name: /Tape Delay/ }));
    await userEvent.keyboard("a");
    const dialog = screen.getByRole("dialog", { name: "Add block" });
    expect(within(dialog).getByText("Insert after Tape Delay, position 2")).toBeInTheDocument();
    await userEvent.type(within(dialog).getByRole("searchbox", { name: "Search blocks" }), "Chorus{Enter}");
    expect(editor().present.blocks.map(({ type }) => type)).toEqual(["delay", "mod"]);
    expect(screen.queryByRole("dialog", { name: "Add block" })).not.toBeInTheDocument();
  });

  it("disables insert points once the serial chain holds ten blocks", () => {
    session.current.preset.blocks = Array.from({ length: 10 }, (_, i) => blockOf("mod:chorus", `m${i}`));
    render(<AppProviders>{stage()}</AppProviders>);
    expect(screen.getByRole("button", { name: "Add a block at position 1" })).toBeDisabled();
  });

  it("disables every add control and ignores A once the chain holds ten blocks", async () => {
    session.current.preset.blocks = Array.from({ length: 10 }, (_, i) => blockOf("mod:chorus", `m${i}`));
    render(<AppProviders>{stage()}</AppProviders>);
    const add = screen.getByRole("button", { name: "Add block" });
    expect(add).toBeDisabled();
    expect(add).toHaveAttribute("title", "The chain holds 10 blocks.");
    act(() => screen.getAllByRole("group", { name: /Chorus/ })[0].focus());
    await userEvent.keyboard("a");
    expect(screen.queryByRole("dialog", { name: "Add block" })).not.toBeInTheDocument();
    await userEvent.click(screen.getAllByRole("group", { name: /Chorus/ })[0]);
    const chipAdd = within(screen.getByRole("navigation", { name: "Signal chain" })).getByRole("button", { name: "Add a block at the end" });
    expect(chipAdd).toBeDisabled();
    expect(chipAdd).toHaveAttribute("title", "The chain holds 10 blocks.");
    await userEvent.keyboard("a");
    expect(screen.queryByRole("dialog", { name: "Add block" })).not.toBeInTheDocument();
  });

  it("disables the insert points of a WDW lane that holds ten blocks", () => {
    session.current.preset = {
      ...structuredClone(delayPreset), version: 3, routing: "wdw", blocks: [],
      wdw: {
        dry: { enabled: true, levelDb: 0, blocks: [blockOf("dynamics:compressor", "x1")] },
        wet: { enabled: true, levelDb: 0, blocks: Array.from({ length: 10 }, (_, i) => blockOf("mod:chorus", `w${i}`)) },
      },
    } as Preset;
    render(<AppProviders>{stage()}</AppProviders>);
    expect(screen.getByRole("button", { name: "Add a block to lane WET at position 1" })).toBeDisabled();
    expect(screen.getByRole("button", { name: "Add a block to lane DRY at position 1" })).toBeEnabled();
  });

  it("keeps the WDW lanes in the chip strip while a drawer is open", async () => {
    session.current.preset = {
      ...structuredClone(delayPreset), version: 3, routing: "wdw", blocks: [],
      wdw: { dry: { enabled: true, levelDb: 0, blocks: [blockOf("dynamics:compressor", "x1")] }, wet: { enabled: true, levelDb: 0, blocks: [blockOf("delay:tape", "w1")] } },
    } as Preset;
    render(<AppProviders>{stage()}</AppProviders>);
    await userEvent.click(screen.getByRole("button", { name: "Global" }));
    const chips = screen.getByRole("navigation", { name: "Signal chain" });
    expect(within(chips).getByRole("button", { name: /Compressor/ })).toBeInTheDocument();
    expect(within(chips).getByRole("button", { name: /Tape Delay/ })).toBeInTheDocument();
  });

  it("offers the fix for a preset-level issue in the stage head", async () => {
    session.current.preset = { ...structuredClone(delayPreset), version: 4 };
    const { editor } = renderWithEditor(stage());
    expect(screen.getByText("Preset version 4 requires a scene set.")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "Create four scenes" }));
    expect(editor().present.sceneSet?.scenes).toHaveLength(4);
  });
});

describe("StageWorkspace keyboard shortcuts", () => {
  beforeEach(() => { session.current.preset = structuredClone(delayPreset); });

  it("bypasses with B, undoes with Cmd+Z, redoes with Shift+Cmd+Z and saves with Cmd+S", async () => {
    const { editor } = renderWithEditor(stage());
    await userEvent.click(screen.getByRole("group", { name: /Tape Delay/ }));
    await userEvent.keyboard("b");
    expect(editor().present.blocks[0].enabled).toBe(false);
    await userEvent.keyboard("{Meta>}z{/Meta}");
    expect(editor().present.blocks[0].enabled).toBe(true);
    await userEvent.keyboard("{Shift>}{Meta>}z{/Meta}{/Shift}");
    expect(editor().present.blocks[0].enabled).toBe(false);
    await userEvent.keyboard("{Control>}s{/Control}");
    expect(session.saveCurrent).toHaveBeenCalledWith(expect.objectContaining({ blocks: [expect.objectContaining({ enabled: false })] }));
  });

  it("removes the selected block with Delete and closes the drawer", async () => {
    const { editor } = renderWithEditor(stage());
    await userEvent.click(screen.getByRole("group", { name: /Tape Delay/ }));
    await userEvent.keyboard("{Delete}");
    expect(editor().present.blocks).toHaveLength(0);
    expect(screen.getByRole("region", { name: "Signal chain" })).toBeInTheDocument();
  });

  it("ignores letter keys on a slider but still undoes from it", async () => {
    const { editor } = renderWithEditor(stage());
    await userEvent.click(screen.getByRole("group", { name: /Tape Delay/ }));
    const mix = screen.getByRole("slider", { name: "Mix" });
    act(() => mix.focus());
    await userEvent.keyboard("{ArrowRight}");
    const edited = editor().present.blocks[0].params.mix;
    await userEvent.keyboard("b{Backspace}");
    expect(editor().present.blocks[0]).toMatchObject({ enabled: true, params: { mix: edited } });
    await userEvent.keyboard("{Meta>}z{/Meta}");
    expect(editor().present.blocks[0].params.mix).toBe(delayPreset.blocks[0].params.mix);
  });

  it("leaves text fields alone", async () => {
    const { editor } = renderWithEditor(stage());
    await userEvent.type(screen.getByRole("textbox", { name: "Preset name" }), "a");
    expect(screen.queryByRole("dialog", { name: "Add block" })).not.toBeInTheDocument();
    expect(editor().present.name).toBe("Glassa");
  });

  it("saves with Cmd+S and Ctrl+S from the preset name field, but leaves Cmd+Z to the field", async () => {
    renderWithEditor(stage());
    const name = screen.getByRole("textbox", { name: "Preset name" });
    await userEvent.type(name, " II");
    expect(fireEvent.keyDown(name, { key: "z", metaKey: true })).toBe(true);
    expect(fireEvent.keyDown(name, { key: "s", metaKey: true })).toBe(false);
    expect(session.saveCurrent).toHaveBeenCalledTimes(1);
    await act(async () => undefined);
    await userEvent.type(screen.getByRole("textbox", { name: "Preset name" }), "I");
    expect(fireEvent.keyDown(screen.getByRole("textbox", { name: "Preset name" }), { key: "s", ctrlKey: true })).toBe(false);
    expect(session.saveCurrent).toHaveBeenCalledTimes(2);
  });

  it("acts on the focused card in the overview with B, A and Delete", async () => {
    session.current.preset.blocks = [blockOf("delay:tape", "d1"), blockOf("dynamics:compressor", "c1")];
    const { editor } = renderWithEditor(stage());
    const card = () => screen.getByRole("group", { name: /Compressor/ });
    act(() => card().focus());
    await userEvent.keyboard("b");
    expect(editor().present.blocks[1].enabled).toBe(false);
    act(() => card().focus());
    await userEvent.keyboard("a");
    expect(within(screen.getByRole("dialog", { name: "Add block" })).getByText(/^Insert after Compressor/)).toBeInTheDocument();
    await userEvent.keyboard("{Escape}");
    expect(screen.queryByRole("dialog", { name: "Add block" })).not.toBeInTheDocument();
    act(() => card().focus());
    await userEvent.keyboard("{Delete}");
    expect(editor().present.blocks.map(({ id }) => id)).toEqual(["d1"]);
  });
});
