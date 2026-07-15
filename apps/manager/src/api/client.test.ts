import { describe, expect, it, vi } from "vitest";
import { ArdorApiClient } from "./client";

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
});
