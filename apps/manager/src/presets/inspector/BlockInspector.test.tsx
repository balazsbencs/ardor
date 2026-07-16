import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { createBlockFromDefinition } from "../../effects/catalog";
import { renderWithProviders } from "../../test/render";
import { BlockInspector } from "./BlockInspector";

describe("BlockInspector", () => {
  it("renders the complete compressor control surface", async () => {
    const user = userEvent.setup();
    const block = createBlockFromDefinition("dynamics:compressor", []);
    const onParam = vi.fn();
    renderWithProviders(<BlockInspector block={block} issues={[]} models={[]} irs={[]} onToggle={() => undefined} onParam={onParam} onAsset={() => undefined} onMode={() => undefined} onEqBand={() => undefined} onReset={() => undefined} onDuplicate={() => undefined} onDelete={() => undefined} onAssets={() => undefined} />);

    for (const label of ["Threshold", "Ratio", "Attack", "Release", "Knee", "Makeup", "Input", "Mix", "Sidechain HPF"]) {
      expect(screen.getByRole("slider", { name: label })).toBeInTheDocument();
    }
    expect(screen.getByRole("combobox", { name: "Detector" })).toBeInTheDocument();

    await user.selectOptions(screen.getByRole("combobox", { name: "Detector" }), "rms");
    expect(onParam).toHaveBeenCalledWith(block.id, "detector", "rms");
  });
});
