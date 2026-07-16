import { fireEvent, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import { renderWithProviders } from "../../test/render";
import { EqResponseGraph, frequencyForGraphX, gainForGraphY, graphXForFrequency, graphYForGain } from "./EqResponseGraph";

describe("EqResponseGraph", () => {
  it("maps the graph on a log-frequency axis and preserves the gain range", () => {
    expect(frequencyForGraphX(graphXForFrequency(1000))).toBeCloseTo(1000, 0);
    expect(gainForGraphY(graphYForGain(-6))).toBeCloseTo(-6, 3);
  });

  it("selects a band and writes frequency and gain while a node is dragged", () => {
    const onChange = vi.fn();
    const onActiveBand = vi.fn();
    renderWithProviders(<EqResponseGraph bands={[
      { enabled: true, frequency_hz: 80, q: 1, gain_db: 0 },
      { enabled: true, frequency_hz: 250, q: 1, gain_db: 0 },
      { enabled: true, frequency_hz: 800, q: 1, gain_db: 0 },
      { enabled: true, frequency_hz: 2500, q: 1, gain_db: 0 },
      { enabled: true, frequency_hz: 8000, q: 1, gain_db: 0 },
    ]} activeBand={0} onActiveBand={onActiveBand} onChange={onChange} />);

    const node = screen.getByRole("button", { name: "Adjust band 3" });
    fireEvent.pointerDown(node, { pointerId: 1, clientX: 330, clientY: 100 });
    fireEvent.pointerMove(screen.getByRole("img", { name: "EQ response graph" }), { pointerId: 1, clientX: 360, clientY: 80 });

    expect(onActiveBand).toHaveBeenCalledWith(2);
    expect(onChange).toHaveBeenCalledWith(2, expect.objectContaining({ frequency_hz: expect.any(Number), gain_db: expect.any(Number) }));
  });
});
