import { act, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, expect, it, vi } from "vitest";

import type { Preset } from "../api/types";
import { renderWithEditor } from "../stage/renderWithEditor";
import { AppBar } from "./AppBar";

const preset: Preset = {
  version: 1, name: "Glass", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [],
};
const platform = vi.hoisted(() => ({ hosted: false }));
const session = {
  status: "connected" as const, baseUrl: "http://192.168.88.12:8080",
  current: { location: { bank: 3, slot: 1 }, preset: structuredClone(preset), exists: true },
  device: { deviceName: "Ardor Pedal", active: { bank: 3, slot: 1, storedRevisionMatches: true }, capabilities: {} },
  presets: [], irs: [], reverbIrs: [], models: [], busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(), applyCurrent: vi.fn(), refreshPresets: vi.fn(), selectLocation: vi.fn(async () => undefined),
};
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));
vi.mock("../runtime/platform", () => ({ isHostedCloudRuntime: () => platform.hosted }));

const props = () => ({ onView: vi.fn(), onConnection: vi.fn(), onSettings: vi.fn(), onCloudDevices: vi.fn() });

beforeEach(() => { platform.hosted = false; });

it("names the open preset and marks it MODIFIED after an edit", () => {
  const { editor } = renderWithEditor(<AppBar view="edit" {...props()} />);
  expect(screen.getByText("Glass")).toBeInTheDocument();
  expect(screen.getByText("BANK 03 · FS 2")).toBeInTheDocument();
  expect(screen.queryByText("MODIFIED")).not.toBeInTheDocument();
  act(() => editor().dispatch({ type: "set-name", name: "Glass II" }));
  expect(screen.getByText("MODIFIED")).toBeInTheDocument();
});

it("switches views and opens Settings", async () => {
  const handlers = props();
  renderWithEditor(<AppBar view="edit" {...handlers} />);
  expect(screen.getByRole("button", { name: "Edit" })).toHaveAttribute("aria-pressed", "true");
  await userEvent.click(screen.getByRole("button", { name: "Assets" }));
  expect(handlers.onView).toHaveBeenCalledWith("assets");
  await userEvent.click(screen.getByRole("button", { name: "Open settings" }));
  expect(handlers.onSettings).toHaveBeenCalled();
  expect(screen.queryByRole("button", { name: "Devices" })).not.toBeInTheDocument();
});

it("shows the live state only in the Edit view", () => {
  renderWithEditor(<AppBar view="assets" {...props()} />);
  expect(screen.queryByText("LIVE ON PEDAL")).not.toBeInTheDocument();
});

it("offers Devices instead of Settings on the hosted build", async () => {
  platform.hosted = true;
  const handlers = props();
  renderWithEditor(<AppBar view="edit" {...handlers} />);
  expect(screen.queryByRole("button", { name: "Open settings" })).not.toBeInTheDocument();
  await userEvent.click(screen.getByRole("button", { name: "Devices" }));
  expect(handlers.onCloudDevices).toHaveBeenCalled();
});
