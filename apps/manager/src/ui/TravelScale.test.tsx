import { fireEvent, render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import type { NumberControl } from "../effects/types";
import { TravelScale, positionOf, snapValue } from "./TravelScale";

const mix: NumberControl = { kind: "number", key: "mix", label: "Mix", minimum: 0, maximum: 1, step: 0.05, unit: "percent", defaultValue: 0.25 };
const stepped: NumberControl = {
  ...mix, key: "time", label: "Time",
  display: { format: (v) => `${v}`, toInput: (v) => v, fromInput: (v) => v, minimum: 0, maximum: 1, step: 1,
    choices: [{ value: 0.1, label: "1/8" }, { value: 0.5, label: "1/4" }, { value: 0.9, label: "1/2" }] },
};

describe("TravelScale", () => {
  it("shows the value and its unit as separate text", () => {
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={vi.fn()} />);
    expect(screen.getByText("30")).toBeInTheDocument();
    expect(screen.getByText("%")).toBeInTheDocument();
    expect(screen.getByRole("slider", { name: "Mix" })).toHaveAttribute("aria-valuetext", "30%");
  });

  it("steps with the arrow keys and keeps one gesture for a key burst", () => {
    const onChange = vi.fn();
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={onChange} />);
    const slider = screen.getByRole("slider", { name: "Mix" });
    fireEvent.keyDown(slider, { key: "ArrowRight" });
    fireEvent.keyDown(slider, { key: "ArrowRight" });
    expect(onChange.mock.calls[0][0]).toBeCloseTo(0.35);
    expect(onChange.mock.calls[0][1]).toBe(onChange.mock.calls[1][1]);
  });

  it("goes to the ends with Home and End, and resets on double-click", () => {
    const onChange = vi.fn();
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={onChange} />);
    const slider = screen.getByRole("slider", { name: "Mix" });
    fireEvent.keyDown(slider, { key: "End" });
    fireEvent.keyDown(slider, { key: "Home" });
    fireEvent.doubleClick(slider);
    expect(onChange.mock.calls.map(([v]) => v)).toEqual([1, 0, 0.25]);
  });

  it("snaps Home, End and reset to a choice on stepped controls", () => {
    const onChange = vi.fn();
    render(<TravelScale control={stepped} value={0.5} family="dly" onChange={onChange} />);
    const slider = screen.getByRole("slider", { name: "Time" });
    fireEvent.keyDown(slider, { key: "End" });
    fireEvent.keyDown(slider, { key: "Home" });
    fireEvent.doubleClick(slider);
    expect(onChange.mock.calls.map(([v]) => v)).toEqual([0.9, 0.1, 0.1]);
  });

  it("sets the value from the pointer position", () => {
    const onChange = vi.fn();
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={onChange} />);
    const slider = screen.getByRole("slider", { name: "Mix" });
    slider.getBoundingClientRect = () => ({ left: 0, width: 200, top: 0, height: 30, right: 200, bottom: 30, x: 0, y: 0, toJSON: () => ({}) });
    fireEvent.pointerDown(slider, { clientX: 100, button: 0, pointerId: 1 });
    expect(onChange).toHaveBeenCalledWith(0.5, expect.any(String));
  });

  it("changes the value with the wheel only while focused", () => {
    const onChange = vi.fn();
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={onChange} />);
    const slider = screen.getByRole("slider", { name: "Mix" });
    fireEvent.wheel(slider, { deltaY: -100 });
    expect(onChange).not.toHaveBeenCalled();
    slider.focus();
    fireEvent.wheel(slider, { deltaY: -100 });
    expect(onChange.mock.calls[0][0]).toBeCloseTo(0.35);
  });

  it("ignores a sideways wheel swipe", () => {
    const onChange = vi.fn();
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={onChange} />);
    const slider = screen.getByRole("slider", { name: "Mix" });
    slider.focus();
    fireEvent.wheel(slider, { deltaX: 120, deltaY: 0 });
    expect(onChange).not.toHaveBeenCalled();
    fireEvent.wheel(slider, { deltaY: 100 });
    expect(onChange.mock.calls[0][0]).toBeCloseTo(0.25);
  });

  it("snaps stepped displays to the nearest choice", () => {
    expect(snapValue(stepped, 0.45)).toBe(0.5);
    expect(positionOf(stepped, 0.9)).toBe(1);
    expect(snapValue(mix, 0.333)).toBeCloseTo(0.35);
  });

  it("marks a scene-owned value and offers Share", async () => {
    const onShare = vi.fn();
    render(<TravelScale control={mix} value={0.3} family="dly" onChange={vi.fn()} owned="scene" onShare={onShare} />);
    expect(screen.getByText("SCENE")).toBeInTheDocument();
    await userEvent.click(screen.getByRole("button", { name: "Share" }));
    expect(onShare).toHaveBeenCalled();
  });
});
