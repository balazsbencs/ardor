import type { NumberDisplay } from "./types";

const switches: Record<string, string[]> = {
  low_freq: ["Off", "35 Hz", "60 Hz", "110 Hz", "220 Hz"],
  mid_freq: ["Off", "360 Hz", "700 Hz", "1.6 kHz", "3.2 kHz", "4.8 kHz", "7.2 kHz"],
  high_pass: ["Off", "50 Hz", "80 Hz", "160 Hz", "300 Hz"],
  polarity: ["Normal", "Inverted"],
};

export function consoleEqDisplay(key: string): NumberDisplay | undefined {
  const labels = switches[key];
  if (!labels) return undefined;
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
