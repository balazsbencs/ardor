export const site = {
  name: 'Ardor',
  tagline: 'The open-source guitar processor that runs the whole rig on a Raspberry Pi.',
  description:
    'Ardor is a standalone Raspberry Pi guitar-processing pedal: realtime neural amp modeling, cabinet impulse responses, 35 studio effects, dual-rig routing, a touchscreen and footswitch interface, and a fully open, reproducible firmware image.',
  repo: 'https://github.com/balazsbencs/ardor',
  license: 'MIT',
};

export type NavLink = { label: string; href: string };

export const primaryNav: NavLink[] = [
  { label: 'Features', href: '/#features' },
  { label: 'Effects', href: '/#effects' },
  { label: 'The UI', href: '/#interface' },
  { label: 'Docs', href: '/docs' },
];

export type DocLink = { label: string; href: string; blurb: string };

export const docsNav: DocLink[] = [
  { label: 'Getting Started', href: '/docs/getting-started', blurb: 'Flash, boot, and make your first sound.' },
  { label: 'Signal Chain & Routing', href: '/docs/signal-chain', blurb: 'How blocks connect from input to output.' },
  { label: 'Effects Reference', href: '/docs/effects', blurb: 'Every block and its parameters.' },
  { label: 'UI & Controls', href: '/docs/ui-guide', blurb: 'Touchscreen, footswitches, and the encoder.' },
  { label: 'Dual Rig & Dual Amp', href: '/docs/dual-rig', blurb: 'Parallel amp and effect lanes.' },
  { label: 'Presets & Storage', href: '/docs/presets', blurb: 'Banks, slots, and the preset format.' },
  { label: 'Hardware', href: '/docs/hardware', blurb: 'Enclosure, wiring, and the build.' },
  { label: 'Manager App', href: '/docs/manager', blurb: 'Edit chains from your computer or browser.' },
];
