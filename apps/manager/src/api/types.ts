export type AssetKind = "models" | "irs" | "reverb-irs";

export type DeviceStatus = {
  deviceName: string;
  apiVersion: string;
  softwareVersion?: string;
  buildCommit?: string;
  baseSystemVersion?: string;
  updaterVersion?: string;
  authEnabled: boolean;
  localAuthState?: "disabled" | "setup_required" | "login_required" | "authenticated";
  dataRootWritable: boolean;
  maxBanks: 100;
  slotsPerBank: 4;
  supportedPresetVersion: 1 | 2;
  active?: {
    bank: number;
    slot: number;
    name?: string;
  };
  capabilities: {
    modelUpload: boolean;
    irUpload: boolean;
    assetRename?: boolean;
    presetRead: boolean;
    presetWrite: boolean;
    presetApply: boolean;
    wifiSettings?: boolean;
    softwareUpdate?: boolean;
    tone3000?: boolean;
  };
};

export type UpdateSelection = {
  version: string;
  tag: string;
  releaseUrl: string;
  releaseNotes?: string;
  bundleSize: number;
  reflashRequired: boolean;
  incompatibility?: string;
};

export type UpdateStatus = {
  state: "idle" | "checking" | "available" | "downloading" | "verifying" | "staged" | "restarting" | "validating" | "succeeded" | "rolled_back" | "failed" | string;
  enabled: boolean;
  installedVersion: string;
  baseVersion: string;
  updaterVersion: string;
  checkedAt?: string;
  available?: UpdateSelection;
  errorCode?: string;
  errorMessage?: string;
};

export type WiFiSettings = {
  configured: boolean;
  ssid?: string;
  country: string;
  status: "connected" | "connecting" | "disconnected" | "restarting" | string;
  ipAddress?: string;
};

export type WiFiSettingsUpdate = {
  ssid: string;
  password?: string;
  country: string;
};

export type Asset = {
  id: string;
  kind: "model" | "ir";
  filename: string;
  path: string;
  sizeBytes: number;
};

export type RenameAssetResponse = {
  asset: Asset;
  updatedPresetCount: number;
};

export type PresetBlock = {
  id: string;
  type: string;
  enabled: boolean;
  asset: string;
  params: Record<string, unknown>;
  lanes?: {
    left: { blocks: PresetBlock[] };
    right: { blocks: PresetBlock[] };
  };
  [key: string]: unknown;
};

export type Preset = {
  version: 1 | 2;
  name: string;
  routing: "serial";
  global: {
    inputGainDb: number;
    outputGainDb: number;
    safetyLimitDb: number;
    [key: string]: unknown;
  };
  expression?: {
    blockId: string;
    parameter: string;
    minimum: number;
    maximum: number;
    inverted: boolean;
  };
  midiMappings?: Array<{
    channel: number;
    controlChange: number;
    mode: "continuous" | "toggle";
    actions: Array<{
      target: "parameter" | "blockEnabled";
      blockId: string;
      parameter?: string;
      value1: number;
      value2: number;
    }>;
  }>;
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
