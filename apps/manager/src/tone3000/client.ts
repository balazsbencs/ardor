const PKCE_VERIFIER_KEY = "ardor-manager.tone3000.pkce-verifier";
const OAUTH_STATE_KEY = "ardor-manager.tone3000.oauth-state";

export const TONE3000_REDIRECT_URI = "http://localhost:43821/tone3000/callback";

export const TONE3000_CLIENT_ID = import.meta.env.TONE3000_CLIENT_ID?.trim() || "";

const CONFIGURED_BASE_URL = (import.meta.env.TONE3000_BASE_URL?.trim()
  || "https://www.tone3000.com").replace(/\/$/, "");

const TONE3000_API_URL = CONFIGURED_BASE_URL.endsWith("/api/v1")
  ? CONFIGURED_BASE_URL
  : `${CONFIGURED_BASE_URL}/api/v1`;

export type Tone3000User = {
  id: number;
  username: string;
  avatar_url: string | null;
  url: string;
};

export type Tone3000Tone = {
  id: number;
  title: string;
  description: string | null;
  gear: string;
  images: string[] | null;
  format: string;
  license: string;
  user: Tone3000User;
  url: string;
};

export type Tone3000Model = {
  id: number;
  model_url: string;
  name: string;
  size: string;
  tone_id: number;
  architecture_version: "1" | "2" | "custom" | null;
};

type Tone3000Tokens = {
  accessToken: string;
  refreshToken: string;
  expiresAt: number;
};

export type Tone3000Selection = {
  tone: Tone3000Tone;
  models: Tone3000Model[];
  tokens: Tone3000Tokens;
};

type TokenResponse = {
  access_token: string;
  refresh_token: string;
  expires_in: number;
};

type PaginatedResponse<T> = { data: T[] };

function randomBase64Url(byteCount: number): string {
  const bytes = crypto.getRandomValues(new Uint8Array(byteCount));
  return btoa(String.fromCharCode(...bytes))
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=/g, "");
}

async function sha256Base64Url(value: string): Promise<string> {
  const hash = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value));
  return btoa(String.fromCharCode(...new Uint8Array(hash)))
    .replace(/\+/g, "-")
    .replace(/\//g, "_")
    .replace(/=/g, "");
}

async function responseError(response: Response, fallback: string): Promise<Error> {
  const payload = await response.json().catch(() => undefined) as { error?: string; message?: string } | undefined;
  return new Error(payload?.message || payload?.error || `${fallback} (${response.status})`);
}

async function authenticatedJson<T>(path: string, accessToken: string): Promise<T> {
  const response = await fetch(`${TONE3000_API_URL}${path}`, {
    headers: { Authorization: `Bearer ${accessToken}`, Accept: "application/json" },
  });
  if (!response.ok) throw await responseError(response, "Tone3000 request failed");
  return response.json() as Promise<T>;
}

export function tone3000Configured(): boolean {
  return TONE3000_CLIENT_ID.length > 0;
}

export async function createTone3000SelectUrl(): Promise<string> {
  if (!tone3000Configured()) throw new Error("Tone3000 is not configured for this build.");

  const verifier = randomBase64Url(32);
  const [challenge, state] = await Promise.all([
    sha256Base64Url(verifier),
    Promise.resolve(randomBase64Url(16)),
  ]);
  sessionStorage.setItem(PKCE_VERIFIER_KEY, verifier);
  sessionStorage.setItem(OAUTH_STATE_KEY, state);

  const url = new URL(`${TONE3000_API_URL}/oauth/authorize`);
  url.search = new URLSearchParams({
    client_id: TONE3000_CLIENT_ID,
    redirect_uri: TONE3000_REDIRECT_URI,
    response_type: "code",
    code_challenge: challenge,
    code_challenge_method: "S256",
    state,
    prompt: "select_tone",
    format: "nam",
    menubar: "true",
  }).toString();
  return url.toString();
}

export async function completeTone3000Selection(callbackUrl: string): Promise<Tone3000Selection> {
  const callback = new URL(callbackUrl);
  const expected = new URL(TONE3000_REDIRECT_URI);
  if (callback.origin !== expected.origin || callback.pathname !== expected.pathname) {
    throw new Error("Ignored an unexpected Tone3000 callback URL.");
  }

  const verifier = sessionStorage.getItem(PKCE_VERIFIER_KEY);
  const expectedState = sessionStorage.getItem(OAUTH_STATE_KEY);
  sessionStorage.removeItem(PKCE_VERIFIER_KEY);
  sessionStorage.removeItem(OAUTH_STATE_KEY);

  if (!expectedState || callback.searchParams.get("state") !== expectedState) {
    throw new Error("Tone3000 sign-in could not be verified. Please try again.");
  }
  if (callback.searchParams.get("canceled") === "true") {
    throw new Error("Tone3000 browsing was canceled.");
  }
  const oauthError = callback.searchParams.get("error");
  if (oauthError) throw new Error(`Tone3000 sign-in failed: ${oauthError}`);

  const code = callback.searchParams.get("code");
  const toneId = callback.searchParams.get("tone_id");
  if (!code || !toneId || !verifier) throw new Error("Tone3000 returned an incomplete selection.");

  const tokenResponse = await fetch(`${TONE3000_API_URL}/oauth/token`, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded", Accept: "application/json" },
    body: new URLSearchParams({
      grant_type: "authorization_code",
      code,
      code_verifier: verifier,
      redirect_uri: TONE3000_REDIRECT_URI,
      client_id: TONE3000_CLIENT_ID,
    }),
  });
  if (!tokenResponse.ok) throw await responseError(tokenResponse, "Tone3000 sign-in failed");

  const tokenPayload = await tokenResponse.json() as TokenResponse;
  const tokens: Tone3000Tokens = {
    accessToken: tokenPayload.access_token,
    refreshToken: tokenPayload.refresh_token,
    expiresAt: Date.now() + tokenPayload.expires_in * 1000,
  };
  const [tone, modelResponse] = await Promise.all([
    authenticatedJson<Tone3000Tone>(`/tones/${encodeURIComponent(toneId)}`, tokens.accessToken),
    authenticatedJson<PaginatedResponse<Tone3000Model>>(
      `/models?tone_id=${encodeURIComponent(toneId)}&page_size=300`,
      tokens.accessToken,
    ),
  ]);
  if (tone.format !== "nam") throw new Error("The selected Tone3000 tone is not a NAM tone.");
  if (modelResponse.data.length === 0) throw new Error("This Tone3000 tone has no downloadable NAM models.");
  return { tone, models: modelResponse.data, tokens };
}

function deviceFilename(selection: Tone3000Selection, model: Tone3000Model): string {
  const attribution = `T3K_${selection.tone.user.username}_${model.name}_${model.id}`;
  const stem = attribution
    .normalize("NFKD")
    .replace(/[^a-zA-Z0-9_.-]+/g, "_")
    .replace(/^[-_.]+|[-_.]+$/g, "")
    .slice(0, 110);
  return `${stem || `T3K_model_${model.id}`}.nam`;
}

export async function downloadTone3000Model(
  selection: Tone3000Selection,
  model: Tone3000Model,
): Promise<File> {
  const modelUrl = new URL(model.model_url);
  const apiUrl = new URL(TONE3000_API_URL);
  if (modelUrl.origin !== apiUrl.origin) {
    throw new Error("Tone3000 returned an unexpected model download address.");
  }
  const response = await fetch(modelUrl, {
    headers: { Authorization: `Bearer ${selection.tokens.accessToken}` },
  });
  if (!response.ok) throw await responseError(response, "Tone3000 model download failed");
  const blob = await response.blob();
  return new File([blob], deviceFilename(selection, model), {
    type: blob.type || "application/octet-stream",
  });
}
