import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { ArdorApiError } from "../api/errors";
import { ArdorApiClient } from "../api/client";
import { DeviceSessionContext, type DeviceSessionValue } from "../connection/deviceSession";
import { renderWithProviders } from "../test/render";
import { AssetLibrary } from "./AssetLibrary";

function sessionWith(overrides: Partial<DeviceSessionValue>): DeviceSessionValue {
  return {
    status: "connected", baseUrl: "http://pedal", models: [], irs: [], reverbIrs: [], presets: [], needsTokenFocus: false,
    busy: { save: false, apply: false, upload: false }, connect: async () => undefined, disconnect: () => undefined,
    selectLocation: async () => undefined, refreshAssets: async () => undefined, refreshPresets: async () => undefined,
    saveCurrent: async () => undefined, applyCurrent: async () => undefined, uploadAsset: async () => undefined,
    ...overrides,
  };
}

describe("AssetLibrary", () => {
  it("keeps reverb IRs in their own management section", async () => {
    const user = userEvent.setup();
    const uploadAsset = vi.fn().mockResolvedValue({ id: "room.wav", kind: "ir", filename: "room.wav", path: "reverb-irs/room.wav", sizeBytes: 4 });
    const session = sessionWith({
      reverbIrs: [{ id: "room.wav", kind: "ir", filename: "room.wav", path: "reverb-irs/room.wav", sizeBytes: 4 }],
      uploadAsset,
    });
    const view = renderWithProviders(<DeviceSessionContext.Provider value={session}><AssetLibrary /></DeviceSessionContext.Provider>);

    await user.click(screen.getByRole("button", { name: "Reverb IRs" }));
    expect(screen.getByRole("button", { name: "Reverb IRs" })).toHaveAttribute("aria-pressed", "true");
    expect(screen.getByText("room.wav")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Upload reverb IR" })).toBeInTheDocument();

    const fileInput = view.container.querySelector('input[type="file"]') as HTMLInputElement;
    await user.upload(fileInput, new File(["room"], "new-room.wav", { type: "audio/wav" }));
    await screen.findByRole("status");
    expect(uploadAsset).toHaveBeenCalledWith("reverb-irs", expect.any(File), false);
  });

  it("offers to replace only the conflicting file, then continues the upload queue", async () => {
    const user = userEvent.setup();
    const first = new File(["old"], "amp.nam", { type: "application/octet-stream" });
    const second = new File(["new"], "next.nam", { type: "application/octet-stream" });
    const uploadAsset = vi.fn()
      .mockRejectedValueOnce(new ArdorApiError(409, "asset_exists", "asset already exists"))
      .mockResolvedValue({ id: "amp.nam", kind: "model", filename: "amp.nam", path: "models/amp.nam", sizeBytes: 3 });
    const refreshAssets = vi.fn(async () => undefined);
    const session = sessionWith({ uploadAsset, refreshAssets });
    const view = renderWithProviders(<DeviceSessionContext.Provider value={session}><AssetLibrary /></DeviceSessionContext.Provider>);

    const input = view.container.querySelector('input[type="file"]') as HTMLInputElement;
    await user.upload(input, [first, second]);
    expect(await screen.findByRole("alertdialog", { name: "Replace amp.nam?" })).toHaveTextContent("File already exists");

    await user.click(screen.getByRole("button", { name: "Replace asset" }));
    expect(uploadAsset).toHaveBeenNthCalledWith(1, "models", first, false);
    expect(uploadAsset).toHaveBeenNthCalledWith(2, "models", first, true);
    expect(uploadAsset).toHaveBeenNthCalledWith(3, "models", second, false);
    expect(refreshAssets).toHaveBeenCalledTimes(2);
  });

  it("deletes every selected asset and reports the ones that failed", async () => {
    const user = userEvent.setup();
    const deleteAsset = vi.fn(async (_kind: string, id: string) => {
      if (id === "b.nam") throw new Error("device busy");
    });
    const refreshAssets = vi.fn(async () => undefined);
    const session = sessionWith({
      client: { deleteAsset } as unknown as ArdorApiClient,
      models: [
        { id: "a.nam", kind: "model", filename: "a.nam", path: "models/a.nam", sizeBytes: 3 },
        { id: "b.nam", kind: "model", filename: "b.nam", path: "models/b.nam", sizeBytes: 3 },
        { id: "c.nam", kind: "model", filename: "c.nam", path: "models/c.nam", sizeBytes: 3 },
      ],
      refreshAssets,
    });
    renderWithProviders(<DeviceSessionContext.Provider value={session}><AssetLibrary /></DeviceSessionContext.Provider>);

    await user.click(screen.getByRole("checkbox", { name: "Select a.nam" }));
    await user.click(screen.getByRole("checkbox", { name: "Select b.nam" }));
    await user.click(screen.getByRole("button", { name: "Delete selected" }));
    expect(await screen.findByRole("alertdialog")).toHaveTextContent("Delete 2 files?");

    await user.click(screen.getByRole("button", { name: "Delete 2 assets" }));
    expect(deleteAsset.mock.calls).toEqual([["models", "a.nam"], ["models", "b.nam"]]);
    expect(refreshAssets).toHaveBeenCalledWith("models");
    expect(await screen.findByRole("alert")).toHaveTextContent("Could not delete b.nam (device busy).");
  });

  it("selects and clears every visible asset with the select-all checkbox", async () => {
    const user = userEvent.setup();
    const session = sessionWith({
      models: [
        { id: "a.nam", kind: "model", filename: "a.nam", path: "models/a.nam", sizeBytes: 3 },
        { id: "b.nam", kind: "model", filename: "b.nam", path: "models/b.nam", sizeBytes: 3 },
      ],
    });
    renderWithProviders(<DeviceSessionContext.Provider value={session}><AssetLibrary /></DeviceSessionContext.Provider>);

    await user.click(screen.getByRole("checkbox", { name: "Select all files" }));
    expect(screen.getByRole("checkbox", { name: "Select a.nam" })).toBeChecked();
    expect(screen.getByRole("checkbox", { name: "Select b.nam" })).toBeChecked();

    await user.click(screen.getByRole("checkbox", { name: "Select all files" }));
    expect(screen.queryByRole("button", { name: "Delete selected" })).toBeNull();
  });

  it("renames a device asset and reports the saved presets updated by the daemon", async () => {
    const user = userEvent.setup();
    const renameAsset = vi.fn(async () => ({
      asset: { id: "01-clean.nam", kind: "model" as const, filename: "01-clean.nam", path: "models/01-clean.nam", sizeBytes: 3 },
      updatedPresetCount: 2,
    }));
    const refreshAssets = vi.fn(async () => undefined);
    const selectLocation = vi.fn(async () => undefined);
    const session = sessionWith({
      client: { renameAsset } as unknown as ArdorApiClient,
      models: [{ id: "raw.nam", kind: "model", filename: "raw.nam", path: "models/raw.nam", sizeBytes: 3 }],
      current: { location: { bank: 1, slot: 2 }, exists: true, preset: { version: 1, name: "A", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [] } },
      refreshAssets,
      selectLocation,
    });
    renderWithProviders(<DeviceSessionContext.Provider value={session}><AssetLibrary /></DeviceSessionContext.Provider>);

    await user.click(screen.getByRole("button", { name: "Rename raw.nam" }));
    await user.clear(screen.getByRole("textbox", { name: "New filename" }));
    await user.type(screen.getByRole("textbox", { name: "New filename" }), "01-clean.nam");
    await user.click(screen.getByRole("button", { name: "Rename asset" }));

    expect(renameAsset).toHaveBeenCalledWith("models", "raw.nam", "01-clean.nam");
    expect(refreshAssets).toHaveBeenCalledWith("models");
    expect(selectLocation).toHaveBeenCalledWith({ bank: 1, slot: 2 });
    expect(await screen.findByRole("status")).toHaveTextContent("Updated 2 saved presets");
  });
});
