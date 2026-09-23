import { describe, expect, it } from "vitest";

import type { DeviceStatus } from "../../api/types";
import { activeRevisionMatchesDraft } from "./sceneRevision";

const device: DeviceStatus = {
  deviceName: "Ardor", apiVersion: "1", authEnabled: false, dataRootWritable: true,
  maxBanks: 100, slotsPerBank: 4, supportedPresetVersion: 4,
  capabilities: { modelUpload: true, irUpload: true, presetRead: true, presetWrite: true, presetApply: true },
  active: { bank: 2, slot: 1, generation: 81, storedRevisionMatches: true },
};

describe("activeRevisionMatchesDraft", () => {
  it("restores recall eligibility from acknowledged device state after reconnect", () => {
    expect(activeRevisionMatchesDraft(device, { bank: 2, slot: 1 }, false)).toBe(true);
  });

  it("rejects dirty, stale, and different-slot documents", () => {
    expect(activeRevisionMatchesDraft(device, { bank: 2, slot: 1 }, true)).toBe(false);
    expect(activeRevisionMatchesDraft({ ...device, active: { ...device.active!, storedRevisionMatches: false } }, { bank: 2, slot: 1 }, false)).toBe(false);
    expect(activeRevisionMatchesDraft(device, { bank: 2, slot: 2 }, false)).toBe(false);
  });
});
