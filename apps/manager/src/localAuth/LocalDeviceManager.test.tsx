import { screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import type { ReactNode } from "react";
import { beforeEach, describe, expect, it, vi } from "vitest";

import { renderWithProviders } from "../test/render";
import { LocalDeviceManager } from "./LocalDeviceManager";

const authMocks = vi.hoisted(() => ({
  login: vi.fn(),
  setup: vi.fn(),
  status: vi.fn(),
}));

vi.mock("./api", () => ({ localAuthAPI: authMocks }));
vi.mock("../app/AppShell", () => ({ AppShell: () => <div>Device manager</div> }));
vi.mock("../connection/deviceSession", () => ({
  DeviceSessionProvider: ({ children }: { children: ReactNode }) => children,
}));

describe("LocalDeviceManager", () => {
  beforeEach(() => {
    authMocks.login.mockReset();
    authMocks.setup.mockReset();
    authMocks.status.mockReset();
  });

  it("requires the physical code during first-time setup", async () => {
    authMocks.status
      .mockResolvedValueOnce({ state: "setup_required", insecureTransport: true })
      .mockResolvedValueOnce({ state: "authenticated", account: { username: "owner" }, insecureTransport: true });
    authMocks.setup.mockResolvedValue({ account: { username: "owner" } });
    renderWithProviders(<LocalDeviceManager />);

    await screen.findByRole("heading", { name: "Protect this pedal" });
    await userEvent.type(screen.getByLabelText("Code shown on pedal"), "abcd-efgh");
    await userEvent.type(screen.getByLabelText("Local username"), "owner");
    await userEvent.type(screen.getByLabelText("Local password"), "long-local-password");
    await userEvent.type(screen.getByLabelText("Confirm password"), "long-local-password");
    await userEvent.click(screen.getByRole("button", { name: "Create local account" }));

    await waitFor(() => expect(authMocks.setup).toHaveBeenCalledWith("ABCD-EFGH", "owner", "long-local-password"));
    expect(await screen.findByText("Device manager")).toBeInTheDocument();
  });

  it("does not submit mismatched setup passwords", async () => {
    authMocks.status.mockResolvedValue({ state: "setup_required", insecureTransport: true });
    renderWithProviders(<LocalDeviceManager />);

    await screen.findByRole("heading", { name: "Protect this pedal" });
    await userEvent.type(screen.getByLabelText("Code shown on pedal"), "ABCD-EFGH");
    await userEvent.type(screen.getByLabelText("Local username"), "owner");
    await userEvent.type(screen.getByLabelText("Local password"), "long-local-password");
    await userEvent.type(screen.getByLabelText("Confirm password"), "different-password");
    await userEvent.click(screen.getByRole("button", { name: "Create local account" }));

    expect(await screen.findByRole("alert")).toHaveTextContent("Passwords do not match.");
    expect(authMocks.setup).not.toHaveBeenCalled();
  });

  it("signs in to an existing local account", async () => {
    authMocks.status
      .mockResolvedValueOnce({ state: "login_required", account: { username: "owner" }, insecureTransport: true })
      .mockResolvedValueOnce({ state: "authenticated", account: { username: "owner" }, insecureTransport: true });
    authMocks.login.mockResolvedValue({ account: { username: "owner" } });
    renderWithProviders(<LocalDeviceManager />);

    await screen.findByRole("heading", { name: "Sign in to your pedal" });
    await userEvent.type(screen.getByLabelText("Local username"), "owner");
    await userEvent.type(screen.getByLabelText("Password"), "long-local-password");
    await userEvent.click(screen.getByRole("button", { name: "Sign in" }));

    await waitFor(() => expect(authMocks.login).toHaveBeenCalledWith("owner", "long-local-password"));
    expect(await screen.findByText("Device manager")).toBeInTheDocument();
  });
});
