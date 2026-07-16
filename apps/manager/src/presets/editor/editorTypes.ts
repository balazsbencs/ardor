import type { Preset } from "../../api/types";

export type PresetLocation = { bank: number; slot: number };

export type EqBand = {
  enabled: boolean;
  frequency_hz: number;
  q: number;
  gain_db: number;
  [key: string]: unknown;
};

export type PresetHistory = {
  past: Preset[];
  present: Preset;
  future: Preset[];
};

export type EditorState = {
  location: PresetLocation;
  saved: Preset;
  history: PresetHistory;
  selectedBlockId?: string;
  recoveryAvailable?: Preset;
};

export type EditorAction =
  | { type: "load"; location: PresetLocation; preset: Preset }
  | { type: "select-block"; blockId?: string }
  | { type: "set-name"; name: string }
  | { type: "set-global"; key: "inputGainDb" | "outputGainDb"; value: number }
  | { type: "add-block"; definitionId: string; index: number; initialAsset?: string }
  | { type: "move-block"; blockId: string; index: number }
  | { type: "toggle-block"; blockId: string; enabled: boolean }
  | { type: "duplicate-block"; blockId: string }
  | { type: "remove-block"; blockId: string }
  | { type: "set-block-asset"; blockId: string; asset: string }
  | { type: "set-block-param"; blockId: string; key: string; value: unknown }
  | { type: "set-eq-band"; blockId: string; band: number; patch: Partial<EqBand> }
  | { type: "change-definition"; blockId: string; definitionId: string }
  | { type: "reset-block"; blockId: string }
  | { type: "replace-present"; preset: Preset }
  | { type: "mark-saved"; preset: Preset }
  | { type: "undo" }
  | { type: "redo" };
