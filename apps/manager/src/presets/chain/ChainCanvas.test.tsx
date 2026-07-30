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
    renderWithProviders(<ChainCanvas blocks={[first, second]} maxed={false} issuesFor={() => []} onSelect={() => undefined} onAdd={() => undefined} onMove={onMove} onLaneAdd={() => undefined} onLaneMove={() => undefined} onToggle={() => undefined} onDuplicate={() => undefined} onReset={() => undefined} onDelete={() => undefined} />);

    expect(screen.getByRole("button", { name: "Drag Compressor" })).toBeInTheDocument();
    await user.click(screen.getAllByRole("button", { name: "Move block right" })[0]);
    expect(onMove).toHaveBeenCalledWith(first.id, 1);
  });

  it("marks an enabled NAM block as the stereo-to-mono boundary", () => {
    const nam = createBlockFromDefinition("nam", [], "models/amp.nam");
    renderWithProviders(<ChainCanvas blocks={[nam]} maxed={false} issuesFor={() => []} onSelect={() => undefined} onAdd={() => undefined} onMove={() => undefined} onLaneAdd={() => undefined} onLaneMove={() => undefined} onToggle={() => undefined} onDuplicate={() => undefined} onReset={() => undefined} onDelete={() => undefined} />);

    expect(screen.getByText("Stereo → Mono")).toBeInTheDocument();
  });

  it("renders a Dual Amp as two fixed NAM-to-IR lanes", () => {
    const dual = createBlockFromDefinition("dualAmp", []);
    dual.params.leftNamAsset = "models/clean.nam";
    dual.params.leftIrAsset = "irs/open.wav";
    dual.params.rightNamAsset = "models/crunch.nam";
    dual.params.rightIrAsset = "irs/closed.wav";
    renderWithProviders(<ChainCanvas blocks={[dual]} maxed={false} issuesFor={() => []} onSelect={() => undefined} onAdd={() => undefined} onMove={() => undefined} onLaneAdd={() => undefined} onLaneMove={() => undefined} onToggle={() => undefined} onDuplicate={() => undefined} onReset={() => undefined} onDelete={() => undefined} />);

    expect(screen.getByText("Parallel Stereo")).toBeInTheDocument();
    for (const asset of ["clean.nam", "open.wav", "crunch.nam", "closed.wav"]) {
      expect(screen.getByText(asset)).toBeInTheDocument();
    }
  });

  it("renders editable Dual Rig lanes and reports cross-lane moves", async () => {
    const user = userEvent.setup();
    const rig = createBlockFromDefinition("dualRig", []);
    const onLaneMove = vi.fn();
    const onLaneAdd = vi.fn();
    renderWithProviders(<ChainCanvas blocks={[rig]} maxed={false} issuesFor={() => []} onSelect={() => undefined} onAdd={() => undefined} onMove={() => undefined} onLaneAdd={onLaneAdd} onLaneMove={onLaneMove} onToggle={() => undefined} onDuplicate={() => undefined} onReset={() => undefined} onDelete={() => undefined} />);

    expect(screen.getByText("Split")).toBeInTheDocument();
    expect(screen.getByText("Merge")).toBeInTheDocument();
    expect(screen.getAllByText("NAM Model")).toHaveLength(2);
    expect(screen.getAllByText("Cabinet IR")).toHaveLength(2);

    await user.click(screen.getAllByRole("button", { name: "Move to other lane" })[0]);
    expect(onLaneMove).toHaveBeenCalledWith(rig.id, rig.lanes!.left.blocks[0].id, "right", 2);
    await user.click(screen.getByRole("button", { name: "Add effect to left lane" }));
    expect(onLaneAdd).toHaveBeenCalledWith(rig.id, "left", 2);
  });
});
