import type { Asset, AssetKind, Preset, PresetBlock } from "../src/api/types";

/** What the mocked pedal holds. Presets are keyed `bank:slot`. */
export type MockState = {
  presets: Record<string, Preset>;
  assets: Record<AssetKind, Asset[]>;
  active: { bank: number; slot: number };
};

export const slotKey = (bank: number, slot: number): string => `${bank}:${slot}`;

export function assetOf(kind: AssetKind, filename: string, sizeBytes: number): Asset {
  return { id: filename, kind: kind === "models" ? "model" : "ir", filename, path: `${kind}/${filename}`, sizeBytes };
}

const block = (id: string, type: string, params: Record<string, unknown>, extra: Partial<PresetBlock> = {}): PresetBlock =>
  ({ id, type, enabled: true, asset: "", params, ...extra });

const nam = (id: string, asset: string) => block(id, "nam", { inputMode: "sum", useNano: false }, { asset });
const cab = (id: string, asset: string) => block(id, "cab", { levelDb: 0, mix: 1 }, { asset });
const delay = (mode: string, values: Record<string, number>) =>
  ({ mode, time: 0.25, repeats: 0.35, mix: 0.25, filter: 0.5, grit: 0, mod_spd: 0, mod_dep: 0, width: 1, ...values });
const reverb = (mode: string, values: Record<string, number>, param1 = 0.5, param2 = 0.5) =>
  ({ mode, decay: 0.45, pre_delay: 0.15, mix: 0.25, tone: 0.5, mod: 0, param1, param2, ...values });

/** The EQ curve of the mockup (`EQ_BANDS` in mockups/manager-taste/data.js). */
const EQ = {
  mode: "parametric_eq_5",
  high_pass: { enabled: false, frequency_hz: 40, q: 0.70710678, slope_db_per_octave: 12 },
  bands: [[90, -2, 0.8], [250, 1.5, 1], [900, 2.5, 1.2], [3200, -3, 3], [8000, 0, 1]]
    .map(([frequency_hz, gain_db, q]) => ({ enabled: true, frequency_hz, q, gain_db })),
  low_pass: { enabled: false, frequency_hz: 16000, q: 0.70710678, slope_db_per_octave: 12 },
};

const preset = (name: string, blocks: PresetBlock[]): Preset => ({
  version: 1, name, routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks,
});

/** "Glass Cathedral" from mockups/manager-taste/data.js, as a real preset. Values sit on the catalog step grid. */
function glassCathedral(): Preset {
  return preset("Glass Cathedral", [
    block("gate-1", "dynamics", { mode: "noise_gate", threshold_db: -58, reduction_db: 80, attack_ms: 2, hold_ms: 50, release_ms: 150, hysteresis_db: 6, sidechain_hpf_hz: 80 }),
    block("cmp-1", "dynamics", { mode: "compressor", threshold_db: -22, ratio: 3, attack_ms: 12, release_ms: 160, knee_db: 6, makeup_db: 2, input_gain_db: 0, mix: 1, sidechain_hpf_hz: 80, detector: "peak", auto_makeup: false }),
    block("rat-1", "distortion", { mode: "rat", distortion: 0.3, filter: 0.55, volume: 0.6 }, { enabled: false }),
    nam("nam-1", "models/Glass Clean.nam"),
    cab("cab-1", "irs/Open Back 2x12.wav"),
    block("eq-1", "eq", EQ),
    block("cho-1", "mod", { mode: "chorus", speed: 0.3, depth: 0.55, mix: 0.4, tone: 0.5, p1: 0, p2: 0, level: 0.5, p3: 1 }),
    block("tape-1", "delay", delay("tape", { time: 0.4, repeats: 0.4, mix: 0.3, filter: 0.45, grit: 0.2, mod_spd: 0.2, mod_dep: 0.2 })),
    block("verb-1", "reverb", reverb("shimmer", { decay: 0.7, pre_delay: 0.2, mix: 0.35, tone: 0.45, mod: 0.3 }, 0.6666667, 0.5277778)),
  ]);
}

/** "Doom Fuzz" needs a model that is not on the pedal. */
function doomFuzz(): Preset {
  return preset("Doom Fuzz", [
    block("fuzz-1", "distortion", { mode: "big_cheese", fuzz: 0.7, tone: 0.5, volume: 0.7 }),
    nam("nam-1", "models/Fuzz Stack.nam"),
    cab("cab-1", "irs/Open Back 2x12.wav"),
    block("dly-1", "delay", delay("pattern", {}), { enabled: false }),
    block("verb-1", "reverb", reverb("hall", { decay: 0.8 })),
  ]);
}

/** Bank 0: Glass Cathedral (live, FS 1) and Doom Fuzz (FS 2). Three models and one cabinet IR. */
export function defaultState(): MockState {
  return {
    presets: { [slotKey(0, 0)]: glassCathedral(), [slotKey(0, 1)]: doomFuzz() },
    assets: {
      models: [assetOf("models", "Glass Clean.nam", 412300), assetOf("models", "Brown Sound.nam", 2310400), assetOf("models", "Plexi Lead.nam", 401200)],
      irs: [assetOf("irs", "Open Back 2x12.wav", 96044)],
      "reverb-irs": [],
    },
    active: { bank: 0, slot: 0 },
  };
}
