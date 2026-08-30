export type Feature = {
  id: string;
  title: string;
  kicker: string;
  body: string;
  spec?: { label: string; value: string }[];
};

// Landing highlights. Keep the homepage human and move implementation detail into docs.
export const features: Feature[] = [
  {
    id: 'nam',
    title: 'Neural amp modeling',
    kicker: 'AMP',
    body: 'Load the captures you already trust and bring their feel with you. Ardor keeps the amp at the center of the preset.',
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
    body: 'Bring the cabinet sound that makes the rig feel like yours. Place it in the chain and move on to the playing.',
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
    body: 'A considered palette of modulation, delay, and reverb textures, ready to turn a good amp sound into your sound.',
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
    body: 'Split wide when the song needs it. Two independent lanes keep the left and right sides distinct all the way to the join.',
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
    body: 'Shape the feel without losing the thread. Dynamics and a readable five-band EQ keep the important moves close at hand.',
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
    body: 'Four switches, one screen, and a tuner you can reach without putting the guitar down. The interface tells you what is live at a glance.',
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
