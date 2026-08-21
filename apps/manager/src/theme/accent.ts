import type { CSSProperties } from "react";

/** Mirrors the named Panel palettes in src/ui/LvglUiStyle.cpp. */
export type PaletteId = "slate" | "ink" | "sodium" | "nord";

type Palette = {
  id: PaletteId;
  name: string;
  plate: string;
  plate2: string;
  plate3: string;
  engrave: string;
  engraveLo: string;
  engraveOff: string;
  rule: string;
  lamp: string;
  warn: string;
  faultLine: string;
  faultText: string;
  laneL: string;
  laneR: string;
  amp: string;
  cab: string;
  utility: string;
  modulation: string;
  delay: string;
  reverb: string;
};

export const palettes: readonly Palette[] = [
  { id: "slate", name: "Slate", plate: "#212528", plate2: "#2a2f33", plate3: "#191c1f", engrave: "#e2e4e3", engraveLo: "#8d9499", engraveOff: "#5b6266", rule: "#3b4247", lamp: "#d8422f", warn: "#c9973f", faultLine: "#6b463c", faultText: "#bb9186", laneL: "#7fa6c8", laneR: "#c9a06a", amp: "#a8814e", cab: "#939a9e", utility: "#5f7f9c", modulation: "#5d8f80", delay: "#8175a0", reverb: "#a8785c" },
  { id: "ink", name: "Ink", plate: "#10161f", plate2: "#182130", plate3: "#0b1017", engrave: "#dde6ee", engraveLo: "#7e8fa3", engraveOff: "#4d5b6b", rule: "#2a3646", lamp: "#5fd0e8", warn: "#d9a04e", faultLine: "#5c3946", faultText: "#c08e97", laneL: "#6d8fd0", laneR: "#c99050", amp: "#c99050", cab: "#8296ab", utility: "#6d8fd0", modulation: "#4fa89a", delay: "#8d7fc4", reverb: "#cf7a86" },
  { id: "sodium", name: "Sodium", plate: "#0c0b09", plate2: "#16140f", plate3: "#070605", engrave: "#f0e4cd", engraveLo: "#9a8f7a", engraveOff: "#5e574a", rule: "#302b22", lamp: "#ffb01f", warn: "#c98a3c", faultLine: "#5d3a2e", faultText: "#c19183", laneL: "#7f9ab5", laneR: "#c9924f", amp: "#b06a3a", cab: "#9a8f7a", utility: "#6f8296", modulation: "#5f9280", delay: "#8b7aa5", reverb: "#b3705f" },
  { id: "nord", name: "Nord", plate: "#2e3440", plate2: "#3b4252", plate3: "#242933", engrave: "#d8dee9", engraveLo: "#8b96a8", engraveOff: "#4c566a", rule: "#434c5e", lamp: "#88c0d0", warn: "#ebcb8b", faultLine: "#5c3a40", faultText: "#c38f96", laneL: "#81a1c1", laneR: "#d08770", amp: "#d6975f", cab: "#8fa0b8", utility: "#5e81ac", modulation: "#8fbcbb", delay: "#b48ead", reverb: "#c17a72" },
];

export const defaultPalette: PaletteId = "slate";

export function normalizePalette(value: string | null | undefined): PaletteId {
  return palettes.some((palette) => palette.id === value) ? value as PaletteId : defaultPalette;
}

export function paletteById(id: PaletteId): Palette {
  return palettes.find((palette) => palette.id === id) ?? palettes[0];
}

/** A semantic CSS scope shared by the app shell and Radix portal surfaces. */
export function paletteVariables(id: PaletteId): CSSProperties {
  const palette = paletteById(id);
  return {
    "--bg": palette.plate,
    "--surface": palette.plate2,
    "--surface-raised": palette.plate2,
    "--surface-muted": palette.plate3,
    "--line": palette.rule,
    "--line-strong": palette.engraveLo,
    "--text": palette.engrave,
    "--muted": palette.engraveLo,
    "--faint": palette.engraveLo,
    "--disabled": palette.engraveOff,
    "--lamp": palette.lamp,
    "--accent": palette.lamp,
    "--accent-ink": palette.plate,
    "--focus": palette.engrave,
    "--danger": palette.faultText,
    "--fault-line": palette.faultLine,
    "--warning": palette.warn,
    "--info": palette.laneL,
    "--success": palette.modulation,
    "--amp": palette.amp,
    "--cabinet": palette.cab,
    "--utility": palette.utility,
    "--eq": palette.utility,
    "--modulation": palette.modulation,
    "--delay": palette.delay,
    "--reverb": palette.reverb,
    "--unknown": palette.engraveLo,
    "--lane-left": palette.laneL,
    "--lane-right": palette.laneR,
  } as CSSProperties;
}
