import { describe, expect, it } from "vitest";

import fixture from "./daisyValues.fixture.json";
import { daisyNumberDisplay, daisyParameterLabel } from "./daisyValues";

// daisyValues.ts is a hand-written mirror of formatMod/formatDelay/formatReverb
// in src/daisyfx/DaisyFxCatalog.cpp. Nothing used to hold the two together, and
// they drifted: the Whammy was missing entirely, and the Quadrature sub-mode list
// still read "FM" after the C++ renamed it. The fixture is generated from the
// device itself by apps/daisy-values-dump, so these assertions fail whenever the
// two disagree.
//
// Regenerate after changing the C++ formatters:
//   cmake --build <build> --target daisy-values-dump
//   ./<build>/daisy-values-dump > apps/manager/src/effects/daisyValues.fixture.json

// The device computes in 32-bit float and the manager in 64-bit double, so a
// sample landing on a rounding boundary can differ by one unit in the last
// displayed digit. That is not drift worth failing over — the formatter is the
// same, the arithmetic width is not. Anything larger, or any difference in the
// suffix, is a real mismatch.
function matches(actual: string, expected: string): boolean {
  if (actual === expected) return true;
  const pattern = /^(-?\d+(?:\.(\d+))?)(.*)$/;
  const a = pattern.exec(actual);
  const b = pattern.exec(expected);
  if (!a || !b) return false;
  if (a[3] !== b[3]) return false;                       // suffix must match
  if ((a[2]?.length ?? 0) !== (b[2]?.length ?? 0)) return false;  // precision must match
  const ulp = 10 ** -(b[2]?.length ?? 0);
  return Math.abs(Number(a[1]) - Number(b[1])) <= ulp * 1.001;
}

type Sample = { value: number; text: string };
type Param = {
  key: string;
  label: string;
  defaultValue: number;
  choiceCount: number;
  samples: Sample[];
};
type Effect = { id: string; blockType: string; mode: string; params: Param[] };

const effects = (fixture as { effects: Effect[] }).effects;

describe("daisyValues mirrors the device catalog", () => {
  it("covers every effect the device exposes", () => {
    expect(effects.length).toBeGreaterThan(0);
  });

  for (const effect of effects) {
    describe(effect.id, () => {
      for (const param of effect.params) {
        it(`renders ${param.key} the way the device does`, () => {
          const display = daisyNumberDisplay(effect.blockType, effect.mode, param.key);

          if (param.choiceCount > 0) {
            // A parameter the device renders as a discrete list must be a list
            // here too, with the same entries in the same order — otherwise the
            // manager shows a percentage slider where the pedal shows names.
            expect(
              display?.choices,
              `${effect.id}/${param.key} is a ${param.choiceCount}-way choice on the device`,
            ).toBeDefined();
            expect(display?.choices?.map(({ label }) => label)).toEqual(
              param.samples.map(({ text }) => text),
            );
          }

          for (const sample of param.samples) {
            // Continuous parameters without a display fall back to a percentage
            // on both sides, which the device also reports as "NN%".
            const rendered = display ? display.format(sample.value) : `${Math.round(sample.value * 100)}%`;
            expect(
              matches(rendered, sample.text),
              `${effect.id}/${param.key} at ${sample.value}: got "${rendered}", device says "${sample.text}"`,
            ).toBe(true);
          }
        });
      }

      it("uses the device's parameter labels", () => {
        for (const param of effect.params) {
          expect(daisyParameterLabel(effect.blockType, effect.mode, param.key, param.label))
            .toBe(param.label);
        }
      });
    });
  }
});
