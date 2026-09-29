import { renderHook, waitFor } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import type { Preset } from "../api/types";
import { useBankPresets } from "./useBankPresets";

const make = (name: string): Preset => ({ version: 1, name, routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [] });
const getPreset = vi.fn(async (bank: number, slot: number) => ({ bank, slot, preset: make(`B${bank}S${slot}`) }));
const session = { client: { getPreset }, presets: [{ bank: 1, slot: 0, exists: true }, { bank: 1, slot: 2, exists: true }, { bank: 1, slot: 1, exists: false }] };
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

it("loads the existing slots of the bank and uses the draft for its own slot", async () => {
  const { result } = renderHook(() => useBankPresets(1, { location: { bank: 1, slot: 2 }, preset: make("Draft") }));
  await waitFor(() => expect(result.current.get(0)?.name).toBe("B1S0"));
  expect(result.current.get(2)?.name).toBe("Draft");
  expect(result.current.has(1)).toBe(false);
  expect(getPreset).toHaveBeenCalledTimes(1);
});
