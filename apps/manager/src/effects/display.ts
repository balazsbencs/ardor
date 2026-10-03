import type { NumberControl } from "./types";

export function displayValue(control: NumberControl, value: number): string {
  if (control.display) return control.display.format(value);
  if (control.unit === "percent") return `${Math.round(value * 100)}%`;
  if (control.unit === "ratio") return `${value.toFixed(value % 1 === 0 ? 0 : 1)}:1`;
  if (control.unit === "db") return `${value.toFixed(control.step < 1 ? 1 : 0)} dB`;
  if (control.unit === "ms") return `${value.toFixed(control.step < 1 ? 1 : 0)} ms`;
  if (control.unit === "hz") return `${Math.round(value)} Hz`;
  return String(value);
}
