import type {
  ApplyPresetResponse,
  Asset,
  AssetKind,
  DeviceStatus,
  Preset,
  PresetSlot,
  PresetSlotSummary,
  RenameAssetResponse,
  WiFiSettings,
  WiFiSettingsUpdate,
  UpdateStatus,
	BackupRestoreResult,
} from "./types";

/**
 * The manager UI's device-facing boundary.
 *
 * The local HTTP client is the only implementation today. Keeping the UI
 * coupled to this contract lets a hosted relay transport be added without
 * changing feature components or their state model.
 */
export interface ManagerTransport {
  getDevice(): Promise<DeviceStatus>;
  getWiFiSettings(): Promise<WiFiSettings>;
  updateWiFiSettings(settings: WiFiSettingsUpdate): Promise<WiFiSettings>;
  getUpdateStatus(): Promise<UpdateStatus>;
  checkForUpdate(): Promise<UpdateStatus>;
  installUpdate(version: string): Promise<UpdateStatus>;
	downloadBackup(): Promise<Blob>;
	restoreBackup(file: File): Promise<BackupRestoreResult>;
  listAssets(kind: AssetKind): Promise<Asset[]>;
  uploadAsset(kind: AssetKind, file: File, overwrite: boolean): Promise<Asset>;
  deleteAsset(kind: AssetKind, assetId: string): Promise<void>;
  renameAsset(kind: AssetKind, assetId: string, filename: string): Promise<RenameAssetResponse>;
  listPresets(): Promise<PresetSlotSummary[]>;
  getPreset(bank: number, slot: number): Promise<PresetSlot>;
  savePreset(bank: number, slot: number, preset: Preset): Promise<PresetSlot>;
  applyPreset(bank: number, slot: number): Promise<ApplyPresetResponse>;
}
