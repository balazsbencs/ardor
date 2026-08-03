import { ArdorApiError } from "../api/errors";

export type LocalAuthStatus = {
  state: "disabled" | "setup_required" | "login_required" | "authenticated";
  account?: { username: string };
  insecureTransport: boolean;
};

export type LocalAuthResult = { account: { username: string }; sessionToken?: string };
export type FactoryResetStatus = {
  resetId: string;
  kind: "factory";
  state: "awaiting_physical_confirmation" | "applying" | "expired";
  expiresAt?: string;
};

async function request<T>(baseUrl: string, path: string, init: RequestInit = {}): Promise<T> {
  const response = await fetch(`${baseUrl.replace(/\/+$/, "")}${path}`, { ...init, credentials: "include" });
  const text = response.status === 204 ? "" : await response.text();
  if (!response.ok) {
    let payload: { error?: string; message?: string } = {};
    try { payload = JSON.parse(text) as typeof payload; } catch { /* use HTTP fallback */ }
    throw new ArdorApiError(response.status, payload.error ?? `http_${response.status}`, payload.message ?? response.statusText);
  }
  return (text ? JSON.parse(text) : undefined) as T;
}

const json = (value: unknown): RequestInit => ({
  method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(value),
});

export const localAuthAPI = {
  status: (baseUrl = "") => request<LocalAuthStatus>(baseUrl, "/api/auth/status"),
  setup: (setupCode: string, username: string, password: string, baseUrl = "") => request<LocalAuthResult>(baseUrl, "/api/auth/setup", json({ setupCode, username, password })),
  login: (username: string, password: string, baseUrl = "") => request<LocalAuthResult>(baseUrl, "/api/auth/login", json({ username, password })),
  logout: (baseUrl = "") => request<void>(baseUrl, "/api/auth/logout", { method: "POST" }),
  resetLocalAccess: (baseUrl = "") => request<void>(baseUrl, "/api/auth/reset-local-access", { method: "POST" }),
  beginFactoryReset: (baseUrl = "") => request<FactoryResetStatus>(baseUrl, "/api/reset/factory", { method: "POST" }),
  factoryReset: (resetId: string, baseUrl = "") => request<FactoryResetStatus>(baseUrl, `/api/reset/factory/${encodeURIComponent(resetId)}`),
};
