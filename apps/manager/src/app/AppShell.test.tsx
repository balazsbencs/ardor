import { act, screen } from "@testing-library/react";
import { beforeEach, describe, expect, it } from "vitest";

import { DeviceSessionProvider } from "../connection/deviceSession";
import { renderWithProviders } from "../test/render";
import { AppShell } from "./AppShell";

function dropEvent(type: string, files: File[] = []) {
  const event = new Event(type, { bubbles: true, cancelable: true }) as DragEvent;
  Object.defineProperty(event, "dataTransfer", { value: { types: ["Files"], files } });
  return event;
}

describe("AppShell file drop", () => {
  beforeEach(() => { window.history.replaceState(null, "", "/"); });

  it("shows the drop overlay and moves to the Assets view when files land", () => {
    renderWithProviders(<DeviceSessionProvider><AppShell /></DeviceSessionProvider>);
    act(() => { window.dispatchEvent(dropEvent("dragenter")); });
    expect(screen.getByText("Drop to upload")).toBeInTheDocument();
    expect(screen.getByText(".nam files go to NAM models. .wav files go to Cabinet IRs.")).toBeInTheDocument();
    act(() => { window.dispatchEvent(dropEvent("drop", [new File(["x"], "A.nam")])); });
    expect(screen.queryByText("Drop to upload")).not.toBeInTheDocument();
    expect(screen.getByRole("heading", { name: "Connect to manage files" })).toBeInTheDocument();
  });
});
