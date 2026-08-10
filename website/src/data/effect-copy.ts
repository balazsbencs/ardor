// Editorial one-liners keyed by catalog id. The catalog owns names and
// parameters; these add a human description the terse catalog text lacks.
// Any id without an entry falls back to the catalog's own description.
export const effectCopy: Record<string, string> = {
  // amp / cab / utility
  nam: 'A Neural Amp Modeler capture — a full amplifier stage learned from a real rig and rendered in realtime.',
  cab: 'Convolution cabinet: run a full-length speaker impulse response with level and dry/wet control.',
  dualAmp: 'Two fixed parallel amp+cab lanes feeding hard-left and hard-right for an instant stereo rig.',
  dualRig: 'Two fully independent chains split from one input and merged into a true stereo field.',
  'dynamics:compressor': 'Studio compressor with adjustable knee, sidechain high-pass, makeup gain, and parallel mix.',
  'dynamics:noise_gate': 'Stereo-linked gate with hold, hysteresis, and sidechain filtering — and no added latency.',
  'eq:parametric_eq_5': 'Five fully parametric bands with frequency, Q, and gain, drawn on a live response graph.',

  // modulation (13)
  'mod:chorus': 'Classic pitch-shimmer thickening from a modulated short delay — from subtle width to full swirl.',
  'mod:flanger': 'Sweeping comb filter with feedback for jet-plane whooshes and metallic resonance.',
  'mod:rotary': 'Rotating-speaker emulation with independent horn and drum speeds and Doppler swirl.',
  'mod:vibe': 'Photocell-style uni-vibe with a watery, phase-driven throb.',
  'mod:phaser': 'Cascaded all-pass stages sweep notches through the signal for a vocal, breathing motion.',
  'mod:vintage_trem': 'Amp-style amplitude tremolo with vintage-voiced depth and shape.',
  'mod:poly_octave': 'Polyphonic octave generation stacked above and below the dry note.',
  'mod:pattern_trem': 'Rhythmic, pattern-sequenced tremolo for chopped and stuttered volume shapes.',
  'mod:auto_swell': 'Automatic volume swells that soften every note attack into a bowed rise.',
  'mod:filter': 'Envelope- and LFO-driven resonant filter for auto-wah and synth-like sweeps.',
  'mod:formant': 'Vowel-formant filtering that morphs the tone through spoken "ah / ee / oo" shapes.',
  'mod:quadrature': 'Quadrature modulation for lush, phase-offset stereo movement.',
  'mod:destroyer': 'Bit-crushing and sample-rate decimation for lo-fi grit and digital destruction.',

  // delay (10)
  'delay:digital': 'Clean, precise digital delay with filtering and a wide time range.',
  'delay:tape': 'Warm tape echo with saturation, filtering, and wow-and-flutter movement.',
  'delay:dual': 'Two delay voices for rhythmic ping-pong and dotted patterns.',
  'delay:filter': 'Delay with a resonant filter in the feedback path for evolving, dubby repeats.',
  'delay:lofi': 'Degraded, band-limited delay for gritty vintage echoes.',
  'delay:dbucket': 'Bucket-brigade analog delay with the characteristic darkening of each repeat.',
  'delay:duck': 'Ducking delay that pulls the repeats down while you play and blooms in the gaps.',
  'delay:pattern': 'Sequenced multi-tap delay for programmable rhythmic patterns.',
  'delay:swell': 'Delay with volume-swell attack softening on the repeats.',
  'delay:trem': 'Delay with tremolo modulating the wet repeats.',

  // reverb (12)
  'reverb:room': 'Tight, natural room ambience for realistic space without wash.',
  'reverb:hall': 'Large concert-hall decay with long, smooth tails.',
  'reverb:plate': 'Bright, dense studio plate reverb — a vocal and lead classic.',
  'reverb:spring': 'Drippy, boingy spring-tank reverb straight out of a vintage amp.',
  'reverb:bloom': 'Swelling reverb that blooms in intensity after the note.',
  'reverb:cloud': 'Diffuse, granular cloud of reflections for ambient pads.',
  'reverb:shimmer': 'Pitch-shifted reverb with octave-up voices for ethereal, choral tails.',
  'reverb:chorale': 'Modulated, chorused reverb for a shimmering vocal texture.',
  'reverb:nonlinear': 'Gated and reverse-style nonlinear reverb for dramatic, truncated tails.',
  'reverb:swell': 'Reverb with an automatic volume-swell attack on the reflected field.',
  'reverb:magneto': 'Tape-drum echo-plus-reverb hybrid with magnetic, saturated character.',
  'reverb:reflections': 'Sparse early-reflection engine for placing the signal in a defined space.',
};
