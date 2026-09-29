/* Ardor Manager mockups: block definitions and demo presets.
   Block names, families, parameter labels, ranges and the one-enabled
   constraint groups come from apps/manager/src/effects/catalog.v1.json.
   Preset names, asset file names and values are synthetic demo data. */
(function () {
  "use strict";

  const pct = (key, label, def) => ({ key, label, min: 0, max: 1, step: 0.01, unit: "percent", def });
  const db = (key, label, min, max, def, step = 1) => ({ key, label, min, max, step, unit: "db", def });
  const ms = (key, label, min, max, def, step = 1) => ({ key, label, min, max, step, unit: "ms", def });
  const hz = (key, label, min, max, def, step = 10) => ({ key, label, min, max, step, unit: "hz", def });

  const MOD = () => [pct("speed", "Speed", 0.35), pct("depth", "Depth", 0.7), pct("mix", "Mix", 0.5), pct("tone", "Tone", 0.5), pct("level", "Level", 0.5)];
  const DLY = () => [
    { key: "time", label: "Time", min: 0, max: 1, step: 0.005, unit: "delayTime", def: 0.25 },
    pct("repeats", "Repeats", 0.35), pct("mix", "Mix", 0.25), pct("filter", "Filter", 0.5),
    pct("grit", "Saturation", 0), pct("mod_spd", "Mod Rate", 0), pct("mod_dep", "Mod Depth", 0),
  ];
  const REV = () => [pct("decay", "Decay", 0.45), pct("pre_delay", "Pre-delay", 0.15), pct("mix", "Mix", 0.25), pct("tone", "Tone", 0.5), pct("mod", "Mod", 0)];

  // fam: colour family. cap: the card cap text, as on the device.
  // group: catalog constraint group (one enabled block per chain).
  const list = [
    ["nam", "NAM Model", "amp", "Neural amp", "NAM", "Neural Amp Modeler amplifier model.", "nam", [
      { key: "inputMode", label: "Input source", kind: "choice", choices: [["sum", "L+R Avg"], ["left", "Left"], ["right", "Right"]], def: "sum" },
      { key: "useNano", label: "Nano model", kind: "toggle", def: false }]],
    ["dualAmp", "Dual Amp", "amp", "Dual amp", "DAMP", "Two amp and cab lanes, hard left and hard right.", null, []],
    ["dualRig", "Dual Rig", "amp", "Dual rig", "RIG", "Two independent chains, merged left and right.", null, [
      db("leftLevelDb", "Lane A level", -60, 12, 0), db("rightLevelDb", "Lane B level", -60, 12, 0)]],
    ["cab", "Cabinet IR", "cab", "Cab", "CAB", "Convolution cabinet impulse response.", "cab", [
      db("levelDb", "Level", -60, 12, 0), pct("mix", "Mix", 1)]],
    ["dynamics:compressor", "Compressor", "util", "Dynamics", "CMP", "Feed-forward compressor with soft knee.", null, [
      db("threshold_db", "Threshold", -60, 0, -24), { key: "ratio", label: "Ratio", min: 1, max: 20, step: 0.5, unit: "ratio", def: 4 },
      ms("attack_ms", "Attack", 0.1, 200, 10), ms("release_ms", "Release", 10, 2000, 150, 10),
      db("knee_db", "Knee", 0, 24, 6), db("makeup_db", "Makeup", 0, 24, 0), pct("mix", "Mix", 1)]],
    ["dynamics:noise_gate", "Noise Gate", "util", "Dynamics", "GATE", "Stereo-linked gate with zero added latency.", null, [
      db("threshold_db", "Threshold", -80, 0, -55), db("reduction_db", "Reduction", 0, 96, 80),
      ms("attack_ms", "Attack", 0.1, 50, 2, 0.5), ms("hold_ms", "Hold", 0, 500, 50, 5),
      ms("release_ms", "Release", 10, 2000, 150, 10), db("hysteresis_db", "Hysteresis", 0, 18, 6), hz("sidechain_hpf_hz", "Sidechain HPF", 20, 500, 80)]],
    ["dynamics:transient_shaper", "Transient Shaper", "util", "Dynamics", "TRN", "Attack and sustain shaping.", null, [
      pct("attack", "Attack", 0.5), pct("sustain", "Sustain", 0.5), db("output_db", "Output", -24, 12, 0)]],
    ["eq:parametric_eq_5", "Five Band EQ", "util", "EQ", "EQ", "Five-band parametric EQ, ±18 dB, with HPF and LPF.", null, "eq"],
    ["stereo:widener", "Stereo Widener", "util", "Stereo", "WIDE", "Mid/side width with bass mono.", null, [
      { key: "width", label: "Width", min: 0, max: 2, step: 0.05, unit: "x", def: 1 }, ms("delayMs", "Side Delay", 0, 30, 0, 0.5), hz("bassMonoHz", "Bass Mono", 0, 500, 0), db("levelDb", "Level", -24, 12, 0)]],
    ["wah:gcb95", "GCB-95 Wah", "util", "Wah", "WAH", "Circuit model of the classic wah.", null, [
      pct("position", "Position", 0), db("level", "Level", -24, 24, 0)]],
    ["distortion:rat", "RAT Distortion", "amp", "Drive", "RAT", "Circuit model of the op-amp distortion.", null, [
      pct("distortion", "Distortion", 0.5), pct("filter", "Filter", 0.5), pct("volume", "Volume", 0.7)]],
    ["distortion:big_cheese", "Big Cheese Fuzz", "amp", "Drive", "FUZZ", "Circuit model of the silicon fuzz.", null, [
      pct("fuzz", "Fuzz", 0.7), pct("tone", "Tone", 0.5), pct("volume", "Volume", 0.7)]],
    ["distortion:tape", "Tape Machine", "amp", "Drive", "TMC", "Tape saturation with head bump and flutter.", null, [
      db("drive", "Drive", -12, 24, 0), pct("saturation", "Saturation", 0.5), pct("bias", "Bias", 0.5), pct("flutter", "Flutter", 0)]],
  ];
  const mods = [["chorus", "Chorus", "CHO"], ["flanger", "Flanger", "FLG"], ["rotary", "Rotary", "ROT"], ["vibe", "Vibe", "VIBE"], ["phaser", "Phaser", "PHS"],
    ["vintage_trem", "Vintage Trem", "TREM"], ["poly_octave", "Poly Octave", "OCT"], ["pattern_trem", "Pattern Trem", "PTRM"], ["auto_swell", "Auto Swell", "SWL"],
    ["filter", "Filter", "FLT"], ["ladder_sweep", "Ladder Sweep", "LADR"], ["formant", "Formant", "FORM"], ["quadrature", "Quadrature", "QUAD"],
    ["destroyer", "Destroyer", "DSTR"], ["whammy", "Whammy", "WHAM"], ["harmonizer", "Harmonizer", "HARM"]];
  const dlys = [["digital", "Digital Delay", "DIG"], ["tape", "Tape Delay", "TAPE"], ["dual", "Dual Delay", "DUAL"], ["filter", "Filter Delay", "FDLY"],
    ["lofi", "Lo-fi Delay", "LOFI"], ["dbucket", "Bucket Brigade", "BBD"], ["duck", "Duck Delay", "DUCK"], ["pattern", "Pattern Delay", "PAT"],
    ["swell", "Swell Delay", "SWDL"], ["trem", "Tremolo Delay", "TRDL"]];
  const revs = [["room", "Room Reverb", "ROOM"], ["hall", "Hall Reverb", "HALL"], ["plate", "Plate Reverb", "PLATE"], ["spring", "Spring Reverb", "SPRG"],
    ["bloom", "Bloom Reverb", "BLOOM"], ["cloud", "Cloud Reverb", "CLOUD"], ["shimmer", "Shimmer Reverb", "SHIM"], ["chorale", "Chorale Reverb", "CHRL"],
    ["nonlinear", "Nonlinear Reverb", "NLIN"], ["swell", "Swell Reverb", "SWRV"], ["magneto", "Magneto Reverb", "MAG"], ["reflections", "Reflections", "REFL"]];

  mods.forEach(([m, n, c]) => list.push([`mod:${m}`, n, "mod", "Modulation", c, "Modulation block.", "mod", MOD()]));
  dlys.forEach(([m, n, c]) => list.push([`delay:${m}`, n, "dly", "Delay", c, "Delay block.", "delay", DLY()]));
  revs.forEach(([m, n, c]) => list.push([`reverb:${m}`, n, "rev", "Reverb", c, "Reverb block.", "reverb", REV()]));
  list.push(["irreverb", "Convolution Reverb", "rev", "Reverb", "IRV", "Reverb from an impulse response file.", "reverb", [pct("mix", "Mix", 0.35), db("levelDb", "Level", -60, 12, 0)]]);

  list.push(["global", "Global", "util", "Global", "IO", "Preset input and output.", null, [db("inputDb", "Input gain", -60, 24, 0), db("outputDb", "Output level", -60, 12, 0)]]);

  const DEFS = {};
  list.forEach(([id, name, fam, cap, code, desc, group, params]) => {
    DEFS[id] = Object.freeze({ id, name, fam, cap, code, desc, group, eq: params === "eq", params: params === "eq" ? [] : params });
  });

  // Short parameter pairs shown on the card, like the device.
  const MAIN = {
    "dynamics:compressor": ["threshold_db", "ratio"], "dynamics:noise_gate": ["threshold_db", "release_ms"],
    "distortion:rat": ["distortion", "filter"], "distortion:big_cheese": ["fuzz", "tone"], "distortion:tape": ["drive", "saturation"],
    cab: ["levelDb", "mix"], dualRig: ["leftLevelDb", "rightLevelDb"], "wah:gcb95": ["position", "level"],
    "stereo:widener": ["width", "bassMonoHz"], "dynamics:transient_shaper": ["attack", "sustain"], irreverb: ["mix", "levelDb"],
  };
  const mainFor = (def) => MAIN[def.id] || (def.fam === "mod" ? ["speed", "depth"] : def.fam === "dly" ? ["time", "repeats"] : def.fam === "rev" ? ["decay", "mix"] : def.params.slice(0, 2).map((p) => p.key));

  const FAMILIES = [
    { id: "amp", label: "Amp & drive" }, { id: "cab", label: "Cab" }, { id: "util", label: "Dynamics & tone" },
    { id: "mod", label: "Modulation" }, { id: "dly", label: "Delay" }, { id: "rev", label: "Reverb" },
  ];

  const EQ_BANDS = [
    { f: 90, g: -2, q: 0.8 }, { f: 250, g: 1.5, q: 1 }, { f: 900, g: 2.5, q: 1.2 }, { f: 3200, g: -3, q: 3 }, { f: 8000, g: 0, q: 1 },
  ];

  let seq = 0;
  const uid = (defId) => `${DEFS[defId].code.toLowerCase()}-${++seq}`;
  function block(defId, on, values = {}, extra = {}) {
    const def = DEFS[defId];
    const v = {};
    def.params.forEach((p) => { v[p.key] = p.def; });
    Object.assign(v, values);
    const b = { uid: uid(defId), def: defId, on, v };
    if (def.eq) b.bands = EQ_BANDS.map((x) => ({ ...x }));
    return Object.assign(b, extra);
  }

  const glass = [
    block("dynamics:noise_gate", true, { threshold_db: -58 }),
    block("dynamics:compressor", true, { threshold_db: -22, ratio: 3, attack_ms: 12, release_ms: 160, makeup_db: 2 }),
    block("distortion:rat", false, { distortion: 0.32, filter: 0.55, volume: 0.62 }),
    block("nam", true, {}, { asset: "Glass Clean.nam" }),
    block("cab", true, {}, { asset: "Open Back 2x12.wav" }),
    block("eq:parametric_eq_5", true),
    block("mod:chorus", true, { speed: 0.3, depth: 0.55, mix: 0.4 }),
    block("delay:tape", true, { time: 0.38, repeats: 0.42, mix: 0.28, filter: 0.45, grit: 0.2, mod_spd: 0.2, mod_dep: 0.2 }),
    block("reverb:shimmer", true, { decay: 0.7, pre_delay: 0.2, mix: 0.35, tone: 0.45, mod: 0.3 }),
  ];
  const g = (code) => glass.find((b) => DEFS[b.def].code === code).uid;

  const scenes = [
    { id: "s1", name: "Verse", ov: {} },
    { id: "s2", name: "Chorus", ov: { [`${g("RAT")}:on`]: true, [`${g("TAPE")}:mix`]: 0.34 } },
    { id: "s3", name: "Solo", ov: { [`${g("RAT")}:on`]: true, [`${g("CHO")}:on`]: false, [`${g("TAPE")}:repeats`]: 0.55, [`${g("TAPE")}:mix`]: 0.36, [`${g("RAT")}:volume`]: 0.72 } },
    { id: "s4", name: "Outro", ov: { [`${g("SHIM")}:mix`]: 0.6, [`${g("SHIM")}:decay`]: 0.88, [`${g("TAPE")}:repeats`]: 0.6 } },
  ];

  const P = (id, name, blocks, more = {}) => ({ id, name, blocks, inputDb: 0, outputDb: 0, scenes: null, ...more });
  const presets = [
    P("p11", "Glass Cathedral", glass, { scenes, exp: { uid: g("SHIM"), key: "mix", min: 0.2, max: 0.7 } }),
    P("p12", "Clean Sparkle", [block("dynamics:compressor", true), block("nam", true, {}, { asset: "Clean Twin.nam" }), block("cab", true, {}, { asset: "1x12 Blue.wav" }), block("mod:chorus", true), block("delay:digital", true, { time: 0.3 }), block("reverb:plate", true)]),
    P("p13", "Edge of Breakup", [block("dynamics:noise_gate", true), block("nam", true, {}, { asset: "Tweed Crunch.nam" }), block("cab", true, {}, { asset: "Open Back 2x12.wav" }), block("reverb:spring", true, { mix: 0.3 })]),
    P("p14", "Lead Boost", [block("dynamics:noise_gate", true), block("distortion:rat", true, { distortion: 0.45 }), block("nam", true, {}, { asset: "Plexi Lead.nam" }), block("cab", true, {}, { asset: "4x12 V30.wav" }), block("delay:digital", true, { time: 0.42, mix: 0.22 }), block("reverb:hall", true)]),
    P("p21", "Funk Rhythm", [block("dynamics:compressor", true, { ratio: 6 }), block("wah:gcb95", true, { position: 0.4 }), block("nam", true, {}, { asset: "Clean Twin.nam" }), block("cab", true, {}, { asset: "1x12 Blue.wav" }), block("mod:phaser", true), block("reverb:room", true)]),
    P("p22", "Doom Fuzz", [block("distortion:big_cheese", true), block("nam", true, {}, { asset: "Fuzz Stack.nam" }), block("cab", true, {}, { asset: "4x12 V30.wav" }), block("delay:pattern", false), block("reverb:hall", true, { decay: 0.8 })]),
    P("p23", "Slapback", [block("distortion:tape", true, { drive: 4 }), block("nam", true, {}, { asset: "Tweed Crunch.nam" }), block("cab", true, {}, { asset: "Open Back 2x12.wav" }), block("delay:tape", true, { time: 0.12, repeats: 0.1, mix: 0.3 }), block("reverb:spring", true)]),
    P("p24", "Wide Stereo", [
      block("dynamics:compressor", true),
      block("dualRig", true, {}, { lanes: {
        a: [block("nam", true, {}, { asset: "Clean Twin.nam" }), block("cab", true, {}, { asset: "1x12 Blue.wav" }), block("mod:chorus", true)],
        b: [block("nam", true, {}, { asset: "Plexi Lead.nam" }), block("cab", true, {}, { asset: "4x12 V30.wav" }), block("delay:digital", true)],
      } }),
      block("reverb:hall", true, { mix: 0.3 })]),
    P("p31", "Ambient Pad", [block("mod:auto_swell", true), block("nam", true, {}, { asset: "Glass Clean.nam" }), block("cab", true, {}, { asset: "Open Back 2x12.wav" }), block("delay:dual", true, { mix: 0.4 }), block("irreverb", true, { mix: 0.45 }, { asset: "Stone Chapel.wav" })]),
    P("p32", "Swamp Trem", [block("nam", true, {}, { asset: "Tweed Crunch.nam" }), block("cab", true, {}, { asset: "1x12 Blue.wav" }), block("mod:vintage_trem", true), block("reverb:spring", true)]),
  ];

  const banks = [
    { n: 1, name: "Sunday Set", slots: ["p11", "p12", "p13", "p14"] },
    { n: 2, name: "Club Night", slots: ["p21", "p22", "p23", "p24"] },
    { n: 3, name: "Studio", slots: ["p31", "p32", null, null] },
    { n: 4, name: "", slots: [null, null, null, null] },
    { n: 5, name: "", slots: [null, null, null, null] },
  ];

  // Files on the pedal. Sizes are typical for the format; names are demo data.
  const file = (filename, sizeBytes) => ({ id: filename.toLowerCase().replace(/[^a-z0-9]+/g, "-"), filename, sizeBytes });
  const ASSETS = {
    models: [file("Glass Clean.nam", 412300), file("Clean Twin.nam", 398100), file("Tweed Crunch.nam", 405800), file("Plexi Lead.nam", 401200),
      file("Brown Sound.nam", 2310400), file("Bass Machine.nam", 399700), file("Studio Clean DI.nam", 118900)],
    irs: [file("Open Back 2x12.wav", 96044), file("1x12 Blue.wav", 96044), file("4x12 V30.wav", 96044), file("2x10 Tweed.wav", 48044), file("Room 4x12 Far.wav", 192044)],
    "reverb-irs": [file("Stone Chapel.wav", 1152044), file("Plate 140.wav", 576044)],
  };
  // Which asset kind a block reads, from the catalog asset control.
  const ASSET_KIND = { nam: "models", cab: "irs", irreverb: "reverb-irs" };

  window.ArdorData = Object.freeze({
    DEFS, FAMILIES, ASSETS, ASSET_KIND, mainFor, newBlock: block,
    initial() {
      return {
        presets: Object.fromEntries(presets.map((p) => [p.id, structuredClone(p)])),
        banks: structuredClone(banks),
        live: { presetId: "p11", sceneId: "s1" },
        editId: "p11",
        sel: g("TAPE"),
        scene: null,
        dirty: [],
        audition: true,
        assets: structuredClone(ASSETS),
      };
    },
  });
})();
