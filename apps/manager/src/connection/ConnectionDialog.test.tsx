import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { useState } from "react";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { ArdorApiClient } from "../api/client";
import type { DeviceStatus } from "../api/types";
import { renderWithProviders } from "../test/render";
import { ConnectionDialog } from "./ConnectionDialog";
import { DeviceSessionProvider } from "./deviceSession";

const localAuthMocks = vi.hoisted(() => ({
  login: vi.fn(),
  status: vi.fn(),
}));
const clientFactoryMock = vi.fn();

vi.mock("../localAuth/api", () => ({
  localAuthAPI: localAuthMocks,
}));

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
    <DeviceSessionProvider clientFactory={(config) => { clientFactoryMock(config); return apiClient; }}>
      <ConnectionDialog open={open} onOpenChange={setOpen} />
      <output>{open ? "open" : "closed"}</output>
    </DeviceSessionProvider>
  );
}

describe("ConnectionDialog", () => {
  beforeEach(() => {
    clientFactoryMock.mockReset();
    localAuthMocks.login.mockReset();
    localAuthMocks.status.mockReset().mockResolvedValue({ state: "disabled", insecureTransport: true });
  });

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

  it("uses the local session token when authentication is enabled", async () => {
    const authenticatedClient = client();
    localAuthMocks.status.mockResolvedValue({ state: "login_required", insecureTransport: true });
    localAuthMocks.login.mockResolvedValue({ account: { username: "owner" }, sessionToken: "local-session" });
    renderWithProviders(<DialogHarness apiClient={authenticatedClient} />);

    await userEvent.type(screen.getByLabelText("Local username"), "owner");
    await userEvent.type(screen.getByLabelText("Local password"), "long-local-password");
    await userEvent.click(screen.getByRole("button", { name: "Connect" }));

    expect(await screen.findByText("closed")).toBeInTheDocument();
    expect(localAuthMocks.login).toHaveBeenCalledWith("owner", "long-local-password", "http://127.0.0.1:8080");
    expect(clientFactoryMock).toHaveBeenCalledWith({ baseUrl: "http://127.0.0.1:8080", token: "local-session" });
  });
});
