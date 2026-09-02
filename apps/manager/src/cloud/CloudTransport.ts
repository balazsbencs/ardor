import { ArdorApiError } from "../api/errors";
import type { ManagerTransport } from "../api/transport";
import type {
  ApplyPresetResponse, Asset, AssetKind, DeviceStatus, Preset, PresetSlot,
  PresetSlotSummary, RenameAssetResponse, WiFiSettings, WiFiSettingsUpdate,
  UpdateStatus,
	BackupRestoreResult,
} from "../api/types";

type FetchImpl = typeof fetch;

export class CloudTransport implements ManagerTransport {
  constructor(
    private readonly deviceId: string,
    private readonly remoteMutationsEnabled: boolean,
    private readonly fetchImpl: FetchImpl = globalThis.fetch.bind(globalThis),
  ) {}

  getDevice(): Promise<DeviceStatus> {
    return Promise.resolve({
      deviceName: "Ardor Pedal", apiVersion: "0.1.0", authEnabled: true, dataRootWritable: true,
      maxBanks: 100, slotsPerBank: 4, supportedPresetVersion: 2,
      capabilities: {
        modelUpload: this.remoteMutationsEnabled, irUpload: this.remoteMutationsEnabled, assetRename: this.remoteMutationsEnabled,
        presetRead: true, presetWrite: this.remoteMutationsEnabled, presetApply: this.remoteMutationsEnabled, wifiSettings: false,
      },
    });
  }

  getWiFiSettings(): Promise<WiFiSettings> { return this.unsupported("Wi-Fi settings"); }
  updateWiFiSettings(_settings: WiFiSettingsUpdate): Promise<WiFiSettings> { return this.unsupported("Wi-Fi settings"); }
  getUpdateStatus(): Promise<UpdateStatus> { return this.unsupported("Device updates"); }
  checkForUpdate(): Promise<UpdateStatus> { return this.unsupported("Device updates"); }
  installUpdate(_version: string): Promise<UpdateStatus> { return this.unsupported("Device updates"); }
	downloadBackup(): Promise<Blob> { return this.unsupported("Backups"); }
	restoreBackup(_file: File): Promise<BackupRestoreResult> { return this.unsupported("Backups"); }
  async listAssets(kind: AssetKind): Promise<Asset[]> {
    const response = await this.request<{ assets: Asset[] }>(this.assetPath(kind));
    return response.assets;
  }

  uploadAsset(kind: AssetKind, file: File, overwrite: boolean): Promise<Asset> {
    const body = new FormData();
    body.set("file", file);
    body.set("overwrite", overwrite ? "true" : "false");
    return this.request<Asset>(this.assetPath(kind), { method: "POST", body });
  }

  async deleteAsset(kind: AssetKind, assetId: string): Promise<void> {
    await this.request<unknown>(`${this.assetPath(kind)}/${encodeURIComponent(assetId)}`, { method: "DELETE" });
  }

  renameAsset(kind: AssetKind, assetId: string, filename: string): Promise<RenameAssetResponse> {
    return this.request<RenameAssetResponse>(`${this.assetPath(kind)}/${encodeURIComponent(assetId)}`, {
      method: "PATCH", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ filename }),
    });
  }

  async listPresets(): Promise<PresetSlotSummary[]> {
    const response = await this.request<{ presets: PresetSlotSummary[] }>(this.presetPath());
    return response.presets;
  }

  getPreset(bank: number, slot: number): Promise<PresetSlot> {
    return this.request<PresetSlot>(this.slotPath(bank, slot));
  }

  savePreset(bank: number, slot: number, preset: Preset): Promise<PresetSlot> {
    return this.request<PresetSlot>(this.slotPath(bank, slot), {
      method: "PUT", headers: { "Content-Type": "application/json", "Idempotency-Key": crypto.randomUUID() },
      body: JSON.stringify(preset),
    });
  }

  applyPreset(bank: number, slot: number): Promise<ApplyPresetResponse> {
    return this.request<ApplyPresetResponse>(`${this.slotPath(bank, slot)}/apply`, {
      method: "POST", headers: { "Idempotency-Key": crypto.randomUUID() },
    });
  }

  private presetPath(): string { return `/v1/devices/${encodeURIComponent(this.deviceId)}/presets`; }
  private assetPath(kind: AssetKind): string { return `/v1/devices/${encodeURIComponent(this.deviceId)}/assets/${kind}`; }
  private slotPath(bank: number, slot: number): string { return `${this.presetPath()}/banks/${bank}/slots/${slot}`; }

  private unsupported<T>(feature: string): Promise<T> {
    return Promise.reject(new ArdorApiError(501, "cloud_feature_unavailable", `${feature} is not available through the hosted manager`));
  }

  private async request<T>(path: string, init: RequestInit = {}): Promise<T> {
    const response = await this.fetchImpl(path, { ...init, credentials: "include" });
    const text = await response.text();
    if (!response.ok) {
      let error: { error?: string; message?: string } = {};
      try { error = JSON.parse(text) as typeof error; } catch { /* use the HTTP fallback */ }
      throw new ArdorApiError(response.status, error.error ?? `http_${response.status}`, error.message ?? response.statusText);
    }
    return JSON.parse(text) as T;
  }
}
