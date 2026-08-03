export type Account = { id: string; username: string };

export type Device = {
  id: string;
  role: string;
  claimEpoch: number;
  online: boolean;
  lastSeenAt: string | null;
};

export type Claim = {
  id: string;
  status: "confirm_on_device" | "claimed" | "rejected" | "expired";
  deviceId?: string;
  deviceOnline?: boolean;
  expiresAt: string;
};

export class CloudAPIError extends Error {
  constructor(public readonly status: number, public readonly code: string, message: string) {
    super(message);
    this.name = "CloudAPIError";
  }
}

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  const response = await fetch(path, {
    ...init,
    credentials: "include",
    headers: init.body ? { "Content-Type": "application/json", ...init.headers } : init.headers,
  });
  if (!response.ok) {
    const payload = await response.json().catch(() => ({})) as { error?: string; message?: string };
    throw new CloudAPIError(response.status, payload.error ?? "request_failed", payload.message ?? "Request failed");
  }
  if (response.status === 204) return undefined as T;
  return response.json() as Promise<T>;
}

export const cloudAPI = {
  me: () => request<Account>("/v1/auth/me"),
  register: (username: string, password: string) => request<{ account: Account; recoveryCodes: string[] }>("/v1/auth/register", {
    method: "POST", body: JSON.stringify({ username, password }),
  }),
  login: (username: string, password: string) => request<{ account: Account }>("/v1/auth/login", {
    method: "POST", body: JSON.stringify({ username, password }),
  }),
  recover: (username: string, recoveryCode: string, newPassword: string) => request<{ account: Account }>("/v1/auth/recover", {
    method: "POST", body: JSON.stringify({ username, recoveryCode, newPassword }),
  }),
  logout: () => request<void>("/v1/auth/logout", { method: "POST" }),
  logoutAll: () => request<void>("/v1/auth/logout-all", { method: "POST" }),
  devices: () => request<{ devices: Device[] }>("/v1/devices"),
  beginClaim: (code: string) => request<Claim>("/v1/device-claims", { method: "POST", body: JSON.stringify({ code }) }),
  claim: (id: string) => request<Claim>(`/v1/device-claims/${encodeURIComponent(id)}`),
  unclaim: (id: string) => request<void>(`/v1/devices/${encodeURIComponent(id)}/membership`, { method: "DELETE" }),
};
