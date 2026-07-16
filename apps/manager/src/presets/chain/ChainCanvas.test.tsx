import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { createBlockFromDefinition } from "../../effects/catalog";
import { renderWithProviders } from "../../test/render";
import { ChainCanvas } from "./ChainCanvas";

describe("ChainCanvas", () => {
  it("keeps button reordering available alongside drag handles", async () => {
    const user = userEvent.setup();
    const first = createBlockFromDefinition("dynamics:compressor", []);
    const second = createBlockFromDefinition("eq:parametric_eq_5", [first]);
    const onMove = vi.fn();
    renderWithProviders(<ChainCanvas blocks={[first, second]} maxed={false} issuesFor={() => []} onSelect={() => undefined} onAdd={() => undefined} onMove={onMove} onToggle={() => undefined} onDuplicate={() => undefined} onReset={() => undefined} onDelete={() => undefined} />);

    expect(screen.getByRole("button", { name: "Drag Compressor" })).toBeInTheDocument();
    await user.click(screen.getAllByRole("button", { name: "Move block right" })[0]);
    expect(onMove).toHaveBeenCalledWith(first.id, 1);
  });
});
