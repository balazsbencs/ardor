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
    const onActiveStage = vi.fn();
    const onPassFilterChange = vi.fn();
    renderWithProviders(<EqResponseGraph bands={[
      { enabled: true, frequency_hz: 80, q: 1, gain_db: 0 },
      { enabled: true, frequency_hz: 250, q: 1, gain_db: 0 },
      { enabled: true, frequency_hz: 800, q: 1, gain_db: 0 },
      { enabled: true, frequency_hz: 2500, q: 1, gain_db: 0 },
      { enabled: true, frequency_hz: 8000, q: 1, gain_db: 0 },
    ]} highPass={{ enabled: false, frequency_hz: 40, q: 0.70710678, slope_db_per_octave: 12 }} lowPass={{ enabled: false, frequency_hz: 16000, q: 0.70710678, slope_db_per_octave: 12 }} activeStage={1} onActiveStage={onActiveStage} onBandChange={onChange} onPassFilterChange={onPassFilterChange} />);

    const node = screen.getByRole("button", { name: "Adjust Band 3" });
    fireEvent.pointerDown(node, { pointerId: 1, clientX: 330, clientY: 100 });
    fireEvent.pointerMove(screen.getByRole("img", { name: "EQ response graph" }), { pointerId: 1, clientX: 360, clientY: 80 });

    expect(onActiveStage).toHaveBeenCalledWith(3);
    expect(onChange).toHaveBeenCalledWith(2, expect.objectContaining({ frequency_hz: expect.any(Number), gain_db: expect.any(Number) }));
    expect(onPassFilterChange).not.toHaveBeenCalled();
  });

  it("drags pass-filter nodes horizontally without changing gain", () => {
    const onPassFilterChange = vi.fn();
    renderWithProviders(<EqResponseGraph bands={[]} highPass={{ enabled: true, frequency_hz: 80, q: 0.70710678, slope_db_per_octave: 24 }} lowPass={{ enabled: false, frequency_hz: 16000, q: 0.70710678, slope_db_per_octave: 12 }} activeStage={0} onActiveStage={() => undefined} onBandChange={() => undefined} onPassFilterChange={onPassFilterChange} />);
    fireEvent.pointerDown(screen.getByRole("button", { name: "Adjust High-pass" }), { pointerId: 1, clientX: 100, clientY: 100 });
    fireEvent.pointerMove(screen.getByRole("img", { name: "EQ response graph" }), { pointerId: 1, clientX: 180, clientY: 20 });
    expect(onPassFilterChange).toHaveBeenCalledWith("high_pass", { frequency_hz: expect.any(Number) });
  });
});
