import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { Tone3000Dialog } from "./Tone3000Dialog";
import type { Tone3000Selection } from "./types";

const selection: Tone3000Selection = {
  tone: {
    id: 1, title: "Plexi Crunch", description: "A loud one.", gear: "amp", images: null, format: "nam", license: "cc-by",
    user: { id: 2, username: "riffer", avatar_url: null, url: "https://example.test/riffer" }, url: "https://example.test/tone",
  },
  models: [{ id: 10, name: "Gain 5", size: "standard", tone_id: 1, architecture_version: "2" }],
};

const handlers = () => ({ onSelectedModelId: vi.fn(), onContinue: vi.fn(), onCancel: vi.fn(), onInstall: vi.fn() });

describe("Tone3000Dialog", () => {
  it("renders nothing while idle", () => {
    render(<Tone3000Dialog phase="idle" {...handlers()} />);
    expect(screen.queryByRole("dialog")).not.toBeInTheDocument();
  });

  it("offers Continue on the intro and shows no eyebrow label", async () => {
    const h = handlers();
    render(<Tone3000Dialog phase="intro" {...h} />);
    expect(screen.getByRole("heading", { name: "Find a new sound without leaving Ardor" })).toBeInTheDocument();
    expect(screen.queryByText("Models from a global community")).not.toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: /Continue to TONE3000/ }));
    expect(h.onContinue).toHaveBeenCalled();
  });

  it("lets the user cancel while it waits, and locks Close while it loads", async () => {
    const h = handlers();
    const { rerender } = render(<Tone3000Dialog phase="waiting" {...h} />);
    await userEvent.click(screen.getByRole("button", { name: "Cancel" }));
    expect(h.onCancel).toHaveBeenCalledTimes(1);
    rerender(<Tone3000Dialog phase="loading" {...h} />);
    expect(screen.getByRole("button", { name: "Close TONE3000" })).toBeDisabled();
  });

  it("installs the chosen model on the pedal", async () => {
    const h = handlers();
    render(<Tone3000Dialog phase="detail" selection={selection} selectedModelId={10} {...h} />);
    expect(screen.getByRole("heading", { name: "Plexi Crunch" })).toBeInTheDocument();
    expect(screen.getByText("Amp · NAM")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: /Install on pedal/ }));
    expect(h.onInstall).toHaveBeenCalled();
  });
});
