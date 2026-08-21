import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it } from "vitest";

import { AppShell } from "../app/AppShell";
import { DeviceSessionProvider } from "../connection/deviceSession";
import { renderWithProviders } from "../test/render";

describe("global settings", () => {
  beforeEach(() => localStorage.clear());

  it("opens from the gear and persists the selected Panel palette", async () => {
    const user = userEvent.setup();
    const { container } = renderWithProviders(
      <DeviceSessionProvider><AppShell /></DeviceSessionProvider>,
    );

    await user.click(screen.getByRole("button", { name: "Open settings" }));
    expect(await screen.findByRole("heading", { name: "Settings" })).toBeInTheDocument();

    await user.click(screen.getByRole("button", { name: "Ink" }));
    expect(container.querySelector(".app-shell")).toHaveStyle("--lamp: #5fd0e8");
    expect(localStorage.getItem("ardor-manager.palette")).toBe("ink");
  });

  it("explains that Wi-Fi setup needs a connected pedal", async () => {
    const user = userEvent.setup();
    renderWithProviders(<DeviceSessionProvider><AppShell /></DeviceSessionProvider>);

    await user.click(screen.getByRole("button", { name: "Open settings" }));
    await user.click(await screen.findByRole("button", { name: "Wi-Fi" }));

    expect(screen.getByText("Connect to a pedal first")).toBeInTheDocument();
  });
});
