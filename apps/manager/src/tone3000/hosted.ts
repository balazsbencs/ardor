import { ArdorApiError } from "../api/errors";
import type { Asset } from "../api/types";
import type { Tone3000Selection } from "./types";

export type Tone3000Architecture = "legacy" | "2";

type StartResponse = { flowId: string; authorizeUrl: string; expiresAt: string };
type SelectionResponse = {
  flowId: string;
  status: "pending" | "loading" | "ready" | "failed";
  message?: string;
  selection?: Tone3000Selection;
};

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  const response = await fetch(path, { ...init, credentials: "include" });
  const text = await response.text();
  if (!response.ok) {
    let error: { error?: string; message?: string } = {};
    try { error = JSON.parse(text) as typeof error; } catch { /* use HTTP fallback */ }
    throw new ArdorApiError(response.status, error.error ?? `http_${response.status}`, error.message ?? response.statusText);
  }
  return JSON.parse(text) as T;
}

export function startHostedTone3000Selection(deviceId: string, architecture: Tone3000Architecture): Promise<StartResponse> {
  return request<StartResponse>("/v1/integrations/tone3000/selections", {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ deviceId, architecture }),
  });
}

export async function getHostedTone3000Selection(flowId: string): Promise<{
  status: SelectionResponse["status"];
  message?: string;
  selection?: Tone3000Selection;
}> {
  const response = await request<SelectionResponse>(`/v1/integrations/tone3000/selections/${encodeURIComponent(flowId)}`);
  return { status: response.status, message: response.message, selection: response.selection };
}

export function installHostedTone3000Model(flowId: string, modelId: number): Promise<Asset> {
  return request<Asset>(`/v1/integrations/tone3000/selections/${encodeURIComponent(flowId)}/install`, {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ modelId }),
  });
}
