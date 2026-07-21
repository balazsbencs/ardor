import { act, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { ArdorApiClient } from "../api/client";
import { ArdorApiError } from "../api/errors";
import type { DeviceStatus, Preset } from "../api/types";
import { renderWithProviders } from "../test/render";
import {
  DeviceSessionProvider,
  useDeviceSession,
  type DeviceClientFactory,
} from "./deviceSession";

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

const saved: Preset = {
  version: 1, name: "Saved", routing: "serial",
  global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [],
};

function mockClient(overrides: Partial<ArdorApiClient> = {}): ArdorApiClient {
  return {
    getDevice: vi.fn(async () => device),
    listAssets: vi.fn(async () => []),
    listPresets: vi.fn(async () => []),
    getPreset: vi.fn(async (bank: number, slot: number) => ({ bank, slot, preset: saved })),
    ...overrides,
  } as unknown as ArdorApiClient;
}

function Probe() {
  const session = useDeviceSession();
  return (
    <div>
      <span data-testid="status">{session.status}</span>
      <span data-testid="location">{session.current ? `${session.current.location.bank}:${session.current.location.slot}` : "none"}</span>
      <span data-testid="name">{session.current?.preset.name ?? "none"}</span>
      <span data-testid="token-focus">{String(session.needsTokenFocus)}</span>
      <span>{session.error?.message}</span>
      <button type="button" onClick={() => void session.connect("http://pedal", "secret")}>Connect</button>
      <button type="button" onClick={session.disconnect}>Disconnect</button>
    </div>
  );
}

function renderSession(factory: DeviceClientFactory) {
  return renderWithProviders(
    <DeviceSessionProvider clientFactory={factory}><Probe /></DeviceSessionProvider>,
  );
}

afterEach(() => localStorage.clear());

describe("DeviceSessionProvider", () => {
  it("loads authenticated resources and the authoritative active preset", async () => {
    const client = mockClient({
      getDevice: vi.fn(async () => ({ ...device, active: { bank: 3, slot: 2, name: "Saved" } })),
      listAssets: vi.fn(async (kind) => kind === "models"
        ? [{ id: "a", kind: "model" as const, filename: "a", path: "models/a.nam", sizeBytes: 1 }]
        : []),
      listPresets: vi.fn(async () => [{ bank: 3, slot: 2, exists: true, name: "Saved" }]),
    });
    renderSession(() => client);
    await userEvent.click(screen.getByRole("button", { name: "Connect" }));
    expect(await screen.findByText("connected")).toBeInTheDocument();
    expect(screen.getByTestId("location")).toHaveTextContent("3:2");
    expect(screen.getByTestId("name")).toHaveTextContent("Saved");
    expect(client.getPreset).toHaveBeenCalledWith(3, 2);
  });

  it("creates a local empty preset when the chosen slot does not exist", async () => {
    localStorage.setItem("ardor-manager.location:http://pedal", JSON.stringify({ bank: 7, slot: 1 }));
    const client = mockClient({ listPresets: vi.fn(async () => []) });
    renderSession(() => client);
    await userEvent.click(screen.getByRole("button", { name: "Connect" }));
    expect(await screen.findByText("connected")).toBeInTheDocument();
    expect(screen.getByTestId("location")).toHaveTextContent("7:1");
    expect(screen.getByTestId("name")).toHaveTextContent("New Preset");
    expect(client.getPreset).not.toHaveBeenCalled();
  });

  it("keeps authentication errors actionable", async () => {
    const client = mockClient({
      listAssets: vi.fn(async () => { throw new ArdorApiError(401, "unauthorized", "Bad token"); }),
    });
    renderSession(() => client);
    await userEvent.click(screen.getByRole("button", { name: "Connect" }));
    expect(await screen.findByText("error")).toBeInTheDocument();
    expect(screen.getByTestId("token-focus")).toHaveTextContent("true");
    expect(screen.getByText("Bad token")).toBeInTheDocument();
  });

  it("reports network failures without retaining a client", async () => {
    const client = mockClient({ getDevice: vi.fn(async () => { throw new TypeError("Network down"); }) });
    renderSession(() => client);
    await userEvent.click(screen.getByRole("button", { name: "Connect" }));
    expect(await screen.findByText("Network down")).toBeInTheDocument();
    expect(screen.getByTestId("status")).toHaveTextContent("error");
  });

  it("disconnects and reconnects without persisting the bearer token", async () => {
    const factory = vi.fn(() => mockClient());
    renderSession(factory);
    await userEvent.click(screen.getByRole("button", { name: "Connect" }));
    await screen.findByText("connected");
    await userEvent.click(screen.getByRole("button", { name: "Disconnect" }));
    expect(screen.getByTestId("status")).toHaveTextContent("disconnected");
    await userEvent.click(screen.getByRole("button", { name: "Connect" }));
    await screen.findByText("connected");
    expect(factory).toHaveBeenCalledTimes(2);
    expect(localStorage.getItem("ardor-manager.base-url")).toBe("http://pedal");
    expect(JSON.stringify(localStorage)).not.toContain("secret");
  });

  it("prevents overlapping connection attempts", async () => {
    let resolveDevice!: (value: DeviceStatus) => void;
    const getDevice = vi.fn(() => new Promise<DeviceStatus>((resolve) => { resolveDevice = resolve; }));
    renderSession(() => mockClient({ getDevice }));
    const button = screen.getByRole("button", { name: "Connect" });
    await userEvent.click(button);
    await userEvent.click(button);
    expect(getDevice).toHaveBeenCalledTimes(1);
    await act(async () => resolveDevice(device));
    await screen.findByText("connected");
  });
});
