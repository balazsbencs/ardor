import type { EffectDefinition, NumberControl } from "../effects/types";

// The two values a card shows, the same pairs as the pedal and the mockup.
const MAIN: Record<string, [string, string]> = {
  "dynamics:compressor": ["threshold_db", "ratio"], "dynamics:noise_gate": ["threshold_db", "release_ms"],
  "dynamics:transient_shaper": ["attack", "sustain"], "distortion:rat": ["distortion", "filter"],
  "distortion:big_cheese": ["fuzz", "tone"], "distortion:tape": ["drive", "saturation"],
  cab: ["levelDb", "mix"], dualRig: ["leftLevelDb", "rightLevelDb"], "wah:gcb95": ["position", "level"],
  "stereo:widener": ["width", "bassMonoHz"], irreverb: ["mix", "levelDb"],
};
const BY_CATEGORY: Partial<Record<EffectDefinition["category"], [string, string]>> = {
  modulation: ["speed", "depth"], delay: ["time", "repeats"], reverb: ["decay", "mix"],
};

export function mainControls(definition: EffectDefinition): NumberControl[] {
  const numbers = definition.controls.filter((control): control is NumberControl => control.kind === "number");
  const keys = MAIN[definition.id] ?? BY_CATEGORY[definition.category];
  if (!keys) return numbers.slice(0, 2);
  return keys.map((key) => numbers.find((control) => control.key === key)).filter((control): control is NumberControl => Boolean(control));
}
