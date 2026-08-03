import { ArdorApiError } from "../api/errors";
import type { ManagerTransport } from "../api/transport";
import type {
  ApplyPresetResponse, Asset, AssetKind, DeviceStatus, Preset, PresetSlot,
  PresetSlotSummary, RenameAssetResponse, WiFiSettings, WiFiSettingsUpdate,
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
        modelUpload: false, irUpload: false, assetRename: false,
        presetRead: true, presetWrite: this.remoteMutationsEnabled, presetApply: this.remoteMutationsEnabled, wifiSettings: false,
      },
    });
  }

  getWiFiSettings(): Promise<WiFiSettings> { return this.unsupported("Wi-Fi settings"); }
  updateWiFiSettings(_settings: WiFiSettingsUpdate): Promise<WiFiSettings> { return this.unsupported("Wi-Fi settings"); }
  listAssets(_kind: AssetKind): Promise<Asset[]> { return Promise.resolve([]); }
  uploadAsset(_kind: AssetKind, _file: File, _overwrite: boolean): Promise<Asset> { return this.unsupported("Asset upload"); }
  deleteAsset(_kind: AssetKind, _assetId: string): Promise<void> { return this.unsupported("Asset deletion"); }
  renameAsset(_kind: AssetKind, _assetId: string, _filename: string): Promise<RenameAssetResponse> { return this.unsupported("Asset rename"); }

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
  private slotPath(bank: number, slot: number): string { return `${this.presetPath()}/banks/${bank}/slots/${slot}`; }

  private unsupported<T>(feature: string): Promise<T> {
    return Promise.reject(new ArdorApiError(501, "cloud_feature_unavailable", `${feature} is not available in the hosted preset phase`));
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
