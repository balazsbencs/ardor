import { afterEach, describe, expect, it, vi } from "vitest";

import {
  completeTone3000Selection,
  createTone3000SelectUrl,
  downloadTone3000Model,
  TONE3000_REDIRECT_URI,
} from "./client";

function jsonResponse(value: unknown, status = 200): Response {
  return new Response(JSON.stringify(value), {
    status,
    headers: { "Content-Type": "application/json" },
  });
}

afterEach(() => {
  sessionStorage.clear();
  vi.unstubAllGlobals();
});

describe("Tone3000 client", () => {
  it("builds a PKCE Select URL constrained to NAM models", async () => {
    const url = new URL(await createTone3000SelectUrl());

    expect(url.origin).toBe("https://www.tone3000.com");
    expect(url.pathname).toBe("/api/v1/oauth/authorize");
    expect(url.searchParams).toEqual(expect.objectContaining({}));
    expect(url.searchParams.get("prompt")).toBe("select_tone");
    expect(url.searchParams.get("format")).toBe("nam");
    expect(url.searchParams.get("client_id")).toBe("t3k_test_client");
    expect(url.searchParams.get("code_challenge_method")).toBe("S256");
    expect(url.searchParams.get("redirect_uri")).toBe(TONE3000_REDIRECT_URI);
    expect(url.searchParams.get("state")).toBeTruthy();
  });

  it("verifies the callback, exchanges its code, and loads the selected models", async () => {
    const authorizeUrl = new URL(await createTone3000SelectUrl());
    const callback = new URL(TONE3000_REDIRECT_URI);
    callback.search = new URLSearchParams({
      code: "auth-code",
      state: authorizeUrl.searchParams.get("state")!,
      tone_id: "42",
    }).toString();
    const fetchMock = vi.fn()
      .mockResolvedValueOnce(jsonResponse({ access_token: "access", refresh_token: "refresh", expires_in: 3600 }))
      .mockResolvedValueOnce(jsonResponse({
        id: 42, title: "Clean pack", description: "Sparkly", gear: "amp-cab", images: [],
        format: "nam", license: "cc-by", user: { id: 8, username: "maker", avatar_url: null, url: "https://www.tone3000.com/users/maker" },
        url: "https://www.tone3000.com/tones/clean-pack-42",
      }))
      .mockResolvedValueOnce(jsonResponse({ data: [{
        id: 7, model_url: "https://www.tone3000.com/api/v1/models/7/download", name: "Bright / 10",
        size: "standard", tone_id: 42, architecture_version: "1",
      }] }));
    vi.stubGlobal("fetch", fetchMock);

    const selection = await completeTone3000Selection(callback.toString());

    expect(selection.tone.title).toBe("Clean pack");
    expect(selection.models).toHaveLength(1);
    expect(fetchMock).toHaveBeenNthCalledWith(1, "https://www.tone3000.com/api/v1/oauth/token", expect.objectContaining({ method: "POST" }));
    expect(fetchMock).toHaveBeenNthCalledWith(2, "https://www.tone3000.com/api/v1/tones/42", expect.objectContaining({ headers: expect.objectContaining({ Authorization: "Bearer access" }) }));
  });

  it("rejects callbacks from a different loopback port", async () => {
    const callback = new URL(TONE3000_REDIRECT_URI);
    callback.port = "43822";

    await expect(completeTone3000Selection(callback.toString()))
      .rejects.toThrow("unexpected Tone3000 callback URL");
  });

  it("downloads an attributed NAM file and refuses to send tokens to another origin", async () => {
    const selection = {
      tone: {
        id: 42, title: "Pack", description: null, gear: "amp", images: null, format: "nam", license: "t3k",
        user: { id: 8, username: "tone maker", avatar_url: null, url: "https://www.tone3000.com/users/maker" },
        url: "https://www.tone3000.com/tones/pack-42",
      },
      models: [],
      tokens: { accessToken: "access", refreshToken: "refresh", expiresAt: Date.now() + 60_000 },
    };
    const model = {
      id: 7, model_url: "https://www.tone3000.com/api/v1/models/7/download", name: "Bright / 10",
      size: "standard", tone_id: 42, architecture_version: "1" as const,
    };
    const fetchMock = vi.fn().mockResolvedValue(new Response("nam data"));
    vi.stubGlobal("fetch", fetchMock);

    const file = await downloadTone3000Model(selection, model);

    expect(file.name).toBe("T3K_tone_maker_Bright_10_7.nam");
    expect(fetchMock).toHaveBeenCalledWith(new URL(model.model_url), { headers: { Authorization: "Bearer access" } });
    await expect(downloadTone3000Model(selection, { ...model, model_url: "https://evil.example/model.nam" }))
      .rejects.toThrow("unexpected model download address");
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });
});
