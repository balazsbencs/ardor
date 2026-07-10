import type {
  ApplyPresetResponse,
  Asset,
  AssetKind,
  DeviceStatus,
  Preset,
  PresetSlot,
  PresetSlotSummary,
} from "./types";

type FetchImpl = typeof fetch;

export type ApiClientConfig = {
  baseUrl: string;
  token?: string;
  fetchImpl?: FetchImpl;
};

export class ArdorApiClient {
  private readonly baseUrl: string;
  private readonly token?: string;
  private readonly fetchImpl: FetchImpl;

  constructor(config: ApiClientConfig) {
    this.baseUrl = config.baseUrl.replace(/\/+$/, "");
    this.token = config.token;
    this.fetchImpl = config.fetchImpl ?? fetch;
  }

  getDevice(): Promise<DeviceStatus> {
    return this.request<DeviceStatus>("/api/device");
  }

  async listAssets(kind: AssetKind): Promise<Asset[]> {
    const response = await this.request<{ assets: Asset[] }>(`/api/assets/${kind}`);
    return response.assets;
  }

  uploadAsset(kind: AssetKind, file: File, overwrite: boolean): Promise<Asset> {
    const body = new FormData();
    body.set("file", file);
    body.set("overwrite", overwrite ? "true" : "false");
    return this.request<Asset>(`/api/assets/${kind}`, { method: "POST", body });
  }

  async listPresets(): Promise<PresetSlotSummary[]> {
    const response = await this.request<{ presets: PresetSlotSummary[] }>("/api/presets");
    return response.presets;
  }

  getPreset(bank: number, slot: number): Promise<PresetSlot> {
    return this.request<PresetSlot>(`/api/presets/banks/${bank}/slots/${slot}`);
  }

  savePreset(bank: number, slot: number, preset: Preset): Promise<PresetSlot> {
    return this.request<PresetSlot>(`/api/presets/banks/${bank}/slots/${slot}`, {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(preset),
    });
  }

  applyPreset(bank: number, slot: number): Promise<ApplyPresetResponse> {
    return this.request<ApplyPresetResponse>(`/api/presets/banks/${bank}/slots/${slot}/apply`, { method: "POST" });
  }

  private async request<T>(path: string, init: RequestInit = {}): Promise<T> {
    const headers: Record<string, string> = Object.fromEntries(new Headers(init.headers).entries());
    if (this.token) {
      headers.Authorization = `Bearer ${this.token}`;
    }
    const response = await this.fetchImpl(`${this.baseUrl}${path}`, { ...init, headers });
    if (!response.ok) {
      throw new Error(`API request failed: ${response.status}`);
    }
    return response.json() as Promise<T>;
  }
}
