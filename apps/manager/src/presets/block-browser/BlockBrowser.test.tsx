import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import { renderWithProviders } from "../../test/render";
import { BlockBrowser } from "./BlockBrowser";

describe("BlockBrowser", () => {
  it("exposes every supported definition and filters by aliases", async () => {
    const user = userEvent.setup();
    const onChoose = vi.fn();
    renderWithProviders(<BlockBrowser open onOpenChange={() => undefined} onChoose={onChoose} />);

    expect(screen.getAllByRole("button", { name: "Add" })).toHaveLength(39);

    await user.type(screen.getByPlaceholderText("Search effects"), "bucket");
    expect(screen.getByText("Bucket Brigade Delay")).toBeInTheDocument();
    expect(screen.getAllByRole("button", { name: "Add" })).toHaveLength(1);

    await user.click(screen.getByRole("button", { name: "Add" }));
    expect(onChoose).toHaveBeenCalledWith(expect.objectContaining({ id: "delay:dbucket" }));
  });
});
