import { act, fireEvent, screen, waitFor, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { ArdorApiError } from "../api/errors";
import type { Asset, AssetUsageEntry, Preset } from "../api/types";
import { renderWithEditor } from "../stage/renderWithEditor";
import { AssetQueueProvider } from "./AssetQueue";
import { AssetsView } from "./AssetsView";

const asset = (filename: string, kind: "model" | "ir" = "model", sizeBytes = 400 * 1024, dir = "models"): Asset =>
  ({ id: filename, kind, filename, path: `${dir}/${filename}`, sizeBytes });
const preset: Preset = {
  version: 4, name: "Glass Cathedral", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [{ id: "n1", type: "nam", enabled: true, asset: "models/Clean.nam", params: {} }],
};
const usage: AssetUsageEntry[] = [
  { path: "models/Clean.nam", presets: [{ bank: 0, slot: 0, name: "Glass Cathedral" }] },
  { path: "models/Gone.nam", presets: [{ bank: 1, slot: 1, name: "Doom" }] },
];
const session = {
  status: "connected" as "connected" | "disconnected",
  current: { location: { bank: 0, slot: 0 }, preset, exists: true },
  device: { active: { bank: 0, slot: 0 }, capabilities: { tone3000: false } },
  models: [asset("Clean.nam"), asset("Plexi.nam", "model", 2 * 1024 * 1024)],
  irs: [asset("Room.wav", "ir", 100 * 1024, "irs")], reverbIrs: [asset("Hall.wav", "ir", 100 * 1024, "reverb-irs")],
  supportsReverbIrs: true, presets: [] as unknown[],
  assetUsage: usage as AssetUsageEntry[] | undefined,
  busy: { save: false, apply: false, upload: false },
  uploadAsset: vi.fn(async () => ({})),
  refreshAssets: vi.fn(async () => undefined),
  refreshAssetUsage: vi.fn(async () => undefined),
  selectLocation: vi.fn(async () => undefined),
  client: {
    renameAsset: vi.fn(),
    deleteAsset: vi.fn<(kind: string, id: string) => Promise<void>>(async () => undefined),
  },
};
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

function renderAssets(props: Partial<Parameters<typeof AssetsView>[0]> = {}) {
  const onOpenPreset = vi.fn();
  const onFilesTaken = vi.fn();
  const onBack = vi.fn();
  const view = renderWithEditor(<AssetQueueProvider><AssetsView onFilesTaken={onFilesTaken} onOpenPreset={onOpenPreset} onBack={onBack} {...props} /></AssetQueueProvider>);
  return { ...view, onOpenPreset, onFilesTaken, onBack };
}
const fileInput = () => document.querySelector('input[type="file"]') as HTMLInputElement;
const namBlockAsset = (editor: ReturnType<typeof renderAssets>["editor"]) => editor().present.blocks.find((b) => b.type === "nam")!.asset;

beforeEach(() => {
  vi.clearAllMocks();
  session.status = "connected";
  session.assetUsage = usage;
  session.supportsReverbIrs = true;
  session.device.capabilities.tone3000 = false;
  session.models = [asset("Clean.nam"), asset("Plexi.nam", "model", 2 * 1024 * 1024)];
  session.client.renameAsset.mockResolvedValue({ asset: { id: "Fresh.nam", filename: "Fresh.nam", path: "models/Fresh.nam" }, updatedPresetCount: 1 });
});

describe("AssetsView", () => {
  it("shows kind tiles, files with their presets, and a missing file row", () => {
    renderAssets();
    expect(screen.getByRole("button", { name: /NAM models/ })).toHaveAttribute("aria-pressed", "true");
    expect(screen.getByRole("button", { name: "Clean" })).toBeInTheDocument();
    expect(screen.getByText("Glass Cathedral")).toBeInTheDocument();
    expect(screen.getByText("Not used")).toBeInTheDocument();
    expect(screen.getByText(/needs/)).toHaveTextContent("Doom needs Gone.nam, which is not on the pedal.");
    expect(screen.getByText("4 files on the pedal")).toBeInTheDocument();
    expect(screen.getByText("1 missing")).toBeInTheDocument();
  });

  it("uses the copy of the mockup for search, sort and drop hint", () => {
    renderAssets();
    expect(screen.getByRole("searchbox", { name: "Find a file" })).toHaveAttribute("placeholder", "Find a file in NAM models");
    expect(screen.getByRole("button", { name: "Most used" })).toBeInTheDocument();
    expect(screen.getByText("Drop .nam files anywhere to upload")).toBeInTheDocument();
  });

  it("tries a file in the open preset as an undoable edit", async () => {
    const { editor } = renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Plexi" }));
    await userEvent.click(screen.getByRole("button", { name: /Try in/ }));
    expect(namBlockAsset(editor)).toBe("models/Plexi.nam");
    act(() => editor().dispatch({ type: "undo" }));
    expect(namBlockAsset(editor)).toBe("models/Clean.nam");
  });

  it("shows In <preset> when the block already uses the file, and explains a missing block", async () => {
    renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Clean" }));
    expect(screen.getByRole("button", { name: "In Glass Cathedral" })).toBeDisabled();
    await userEvent.click(screen.getByRole("button", { name: "Close" }));
    await userEvent.click(screen.getByRole("button", { name: /Cabinet IRs/ }));
    await userEvent.click(screen.getByRole("button", { name: "Room" }));
    expect(screen.getByRole("button", { name: /Try in/ })).toHaveAttribute("title", "Glass Cathedral has no Cabinet IR block");
  });

  it("opens the file drawer with the path, size and the presets that use it", async () => {
    const { onOpenPreset } = renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Clean" }));
    const drawer = screen.getByRole("region", { name: "Clean.nam" });
    expect(drawer).toHaveTextContent("models/Clean.nam · 400 KB");
    expect(within(drawer).getByRole("heading", { name: "Used in 1 preset" })).toBeInTheDocument();
    await userEvent.click(within(drawer).getByRole("button", { name: /Glass Cathedral, FS 1/ }));
    expect(onOpenPreset).toHaveBeenCalledWith({ bank: 0, slot: 0 });
  });

  it("says when a file is not used in a preset", async () => {
    renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Plexi" }));
    expect(screen.getByRole("heading", { name: "Not used in a preset" })).toBeInTheDocument();
  });

  it("asks in the rail before it deletes", async () => {
    renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Delete Clean.nam" }));
    const rail = screen.getByRole("alertdialog", { name: "Confirm delete" });
    expect(rail).toHaveTextContent("Delete Clean.nam from the pedal? Glass Cathedral uses it. Those presets stay saved but cannot load until you pick another file. You cannot undo this.");
    expect(rail).toHaveTextContent("You cannot undo this.");
    await userEvent.click(within(rail).getByRole("button", { name: "Delete file" }));
    expect(session.client.deleteAsset).toHaveBeenCalledWith("models", "Clean.nam");
  });

  it("names two presets with the plural verb in the delete question", async () => {
    session.assetUsage = [{ path: "models/Clean.nam", presets: [{ bank: 0, slot: 0, name: "Glass Cathedral" }, { bank: 1, slot: 0, name: "Doom" }] }];
    renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Delete Clean.nam" }));
    expect(screen.getByRole("alertdialog", { name: "Confirm delete" })).toHaveTextContent("Glass Cathedral, Doom use it.");
  });

  it("uses the singular verb for one preset when several files are deleted", async () => {
    renderAssets();
    await userEvent.click(screen.getByRole("checkbox", { name: "Select Clean.nam" }));
    await userEvent.click(screen.getByRole("checkbox", { name: "Select Plexi.nam" }));
    await userEvent.click(screen.getByRole("button", { name: "Delete 2" }));
    expect(screen.getByRole("alertdialog", { name: "Confirm delete" })).toHaveTextContent("Glass Cathedral uses them.");
  });

  it("keeps the drawer open when Escape is pressed inside a dialog", async () => {
    session.device.capabilities.tone3000 = true;
    renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Clean" }));
    await userEvent.click(screen.getByRole("button", { name: "Browse TONE3000" }));
    const dialog = await screen.findByRole("dialog");
    fireEvent.keyDown(dialog, { key: "Escape" });
    expect(screen.getByRole("region", { name: "Clean.nam" })).toBeInTheDocument();
  });

  it("cancels the delete confirmation", async () => {
    renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Delete Clean.nam" }));
    await userEvent.click(screen.getByRole("button", { name: "Cancel" }));
    expect(screen.queryByRole("alertdialog")).not.toBeInTheDocument();
    expect(session.client.deleteAsset).not.toHaveBeenCalled();
  });

  it("hides the Used in column when the pedal cannot report usage", () => {
    session.assetUsage = undefined;
    renderAssets();
    expect(screen.queryByText("Used in")).not.toBeInTheDocument();
    expect(screen.queryByText("Not used")).not.toBeInTheDocument();
  });

  it("hides reverb IR management when an older device does not support it", () => {
    session.supportsReverbIrs = false;
    renderAssets();
    expect(screen.queryByRole("button", { name: /Reverb IRs/ })).not.toBeInTheDocument();
  });

  it("keeps reverb IRs in their own kind and uploads there", async () => {
    renderAssets({ initialKind: "reverb-irs" });
    expect(screen.getByRole("button", { name: /Reverb IRs/ })).toHaveAttribute("aria-pressed", "true");
    expect(screen.getByRole("button", { name: "Hall" })).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Upload .wav" })).toBeInTheDocument();
    await userEvent.upload(fileInput(), new File(["room"], "new-room.wav", { type: "audio/wav" }));
    await screen.findByRole("status");
    expect(session.uploadAsset).toHaveBeenCalledWith("reverb-irs", expect.any(File), false);
  });

  it("uploads from the file picker and reports it", async () => {
    renderAssets();
    await userEvent.upload(fileInput(), new File(["x"], "New.nam"));
    expect(await screen.findByRole("status")).toHaveTextContent("New.nam uploaded to NAM models.");
    expect(session.uploadAsset).toHaveBeenCalledWith("models", expect.objectContaining({ name: "New.nam" }), false);
    expect(session.refreshAssets).toHaveBeenCalledWith("models");
  });

  it("rejects a file that is neither .nam nor .wav", async () => {
    renderAssets();
    await userEvent.upload(fileInput(), new File(["x"], "notes.txt"), { applyAccept: false });
    const row = await screen.findByRole("alert");
    expect(row).toHaveTextContent("notes.txt is not a .nam or .wav file. The pedal takes NAM models and WAV impulse responses.");
    expect(session.uploadAsset).not.toHaveBeenCalled();
    await userEvent.click(within(row).getByRole("button", { name: "Dismiss" }));
    expect(screen.queryByRole("alert")).not.toBeInTheDocument();
  });

  it("offers Replace or Skip for a conflict and continues with the next file", async () => {
    renderAssets();
    await userEvent.upload(fileInput(), [new File(["x"], "Clean.nam"), new File(["x"], "Next.nam")]);
    const row = await screen.findByText(/is already on the pedal/);
    expect(row).toHaveTextContent("Clean.nam is already on the pedal. Replace it? Presets that use it get the new file.");
    await waitFor(() => expect(session.uploadAsset).toHaveBeenCalledWith("models", expect.objectContaining({ name: "Next.nam" }), false));
    await userEvent.click(screen.getByRole("button", { name: "Replace" }));
    await waitFor(() => expect(session.uploadAsset).toHaveBeenCalledWith("models", expect.objectContaining({ name: "Clean.nam" }), true));
  });

  it("shows the conflicts of every kind, each with its kind", async () => {
    renderAssets();
    await userEvent.upload(fileInput(), [new File(["x"], "Clean.nam"), new File(["x"], "Room.wav")], { applyAccept: false });
    const rows = await screen.findAllByRole("alert");
    expect(rows).toHaveLength(2);
    expect(rows[0]).toHaveTextContent("NAM models");
    expect(rows[0]).toHaveTextContent("Clean.nam is already on the pedal.");
    expect(rows[1]).toHaveTextContent("Cabinet IRs");
    expect(rows[1]).toHaveTextContent("Room.wav is already on the pedal.");
  });

  it("shows a failed upload of another kind with its kind", async () => {
    session.uploadAsset.mockRejectedValueOnce(new Error("Disk full"));
    renderAssets();
    await userEvent.upload(fileInput(), [new File(["x"], "Hall2.wav")], { applyAccept: false });
    await userEvent.click(screen.getByRole("button", { name: /NAM models/ }));
    expect(await screen.findByText(/could not be uploaded/)).toBeInTheDocument();
    expect(screen.getByText(/could not be uploaded/).closest(".aq")).toHaveTextContent("Cabinet IRs");
  });

  it("skips a conflicting file", async () => {
    renderAssets();
    await userEvent.upload(fileInput(), new File(["x"], "Clean.nam"));
    await userEvent.click(await screen.findByRole("button", { name: "Skip" }));
    expect(session.uploadAsset).not.toHaveBeenCalled();
    expect(screen.queryByText(/is already on the pedal/)).not.toBeInTheDocument();
  });

  it("shows a running upload with an indeterminate bar", async () => {
    let finish: () => void = () => undefined;
    session.uploadAsset.mockImplementationOnce(() => new Promise((resolve) => { finish = () => resolve({}); }));
    renderAssets();
    await userEvent.upload(fileInput(), new File(["x"], "Slow.nam"));
    expect(await screen.findByText("Slow.nam")).toBeInTheDocument();
    expect(document.querySelector(".aq__bar i")).not.toBeNull();
    await act(async () => { finish(); });
    await screen.findByRole("status");
  });

  it("reports an upload the pedal refused and lets the person dismiss it", async () => {
    session.uploadAsset.mockRejectedValueOnce(new ArdorApiError(500, "disk_full", "Disk full"));
    renderAssets();
    await userEvent.upload(fileInput(), new File(["x"], "Big.nam"));
    expect(await screen.findByText(/could not be uploaded/)).toHaveTextContent("Big.nam could not be uploaded.");
    expect(screen.getByText("Disk full")).toBeInTheDocument();
  });

  it("replaces a file from the drawer under its own name", async () => {
    renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Plexi" }));
    await userEvent.click(screen.getByRole("button", { name: "Replace file" }));
    await userEvent.upload(fileInput(), new File(["y"], "whatever.nam"));
    await waitFor(() => expect(session.uploadAsset).toHaveBeenCalledWith("models", expect.objectContaining({ name: "Plexi.nam" }), true));
  });

  it("uploads dropped files handed over once, then reports them taken", async () => {
    const { onFilesTaken } = renderAssets({ pendingFiles: [new File(["x"], "Dropped.nam")] });
    await waitFor(() => expect(session.uploadAsset).toHaveBeenCalledWith("models", expect.objectContaining({ name: "Dropped.nam" }), false));
    expect(session.uploadAsset).toHaveBeenCalledTimes(1);
    expect(onFilesTaken).toHaveBeenCalledTimes(1);
    await screen.findByRole("status");
  });

  it("renames in the drawer, follows the new name and reports the presets", async () => {
    renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Plexi" }));
    await userEvent.click(within(screen.getByRole("region", { name: "Plexi.nam" })).getByRole("button", { name: "Rename" }));
    const form = screen.getByRole("form", { name: "Rename file" });
    expect(form).toHaveTextContent("Keep the .nam ending. No preset uses this file.");
    const input = within(form).getByRole("textbox", { name: "New file name" });
    await userEvent.clear(input);
    await userEvent.type(input, "Fresh.nam");
    await userEvent.click(within(form).getByRole("button", { name: "Rename" }));
    expect(session.client.renameAsset).toHaveBeenCalledWith("models", "Plexi.nam", "Fresh.nam");
    expect(await screen.findByRole("status")).toHaveTextContent("Renamed to Fresh.nam. 1 saved preset uses the new name.");
    expect(screen.queryByRole("form", { name: "Rename file" })).not.toBeInTheDocument();
  });

  it("shows a rename error next to the field", async () => {
    renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Rename Plexi.nam" }));
    const input = screen.getByRole("textbox", { name: "New file name" });
    await userEvent.clear(input);
    await userEvent.type(input, "Plexi");
    await userEvent.click(within(screen.getByRole("form", { name: "Rename file" })).getByRole("button", { name: "Rename" }));
    expect(screen.getByRole("form", { name: "Rename file" })).toHaveTextContent("The name must end in .nam.");
    expect(session.client.renameAsset).not.toHaveBeenCalled();
  });

  it("deletes every checked file and names the ones that failed", async () => {
    session.client.deleteAsset.mockImplementation(async (_kind: string, id: string) => { if (id === "Plexi.nam") throw new Error("device busy"); });
    renderAssets();
    await userEvent.click(screen.getByRole("checkbox", { name: "Select Clean.nam" }));
    await userEvent.click(screen.getByRole("checkbox", { name: "Select Plexi.nam" }));
    expect(screen.getByText("2 selected")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "Delete 2" }));
    const rail = screen.getByRole("alertdialog", { name: "Confirm delete" });
    expect(rail).toHaveTextContent("Delete 2 files from the pedal?");
    await userEvent.click(within(rail).getByRole("button", { name: "Delete 2 files" }));
    expect(session.client.deleteAsset.mock.calls).toEqual([["models", "Clean.nam"], ["models", "Plexi.nam"]]);
    expect(await screen.findByRole("alert")).toHaveTextContent("Could not delete Plexi.nam.");
    session.client.deleteAsset.mockImplementation(async () => undefined);
  });

  it("selects and clears every visible file with the select-all box", async () => {
    renderAssets();
    await userEvent.click(screen.getByRole("checkbox", { name: "Select all files" }));
    expect(screen.getByRole("checkbox", { name: "Select Clean.nam" })).toBeChecked();
    expect(screen.getByRole("checkbox", { name: "Select Plexi.nam" })).toBeChecked();
    await userEvent.click(screen.getByRole("button", { name: "Clear" }));
    expect(screen.queryByText("2 selected")).not.toBeInTheDocument();
  });

  it("filters by name and says when nothing matches", async () => {
    renderAssets();
    await userEvent.type(screen.getByRole("searchbox", { name: "Find a file" }), "plex");
    expect(screen.queryByRole("button", { name: "Clean" })).not.toBeInTheDocument();
    await userEvent.type(screen.getByRole("searchbox", { name: "Find a file" }), "zz");
    expect(screen.getByRole("heading", { name: "No file matches “plexzz”" })).toBeInTheDocument();
  });

  it("offers Upload and Pick another for a missing file", async () => {
    const { onOpenPreset } = renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Pick another" }));
    expect(onOpenPreset).toHaveBeenCalledWith({ bank: 1, slot: 1 });
    const click = vi.spyOn(fileInput(), "click").mockImplementation(() => undefined);
    await userEvent.click(screen.getByRole("button", { name: "Upload" }));
    expect(click).toHaveBeenCalled();
  });

  it("offers TONE3000 only for models on a device that supports it", async () => {
    session.device.capabilities.tone3000 = true;
    renderAssets();
    expect(screen.getByRole("button", { name: "Browse TONE3000" })).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: /Cabinet IRs/ }));
    expect(screen.queryByRole("button", { name: "Browse TONE3000" })).not.toBeInTheDocument();
  });

  it("hides TONE3000 when the device does not support it", () => {
    renderAssets();
    expect(screen.queryByRole("button", { name: "Browse TONE3000" })).not.toBeInTheDocument();
  });

  it("goes back to the editor", async () => {
    const { onBack } = renderAssets();
    await userEvent.click(screen.getByRole("button", { name: "Back to edit" }));
    expect(onBack).toHaveBeenCalled();
  });

  it("asks to connect when the pedal is offline", () => {
    session.status = "disconnected";
    renderAssets();
    expect(screen.getByRole("heading", { name: "Connect to manage files" })).toBeInTheDocument();
  });

  it("says files dropped while offline were not uploaded", async () => {
    session.status = "disconnected";
    const { onFilesTaken } = renderAssets({ pendingFiles: [new File(["x"], "Dropped.nam")] });
    expect(await screen.findByRole("status")).toHaveTextContent("Connect to the pedal to upload files.");
    expect(onFilesTaken).toHaveBeenCalledTimes(1);
    expect(session.uploadAsset).not.toHaveBeenCalled();
  });
});
