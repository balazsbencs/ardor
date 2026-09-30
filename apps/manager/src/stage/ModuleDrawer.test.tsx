import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { ModuleDrawer } from "./ModuleDrawer";

describe("ModuleDrawer", () => {
  it("groups blocks by family with code squares and says where they go", () => {
    render(<ModuleDrawer open where="Insert after Tape Delay, position 9" disabledIds={new Map()} onOpenChange={vi.fn()} onChoose={vi.fn()} />);
    expect(screen.getByText("Insert after Tape Delay, position 9")).toBeInTheDocument();
    const delay = screen.getByRole("group", { name: "Delay" });
    expect(within(delay).getByRole("button", { name: /Tape Delay/ })).toBeInTheDocument();
    expect(within(delay).getByText("TAPE")).toBeInTheDocument();
  });

  it("filters by search and picks the first match with Enter", async () => {
    const onChoose = vi.fn();
    render(<ModuleDrawer open where="" disabledIds={new Map()} onOpenChange={vi.fn()} onChoose={onChoose} />);
    await userEvent.type(screen.getByRole("searchbox", { name: "Search blocks" }), "shimmer{Enter}");
    expect(onChoose).toHaveBeenCalledWith(expect.objectContaining({ id: "reverb:shimmer" }));
  });

  it("disables a block with the reason", () => {
    render(<ModuleDrawer open where="" disabledIds={new Map([["delay:digital", "Disable Tape Delay first"]])} onOpenChange={vi.fn()} onChoose={vi.fn()} />);
    const row = screen.getByRole("button", { name: /Digital Delay/ });
    expect(row).toBeDisabled();
    expect(row).toHaveTextContent("Disable Tape Delay first");
  });
});
