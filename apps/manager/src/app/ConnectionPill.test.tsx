import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, expect, it, vi } from "vitest";

import { ConnectionPill } from "./ConnectionPill";

const platform = vi.hoisted(() => ({ hosted: false }));
const session = vi.hoisted(() => ({
  status: "connected" as "connected" | "disconnected" | "error",
  baseUrl: "http://192.168.88.12:8080",
  device: { deviceName: "Ardor Pedal" },
}));

vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));
vi.mock("../runtime/platform", () => ({ isHostedCloudRuntime: () => platform.hosted }));

beforeEach(() => {
  platform.hosted = false;
  session.status = "connected";
});

it("says HTTP on the LAN build", () => {
  render(<ConnectionPill onOpen={vi.fn()} />);
  expect(screen.getByText("HTTP")).toHaveAttribute("title", expect.stringMatching(/trusted networks/));
  expect(screen.getByText("Ardor Pedal")).toBeInTheDocument();
  expect(screen.getByText("192.168.88.12:8080")).toBeInTheDocument();
});

it("does not say HTTP on the hosted build", () => {
  platform.hosted = true;
  render(<ConnectionPill onOpen={vi.fn()} />);
  expect(screen.queryByText("HTTP")).not.toBeInTheDocument();
});

it("opens the connection dialog and names a lost connection", async () => {
  session.status = "error";
  const onOpen = vi.fn();
  render(<ConnectionPill onOpen={onOpen} />);
  await userEvent.click(screen.getByRole("button", { name: "Device: Connection error" }));
  expect(onOpen).toHaveBeenCalled();
  expect(screen.queryByText("192.168.88.12:8080")).not.toBeInTheDocument();
});
