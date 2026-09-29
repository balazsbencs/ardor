import { expect, it } from "vitest";

import { allEffectDefinitions } from "../effects/catalog";
import { MODULE_CODES, stripCode } from "./codes";

it("has a code for every catalog definition", () => {
  for (const definition of allEffectDefinitions()) expect(MODULE_CODES[definition.id], definition.id).toBeTruthy();
});

it("derives codes from the file name for amps and cabs", () => {
  expect(stripCode({ id: "a", type: "nam", enabled: true, asset: "models/Glass Clean.nam", params: {} })).toBe("GLASS");
  expect(stripCode({ id: "c", type: "cab", enabled: true, asset: "irs/Open Back 2x12.wav", params: {} })).toBe("2X12");
  expect(stripCode({ id: "d", type: "delay", enabled: true, asset: "", params: { mode: "tape" } })).toBe("TAPE");
});
