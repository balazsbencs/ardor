import { expect, it } from "vitest";

import { getEffectDefinition } from "../effects/catalog";
import { mainControls, textValues } from "./mainValues";

it("picks the device card pairs", () => {
  expect(mainControls(getEffectDefinition("dynamics:compressor")).map(({ key }) => key)).toEqual(["threshold_db", "ratio"]);
  expect(mainControls(getEffectDefinition("delay:tape")).map(({ key }) => key)).toEqual(["time", "repeats"]);
  expect(mainControls(getEffectDefinition("reverb:shimmer")).map(({ key }) => key)).toEqual(["decay", "mix"]);
  expect(mainControls(getEffectDefinition("mod:chorus")).map(({ key }) => key)).toEqual(["speed", "depth"]);
  expect(mainControls(getEffectDefinition("nam"))).toEqual([]);
});

it("gives NAM its Source and Nano text values and other blocks none", () => {
  expect(textValues(getEffectDefinition("nam"), { inputMode: "right", useNano: false })).toEqual([
    { key: "inputMode", label: "Source", value: "Right" }, { key: "useNano", label: "Nano", value: "Off" },
  ]);
  expect(textValues(getEffectDefinition("delay:tape"), {})).toEqual([]);
});
