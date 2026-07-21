import { screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { useState } from "react";
import { describe, expect, it } from "vitest";

import { renderWithProviders } from "./render";

function SmokeComponent() {
  const [enabled, setEnabled] = useState(false);

  return (
    <button type="button" onClick={() => setEnabled(true)}>
      {enabled ? "Ready" : "Start"}
    </button>
  );
}

describe("component test harness", () => {
  it("renders accessible controls and accepts user input", async () => {
    const user = userEvent.setup();
    renderWithProviders(<SmokeComponent />);

    await user.click(screen.getByRole("button", { name: "Start" }));

    expect(screen.getByRole("button", { name: "Ready" })).toBeInTheDocument();
  });
});
