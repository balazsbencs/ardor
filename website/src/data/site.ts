export const site = {
  name: 'Ardor',
  tagline: 'Open-source guitar processor for Raspberry Pi.',
  description:
    'Ardor is an open-source guitar processor for Raspberry Pi. It runs NAM amp captures, cabinet IRs, and a full effects chain from four footswitches.',
  repo: 'https://github.com/balazsbencs/ardor',
  license: 'MIT',
};

export type NavLink = { label: string; href: string };

// Labels match the homepage section headings. The anchors stay stable for old links.
export const primaryNav: NavLink[] = [
  { label: 'Listen', href: '/#listen' },
  { label: 'Interface', href: '/#interface' },
  { label: 'Effects', href: '/#effects' },
  { label: 'Manual', href: '/user-manual' },
  { label: 'Docs', href: '/docs' },
];

export type DocLink = { label: string; href: string; blurb: string };

export const docsNav: DocLink[] = [
  { label: 'Getting Started', href: '/docs/getting-started', blurb: 'Write the release image to a card and make your first sound.' },
  { label: 'Signal Chain & Routing', href: '/docs/signal-chain', blurb: 'Follow the signal from your guitar to the output.' },
  { label: 'Effects Reference', href: '/docs/effects', blurb: 'Find every block, its controls, and its value ranges.' },
  { label: 'UI & Controls', href: '/docs/ui-guide', blurb: 'Use the screen, the footswitches, and the encoder.' },
  { label: 'Dual Rig & Dual Amp', href: '/docs/dual-rig', blurb: 'Split the sound into a left lane and a right lane.' },
  { label: 'Presets & Storage', href: '/docs/presets', blurb: 'Choose, save, and organize your sounds.' },
  { label: 'Hardware', href: '/docs/hardware', blurb: 'See the parts and build the pedal safely.' },
  { label: 'Manager App', href: '/docs/manager', blurb: 'Manage sounds from a browser on your computer.' },
];
