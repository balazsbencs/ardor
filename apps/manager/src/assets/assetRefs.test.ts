import { expect, it } from "vitest";

import type { Preset } from "../api/types";
import { assetRefs, missingPaths } from "./assetRefs";

const preset: Preset = {
  version: 2, name: "Wide", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [
    { id: "n1", type: "nam", enabled: true, asset: "models/Clean.nam", params: {} },
    { id: "r1", type: "dualRig", enabled: true, asset: "", params: {}, lanes: {
      left: { blocks: [{ id: "c1", type: "cab", enabled: true, asset: "irs/2x12.wav", params: {} }] }, right: { blocks: [] } } },
    { id: "v1", type: "irreverb", enabled: true, asset: "reverb-irs/Chapel.wav", params: {} },
  ],
};

it("lists every file reference with its kind", () => {
  expect(assetRefs(preset)).toEqual([
    { blockId: "n1", path: "models/Clean.nam", kind: "models" },
    { blockId: "c1", path: "irs/2x12.wav", kind: "irs" },
    { blockId: "v1", path: "reverb-irs/Chapel.wav", kind: "reverb-irs" },
  ]);
});

it("finds paths with no file on the pedal", () => {
  const file = (path: string) => ({ id: path, kind: "model" as const, filename: path.split("/")[1], path, sizeBytes: 1 });
  expect([...missingPaths(preset, { models: [file("models/Clean.nam")], irs: [], reverbIrs: [] })]).toEqual(["irs/2x12.wav", "reverb-irs/Chapel.wav"]);
});
