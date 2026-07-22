import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { useState } from "react";
import { describe, expect, it, vi } from "vitest";

import type { ArdorApiClient } from "../api/client";
import type { DeviceStatus } from "../api/types";
import { renderWithProviders } from "../test/render";
import { ConnectionDialog } from "./ConnectionDialog";
import { DeviceSessionProvider } from "./deviceSession";

const device: DeviceStatus = {
  deviceName: "Ardor Pedal",
  apiVersion: "0.1.0",
  authEnabled: false,
  dataRootWritable: true,
  maxBanks: 100,
  slotsPerBank: 4,
  supportedPresetVersion: 1,
  capabilities: { modelUpload: true, irUpload: true, presetRead: true, presetWrite: true, presetApply: true },
};

function client(overrides: Partial<ArdorApiClient> = {}): ArdorApiClient {
  return {
    getDevice: vi.fn(async () => device),
    listAssets: vi.fn(async () => []),
    listPresets: vi.fn(async () => []),
    ...overrides,
  } as unknown as ArdorApiClient;
}

function DialogHarness({ apiClient }: { apiClient: ArdorApiClient }) {
  const [open, setOpen] = useState(true);
  return (
    <DeviceSessionProvider clientFactory={() => apiClient}>
      <ConnectionDialog open={open} onOpenChange={setOpen} />
      <output>{open ? "open" : "closed"}</output>
    </DeviceSessionProvider>
  );
}

describe("ConnectionDialog", () => {
  it("closes after a successful connection", async () => {
    renderWithProviders(<DialogHarness apiClient={client()} />);

    await userEvent.click(screen.getByRole("button", { name: "Connect" }));

    expect(await screen.findByText("closed")).toBeInTheDocument();
    expect(screen.queryByRole("dialog", { name: "Connect to Ardor" })).not.toBeInTheDocument();
  });

  it("stays open when connection fails", async () => {
    const failing = client({ getDevice: vi.fn(async () => { throw new Error("Device unavailable"); }) });
    renderWithProviders(<DialogHarness apiClient={failing} />);

    await userEvent.click(screen.getByRole("button", { name: "Connect" }));

    expect(await screen.findByRole("alert")).toHaveTextContent("Device unavailable");
    expect(screen.getByText("open")).toBeInTheDocument();
  });
});
