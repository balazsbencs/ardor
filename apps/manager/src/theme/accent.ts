import type { CSSProperties } from "react";

export type Theme = "dark" | "light";

export const accentChoices = [
  { name: "Ardor green", value: "#c9ff3d" },
  { name: "Signal blue", value: "#67a6ff" },
  { name: "Stage amber", value: "#ffb347" },
  { name: "Violet", value: "#b88cff" },
  { name: "Coral", value: "#ff7b6b" },
];

export const defaultAccent = accentChoices[0].value;

// The page background each theme paints behind accent-colored text and icons.
const DARK_BACKDROP = "#0a0d0b";
const LIGHT_BACKDROP = "#f4f7f2";
const DARK_INK = "#0a0d0b";
const LIGHT_INK = "#ffffff";
const TEXT_CONTRAST = 4.5;

const HEX = /^#[0-9a-f]{6}$/i;

/** The accent arrives from a color input and from localStorage. Neither is trusted. */
export function normalizeAccent(value: string | null | undefined): string {
  return value && HEX.test(value) ? value.toLowerCase() : defaultAccent;
}

function channels(hex: string): number[] {
  return [1, 3, 5].map((index) => parseInt(hex.slice(index, index + 2), 16));
}

function toHex(values: number[]): string {
  return `#${values.map((value) => Math.round(Math.min(255, Math.max(0, value))).toString(16).padStart(2, "0")).join("")}`;
}

/** Relative luminance, WCAG 2.x definition. */
function luminance(hex: string): number {
  const [red, green, blue] = channels(hex).map((value) => {
    const channel = value / 255;
    return channel <= 0.03928 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4;
  });
  return 0.2126 * red + 0.7152 * green + 0.0722 * blue;
}

export function contrast(a: string, b: string): number {
  const first = luminance(a);
  const second = luminance(b);
  return (Math.max(first, second) + 0.05) / (Math.min(first, second) + 0.05);
}

/** Pick the readable ink for text sitting on the accent, by measured ratio rather than a brightness guess. */
export function accentInk(accent: string): string {
  return contrast(DARK_INK, accent) >= contrast(LIGHT_INK, accent) ? DARK_INK : LIGHT_INK;
}

/**
 * Blend the chosen accent toward the theme's far end until it clears 4.5:1 on that
 * theme's backdrop. The accent is both a fill and a foreground, so it has to read as text.
 */
export function accentForTheme(accent: string, theme: Theme): string {
  const backdrop = theme === "dark" ? DARK_BACKDROP : LIGHT_BACKDROP;
  if (contrast(accent, backdrop) >= TEXT_CONTRAST) return accent;
  const base = channels(accent);
  const target = theme === "dark" ? 255 : 0;
  for (let step = 0.02; step <= 1; step += 0.02) {
    const candidate = toHex(base.map((value) => value + (target - value) * step));
    if (contrast(candidate, backdrop) >= TEXT_CONTRAST) return candidate;
  }
  return theme === "dark" ? LIGHT_INK : DARK_INK;
}

/** The accent custom properties for one theme. Apply on every element that owns a token scope. */
export function accentVariables(accent: string, theme: Theme): CSSProperties {
  const resolved = accentForTheme(normalizeAccent(accent), theme);
  return {
    "--accent": resolved,
    "--focus": resolved,
    "--accent-ink": accentInk(resolved),
  } as CSSProperties;
}
