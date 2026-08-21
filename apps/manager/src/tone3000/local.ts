import { ArdorApiError } from "../api/errors";
import type { Asset } from "../api/types";
import type { Tone3000Selection } from "./types";

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

export function startLocalTone3000Selection(): Promise<StartResponse> {
  return request<StartResponse>("/api/integrations/tone3000/selections", { method: "POST", headers: { "Content-Type": "application/json" }, body: "{}" });
}

export async function getLocalTone3000Selection(flowId: string): Promise<Pick<SelectionResponse, "status" | "message" | "selection">> {
  const response = await request<SelectionResponse>(`/api/integrations/tone3000/selections/${encodeURIComponent(flowId)}`);
  return { status: response.status, message: response.message, selection: response.selection };
}

export function installLocalTone3000Model(flowId: string, modelId: number): Promise<Asset> {
  return request<Asset>(`/api/integrations/tone3000/selections/${encodeURIComponent(flowId)}/install`, {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ modelId }),
  });
}
