import { fireEvent, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it } from "vitest";

import { AppShell } from "../app/AppShell";
import { DeviceSessionProvider } from "../connection/deviceSession";
import { renderWithProviders } from "../test/render";

describe("global settings", () => {
  beforeEach(() => localStorage.clear());

  it("opens from the gear and persists a custom accent", async () => {
    const user = userEvent.setup();
    const { container } = renderWithProviders(
      <DeviceSessionProvider><AppShell /></DeviceSessionProvider>,
    );

    await user.click(screen.getByRole("button", { name: "Open settings" }));
    expect(screen.getByRole("heading", { name: "Settings" })).toBeInTheDocument();

    fireEvent.change(screen.getByLabelText("Custom accent color"), { target: { value: "#67a6ff" } });
    expect(container.querySelector(".app-shell")).toHaveStyle("--accent: #67a6ff");
    expect(localStorage.getItem("ardor-manager.accent")).toBe("#67a6ff");
  });

  it("explains that Wi-Fi setup needs a connected pedal", async () => {
    const user = userEvent.setup();
    renderWithProviders(<DeviceSessionProvider><AppShell /></DeviceSessionProvider>);

    await user.click(screen.getByRole("button", { name: "Open settings" }));
    await user.click(screen.getByRole("button", { name: "Wi-Fi" }));

    expect(screen.getByText("Connect to a pedal first")).toBeInTheDocument();
  });
});
