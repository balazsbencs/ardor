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

it("never returns another bank's presets while the new bank loads", async () => {
  session.presets = [{ bank: 1, slot: 0, exists: true }, { bank: 2, slot: 0, exists: true }];
  let release: () => void = () => undefined;
  getPreset.mockImplementation(async (bank: number, slot: number) => {
    if (bank === 2) await new Promise<void>((resolve) => { release = resolve; });
    return { bank, slot, preset: make(`B${bank}S${slot}`) };
  });
  const { result, rerender } = renderHook(({ bank }) => useBankPresets(bank), { initialProps: { bank: 1 } });
  await waitFor(() => expect(result.current.get(0)?.name).toBe("B1S0"));
  rerender({ bank: 2 });
  expect(result.current.get(0)).toBeUndefined();
  release();
  await waitFor(() => expect(result.current.get(0)?.name).toBe("B2S0"));
});

it("keeps the other slots when one fails to load", async () => {
  session.presets = [{ bank: 3, slot: 0, exists: true }, { bank: 3, slot: 1, exists: true }];
  getPreset.mockImplementation(async (bank: number, slot: number) => {
    if (slot === 0) throw new Error("boom");
    return { bank, slot, preset: make(`B${bank}S${slot}`) };
  });
  const { result } = renderHook(() => useBankPresets(3));
  await waitFor(() => expect(result.current.get(1)?.name).toBe("B3S1"));
  expect(result.current.has(0)).toBe(false);
});
