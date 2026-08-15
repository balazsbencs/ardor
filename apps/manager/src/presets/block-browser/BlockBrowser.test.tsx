import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { allEffectDefinitions } from "../../effects/catalog";
import { renderWithProviders } from "../../test/render";
import { BlockBrowser } from "./BlockBrowser";

describe("BlockBrowser", () => {
  it("exposes every supported definition and filters by aliases", async () => {
    const user = userEvent.setup();
    const onChoose = vi.fn();
    renderWithProviders(<BlockBrowser open onOpenChange={() => undefined} onChoose={onChoose} />);

    expect(screen.getAllByRole("button", { name: "Add" })).toHaveLength(allEffectDefinitions().length);

    await user.type(screen.getByPlaceholderText("Search effects"), "bucket");
    expect(screen.getByText("Bucket Brigade Delay")).toBeInTheDocument();
    expect(screen.getAllByRole("button", { name: "Add" })).toHaveLength(1);

    await user.click(screen.getByRole("button", { name: "Add" }));
    expect(onChoose).toHaveBeenCalledWith(expect.objectContaining({ id: "delay:dbucket" }));
  });

  it("groups compressor, noise gate, EQ, wah, and the stereo widener under Utility", async () => {
    const user = userEvent.setup();
    renderWithProviders(<BlockBrowser open onOpenChange={() => undefined} onChoose={() => undefined} />);

    await user.click(screen.getByRole("button", { name: "Utility" }));

    expect(screen.getByText("Compressor")).toBeInTheDocument();
    expect(screen.getByText("Noise Gate")).toBeInTheDocument();
    expect(screen.getByText("Five Band Parametric EQ")).toBeInTheDocument();
    expect(screen.getByText("GCB-95 Wah")).toBeInTheDocument();
    expect(screen.getByText("Stereo Widener")).toBeInTheDocument();
    expect(screen.getAllByRole("button", { name: "Add" })).toHaveLength(5);
    expect(screen.queryByRole("button", { name: "Dynamics" })).not.toBeInTheDocument();
    expect(screen.queryByRole("button", { name: "EQ" })).not.toBeInTheDocument();
  });
});
