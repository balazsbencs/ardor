import { act, renderHook, waitFor } from "@testing-library/react";
import type { ReactNode } from "react";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { Asset, Preset } from "../api/types";
import { EditorProvider, usePresetEditorContext } from "../presets/editor/EditorContext";
import { useAssetLibrary } from "./useAssetLibrary";

const asset = (filename: string, sizeBytes = 1): Asset => ({ id: filename, kind: "model", filename, path: `models/${filename}`, sizeBytes });
const preset: Preset = {
  version: 4, name: "Clean", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [
    { id: "n1", type: "nam", enabled: true, asset: "models/Clean.nam", params: {} },
    { id: "d1", type: "dualAmp", enabled: true, asset: "", params: { leftNamAsset: "models/Clean.nam", rightNamAsset: "models/Other.nam" } },
    { id: "g1", type: "delay", enabled: true, asset: "", params: { mix: 0.2 } },
  ],
};
const session = {
  status: "connected" as const,
  current: { location: { bank: 0, slot: 0 }, preset, exists: true },
  device: { active: { bank: 0, slot: 0 }, capabilities: {} },
  models: [asset("Brown Sound.nam"), asset("Clean.nam", 5)], irs: [], reverbIrs: [], presets: [],
  assetUsage: [{ path: "models/Clean.nam", presets: [{ bank: 0, slot: 0, name: "Clean" }] }],
  busy: { save: false, apply: false, upload: false },
  uploadAsset: vi.fn(async () => ({})),
  refreshAssets: vi.fn(async () => undefined),
  refreshAssetUsage: vi.fn(async () => undefined),
  selectLocation: vi.fn(async () => undefined),
  client: {
    renameAsset: vi.fn(),
    deleteAsset: vi.fn(async () => undefined),
  },
};
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

const wrapper = ({ children }: { children: ReactNode }) => <EditorProvider>{children}</EditorProvider>;
const renderLibrary = () => renderHook(() => ({ library: useAssetLibrary("models"), editor: usePresetEditorContext() }), { wrapper });

beforeEach(() => {
  vi.clearAllMocks();
  session.client.renameAsset.mockResolvedValue({ asset: { filename: "Clean v2.nam", path: "models/Clean v2.nam" }, updatedPresetCount: 2 });
});

describe("useAssetLibrary", () => {
  it("uploads new files one by one and waits on a conflict", async () => {
    const { result } = renderLibrary();
    act(() => result.current.library.enqueue([new File(["x"], "Brown Sound.nam"), new File(["x"], "New.nam")]));
    await waitFor(() => expect(session.uploadAsset).toHaveBeenCalledWith("models", expect.objectContaining({ name: "New.nam" }), false));
    await waitFor(() => expect(result.current.library.queue).toHaveLength(1));
    expect(session.uploadAsset).toHaveBeenCalledTimes(1);
    expect(result.current.library.notice).toBe("New.nam uploaded to NAM models.");
    act(() => result.current.library.resolve(result.current.library.queue[0].id, "replace"));
    await waitFor(() => expect(session.uploadAsset).toHaveBeenCalledWith("models", expect.objectContaining({ name: "Brown Sound.nam" }), true));
  });

  it("keeps a failed upload as a rejection and reports the message", async () => {
    session.uploadAsset.mockRejectedValueOnce(new Error("Disk full"));
    const { result } = renderLibrary();
    act(() => result.current.library.enqueue([new File(["x"], "New.nam")]));
    await waitFor(() => expect(result.current.library.error).toBe("Disk full"));
    expect(result.current.library.queue[0].state).toBe("rejected");
  });

  it("replaces a file under its own name without asking", async () => {
    const { result } = renderLibrary();
    act(() => result.current.library.replaceFile(session.models[0], new File(["y"], "other.nam")));
    await waitFor(() => expect(session.uploadAsset).toHaveBeenCalledWith("models", expect.objectContaining({ name: "Brown Sound.nam" }), true));
  });

  it("searches and sorts the visible files", () => {
    const { result } = renderLibrary();
    act(() => result.current.library.setSort("size"));
    expect(result.current.library.visible.map((a) => a.filename)).toEqual(["Clean.nam", "Brown Sound.nam"]);
    act(() => result.current.library.setQuery("brown"));
    expect(result.current.library.visible.map((a) => a.filename)).toEqual(["Brown Sound.nam"]);
    act(() => result.current.library.setSort("used"));
    act(() => result.current.library.setQuery(""));
    expect(result.current.library.visible[0].filename).toBe("Clean.nam");
  });

  it("checks files and toggles all", () => {
    const { result } = renderLibrary();
    act(() => result.current.library.toggleChecked("Clean.nam"));
    expect([...result.current.library.checked]).toEqual(["Clean.nam"]);
    act(() => result.current.library.toggleAll());
    expect(result.current.library.checked.size).toBe(2);
    act(() => result.current.library.toggleAll());
    expect(result.current.library.checked.size).toBe(0);
  });

  it("renames, reports the updated presets and reloads a clean preset", async () => {
    const { result } = renderLibrary();
    await act(async () => { await result.current.library.rename(session.models[1], "Clean v2.nam"); });
    expect(result.current.library.notice).toBe("Renamed to Clean v2.nam. 2 saved presets use the new name.");
    expect(session.selectLocation).toHaveBeenCalledWith({ bank: 0, slot: 0 });
  });

  it("uses the singular for one preset and no clause for none", async () => {
    const { result } = renderLibrary();
    session.client.renameAsset.mockResolvedValueOnce({ asset: { filename: "A.nam", path: "models/A.nam" }, updatedPresetCount: 1 });
    await act(async () => { await result.current.library.rename(session.models[1], "A.nam"); });
    expect(result.current.library.notice).toBe("Renamed to A.nam. 1 saved preset uses the new name.");
    session.client.renameAsset.mockResolvedValueOnce({ asset: { filename: "B.nam", path: "models/B.nam" }, updatedPresetCount: 0 });
    await act(async () => { await result.current.library.rename(session.models[1], "B.nam"); });
    expect(result.current.library.notice).toBe("Renamed to B.nam.");
  });

  it("keeps an unsaved draft and follows the rename in it", async () => {
    const { result } = renderLibrary();
    act(() => result.current.editor.dispatch({ type: "set-block-param", blockId: "g1", key: "mix", value: 0.5 }));
    expect(result.current.editor.dirty).toBe(true);
    await act(async () => { await result.current.library.rename(session.models[1], "Clean v2.nam"); });
    expect(session.selectLocation).not.toHaveBeenCalled();
    const blocks = result.current.editor.present.blocks;
    expect(blocks[0].asset).toBe("models/Clean v2.nam");
    expect(blocks[1].params).toMatchObject({ leftNamAsset: "models/Clean v2.nam", rightNamAsset: "models/Other.nam" });
    expect(blocks[2].params.mix).toBe(0.5);
  });

  it("refuses a name without the right ending", async () => {
    const { result } = renderLibrary();
    let error: string | undefined;
    await act(async () => { error = await result.current.library.rename(session.models[1], "Clean v2"); });
    expect(error).toBe("The name must end in .nam.");
    await act(async () => { error = await result.current.library.rename(session.models[1], "  "); });
    expect(error).toBe("Type a file name.");
    expect(session.client.renameAsset).not.toHaveBeenCalled();
  });

  it("deletes the checked files after the confirmation", async () => {
    const { result } = renderLibrary();
    act(() => result.current.library.askDelete([session.models[0].id]));
    expect(result.current.library.confirmDelete).toBe(true);
    await act(() => result.current.library.deleteChecked());
    expect(session.client.deleteAsset).toHaveBeenCalledWith("models", session.models[0].id);
    expect(result.current.library.confirmDelete).toBe(false);
    expect(result.current.library.notice).toBe("Brown Sound.nam deleted from the pedal.");
  });

  it("names the files it could not delete", async () => {
    session.client.deleteAsset.mockRejectedValueOnce(new Error("busy"));
    const { result } = renderLibrary();
    act(() => result.current.library.askDelete(["Clean.nam"]));
    await act(() => result.current.library.deleteChecked());
    expect(result.current.library.error).toBe("Could not delete Clean.nam.");
  });
});
