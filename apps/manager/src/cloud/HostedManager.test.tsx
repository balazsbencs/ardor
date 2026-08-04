import { render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { afterEach, describe, expect, it, vi } from "vitest";

import { HostedManager } from "./HostedManager";

function json(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), { status, headers: { "Content-Type": "application/json" } });
}

afterEach(() => {
  vi.unstubAllGlobals();
});

describe("hosted manager", () => {
  it("signs in and lists only the account device response", async () => {
    const fetchMock = vi.fn(async (input: RequestInfo | URL, init?: RequestInit) => {
      const path = String(input);
      if (path === "/v1/auth/me") return json({ error: "authentication_required", message: "Sign in is required" }, 401);
      if (path === "/v1/auth/login" && init?.method === "POST") return json({ account: { id: "account-1", username: "RiffMaster" } });
      if (path === "/v1/devices") return json({ devices: [{ id: "pedal-1", role: "owner", claimEpoch: 1, online: true, lastSeenAt: null }] });
      throw new Error(`unexpected request ${init?.method ?? "GET"} ${path}`);
    });
    vi.stubGlobal("fetch", fetchMock);
    const user = userEvent.setup();
    render(<HostedManager />);

    await user.type(await screen.findByLabelText("Username"), "RiffMaster");
    await user.type(screen.getByLabelText("Password"), "correct horse battery staple");
    await user.click(screen.getByRole("button", { name: "Sign in" }));

    expect(await screen.findByText("pedal-1")).toBeInTheDocument();
    expect(screen.getByText("Online")).toBeInTheDocument();
    expect(fetchMock).toHaveBeenCalledWith("/v1/auth/login", expect.objectContaining({ credentials: "include", method: "POST" }));
  });

  it("requires newly issued recovery codes to be acknowledged", async () => {
    const fetchMock = vi.fn(async (input: RequestInfo | URL) => {
      const path = String(input);
      if (path === "/v1/auth/me") return json({ error: "authentication_required" }, 401);
      if (path === "/v1/auth/register") return json({ account: { id: "account-1", username: "NewUser" }, recoveryCodes: ["AAAA-BBBB", "CCCC-DDDD"] }, 201);
      throw new Error(`unexpected request ${path}`);
    });
    vi.stubGlobal("fetch", fetchMock);
    const user = userEvent.setup();
    render(<HostedManager />);

    await user.click(await screen.findByRole("button", { name: "Create account" }));
    await user.type(screen.getByLabelText("Username"), "NewUser");
    await user.type(screen.getByLabelText("Password"), "correct horse battery staple");
    await user.click(screen.getByRole("button", { name: "Create account" }));

    expect(await screen.findByText("AAAA-BBBB", { exact: false })).toBeInTheDocument();
    const continueButton = screen.getByRole("button", { name: "Continue" });
    expect(continueButton).toBeDisabled();
    await user.click(screen.getByLabelText("I saved these codes somewhere safe."));
    expect(continueButton).toBeEnabled();
    await user.click(continueButton);
    await waitFor(() => expect(screen.getByRole("heading", { name: "Devices" })).toBeInTheDocument());
  });
});
