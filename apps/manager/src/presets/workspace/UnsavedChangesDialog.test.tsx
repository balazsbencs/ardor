import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { UnsavedChangesDialog } from "./UnsavedChangesDialog";

describe("UnsavedChangesDialog", () => {
  it("reports Cancel, Discard and Save", async () => {
    const onChoice = vi.fn();
    render(<UnsavedChangesDialog open onChoice={onChoice} />);
    expect(screen.getByRole("dialog", { name: "Unsaved changes" })).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "Cancel" }));
    await userEvent.click(screen.getByRole("button", { name: "Discard" }));
    await userEvent.click(screen.getByRole("button", { name: "Save" }));
    expect(onChoice.mock.calls.map(([choice]) => choice)).toEqual(["cancel", "discard", "save"]);
  });

  it("treats Escape as Cancel", async () => {
    const onChoice = vi.fn();
    render(<UnsavedChangesDialog open onChoice={onChoice} />);
    await userEvent.keyboard("{Escape}");
    expect(onChoice).toHaveBeenCalledWith("cancel");
  });

  it("locks every choice while it saves", async () => {
    const onChoice = vi.fn();
    render(<UnsavedChangesDialog open busy onChoice={onChoice} />);
    for (const name of ["Cancel", "Discard", "Saving…"]) expect(screen.getByRole("button", { name })).toBeDisabled();
    await userEvent.keyboard("{Escape}");
    expect(onChoice).not.toHaveBeenCalled();
  });
});
