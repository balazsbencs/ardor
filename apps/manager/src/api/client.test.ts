import { describe, expect, it, vi } from "vitest";
import { ArdorApiClient } from "./client";
import { ArdorApiError } from "./errors";

describe("ArdorApiClient", () => {
  it("adds bearer token when present", async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify({
      deviceName: "Ardor Pedal",
      apiVersion: "0.1.0",
      authEnabled: true,
      dataRootWritable: true,
      maxBanks: 100,
      slotsPerBank: 4,
      supportedPresetVersion: 1,
      capabilities: { modelUpload: true, irUpload: true, presetRead: true, presetWrite: true, presetApply: true },
    })));
    const client = new ArdorApiClient({ baseUrl: "http://pedal", token: "secret", fetchImpl: fetchMock });
    await client.getDevice();
    expect(fetchMock).toHaveBeenCalledWith("http://pedal/api/device", expect.objectContaining({
      headers: expect.objectContaining({ Authorization: "Bearer secret" }),
    }));
  });

  it("calls the default fetch with the window receiver", async () => {
    const fetchMock = vi.fn(async function (this: unknown) {
      if (this !== globalThis) {
        throw new TypeError("Can only call Window.fetch on instances of Window");
      }
      return new Response(JSON.stringify({
        deviceName: "Ardor Pedal",
        apiVersion: "0.1.0",
        authEnabled: false,
        dataRootWritable: true,
        maxBanks: 100,
        slotsPerBank: 4,
        supportedPresetVersion: 1,
        capabilities: { modelUpload: true, irUpload: true, presetRead: true, presetWrite: true, presetApply: true },
      }));
    });
    vi.stubGlobal("fetch", fetchMock);

    try {
      await new ArdorApiClient({ baseUrl: "http://pedal" }).getDevice();
      expect(fetchMock).toHaveBeenCalledWith("http://pedal/api/device", expect.any(Object));
    } finally {
      vi.unstubAllGlobals();
    }
  });

  it("saves presets through the slot endpoint", async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify({
      bank: 1,
      slot: 2,
      preset: { version: 1, name: "A", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [] },
    })));
    const client = new ArdorApiClient({ baseUrl: "http://pedal/", fetchImpl: fetchMock });
    await client.savePreset(1, 2, { version: 1, name: "A", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [] });
    expect(fetchMock).toHaveBeenCalledWith("http://pedal/api/presets/banks/1/slots/2", expect.objectContaining({
      method: "PUT",
      body: expect.stringContaining('"name":"A"'),
    }));
  });

  it("surfaces structured daemon errors", async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify({
      error: "invalid_asset_path",
      message: "asset must stay under data root",
      details: { field: "asset" },
    }), { status: 400, headers: { "Content-Type": "application/json" } }));
    const client = new ArdorApiClient({ baseUrl: "http://pedal", fetchImpl: fetchMock });

    await expect(client.getDevice()).rejects.toEqual(new ArdorApiError(
      400, "invalid_asset_path", "asset must stay under data root", { field: "asset" },
    ));
  });

  it("falls back to plain response text for non-JSON failures", async () => {
    const client = new ArdorApiClient({
      baseUrl: "http://pedal",
      fetchImpl: vi.fn(async () => new Response("pedal unavailable", { status: 500, statusText: "Server Error" })),
    });
    await expect(client.getDevice()).rejects.toMatchObject({
      status: 500, code: "http_500", message: "pedal unavailable",
    });
  });

  it("deletes URL-encoded asset ids and accepts 204 No Content", async () => {
    const fetchMock = vi.fn(async () => new Response(null, { status: 204 }));
    const client = new ArdorApiClient({ baseUrl: "http://pedal", fetchImpl: fetchMock });
    await expect(client.deleteAsset("models", "folder/amp #1.nam")).resolves.toBeUndefined();
    expect(fetchMock).toHaveBeenCalledWith(
      "http://pedal/api/assets/models/folder%2Famp%20%231.nam",
      expect.objectContaining({ method: "DELETE" }),
    );
  });

  it("renames an asset through its encoded endpoint", async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify({
      asset: { id: "clean.nam", kind: "model", filename: "clean.nam", path: "models/clean.nam", sizeBytes: 12 },
      updatedPresetCount: 3,
    })));
    const client = new ArdorApiClient({ baseUrl: "http://pedal", fetchImpl: fetchMock });
    await expect(client.renameAsset("models", "raw capture.nam", "clean.nam")).resolves.toMatchObject({ updatedPresetCount: 3 });
    expect(fetchMock).toHaveBeenCalledWith("http://pedal/api/assets/models/raw%20capture.nam", expect.objectContaining({
      method: "PATCH", body: '{"filename":"clean.nam"}',
    }));
  });

  it("aborts JSON calls after the configured timeout", async () => {
    const fetchMock = vi.fn((_url: string | URL | Request, init?: RequestInit) => new Promise<Response>((_resolve, reject) => {
      init?.signal?.addEventListener("abort", () => reject(new DOMException("Timed out", "AbortError")));
    }));
    const client = new ArdorApiClient({ baseUrl: "http://pedal", fetchImpl: fetchMock, timeoutMs: 5 });
    await expect(client.getDevice()).rejects.toMatchObject({ name: "AbortError" });
  });

  it("returns successful apply responses", async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify({
      accepted: true, bank: 4, slot: 2, message: "queued",
    })));
    const client = new ArdorApiClient({ baseUrl: "http://pedal", fetchImpl: fetchMock });
    await expect(client.applyPreset(4, 2)).resolves.toEqual({
      accepted: true, bank: 4, slot: 2, message: "queued",
    });
  });
});
