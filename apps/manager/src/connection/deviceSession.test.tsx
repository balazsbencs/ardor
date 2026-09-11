import { act, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { afterEach, describe, expect, it, vi } from "vitest";

import type { ArdorApiClient } from "../api/client";
import { ArdorApiError } from "../api/errors";
import type { DeviceStatus, Preset } from "../api/types";
import { renderWithProviders } from "../test/render";
import {
  DeviceSessionProvider,
  useDeviceSession,
  waitForApplyResult,
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
      <span data-testid="active">{session.device?.active
        ? `${session.device.active.bank}:${session.device.active.slot}` : "none"}</span>
      <span data-testid="token-focus">{String(session.needsTokenFocus)}</span>
      <span data-testid="reverb-ir-support">{String(session.supportsReverbIrs)}</span>
      <span>{session.error?.message}</span>
      <button type="button" onClick={() => void session.connect("http://pedal", "secret")}>Connect</button>
      <button type="button" onClick={session.disconnect}>Disconnect</button>
    </div>
  );
}

function renderSession(factory: DeviceClientFactory, autoConnect = false) {
  return renderWithProviders(
    <DeviceSessionProvider clientFactory={factory} autoConnect={autoConnect}><Probe /></DeviceSessionProvider>,
  );
}

afterEach(() => {
  localStorage.clear();
  vi.restoreAllMocks();
});

describe("DeviceSessionProvider", () => {
  it("reports an apply request that never leaves pending state", async () => {
    const client = mockClient({
      getApplyStatus: vi.fn(async (_id: string) => ({
        id: "apply-1", state: "pending" as const, bank: 2, slot: 1,
      })),
    });
    await expect(waitForApplyResult(client, {
      accepted: true, id: "apply-1", state: "pending", bank: 2, slot: 1,
    }, 5, 1)).rejects.toThrow("Preset apply timed out");
  });

  it("connects automatically when running as the device-hosted manager", async () => {
    const factory = vi.fn(() => mockClient());
    renderSession(factory, true);

    expect(await screen.findByText("connected")).toBeInTheDocument();
    expect(factory).toHaveBeenCalledWith(expect.objectContaining({ baseUrl: "http://127.0.0.1:8080" }));
  });

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

  it("refreshes the active preset changed by hardware", async () => {
    let active = { bank: 1, slot: 0, name: "First" };
    const client = mockClient({
      getDevice: vi.fn(async () => ({ ...device, active })),
      listPresets: vi.fn(async () => [
        { bank: 1, slot: 0, exists: true, name: "First" },
        { bank: 2, slot: 3, exists: true, name: "Second" },
      ]),
    });
    let refresh: (() => void) | undefined;
    vi.spyOn(window, "setInterval").mockImplementation((handler: TimerHandler, timeout?: number) => {
      if (timeout === 2_000 && typeof handler === "function") refresh = handler as () => void;
      return 1;
    });
    renderSession(() => client);
    await userEvent.click(screen.getByRole("button", { name: "Connect" }));
    await screen.findByText("connected");
    expect(screen.getByTestId("active")).toHaveTextContent("1:0");
    await waitFor(() => expect(refresh).toBeTypeOf("function"));

    active = { bank: 2, slot: 3, name: "Second" };
    await act(async () => {
      refresh?.();
      await Promise.resolve();
    });
    await waitFor(() => expect(screen.getByTestId("active")).toHaveTextContent("2:3"));
  });

  it("connects to older devices that do not expose a reverb IR inventory", async () => {
    const client = mockClient({
      listAssets: vi.fn(async (kind) => {
        if (kind === "reverb-irs") {
          throw new ArdorApiError(400, "invalid_asset_kind", "asset kind must be models or irs");
        }
        return [];
      }),
    });
    renderSession(() => client);

    await userEvent.click(screen.getByRole("button", { name: "Connect" }));

    expect(await screen.findByText("connected")).toBeInTheDocument();
    expect(screen.getByTestId("reverb-ir-support")).toHaveTextContent("false");
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
