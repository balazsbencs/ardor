import { useEffect, useRef, type KeyboardEvent, type PointerEvent } from "react";

import { cx } from "../components/ui";
import { displayValue } from "../effects/display";
import type { NumberControl } from "../effects/types";
import type { Family } from "./family";
import { splitDisplay } from "./format";
import { Tag } from "./Tag";
import "./TravelScale.css";

const KEY_BURST_MS = 900;
let gestureSeq = 0;
const newGesture = (): string => `gesture-${++gestureSeq}`;

const choicesOf = (control: NumberControl): number[] | undefined => control.display?.choices?.map(({ value }) => value);

export function snapValue(control: NumberControl, raw: number): number {
  const choices = choicesOf(control);
  if (choices) return choices.reduce((best, value) => (Math.abs(value - raw) < Math.abs(best - raw) ? value : best), choices[0]);
  const stepped = Math.round((raw - control.minimum) / control.step) * control.step + control.minimum;
  return Math.min(control.maximum, Math.max(control.minimum, Number(stepped.toFixed(6))));
}

export function positionOf(control: NumberControl, value: number): number {
  const choices = choicesOf(control);
  if (choices) return choices.length > 1 ? choices.indexOf(snapValue(control, value)) / (choices.length - 1) : 0;
  if (control.maximum === control.minimum) return 0;
  return Math.min(1, Math.max(0, (value - control.minimum) / (control.maximum - control.minimum)));
}

function valueAt(control: NumberControl, fraction: number): number {
  const clamped = Math.min(1, Math.max(0, fraction));
  const choices = choicesOf(control);
  if (choices) return choices[Math.round(clamped * (choices.length - 1))];
  return snapValue(control, control.minimum + clamped * (control.maximum - control.minimum));
}

function nudge(control: NumberControl, value: number, direction: 1 | -1, big: boolean, fine: boolean): number {
  const choices = choicesOf(control);
  if (choices) {
    const index = choices.indexOf(snapValue(control, value)) + direction * (big ? 4 : 1);
    return choices[Math.min(choices.length - 1, Math.max(0, index))];
  }
  const step = control.step * (big ? 10 : 1) * (fine ? 0.1 : 1);
  return Math.min(control.maximum, Math.max(control.minimum, Number((value + direction * step).toFixed(6))));
}

export type TravelScaleProps = {
  control: NumberControl;
  value: number;
  family: Family;
  onChange(value: number, gesture: string): void;
  onFocusControl?(): void;
  focused?: boolean;
  owned?: "scene" | "shared";
  onShare?(): void;
  compact?: boolean;
};

export function TravelScale({ control, value, family, onChange, onFocusControl, focused, owned, onShare, compact }: TravelScaleProps) {
  const scaleRef = useRef<HTMLDivElement>(null);
  const gesture = useRef<{ id: string; at: number } | null>(null);
  const latest = useRef({ control, value, onChange });
  latest.current = { control, value, onChange };

  const burstGesture = (): string => {
    const now = Date.now();
    gesture.current = !gesture.current || now - gesture.current.at > KEY_BURST_MS
      ? { id: newGesture(), at: now }
      : { id: gesture.current.id, at: now };
    return gesture.current.id;
  };

  // React wheel listeners are passive, so preventDefault needs a native listener.
  useEffect(() => {
    const element = scaleRef.current;
    if (!element) return undefined;
    const onWheel = (event: WheelEvent) => {
      if (document.activeElement !== element) return;
      // A sideways swipe has no deltaY; it must not lower the value, and the page may scroll.
      if (!event.deltaY) return;
      event.preventDefault();
      const { control: c, value: v, onChange: change } = latest.current;
      change(nudge(c, v, event.deltaY < 0 ? 1 : -1, false, event.shiftKey), burstGesture());
    };
    element.addEventListener("wheel", onWheel, { passive: false });
    return () => element.removeEventListener("wheel", onWheel);
  }, []);

  const fromPointer = (event: PointerEvent<HTMLDivElement>): number => {
    const rect = event.currentTarget.getBoundingClientRect();
    return valueAt(control, rect.width ? (event.clientX - rect.left) / rect.width : 0);
  };
  const onPointerDown = (event: PointerEvent<HTMLDivElement>) => {
    if (event.button !== 0) return;
    event.currentTarget.setPointerCapture?.(event.pointerId);
    event.currentTarget.focus();
    gesture.current = { id: newGesture(), at: Date.now() };
    onChange(fromPointer(event), gesture.current.id);
  };
  const onPointerMove = (event: PointerEvent<HTMLDivElement>) => {
    if (!gesture.current || !event.currentTarget.hasPointerCapture?.(event.pointerId)) return;
    onChange(fromPointer(event), gesture.current.id);
  };
  const onPointerUp = (event: PointerEvent<HTMLDivElement>) => {
    event.currentTarget.releasePointerCapture?.(event.pointerId);
    gesture.current = null;
  };
  const onKeyDown = (event: KeyboardEvent<HTMLDivElement>) => {
    if (event.key === "Home" || event.key === "End") {
      event.preventDefault();
      onChange(snapValue(control, event.key === "Home" ? control.minimum : control.maximum), burstGesture());
      return;
    }
    const moves: Record<string, [1 | -1, boolean]> = {
      ArrowRight: [1, false], ArrowUp: [1, false], ArrowLeft: [-1, false], ArrowDown: [-1, false], PageUp: [1, true], PageDown: [-1, true],
    };
    const move = moves[event.key];
    if (!move) return;
    event.preventDefault();
    onChange(nudge(control, value, move[0], move[1], event.shiftKey), burstGesture());
  };

  const text = displayValue(control, value);
  const { value: number, unit } = splitDisplay(text);
  const percent = `${(positionOf(control, value) * 100).toFixed(2)}%`;
  return (
    <div className={cx("lb-ctl", `fam-${family}`, compact && "lb-ctl--compact", focused && "is-focus")}>
      <div className="lb-ctl__top">
        <span className="lb-ctl__label">{control.label}</span>
        {owned === "scene" && <span className="lb-ctl__own"><Tag tone="scene">SCENE</Tag>
          {onShare && <button type="button" className="lb-ctl__share" onClick={onShare} title="All scenes use one value again">Share</button>}</span>}
      </div>
      <div className="lb-ctl__value"><span>{number}</span>{unit && <small>{unit}</small>}</div>
      <div ref={scaleRef} className="lb-scale" role="slider" tabIndex={0} aria-label={control.label}
        aria-valuemin={control.minimum} aria-valuemax={control.maximum} aria-valuenow={value} aria-valuetext={text}
        onPointerDown={onPointerDown} onPointerMove={onPointerMove} onPointerUp={onPointerUp} onPointerCancel={onPointerUp}
        onKeyDown={onKeyDown} onFocus={onFocusControl} onDoubleClick={() => onChange(snapValue(control, control.defaultValue), newGesture())}>
        <span className="lb-scale__ticks" />
        <span className="lb-scale__track"><span className="lb-scale__fill" style={{ width: percent }} /><span className="lb-scale__thumb" style={{ left: percent }} /></span>
      </div>
    </div>
  );
}
