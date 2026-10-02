import { act, render, renderHook, waitFor } from "@testing-library/react";
import type { ReactNode } from "react";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { Asset, Preset } from "../api/types";
import { EditorProvider, usePresetEditorContext } from "../presets/editor/EditorContext";
import { AssetQueueProvider } from "./AssetQueue";
import { useAssetLibrary, type AssetLibrary } from "./useAssetLibrary";

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

const wrapper = ({ children }: { children: ReactNode }) => <EditorProvider><AssetQueueProvider>{children}</AssetQueueProvider></EditorProvider>;
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

  it("refreshes the open kind after a bulk delete", async () => {
    const { result } = renderLibrary();
    act(() => result.current.library.askDelete(session.models.map(({ id }) => id)));
    await act(() => result.current.library.deleteChecked());
    expect(session.client.deleteAsset).toHaveBeenCalledTimes(2);
    expect(session.refreshAssets).toHaveBeenCalledWith("models");
    expect(session.refreshAssetUsage).toHaveBeenCalled();
    expect(result.current.library.notice).toBe("2 files deleted from the pedal.");
  });

  it("refreshes the kind after Replace", async () => {
    const { result } = renderLibrary();
    act(() => result.current.library.replaceFile(session.models[1], new File(["y"], "picked.nam")));
    await waitFor(() => expect(session.refreshAssets).toHaveBeenCalledWith("models"));
    expect(session.uploadAsset).toHaveBeenCalledWith("models", expect.objectContaining({ name: "Clean.nam" }), true);
  });

  it("names the files it could not delete", async () => {
    session.client.deleteAsset.mockRejectedValueOnce(new Error("busy"));
    const { result } = renderLibrary();
    act(() => result.current.library.askDelete(["Clean.nam"]));
    await act(() => result.current.library.deleteChecked());
    expect(result.current.library.error).toBe("Could not delete Clean.nam.");
  });
  it("follows a rename in the draft and keeps the notice when the refresh fails afterwards", async () => {
    session.refreshAssets.mockRejectedValueOnce(new Error("List unavailable"));
    const { result } = renderLibrary();
    act(() => result.current.editor.dispatch({ type: "set-block-param", blockId: "g1", key: "mix", value: 0.5 }));
    let outcome: string | undefined = "unset";
    await act(async () => { outcome = await result.current.library.rename(session.models[1], "Clean v2.nam"); });
    expect(outcome).toBeUndefined();
    expect(result.current.editor.present.blocks[0].asset).toBe("models/Clean v2.nam");
    expect(result.current.library.notice).toBe("Renamed to Clean v2.nam. 2 saved presets use the new name.");
    expect(result.current.library.error).toBe("List unavailable");
  });

  it("keeps the open file open under its new name", async () => {
    session.client.renameAsset.mockResolvedValueOnce({ asset: { id: "Clean v2.nam", filename: "Clean v2.nam", path: "models/Clean v2.nam" }, updatedPresetCount: 0 });
    const { result } = renderLibrary();
    act(() => result.current.library.setOpenId("Clean.nam"));
    await act(async () => { await result.current.library.rename(session.models[1], "Clean v2.nam"); });
    expect(result.current.library.openId).toBe("Clean v2.nam");
  });

  it("does not mark an upload as failed when only the refresh fails", async () => {
    session.refreshAssets.mockRejectedValueOnce(new Error("List unavailable"));
    const { result } = renderLibrary();
    act(() => result.current.library.enqueue([new File(["x"], "New.nam")]));
    await waitFor(() => expect(result.current.library.error).toBe("List unavailable"));
    expect(result.current.library.queue).toHaveLength(0);
    expect(result.current.library.notice).toBe("New.nam uploaded to NAM models.");
  });

  it("closes the drawer when its file is deleted", async () => {
    const { result } = renderLibrary();
    act(() => result.current.library.setOpenId("Brown Sound.nam"));
    act(() => result.current.library.askDelete(["Brown Sound.nam"]));
    await act(() => result.current.library.deleteChecked());
    expect(result.current.library.openId).toBeUndefined();
  });

  it("keeps the drawer open when the delete of its file failed", async () => {
    session.client.deleteAsset.mockRejectedValueOnce(new Error("busy"));
    const { result } = renderLibrary();
    act(() => result.current.library.setOpenId("Brown Sound.nam"));
    act(() => result.current.library.askDelete(["Brown Sound.nam"]));
    await act(() => result.current.library.deleteChecked());
    expect(result.current.library.openId).toBe("Brown Sound.nam");
  });

  it("keeps the partial delete failure when the refresh after it also fails", async () => {
    session.client.deleteAsset.mockRejectedValueOnce(new Error("busy"));
    session.refreshAssets.mockRejectedValueOnce(new Error("List unavailable"));
    const { result } = renderLibrary();
    act(() => result.current.library.askDelete(["Brown Sound.nam"]));
    await act(() => result.current.library.deleteChecked());
    expect(result.current.library.error).toBe("Could not delete Brown Sound.nam. List unavailable");
  });

  it("clears the selection and withdraws a pending delete", () => {
    const { result } = renderLibrary();
    act(() => result.current.library.askDelete(["Clean.nam"]));
    act(() => result.current.library.clearChecked());
    expect(result.current.library.checked.size).toBe(0);
    expect(result.current.library.confirmDelete).toBe(false);
    act(() => result.current.library.askDelete(["Clean.nam"]));
    act(() => result.current.library.toggleChecked("Brown Sound.nam"));
    expect(result.current.library.confirmDelete).toBe(false);
  });

  it("announces a notice from outside, such as a TONE3000 install", () => {
    const { result } = renderLibrary();
    act(() => result.current.library.announce("Amp installed."));
    expect(result.current.library.notice).toBe("Amp installed.");
  });

  it("keeps uploading and keeps parked conflicts when the Assets view closes", async () => {
    let release = () => undefined as void;
    session.uploadAsset.mockImplementationOnce(() => new Promise((resolve) => { release = () => resolve({}); }));
    const ref: { current?: AssetLibrary } = {};
    function Library() { ref.current = useAssetLibrary("models"); return null; }
    const Harness = ({ open }: { open: boolean }) => (open ? <Library /> : null);
    const { rerender } = render(<Harness open />, { wrapper });
    act(() => ref.current!.enqueue([new File(["x"], "A.nam"), new File(["x"], "B.nam"), new File(["x"], "Clean.nam")]));
    await waitFor(() => expect(session.uploadAsset).toHaveBeenCalledTimes(1));
    rerender(<Harness open={false} />);
    await act(async () => release());
    await waitFor(() => expect(session.uploadAsset).toHaveBeenCalledWith("models", expect.objectContaining({ name: "B.nam" }), false));
    rerender(<Harness open />);
    await waitFor(() => expect(ref.current!.queue.map(({ file, state }) => [file.name, state])).toEqual([["Clean.nam", "conflict"]]));
  });
});
