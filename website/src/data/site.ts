export const site = {
  name: 'Ardor',
  tagline: 'Open source power. Endless sound.',
  description:
    'Ardor is an open-source guitar pedal for Raspberry Pi. Build sounds with amp models, effects, and a clear touch interface.',
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
  { label: 'Getting Started', href: '/docs/getting-started', blurb: 'Flash the ready-made image and make your first sound.' },
  { label: 'Signal Chain & Routing', href: '/docs/signal-chain', blurb: 'Follow the signal from your guitar to the output.' },
  { label: 'Effects Reference', href: '/docs/effects', blurb: 'Find the effects and controls in Ardor.' },
  { label: 'UI & Controls', href: '/docs/ui-guide', blurb: 'Use the screen, footswitches, and encoder.' },
  { label: 'Dual Rig & Dual Amp', href: '/docs/dual-rig', blurb: 'Use two horizontal amp and effect paths.' },
  { label: 'Presets & Storage', href: '/docs/presets', blurb: 'Choose, save, and organize your sounds.' },
  { label: 'Hardware', href: '/docs/hardware', blurb: 'See the parts and build the pedal safely.' },
  { label: 'Manager App', href: '/docs/manager', blurb: 'Manage sounds from a browser on your computer.' },
];
