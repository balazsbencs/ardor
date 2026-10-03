import type { AssetKind, AssetUse } from "../api/types";
import { bankLabel, fileSize, slotLabel } from "../ui/format";

export type KindInfo = {
  code: string;
  family: "amp" | "cab" | "rev";
  label: string;
  one: string;
  extension: ".nam" | ".wav";
  block: string;
  blockType: "nam" | "cab" | "irreverb";
};

/** Labels and block names the Assets view shows for each kind of file. */
export const KIND_INFO: Record<AssetKind, KindInfo> = {
  models: { code: "NAM", family: "amp", label: "NAM models", one: "NAM model", extension: ".nam", block: "NAM Model", blockType: "nam" },
  irs: { code: "CAB", family: "cab", label: "Cabinet IRs", one: "cabinet IR", extension: ".wav", block: "Cabinet IR", blockType: "cab" },
  "reverb-irs": { code: "IRV", family: "rev", label: "Reverb IRs", one: "reverb IR", extension: ".wav", block: "Convolution Reverb", blockType: "irreverb" },
};

export const ASSET_KINDS: AssetKind[] = ["models", "irs", "reverb-irs"];

/** The preset name, or its place on the pedal when the pedal did not report a name. */
export const presetName = (use: AssetUse): string => use.name?.trim() || `${bankLabel(use.bank)} · ${slotLabel(use.slot)}`;

export const plural = (count: number, one: string, many = `${one}s`): string => `${count} ${count === 1 ? one : many}`;

/** Like fileSize, but an empty set reads 0 KB. */
export const totalSize = (bytes: number): string => (bytes === 0 ? "0 KB" : fileSize(bytes));
