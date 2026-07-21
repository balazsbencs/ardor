import type { NumberDisplay } from "./types";

const clamp = (value: number) => Math.min(1, Math.max(0, value));
const exponent = (curve: number) => curve > 0 ? curve + 1 : curve < 0 ? 1 / (1 - curve) : 1;
const map = (value: number, minimum: number, maximum: number, curve = 0) =>
  minimum + clamp(value) ** exponent(curve) * (maximum - minimum);
const unmap = (value: number, minimum: number, maximum: number, curve = 0) =>
  clamp(((value - minimum) / (maximum - minimum)) ** (1 / exponent(curve)));

const number = (value: number, precision: number) => value.toFixed(precision);
const percent = (value: number) => `${Math.round(Math.min(1, Math.max(0, value)) * 100)}%`;
const frequency = (hz: number) => hz >= 1000
  ? `${number(hz / 1000, hz < 10000 ? 1 : 0)} kHz`
  : `${number(hz, hz < 1 ? 2 : hz < 100 ? 1 : 0)} Hz`;
const milliseconds = (ms: number) => `${number(ms, ms < 100 ? 1 : 0)} ms`;
const seconds = (value: number) => value < 1
  ? milliseconds(value * 1000)
  : `${number(value, value < 10 ? 2 : 1)} s`;
const decibels = (linear: number) => linear <= 0.000001 ? "-inf dB" : `${number(20 * Math.log10(linear), 1)} dB`;
const semitones = (value: number) => {
  const precision = Math.abs(value - Math.round(value)) < 0.01 ? 0 : 1;
  return `${value > 0 ? "+" : ""}${number(value, precision)} st`;
};

function physical(
  minimum: number,
  maximum: number,
  curve: number,
  format: (value: number) => string,
  inputStep: number,
): NumberDisplay {
  return {
    format: (value) => format(map(value, minimum, maximum, curve)),
    toInput: (value) => map(value, minimum, maximum, curve),
    fromInput: (value) => unmap(value, minimum, maximum, curve),
    minimum,
    maximum,
    step: inputStep,
  };
}

function custom(
  format: (value: number) => string,
  toInput: (value: number) => number = (value) => Math.round(clamp(value) * 100),
  fromInput: (value: number) => number = (value) => clamp(value / 100),
  minimum = 0,
  maximum = 100,
  step = 1,
): NumberDisplay {
  return { format, toInput, fromInput, minimum, maximum, step };
}

function mappedChoices(labels: string[], values: number[], indexFor: (value: number) => number): NumberDisplay {
  const entries = labels.map((label, index) => ({
    label,
    value: values[index],
  }));
  return {
    format: (value) => labels[Math.min(labels.length - 1, Math.max(0, indexFor(clamp(value))))],
    toInput: (value) => indexFor(clamp(value)),
    fromInput: (value) => entries[Math.min(labels.length - 1, Math.max(0, Math.round(value)))].value,
    minimum: 0,
    maximum: labels.length - 1,
    step: 1,
    choices: entries,
  };
}

function choices(labels: string[]): NumberDisplay {
  const values = labels.map((_, index) => labels.length === 1 ? 0 : index / (labels.length - 1));
  return mappedChoices(labels, values, (value) => Math.min(labels.length - 1, Math.floor(value * labels.length)));
}

function descendingIntegerChoices(maximum: number, minimum: number): NumberDisplay {
  const labels = Array.from({ length: maximum - minimum + 1 }, (_, index) => `${maximum - index} bit`);
  const values = labels.map((_, index) => labels.length === 1 ? 0 : index / (labels.length - 1));
  return mappedChoices(labels, values, (value) => Math.min(labels.length - 1, Math.trunc(value * (labels.length - 1))));
}

function logPhysical(minimum: number, maximum: number, format: (value: number) => string, inputStep: number): NumberDisplay {
  const toInput = (value: number) => minimum * (maximum / minimum) ** clamp(value);
  return custom(format, toInput, (value) => clamp(Math.log(value / minimum) / Math.log(maximum / minimum)), minimum, maximum, inputStep);
}

function tone(sampleRate: number): NumberDisplay {
  return custom((value) => {
    value = clamp(value);
    if (Math.abs(value - 0.5) < 0.001) return "Flat";
    if (value < 0.5) {
      const maximum = Math.min(20000, sampleRate * 0.45);
      const amount = 1 - value * 2;
      return `LP ${frequency(Math.exp(Math.log(maximum) + amount * (Math.log(200) - Math.log(maximum))))}`;
    }
    const amount = (value - 0.5) * 2;
    return `HP ${frequency(Math.exp(Math.log(20) + amount * (Math.log(3000) - Math.log(20))))}`;
  });
}

const q = (minimum: number, maximum: number) => physical(minimum, maximum, 0, (value) => `Q ${number(value, value < 10 ? 1 : 0)}`, 0.1);
const normalizedPercent = custom(percent);
const scaledPercent = (maximum: number, curve = 0) => physical(0, maximum, curve, (value) => `${number(value, value < 10 ? 1 : 0)}%`, 1);

function modDisplay(mode: string, key: string): NumberDisplay {
  if (key === "speed") {
    if (mode === "vintage_trem") return physical(1, 15, 1, frequency, 0.1);
    if (mode === "destroyer") return physical(1, 48, 1, (value) => `${number(value, value < 10 ? 1 : 0)}x`, 0.1);
    if (mode === "pattern_trem") return physical(30, 480, 1, (value) => `${number(value, 0)} BPM`, 1);
    if (mode === "auto_swell") return physical(10, 500, 1, milliseconds, 1);
    if (mode === "quadrature") return physical(0.1, 1000, 2, frequency, 0.1);
    if (mode === "poly_octave") return scaledPercent(100, 1);
    return physical(0.05, 10, 1, frequency, 0.01);
  }
  if (key === "depth") {
    if (mode === "destroyer") return descendingIntegerChoices(16, 1);
    if (mode === "auto_swell") return custom((value) => `${number(20 * Math.log10(1 + clamp(value)), 1)} dB`);
    if (mode === "quadrature") return physical(0, 80, 0, (value) => `+/-${frequency(value)}`, 1);
    return normalizedPercent;
  }
  if (key === "mix") return normalizedPercent;
  if (key === "tone") {
    if (mode === "filter") return physical(80, 12000, 0, frequency, 10);
    if (mode === "destroyer") return physical(80, 21600, 0, frequency, 10);
    if (mode === "rotary") return physical(500, 2000, 0, frequency, 10);
    if (mode === "phaser") return logPhysical(300, 10000, frequency, 10);
    if (mode === "flanger" || mode === "chorus") return normalizedPercent;
    return tone(48000);
  }
  if (key === "p1") {
    if (mode === "flanger" || mode === "phaser") return scaledPercent(95);
    if (mode === "vibe") return scaledPercent(70);
    if (mode === "filter") return q(0.5, 20);
    if (mode === "formant") return q(2, 10);
    if (mode === "destroyer") return q(0.5, 8.5);
    if (mode === "pattern_trem") return choices(Array.from({ length: 16 }, (_, index) => `Pattern ${index + 1}`));
    if (mode === "auto_swell") return physical(50, 2000, 0, milliseconds, 1);
    if (mode === "rotary") return custom((value) => `${number(1 + clamp(value) ** 2 * 0.6, 1)}x`);
    return normalizedPercent;
  }
  if (key === "p2") {
    if (mode === "chorus") return choices(["dBucket", "Multi", "Vibrato", "Detune", "Digital"]);
    if (mode === "flanger") return choices(["Silver", "Grey", "Black+", "Black-", "Zero+", "Zero-"]);
    if (mode === "rotary") return choices(["Slow", "Fast"]);
    if (mode === "phaser") return choices(["2 stages", "4 stages", "6 stages", "8 stages", "12 stages", "16 stages", "Barber pole"]);
    if (mode === "vintage_trem") return choices(["Tube", "Harmonic", "Photoresistor"]);
    if (mode === "pattern_trem") return choices(["16th", "8th", "Triplet"]);
    if (mode === "filter") return choices(["Sine", "Triangle", "Square", "Ramp up", "Ramp down", "Sample & hold", "Envelope+", "Envelope-"]);
    if (mode === "formant") return choices(["Ah", "Oh", "Oo", "Ee", "Ay", "Ah-Oh", "Oo-Oh"]);
    if (mode === "quadrature") return choices(["AM", "FM", "Shift +", "Shift -"]);
    if (mode === "auto_swell") return scaledPercent(30);
    return normalizedPercent;
  }
  if (key === "level") return custom((value) => decibels(clamp(value) * 2));
  return normalizedPercent;
}

function delayModDepth(mode: string): NumberDisplay {
  const samples: Record<string, number> = { digital: 30, tape: 50, filter: 1500, lofi: 20, dbucket: 20, duck: 15, pattern: 25 };
  if (samples[mode]) return physical(0, samples[mode] * 1000 / 48000, 0, milliseconds, 0.01);
  if (mode === "dual") return scaledPercent(0.5);
  return normalizedPercent;
}

function delayDisplay(mode: string, key: string): NumberDisplay {
  if (key === "time") return physical(mode === "lofi" ? 2 : 60, 2500, 2, milliseconds, 1);
  if (key === "repeats") return scaledPercent(98);
  if (key === "mix") return normalizedPercent;
  if (key === "filter") {
    if (mode === "filter") return q(0.5, 15);
    if (mode === "lofi" || mode === "dbucket") return normalizedPercent;
    return tone(48000);
  }
  if (key === "grit") {
    if (mode === "filter") return choices(["Low-pass", "Band-pass", "High-pass"]);
    if (mode === "pattern") return choices(["Straight", "Dotted 8th", "Triplet"]);
    if (mode === "lofi") return custom((value) => `${16 - Math.trunc(clamp(value) * 12)} bit / ${number(1 + clamp(value) * 15, 1)}x`);
    if (mode === "swell") return custom((value) => `${number(20 * Math.log10(0.05 + clamp(value) * 0.2), 1)} dBFS`);
    return normalizedPercent;
  }
  if (key === "mod_spd") {
    if (mode === "swell") return custom(
      (value) => seconds(1.5 - 1.48 * clamp(value) ** 2),
      (value) => 1.5 - 1.48 * clamp(value) ** 2,
      (value) => Math.sqrt(clamp((1.5 - value) / 1.48)), 0.02, 1.5, 0.01,
    );
    return physical(0.05, 10, 1, frequency, 0.01);
  }
  if (key === "mod_dep") {
    if (mode === "swell") return custom(
      (value) => seconds(2.5 - 2.42 * clamp(value)),
      (value) => 2.5 - 2.42 * clamp(value),
      (value) => clamp((2.5 - value) / 2.42), 0.08, 2.5, 0.01,
    );
    return delayModDepth(mode);
  }
  return normalizedPercent;
}

const decayRanges: Record<string, [number, number]> = {
  room: [0.2, 20], spring: [0.8, 10], cloud: [1, 50], magneto: [0.2, 1.5],
  nonlinear: [0.05, 2], bloom: [1, 30], swell: [0.5, 20],
};

function reverbDisplay(mode: string, key: string): NumberDisplay {
  if (key === "decay") {
    if (mode === "reflections") return physical(13.3, 40, 0, (value) => `${number(value, 0)}%`, 1);
    const [minimum, maximum] = decayRanges[mode] ?? [0.5, 20];
    return physical(minimum, maximum, mode === "reflections" ? 0 : mode === "magneto" || mode === "nonlinear" ? 1 : 2, seconds, 0.01);
  }
  if (key === "pre_delay") return mode === "magneto" ? scaledPercent(95) : physical(0, 500, 0, milliseconds, 1);
  if (key === "mix") return normalizedPercent;
  if (key === "tone") return tone(24000);
  if (key === "mod") {
    if (mode === "plate") return physical(0.3, 2, 0, frequency, 0.01);
    if (mode === "magneto") return physical(40, 80, 0, (value) => `${number(value, 0)}%`, 1);
    if (mode === "reflections") return physical(0.05, 0.5, 0, frequency, 0.01);
    if (mode === "shimmer") return normalizedPercent;
    const samples = ["spring", "chorale", "swell"].includes(mode) ? 4 : 8;
    return physical(0, samples * 1000 / 24000, 0, milliseconds, 0.01);
  }
  if (key === "param1") {
    if (mode === "bloom") return physical(0.5, 5, 1, seconds, 0.01);
    if (mode === "swell") return physical(0.08, 4, 1, seconds, 0.01);
    if (mode === "shimmer") return physical(-12, 24, 0, semitones, 0.1);
    if (mode === "chorale") return choices(["Ah", "Oh", "Oo", "Ee", "Ay", "Ah-Oh", "Oo-Oh"]);
    if (mode === "spring") return custom((value) => `${number(2 ** (clamp(value) * 3), 1)}x`);
    if (mode === "hall") return physical(35, 80, 0, (value) => `${number(value, 0)}%`, 1);
    if (mode === "plate") return physical(4 * 1000 / 24000, 20 * 1000 / 24000, 0, milliseconds, 0.01);
    if (mode === "cloud") return physical(50, 85, 0, (value) => `${number(value, 0)}%`, 1);
    if (mode === "nonlinear") return choices(["Swoosh", "Reverse", "Ramp", "Gate", "Gauss", "Bounce"]);
    if (mode === "magneto") return mappedChoices(
      ["3 heads", "4 heads", "6 heads"], [0, 0.5, 1],
      (value) => value < 0.48 ? 0 : value <= 0.82 ? 1 : 2,
    );
    return normalizedPercent;
  }
  if (key === "param2") {
    if (mode === "bloom") return scaledPercent(70);
    if (mode === "shimmer") return physical(-12, 24, 0, semitones, 0.1);
    if (mode === "spring" || mode === "chorale") return choices(["Mild", "Medium", "High"]);
    if (mode === "nonlinear") return physical(40, 80, 0, (value) => `${number(value, 0)}%`, 1);
    if (mode === "swell") return choices(["Wet swell", "Dry swell"]);
    if (mode === "magneto") return mappedChoices(["Even", "Golden"], [0, 1], (value) => value <= 0.5 ? 0 : 1);
    return normalizedPercent;
  }
  return normalizedPercent;
}

export function daisyNumberDisplay(blockType: string, mode: string, key: string): NumberDisplay | undefined {
  if (blockType === "mod") return modDisplay(mode, key);
  if (blockType === "delay") return delayDisplay(mode, key);
  if (blockType === "reverb") return reverbDisplay(mode, key);
  return undefined;
}

export function daisyNormalizedStep(blockType: string, mode: string, key: string, display?: NumberDisplay): number {
  if (display?.choices) return 1;
  if (["time", "speed", "mod_spd", "decay", "pre_delay"].includes(key)) return 0.001;
  if (blockType === "reverb"
      && ((mode === "shimmer" && ["param1", "param2"].includes(key))
          || (key === "param1" && ["bloom", "swell"].includes(mode)))) return 0.001;
  if (key === "level") return 0.005;
  return 0.01;
}

export function daisyParameterLabel(blockType: string, mode: string, key: string, fallback: string): string {
  if (blockType === "delay") {
    if (key === "mod_spd" && mode === "tape") return "Flutter Rate";
    if (key === "mod_dep" && mode === "tape") return "Flutter";
    if (key === "filter" && mode === "filter") return "Resonance";
    if (key === "filter" && mode === "lofi") return "Anti-alias";
    if (mode === "swell" && key === "mod_spd") return "Attack";
    if (mode === "swell" && key === "mod_dep") return "Release";
    if (mode === "trem" && key === "mod_spd") return "Trem Rate";
    if (mode === "trem" && key === "mod_dep") return "Trem Depth";
  }
  if (blockType === "reverb" && mode === "magneto") {
    if (key === "pre_delay") return "Feedback";
    if (key === "mod") return "Diffusion";
  }
  if (blockType === "reverb" && key === "mod") {
    if (mode === "plate") return "Mod Rate";
    if (mode === "spring") return "Wobble";
    if (mode === "shimmer") return "Shimmer";
    if (!["magneto", "reflections"].includes(mode)) return "Mod Depth";
  }
  if (blockType === "reverb" && mode === "hall" && key === "param1") return "Diffusion";
  return fallback;
}
