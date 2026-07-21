import type {
  ApplyPresetResponse,
  Asset,
  AssetKind,
  DeviceStatus,
  Preset,
  PresetSlot,
  PresetSlotSummary,
  RenameAssetResponse,
} from "./types";
import { ArdorApiError } from "./errors";

type FetchImpl = typeof fetch;

export type ApiClientConfig = {
  baseUrl: string;
  token?: string;
  fetchImpl?: FetchImpl;
  timeoutMs?: number;
};

export class ArdorApiClient {
  private readonly baseUrl: string;
  private readonly token?: string;
  private readonly fetchImpl: FetchImpl;
  private readonly timeoutMs: number;

  constructor(config: ApiClientConfig) {
    this.baseUrl = config.baseUrl.replace(/\/+$/, "");
    this.token = config.token;
    this.fetchImpl = config.fetchImpl ?? globalThis.fetch.bind(globalThis);
    this.timeoutMs = config.timeoutMs ?? 10_000;
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
    return this.request<Asset>(`/api/assets/${kind}`, { method: "POST", body }, null);
  }

  deleteAsset(kind: AssetKind, assetId: string): Promise<void> {
    return this.request<void>(`/api/assets/${kind}/${encodeURIComponent(assetId)}`, { method: "DELETE" });
  }

  renameAsset(kind: AssetKind, assetId: string, filename: string): Promise<RenameAssetResponse> {
    return this.request<RenameAssetResponse>(`/api/assets/${kind}/${encodeURIComponent(assetId)}`, {
      method: "PATCH",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ filename }),
    });
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

  private async request<T>(
    path: string,
    init: RequestInit = {},
    timeoutMs: number | null = this.timeoutMs,
  ): Promise<T> {
    const headers: Record<string, string> = Object.fromEntries(new Headers(init.headers).entries());
    if (this.token) {
      headers.Authorization = `Bearer ${this.token}`;
    }
    const controller = new AbortController();
    const abortFromCaller = () => controller.abort(init.signal?.reason);
    if (init.signal?.aborted) abortFromCaller();
    else init.signal?.addEventListener("abort", abortFromCaller, { once: true });
    const timeout = timeoutMs === null
      ? undefined
      : globalThis.setTimeout(() => controller.abort(new DOMException("Request timed out", "AbortError")), timeoutMs);

    try {
      const response = await this.fetchImpl(`${this.baseUrl}${path}`, {
        ...init,
        headers,
        signal: controller.signal,
      });
      const body = response.status === 204 ? "" : await response.text();
      if (!response.ok) {
        let parsed: unknown;
        try {
          parsed = body.length > 0 ? JSON.parse(body) : undefined;
        } catch {
          parsed = undefined;
        }
        if (typeof parsed === "object" && parsed !== null) {
          const error = parsed as Record<string, unknown>;
          if (typeof error.error === "string" && typeof error.message === "string") {
            throw new ArdorApiError(response.status, error.error, error.message, error.details);
          }
        }
        throw new ArdorApiError(
          response.status,
          `http_${response.status}`,
          body.trim() || response.statusText || `API request failed with status ${response.status}`,
        );
      }
      if (body.length === 0) return undefined as T;
      return JSON.parse(body) as T;
    } finally {
      if (timeout !== undefined) globalThis.clearTimeout(timeout);
      init.signal?.removeEventListener("abort", abortFromCaller);
    }
  }
}
