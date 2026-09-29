import { expect, it } from "vitest";

import { getEffectDefinition } from "../effects/catalog";
import { mainControls } from "./mainValues";

it("picks the device card pairs", () => {
  expect(mainControls(getEffectDefinition("dynamics:compressor")).map(({ key }) => key)).toEqual(["threshold_db", "ratio"]);
  expect(mainControls(getEffectDefinition("delay:tape")).map(({ key }) => key)).toEqual(["time", "repeats"]);
  expect(mainControls(getEffectDefinition("reverb:shimmer")).map(({ key }) => key)).toEqual(["decay", "mix"]);
  expect(mainControls(getEffectDefinition("mod:chorus")).map(({ key }) => key)).toEqual(["speed", "depth"]);
  expect(mainControls(getEffectDefinition("nam"))).toEqual([]);
});
