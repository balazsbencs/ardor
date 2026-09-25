---
name: Ardor Website
description: An open-source guitar processor on a lit stage, drawn in the pedal's own Lamp Black language.
colors:
  lamp: "#e8472f"
  lamp-light: "#f0725c"
  lamp-dim: "#9a3324"
  lamp-ink: "#1a0906"
  ground: "#0b0c0d"
  ground-2: "#121416"
  surface: "#16181a"
  elevated: "#1d2023"
  hairline: "#2b2f33"
  hairline-strong: "#525a60"
  bezel: "#050506"
  text: "#d2d6d8"
  text-strong: "#eceeed"
  text-bright: "#f7f8f7"
  muted: "#a3aaaf"
  muted-2: "#7c848a"
  warn: "#e0a53c"
  family-amp: "#d2923f"
  family-cab: "#aab2b7"
  family-util: "#5f95c9"
  family-mod: "#3fb08c"
  family-delay: "#9a82d6"
  family-reverb: "#d07a5a"
  glow-lamp: "rgba(232, 71, 47, 0.34)"
  glow-lamp-strong: "rgba(232, 71, 47, 0.46)"
  glow-lamp-soft: "rgba(232, 71, 47, 0.10)"
  light-ground: "#eef0ef"
  light-surface: "#f7f8f7"
  light-text-strong: "#121416"
  light-lamp: "#c73a24"
  device-ground: "#0b0c0d"
  device-recess: "#121416"
  device-plate: "#16181a"
  device-rule: "#2b2f33"
  device-bone: "#eceeed"
  device-secondary: "#9aa1a6"
  device-disabled: "#5c6368"
  device-lamp: "#e8472f"
  device-warning: "#e0a53c"
  device-amp: "#d2923f"
  device-cabinet: "#aab2b7"
  device-utility: "#5f95c9"
  device-modulation: "#3fb08c"
  device-delay: "#9a82d6"
  device-reverb: "#d07a5a"
typography:
  display:
    fontFamily: "Saira Condensed, Arial Narrow, sans-serif"
    fontSize: "clamp(2.8rem, 1.2rem + 4.2vw, 4.6rem)"
    fontWeight: 800
    lineHeight: 0.9
    letterSpacing: "0.005em"
  headline:
    fontFamily: "Saira Condensed, Arial Narrow, sans-serif"
    fontSize: "clamp(2.3rem, 1.4rem + 3vw, 4.2rem)"
    fontWeight: 700
    lineHeight: 0.95
  body:
    fontFamily: "Saira, system-ui, sans-serif"
    fontSize: "clamp(1rem, 0.96rem + 0.16vw, 1.08rem)"
    fontWeight: 400
    lineHeight: 1.6
  label:
    fontFamily: "IBM Plex Mono, ui-monospace, monospace"
    fontSize: "clamp(0.8rem, 0.78rem + 0.1vw, 0.86rem)"
    fontWeight: 400
rounded:
  none: "0"
spacing:
  xs: "0.375rem"
  sm: "0.625rem"
  md: "1rem"
  lg: "1.5rem"
  xl: "2.5rem"
  xxl: "4rem"
  section: "clamp(5rem, 10vw, 8.75rem)"
components:
  button-primary:
    backgroundColor: "{colors.text-strong}"
    textColor: "{colors.ground}"
    rounded: "{rounded.none}"
    padding: "0 1.6rem"
  button-secondary:
    backgroundColor: "{colors.surface}"
    textColor: "{colors.text-strong}"
    rounded: "{rounded.none}"
    padding: "0 1.6rem"
  device-capture:
    backgroundColor: "{colors.bezel}"
    rounded: "{rounded.none}"
    padding: "10px"
---

# Design System: Ardor Website

## Overview

**Creative North Star: "The lit stage."**

The website uses the pedal's own **Lamp Black** language (chosen 2026-09-25 as direction 1 in `mockups/website-taste/`). The page is a dark stage. The real pedal screens are the objects on it. Lamp red is the one light that means live, and it lights the ground around it. The family colours are the stage lights of the chain.

The first read is always a guitarist finding their sound: hear it, see the screens, find every block, then build it from open source.

**Key characteristics:**
- Near-black ground, bone type, saturated family colours.
- Real LVGL captures of the device screen, never HTML copies of it.
- Glows that always come from a visible source.
- Square corners everywhere. Saira Condensed for what a player scans.

## Colors

Source of truth: `website/src/styles/tokens.css`, which uses the device values from `src/ui/LvglUiStyle.cpp`.

- **Ground** (`#0b0c0d`) under **surface** (`#16181a`) and **elevated** (`#1d2023`). The wide value range lets a plate read as raised.
- **Bone** text (`#eceeed`), **muted** (`#a3aaaf`) for ledes, **muted-2** (`#7c848a`) for footnotes. Every text pair passes WCAG AA in both themes.
- **Lamp** (`#e8472f`) with **lamp ink** (`#1a0906`) for text on a flooded lamp.
- **Family colours**: amp `#d2923f` (drive shares it, as on the device), cabinet `#aab2b7`, utility `#5f95c9`, modulation `#3fb08c`, delay `#9a82d6`, reverb `#d07a5a`.

### Named Rules

**The One Lamp Rule.** Lamp red means live or selected: the live preset, the current docs page, the Ardor source in the A/B player. It is not a button colour and not decoration. Buttons are bone or plate, and hover never turns red.

**The Glow Rule.** Every glow has a visible source. Red light appears only around something red and live: the live tile on the pedal, the red part of a capture, the A/B tile when Ardor plays, and the stage light under the closing call to build. The family strip lights the section below it in its own colours; the Dual Rig lanes light the ground blue and amber. A glow is a box centred on its source with `radial-gradient(closest-side, ...)`, so it always fades out before its edge. Docs pages have no glows, because nothing there emits light.

**The Family Bar Rule.** Family colours identify blocks: strips, card headers, chips. They are structural labels, not a rainbow accent system.

### Light theme

The toggle offers a cool neutral light theme (ground `#eef0ef`, lamp `#c73a24`). Rules and letterforms stay. Glows get quieter (`--glow-family` 12% instead of 20%). Device screens stay dark in both themes, and the hero render sits on a dark stage plate, because its own background is dark.

### Device screen: Lamp Black

The pedal's default Slate palette carries the **Lamp Black** values (`device-*` tokens above; source of truth `src/ui/LvglUiStyle.cpp`, mockups in `mockups/lvgl-taste/`). It keeps the Panel language but widens the value range, because the earlier plates were too close in value to separate at a glance:

- **Device ground** (`#0b0c0d`) sits under **device plate** (`#16181a`) and **device recess** (`#121416`), so plates read as raised.
- **Device lamp** (`#e8472f`) floods the whole live preset tile, lettered in device ground. It still means LIVE and the selected control only.
- **Family colours** are raised in chroma so the chain strip on each preset tile, the chain-card value bars and the drawer code squares read at a glance.
- Flatness stays a hard rule on the device: the Pi panel is RGB565, so depth comes from flat planes and hard offset plates, never gradients or blur.

The website shows the real screens as LVGL captures (see **Device captures** below), and the website palette uses these same values.

Device extras beyond the Panel tokens: **raised plate** (`#202326`) for selected chips and segments, **lamp ink** (`#1a0b08`) for lettering on the flooded lamp, **danger** text on a **danger rule** (`#f0a497` on `#6b3a32`) for destructive actions, a **lift shadow** (`#040505`) under the selected chain card, and **warn ink** (`#1b1305`) on warn tags.

**Device type scale.** The device renders the mockup's CSS type exactly: Saira Condensed 500–800, Saira 400–700 and JetBrains Mono 500 (the chain-strip and module codes), cut to bitmap fonts in `src/ui/fonts/lamp/` by `scripts/generate-lamp-black-fonts.sh`. Each role (header title, rail button, control value, chip, cap…) is a named `lb::type` in `src/ui/LampBlack.h`, which also places text on the CSS baseline so glyphs land where the mockup puts them. Every screen shares the 64 px header over a rule, the 108 px rail with 60 px buttons at y = 637, 12 px gaps and the 24 px gutter.

## Typography

**Display:** Saira Condensed 600 to 800. **Body:** Saira 400 to 600. **Mono:** IBM Plex Mono 400 and 500. All are self-hosted through Fontsource.

- **Display** (800, `clamp(2.8rem, 1.2rem + 4.2vw, 4.6rem)`, 0.9): the hero, two lines at 1440 px.
- **Headline** (700, `clamp(2.3rem, 1.4rem + 3vw, 4.2rem)`, 0.95): section titles.
- **Title** (700, `clamp(1.5rem, 1.28rem + 0.9vw, 2.05rem)`): card and pair titles.
- **Body** (400, `clamp(1rem, 0.96rem + 0.16vw, 1.08rem)`, 1.6) and **lede** (`clamp(1.14rem, 1.06rem + 0.34vw, 1.3rem)`, muted).
- **Label** (IBM Plex Mono, `0.8rem` to `0.86rem`): footnotes, gesture hints, captions of renders.

**The Engraved Hierarchy Rule.** Condensed uppercase for what a player scans. Saira for prose and values. Mono only for true labels. Units keep their case (ms, kHz, dBFS): never put a unit inside an uppercase transform.

## Layout

- Container `1320px` with a `clamp(16px, 4vw, 48px)` gutter. The page never scrolls sideways: `main` clips horizontal overflow from glows.
- Homepage order: hero, family strip, listen (only when the demo exists), interface bento, Scenes and Looper pair, chain, tones, effect families, open close. No two neighbouring sections share a layout family.
- At most one eyebrow on the homepage (the hero). No section numbers.
- Below 980 px every grid becomes one column.

## Elevation & Depth

- **Object shadow** (`0 30px 60px -20px rgba(0,0,0,0.8)` plus a 1 px ring): only for device captures and the light-theme stage plate.
- **Glows**: see the Glow Rule. They are the only gradients on the page, apart from the colour wash that falls from a family header into its card.
- The pedal screen itself stays flat (RGB565); depth belongs to the page, never to the screen.

## Shapes

Square corners (`0`) for every panel, button, bezel, card, and chip. Borders are 1 px; family headers and chips carry colour as fills or 3 to 4 px top rules.

## Components

- **Buttons:** primary is a bone fill with ground text; secondary is a surface plate with a hairline. Condensed uppercase, one line, 3.5 rem high. Active state moves down 1 px.
- **Device captures** (`DeviceScreen.astro`): a real capture in a 10 px `#050506` bezel with the object shadow, an optional caption, and an optional glow at the red part (`glow={{ x, y }}`, or `glow="dual"` for the Dual Rig lanes). Images go through `astro:assets`.
- **A/B player** (`home/ListenPlayer.astro`): a preset tile. Ardor floods it with lamp red and lights the ground; Dry leaves it dark. Both clips play in sync and the switch only changes which one you hear. The waveform comes from real peaks in `public/audio/demo.json`. The section renders only when that manifest and both clips exist.
- **Effect families** (`home/EffectFamilies.astro`): module-drawer cards from the catalog, the largest family at double width.
- **Nav:** translucent sticky bar with blur and a solid fallback; red square mark; links match section headings.
- **Docs:** sidebar with the lamp on the current page; callouts are plates (warnings get a warm tint), never a thick side border; "Advanced" sections are collapsed plates.
- **Interactive manual:** the simulated pedal screen follows the real Lamp Black preset screen and keeps fixed device values in both themes.

## Copy

Site copy follows Simplified Technical English: one idea per sentence, active voice, no metaphors, no em or en dashes (ranges read "1 to 4"). Product terms stay exact: NAM, IR, Dual Rig, block, chain, lane, bank, slot, preset. Goals stay goals: "round-trip latency goal", never a measured claim.

Counts come from the catalog at build time (`counts.effects`, `effectFamilies`). `npm run test:copy` fails the build on dashes, on wrong effect totals, on catalog blocks missing from the effects reference, and on known wrong phrases.

## Assets

- **Device captures:** `cmake --build build-sdl --target pedal-lvgl-ui-screenshots`, then `./build-sdl/pedal-lvgl-ui-screenshots <dir>`. Convert the PPM files to PNG and copy the ones in use to `website/src/assets/device/`.
- **Hero:** `website/src/assets/pedal-hero.webp` is the concept render with the real preset capture composited into its screen (screen area x 444 to 1082, y 354 to 691 at 1536 x 1024). Replace it with a photo of the built pedal when one exists.
- **Link preview:** `website/public/og.png` (1200 x 630) repeats the hero. It names no counts, so it cannot drift.
- **Favicon:** the lamp-red square on the Lamp Black ground.

## Do's and Don'ts

### Do:
- **Do** show real captures of the device, and name renders as renders.
- **Do** keep lamp red for live and selected state, and let it light its surroundings.
- **Do** take counts and names from the catalog.
- **Do** check both themes and 390 px width before you ship.

### Don't:
- **Don't** build HTML copies of the device screen.
- **Don't** add a glow without a visible source, or a gradient for decoration.
- **Don't** use section numbers, decorative micro-labels, or a thick side border on cards.
- **Don't** write a number into copy by hand when the catalog has it.
