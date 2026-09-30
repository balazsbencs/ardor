import { render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { allEffectDefinitions } from "../effects/catalog";
import { familyOf } from "../ui/family";
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

  it("reopens with an empty search and the All filter", async () => {
    const props = { where: "", disabledIds: new Map<string, string>(), onOpenChange: vi.fn(), onChoose: vi.fn() };
    const { rerender } = render(<ModuleDrawer open {...props} />);
    await userEvent.type(screen.getByRole("searchbox", { name: "Search blocks" }), "shim");
    await userEvent.click(within(screen.getByRole("group", { name: "Families" })).getByRole("button", { name: "Delay" }));
    rerender(<ModuleDrawer open={false} {...props} />);
    rerender(<ModuleDrawer open {...props} />);
    expect(screen.getByRole("searchbox", { name: "Search blocks" })).toHaveValue("");
    expect(within(screen.getByRole("group", { name: "Families" })).getByRole("button", { name: "All" })).toHaveAttribute("aria-pressed", "true");
    expect(screen.getByRole("group", { name: "Reverb" })).toBeInTheDocument();
  });

  it("skips a disabled first match when Enter is pressed", async () => {
    const delays = allEffectDefinitions().filter((d) => familyOf(d.blockType) === "dly" && d.name.toLowerCase().includes("delay"));
    const onChoose = vi.fn();
    render(<ModuleDrawer open where="" disabledIds={new Map([[delays[0].id, "Disable it first"]])} onOpenChange={vi.fn()} onChoose={onChoose} />);
    await userEvent.click(within(screen.getByRole("group", { name: "Families" })).getByRole("button", { name: "Delay" }));
    await userEvent.type(screen.getByRole("searchbox", { name: "Search blocks" }), "delay{Enter}");
    expect(onChoose).toHaveBeenCalledTimes(1);
    expect(onChoose).toHaveBeenCalledWith(expect.objectContaining({ id: delays[1].id }));
  });
});
