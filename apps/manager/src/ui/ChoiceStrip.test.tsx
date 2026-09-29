import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { expect, it, vi } from "vitest";

import { ChoiceStrip } from "./ChoiceStrip";

it("shows the chosen option and reports a new choice", async () => {
  const onChange = vi.fn();
  render(<ChoiceStrip label="Input source" family="amp" value="sum" onChange={onChange}
    options={[{ value: "sum", label: "L+R Avg" }, { value: "left", label: "Left" }]} />);
  expect(screen.getByRole("radio", { name: "L+R Avg" })).toHaveAttribute("aria-checked", "true");
  await userEvent.click(screen.getByRole("radio", { name: "Left" }));
  expect(onChange).toHaveBeenCalledWith("left");
});
