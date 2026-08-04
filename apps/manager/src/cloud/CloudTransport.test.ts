import { describe, expect, it, vi } from "vitest";

import { CloudTransport } from "./CloudTransport";

describe("CloudTransport", () => {
  it("routes preset reads through the selected account device", async () => {
    const fetchMock = vi.fn(async () => new Response(JSON.stringify({ presets: [] }), { status: 200 }));
    const transport = new CloudTransport("device/with spaces", false, fetchMock as typeof fetch);
    await expect(transport.listPresets()).resolves.toEqual([]);
    await expect(transport.getDevice()).resolves.toMatchObject({ capabilities: { presetRead: true, presetWrite: false, presetApply: false } });
    expect(fetchMock).toHaveBeenCalledWith("/v1/devices/device%2Fwith%20spaces/presets", expect.objectContaining({ credentials: "include" }));
  });

  it("adds a fresh idempotency key to preset mutations", async () => {
    const fetchMock = vi.fn(async (_input: RequestInfo | URL, _init?: RequestInit) => new Response(JSON.stringify({ accepted: true, bank: 2, slot: 1 }), { status: 202 }));
    const transport = new CloudTransport("device-1", true, fetchMock as typeof fetch);
    await transport.applyPreset(2, 1);
    const init = fetchMock.mock.calls[0][1] as RequestInit;
    expect(init.method).toBe("POST");
    expect(new Headers(init.headers).get("Idempotency-Key")).toMatch(/^[0-9a-f-]{36}$/);
  });

  it("routes hosted asset reads and multipart uploads through the device", async () => {
    const fetchMock = vi.fn()
      .mockResolvedValueOnce(new Response(JSON.stringify({ assets: [] }), { status: 200 }))
      .mockResolvedValueOnce(new Response(JSON.stringify({ id: "amp.nam", kind: "model", filename: "amp.nam", path: "models/amp.nam", sizeBytes: 2 }), { status: 201 }));
    const transport = new CloudTransport("device-1", true, fetchMock as typeof fetch);

    await expect(transport.listAssets("models")).resolves.toEqual([]);
    await transport.uploadAsset("models", new File(["{}"], "amp.nam"), false);

    expect(fetchMock.mock.calls[0][0]).toBe("/v1/devices/device-1/assets/models");
    const upload = fetchMock.mock.calls[1][1] as RequestInit;
    expect(upload.method).toBe("POST");
    expect(upload.body).toBeInstanceOf(FormData);
  });
});
