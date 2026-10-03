import { expect, it } from "vitest";

import { missingFiles, usedBy } from "./assetUsage";

const usage = [
  { path: "models/Clean.nam", presets: [{ bank: 0, slot: 0, name: "Clean" }] },
  { path: "models/Gone.nam", presets: [{ bank: 1, slot: 1, name: "Doom" }] },
];
const inventory = { models: [{ id: "c", kind: "model" as const, filename: "Clean.nam", path: "models/Clean.nam", sizeBytes: 1 }], irs: [], reverbIrs: [] };

it("finds the presets that use a file, and says when usage is unknown", () => {
  expect(usedBy(usage, "models/Clean.nam")).toEqual([{ bank: 0, slot: 0, name: "Clean" }]);
  expect(usedBy(usage, "models/Other.nam")).toEqual([]);
  expect(usedBy(undefined, "models/Clean.nam")).toBeUndefined();
});

it("lists referenced paths with no file on the pedal", () => {
  expect(missingFiles(usage, inventory)).toEqual([{ path: "models/Gone.nam", kind: "models", presets: [{ bank: 1, slot: 1, name: "Doom" }] }]);
});

it("reports nothing missing when usage is unknown", () => {
  expect(missingFiles(undefined, inventory)).toEqual([]);
});
