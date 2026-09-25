// Player-facing descriptions keyed by catalog id. The catalog owns names and
// parameters; these add a short description the terse catalog text lacks.
// Style: Simplified Technical English. Say what the block is, then what it
// does to the sound. No slang, no em dashes, 25 words or fewer.
// Any id without an entry falls back to the catalog's own description.
export const effectCopy: Record<string, string> = {
  // amp and cabinet
  nam: 'A Neural Amp Modeler capture of a real amplifier. Load a .nam file. Use its embedded nano model to reduce CPU load.',
  cab: 'A cabinet impulse response. It shapes the amp sound like a real speaker cabinet and microphone. Set its level and dry/wet mix.',
  dualAmp: 'Two amp and cabinet lanes in one block. The left lane goes to the left output, and the right lane goes to the right output.',
  dualRig: 'Two independent effect chains from one guitar input. The left chain goes to the left output, and the right chain goes to the right output.',

  // drive
  'distortion:rat': 'A circuit model of the ProCo RAT distortion. The Filter control works in reverse: turn it up for less treble.',
  'distortion:big_cheese': 'A circuit model of the Lovetone Big Cheese fuzz. Two transistors drive an asymmetric clipper and a Big Muff style tone control.',
  'distortion:tape': 'A tape machine model after the Studer A800. It adds tape saturation, head bump, flutter, and hiss.',

  // dynamics and tone
  'dynamics:compressor': 'A compressor that makes loud and quiet notes more equal. It has knee, makeup gain, a sidechain high-pass filter, and a dry/wet mix.',
  'dynamics:noise_gate': 'A stereo-linked noise gate. It reduces hum and hiss when you stop playing, and it adds no latency.',
  'dynamics:transient_shaper': 'A transient shaper. It makes the pick attack and the sustain stronger or softer, at any playing level.',
  'eq:parametric_eq_5': 'Five parametric bands with frequency, gain, and width, plus high-pass and low-pass filters. A live graph shows the curve.',
  'stereo:widener': 'A mid/side stereo widener. It makes the stereo image wider and keeps the bass in the center, so the sound stays mono-safe.',
  'wah:gcb95': 'A circuit model of the GCB-95 wah. Move the Position control with an expression pedal for the classic sweep.',

  // modulation
  'mod:chorus': 'A short delay with pitch modulation. It makes the sound wider and thicker.',
  'mod:flanger': 'A comb filter that sweeps up and down. More regeneration gives a stronger, metallic sound.',
  'mod:rotary': 'A rotating speaker model with separate horn and drum speeds.',
  'mod:vibe': 'A photocell vibe. It gives a soft, pulsing phase movement.',
  'mod:phaser': 'All-pass stages that sweep notches through the sound. Choose the number of stages for a subtle or strong sweep.',
  'mod:vintage_trem': 'An amp-style tremolo. It moves the volume up and down with a choice of shapes.',
  'mod:poly_octave': 'A polyphonic octave generator. It adds one octave up and one or two octaves down, also on chords.',
  'mod:pattern_trem': 'A rhythmic tremolo that follows a pattern. Set the tempo and the note division.',
  'mod:auto_swell': 'An automatic volume swell. It removes the pick attack, so each note fades in.',
  'mod:filter': 'A resonant filter that an envelope or an LFO moves. Use it for auto-wah and synth sounds.',
  'mod:ladder_sweep': 'A four-pole resonant low-pass filter that sweeps in time with the song.',
  'mod:formant': 'A vowel filter. It moves the tone through vowel shapes such as "ah", "ee", and "oo".',
  'mod:quadrature': 'Quadrature modulation with four modes: ring modulation, pitch vibrato, and frequency shift up or down.',
  'mod:destroyer': 'A bit crusher and sample-rate reducer. It adds lo-fi grit and digital noise.',
  'mod:whammy': 'A pitch shifter that an expression pedal controls. It has ten Whammy presets and nine harmony presets.',
  'mod:harmonizer': 'A harmonizer that follows the key. It shifts by scale steps, so a third stays in the key.',

  // delay
  'delay:digital': 'A clean digital delay with a filter, saturation, and modulation.',
  'delay:tape': 'A tape echo. It adds saturation and flutter to the repeats.',
  'delay:dual': 'Two delay voices. Use ping-pong for repeats that move between left and right.',
  'delay:filter': 'A delay with a resonant filter in the feedback path. The filter sweeps, so the repeats change over time.',
  'delay:lofi': 'A delay with reduced bandwidth and bit depth. The repeats sound old and gritty.',
  'delay:dbucket': 'A bucket-brigade analog delay model. Each repeat is darker than the one before it.',
  'delay:duck': 'A ducking delay. The repeats become quieter while you play and louder when you stop.',
  'delay:pattern': 'A multi-tap delay that follows a rhythmic pattern.',
  'delay:swell': 'A delay that fades in each repeat, so the repeats have no hard attack.',
  'delay:trem': 'A delay with a tremolo on the repeats.',

  // reverb
  irreverb: 'A convolution reverb. It uses a room, plate, or hall impulse response from your asset library.',
  'reverb:room': 'A natural room reverb with a short decay.',
  'reverb:hall': 'A concert hall reverb with a long, smooth decay.',
  'reverb:plate': 'A bright, dense studio plate reverb.',
  'reverb:spring': 'A spring tank reverb, like the reverb in many vintage amps.',
  'reverb:bloom': 'A reverb that becomes louder after the note, then decays.',
  'reverb:cloud': 'A diffuse, dark reverb for ambient pads.',
  'reverb:shimmer': 'A reverb with pitch-shifted voices, such as an octave up. It makes long, bright tails.',
  'reverb:chorale': 'A modulated reverb with a vowel filter. It gives a choir-like texture.',
  'reverb:nonlinear': 'A gated or reverse reverb. The tail stops early or rises instead of decaying.',
  'reverb:swell': 'A reverb that fades in, so the tail rises slowly behind the note.',
  'reverb:magneto': 'A tape drum echo and reverb. It has several heads and adjustable spacing.',
  'reverb:reflections': 'Early reflections only. It puts the guitar in a defined space without a long tail.',
};
