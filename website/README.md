# Ardor website

Marketing and documentation site for the Ardor pedal, built with
[Astro](https://astro.build/). It is a static site deployed to GitHub Pages at
`https://balazsbencs.github.io/ardor/` by
[`.github/workflows/pages.yml`](../.github/workflows/pages.yml).

## Highlights

- **Interactive 3D hero** — a WebGL model of the enclosure (Three.js), built to
  the real geometry in [`3d_files/enclosure.scad`](../3d_files/enclosure.scad),
  with a top-view schematic fallback for reduced-motion / no-WebGL contexts.
- **Faithful UI screens** — the LVGL preset, chain-editor, EQ, and tuner screens
  are recreated as live, scalable HTML/CSS components (see
  `src/components/screens/`).
- **Generated effects reference** — `scripts/gen-effects.mjs` reads the
  manager's authoritative catalog
  ([`apps/manager/src/effects/catalog.v1.json`](../apps/manager/src/effects/catalog.v1.json))
  at build time, so block names, parameters, ranges, and defaults always match
  the firmware.

## Develop

```sh
nvm use            # Node 24 (see ../.nvmrc)
npm install
npm run dev        # http://localhost:4321/ardor
```

## Scripts

| Command | Purpose |
| --- | --- |
| `npm run dev` | Local dev server. |
| `npm run build` | Static build into `dist/`. |
| `npm run check` | `astro check` (type/template diagnostics). |
| `npm run test:links` | Verify every internal link in `dist/` resolves. |
| `npm run gen` | Regenerate `src/data/effects.generated.json` from the catalog. |

`gen` runs automatically before `dev`, `build`, and `check`.

## Structure

```
src/
  components/        Nav, Footer, EffectCatalog, SignalFlow, Hero3D, ParamTable …
    screens/         Live recreations of the on-device LVGL screens
  data/              site config, feature copy, effect descriptions, generated catalog
  layouts/           BaseLayout, DocsLayout
  lib/               url() base-path helper, typed effects accessor
  pages/             index.astro + docs/*
  scripts/           three.js pedal scene, preset-screen canvas texture
  styles/            tokens.css (design tokens), global.css
public/              favicon, og image
```

## Notes

- The GitHub Pages base path is `/ardor`; always build links with the `url()`
  helper in `src/lib/url.ts` so they resolve both in dev and in production.
- Fonts (Chakra Petch, Open Sans, IBM Plex Mono) are self-hosted via
  `@fontsource` — no external CDN requests.
