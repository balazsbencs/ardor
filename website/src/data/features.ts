export type Feature = {
  id: string;
  title: string;
  kicker: string;
  body: string;
  spec?: { label: string; value: string }[];
};

// Landing highlights. Kickers use the instrument's own vernacular.
export const features: Feature[] = [
  {
    id: 'nam',
    title: 'Neural amp modeling',
    kicker: 'AMP',
    body: 'Load any Neural Amp Modeler capture and play through it in realtime. Full and embedded nano submodels let you trade CPU for headroom without leaving the preset.',
    spec: [
      { label: 'Format', value: '.nam' },
      { label: 'Input fold', value: 'sum / left / right' },
      { label: 'Nano mode', value: 'per-model' },
    ],
  },
  {
    id: 'cab',
    title: 'Cabinet impulse responses',
    kicker: 'CAB',
    body: 'Partitioned convolution runs full-length IRs with level and dry/wet mix per block. Cap long IRs on slower hardware when you want to trade tail length for headroom.',
    spec: [
      { label: 'Engine', value: 'partitioned convolution' },
      { label: 'Sample rate', value: '48 kHz' },
      { label: 'Mix', value: '0–100% wet' },
    ],
  },
  {
    id: 'effects',
    title: '35 studio effects',
    kicker: 'FX',
    body: 'Thirteen modulation algorithms, ten delays, and twelve reverbs — each with semantic controls, physical-value readouts, and sensible defaults straight from the catalog.',
    spec: [
      { label: 'Modulation', value: '13' },
      { label: 'Delay', value: '10' },
      { label: 'Reverb', value: '12' },
    ],
  },
  {
    id: 'dualrig',
    title: 'Dual Rig routing',
    kicker: 'SPLIT',
    body: 'Split the signal into two independent chains — two amps, two cabs, two effect lanes — and merge hard-left and hard-right into a true stereo field. On a four-core Pi the lanes run on dedicated realtime cores.',
    spec: [
      { label: 'Lanes', value: '2 × independent' },
      { label: 'Merge', value: 'stereo L/R' },
      { label: 'Cores', value: 'SCHED_FIFO' },
    ],
  },
  {
    id: 'dynamics',
    title: 'Dynamics & 5-band EQ',
    kicker: 'UTILITY',
    body: 'A full-featured compressor with sidechain and auto-makeup, a stereo-linked noise gate with zero added latency, and a five-band parametric EQ with a live response graph.',
    spec: [
      { label: 'Compressor', value: 'sidechain + knee' },
      { label: 'Gate', value: '0 ms lookahead' },
      { label: 'EQ', value: '5 bands, ±18 dB' },
    ],
  },
  {
    id: 'ui',
    title: 'Play-first interface',
    kicker: 'CONTROL',
    body: 'Four preset slots, four footswitches, and a single encoder. A touchscreen drag-and-drop chain editor previews edits live, and a muted chromatic tuner is one gesture away.',
    spec: [
      { label: 'Display', value: 'RPi Touch 2' },
      { label: 'Switches', value: '4 footswitch' },
      { label: 'Tuner', value: 'note / cents' },
    ],
  },
];

// Headline capability numbers for the hero stat strip.
export const heroStats: { value: string; label: string }[] = [
  { value: '35', label: 'built-in effects' },
  { value: '<10 ms', label: 'round-trip latency goal' },
  { value: '48 kHz', label: 'realtime engine' },
  { value: '100%', label: 'open source' },
];
