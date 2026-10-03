import type { NumberDisplay } from "./types";

// A stepped number control whose positions have names, such as a frequency
// switch. The preset keeps the integer position; the display shows the label.
export function labelledNumberDisplay(labels: string[]): NumberDisplay {
  const index = (value: number) => Math.max(0, Math.min(labels.length - 1, Math.round(value)));
  return {
    format: (value) => labels[index(value)],
    toInput: index,
    fromInput: index,
    minimum: 0,
    maximum: labels.length - 1,
    step: 1,
    choices: labels.map((label, value) => ({ label, value })),
  };
}
