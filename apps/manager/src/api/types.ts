export type AssetKind = "models" | "irs";

export type DeviceStatus = {
  deviceName: string;
  apiVersion: string;
  authEnabled: boolean;
  dataRootWritable: boolean;
  maxBanks: 100;
  slotsPerBank: 4;
  supportedPresetVersion: 1;
  capabilities: {
    modelUpload: boolean;
    irUpload: boolean;
    presetRead: boolean;
    presetWrite: boolean;
    presetApply: boolean;
  };
};

export type Asset = {
  id: string;
  kind: "model" | "ir";
  filename: string;
  path: string;
  sizeBytes: number;
};

export type PresetBlock = {
  id: string;
  type: string;
  enabled: boolean;
  asset: string;
  params: Record<string, unknown>;
  [key: string]: unknown;
};

export type Preset = {
  version: 1;
  name: string;
  routing: "serial";
  global: {
    inputGainDb: number;
    outputGainDb: number;
    safetyLimitDb: number;
    [key: string]: unknown;
  };
  blocks: PresetBlock[];
  [key: string]: unknown;
};

export type PresetSlotSummary = {
  bank: number;
  slot: number;
  exists: boolean;
  name?: string;
  unsupportedBlockCount?: number;
  missingAssetCount?: number;
};

export type PresetSlot = {
  bank: number;
  slot: number;
  preset: Preset;
};

export type ApplyPresetResponse = {
  accepted: boolean;
  bank: number;
  slot: number;
  message?: string;
};
