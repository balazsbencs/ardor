import { describe, expect, it } from "vitest";

import { allEffectDefinitions } from "../effects/catalog";
import { capFor, familyOf } from "./family";

describe("family", () => {
  it("gives every catalog block type a family and a device cap label", () => {
    for (const definition of allEffectDefinitions()) {
      expect(familyOf(definition.blockType), definition.blockType).not.toBe("unknown");
      expect(capFor(definition.blockType), definition.blockType).not.toBe(definition.blockType);
    }
  });

  it("uses the pedal's labels and colour families", () => {
    expect([familyOf("nam"), capFor("nam")]).toEqual(["amp", "Neural Amp"]);
    expect([familyOf("distortion"), capFor("distortion")]).toEqual(["amp", "Drive"]);
    expect([familyOf("eq"), capFor("eq")]).toEqual(["util", "EQ"]);
    expect([familyOf("irreverb"), capFor("irreverb")]).toEqual(["rev", "Reverb"]);
    expect(familyOf("futureThing")).toBe("unknown");
  });
});
