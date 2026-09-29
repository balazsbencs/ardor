import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it, vi } from "vitest";

import { PresetTile } from "./PresetTile";

it("floods the live tile, outlines the open one and names the footswitch", async () => {
  const onOpen = vi.fn();
  const preset = { version: 1 as const, name: "Glass Cathedral", routing: "serial" as const, global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
    blocks: [{ id: "d", type: "delay", enabled: true, asset: "", params: { mode: "tape" } }] };
  render(<PresetTile slot={0} preset={preset} live editing dirty missing={false} onOpen={onOpen} />);
  const tile = screen.getByRole("button", { name: /Glass Cathedral/ });
  expect(tile).toHaveClass("is-live", "is-edit");
  expect(screen.getByText("FS 1")).toBeInTheDocument();
  expect(screen.getByText("LIVE")).toBeInTheDocument();
  expect(screen.getByText("EDITED")).toBeInTheDocument();
  expect(screen.getByText("TAPE")).toBeInTheDocument();
  await userEvent.click(tile);
  expect(onOpen).toHaveBeenCalled();
});

it("shows an empty slot", () => {
  render(<PresetTile slot={3} live={false} editing={false} dirty={false} missing={false} onOpen={vi.fn()} />);
  expect(screen.getByRole("button", { name: "FS 4, empty slot" })).toBeInTheDocument();
});
