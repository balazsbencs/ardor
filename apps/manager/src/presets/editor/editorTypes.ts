import type { Preset } from "../../api/types";

export type PresetLocation = { bank: number; slot: number };

export type EqBand = {
  enabled: boolean;
  frequency_hz: number;
  q: number;
  gain_db: number;
  [key: string]: unknown;
};

export type EqPassFilter = {
  enabled: boolean;
  frequency_hz: number;
  q: number;
  slope_db_per_octave: number;
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
  editingSceneId?: string;
  gesture?: string;
  recoveryAvailable?: Preset;
};

export type EditorAction =
  | { type: "load"; location: PresetLocation; preset: Preset }
  | { type: "select-block"; blockId?: string }
  | { type: "select-scene"; sceneId: string }
  | { type: "clear-scene" }
  | { type: "enable-scenes" }
  | { type: "set-scene-name"; sceneId: string; name: string }
  | { type: "set-scene-enter-time"; sceneId: string; value: number }
  | { type: "set-scene-trim"; sceneId: string; value: number }
  | { type: "set-default-scene"; sceneId: string }
  | { type: "set-scene-open-in"; value: "presets" | "scenes" }
  | { type: "copy-scene"; sourceSceneId: string; destinationSceneId: string }
  | { type: "swap-scenes"; firstSceneId: string; secondSceneId: string }
  | { type: "copy-scene-row-across"; sourceSceneId: string; rowKey: string }
  | { type: "set-scene-parameter"; sceneId: string; blockId: string; parameter: string; value: number; gesture?: string }
  | { type: "set-scene-block-enabled"; sceneId: string; blockId: string; value: boolean; gesture?: string }
  | { type: "set-scene-input-gain"; sceneId: string; value: number; gesture?: string }
  | { type: "set-scene-input-scope"; sceneId: string; scope: "shared" | "scene"; gesture?: string }
  | { type: "set-scene-wdw-mix"; sceneId: string; lane: "dry" | "wet"; key: "levelDb" | "pan" | "width" | "enabled"; value: number | boolean; gesture?: string }
  | { type: "set-scene-scope"; sceneId: string; blockId: string; parameter?: string; scope: "shared" | "scene"; value: number | boolean; gesture?: string }
  | { type: "set-name"; name: string }
  | { type: "set-global"; key: "inputGainDb" | "outputGainDb"; value: number; gesture?: string }
  | { type: "set-routing"; routing: "serial" | "wdw" }
  | { type: "set-wdw-mix"; lane: "dry" | "wet"; key: "levelDb" | "pan" | "width" | "enabled"; value: number | boolean; gesture?: string }
  | { type: "set-expression"; expression?: Preset["expression"]; gesture?: string }
  | { type: "add-block"; definitionId: string; index: number; initialAsset?: string }
  | { type: "move-block"; blockId: string; index: number }
  | { type: "add-lane-block"; rigId: string; lane: "left" | "right"; definitionId: string; index: number; initialAsset?: string }
  | { type: "move-lane-block"; rigId: string; blockId: string; lane: "left" | "right"; index: number }
  | { type: "add-wdw-block"; lane: "dry" | "wet"; definitionId: string; index: number; initialAsset?: string }
  | { type: "move-wdw-block"; lane: "dry" | "wet"; blockId: string; index: number }
  | { type: "toggle-block"; blockId: string; enabled: boolean }
  | { type: "duplicate-block"; blockId: string }
  | { type: "remove-block"; blockId: string }
  | { type: "set-block-asset"; blockId: string; asset: string }
  | { type: "set-block-param"; blockId: string; key: string; value: unknown; gesture?: string }
  | { type: "set-scene-bypass"; blockId: string; policy: "cut" | "letRing" }
  | { type: "set-eq-band"; blockId: string; band: number; patch: Partial<EqBand>; gesture?: string }
  | { type: "change-definition"; blockId: string; definitionId: string }
  | { type: "reset-block"; blockId: string }
  | { type: "replace-present"; preset: Preset }
  | { type: "mark-saved"; preset: Preset }
  | { type: "undo" }
  | { type: "redo" };
