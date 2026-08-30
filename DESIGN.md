---
name: Ardor Website
description: An open-source guitar processor presented as a playable instrument panel.
colors:
  live-red: "#d8422f"
  live-red-light: "#ed6b58"
  live-red-dim: "#8e3026"
  graphite-ground: "#141719"
  graphite-recess: "#191c1f"
  slate-plate: "#212528"
  slate-panel: "#2a2f33"
  rule: "#3b4247"
  rule-strong: "#687178"
  bone-engrave: "#e2e4e3"
  secondary-engrave: "#8d9499"
  disabled-engrave: "#697278"
  warning-amber: "#c9973f"
  amp-ochre: "#a8814e"
  cabinet-silver: "#939a9e"
  utility-slate: "#5f7f9c"
  modulation-teal: "#5d8f80"
  delay-violet: "#8175a0"
  reverb-clay: "#a8785c"
typography:
  display:
    fontFamily: "Saira Condensed, Arial Narrow, sans-serif"
    fontSize: "clamp(3.4rem, 2.2rem + 5.4vw, 7rem)"
    fontWeight: 600
    lineHeight: 0.98
    letterSpacing: "0.015em"
  body:
    fontFamily: "Saira, system-ui, sans-serif"
    fontSize: "clamp(0.98rem, 0.94rem + 0.16vw, 1.08rem)"
    fontWeight: 400
    lineHeight: 1.55
  label:
    fontFamily: "IBM Plex Mono, ui-monospace, monospace"
    fontSize: "clamp(0.74rem, 0.72rem + 0.1vw, 0.82rem)"
    fontWeight: 400
    letterSpacing: "0.14em"
rounded:
  none: "0"
spacing:
  xs: "0.375rem"
  sm: "0.625rem"
  md: "1rem"
  lg: "1.5rem"
  xl: "2.5rem"
  section: "7rem"
components:
  button-primary:
    backgroundColor: "{colors.bone-engrave}"
    textColor: "{colors.graphite-ground}"
    rounded: "{rounded.none}"
    padding: "0.75em 1.2em"
  button-ghost:
    backgroundColor: "transparent"
    textColor: "{colors.bone-engrave}"
    rounded: "{rounded.none}"
    padding: "0.75em 1.2em"
  panel:
    backgroundColor: "{colors.slate-plate}"
    textColor: "{colors.bone-engrave}"
    rounded: "{rounded.none}"
    padding: "1.5rem"
---

# Design System: Ardor Website

## Overview

**Creative North Star: "The playable nomenclature plate"**

Ardor's website treats the product as an instrument before it treats it as software. The visual system carries the same Panel language as the device UI: graphite planes, bone lettering, calibrated rules, family color bars, and one reserved live lamp. It is technical in its precision, but the first read is always a guitarist finding their sound.

The homepage is a quiet, dark stage for the physical pedal and its screen. Detail is earned through demonstration: the real preset map, signal rail, EQ editor, and tuner lead into a lighter overview of the rig. The site may offer a light theme, but it keeps the same square geometry, type pairing, rules, and red live accent.

**Key Characteristics:**
- Flat slate plates with hard 1px rules.
- Saira Condensed nomenclature with Saira measurement values.
- Color groups functions; red marks only what is live.
- Screen mockups mirror the actual 1280 × 720 Panel UI.

## Colors

The palette is restrained: graphite and bone do most of the work; muted family colors group modules; live red is rare enough to mean something.

### Primary
- **Live red** (`#d8422f`): The single active/live signal for the homepage and the Slate device screen representations.

### Secondary
- **Warning amber** (`#c9973f`): Caution and muted output states; never a substitute for live red.

### Tertiary
- **Amp ochre** (`#a8814e`), **cabinet silver** (`#939a9e`), **utility slate** (`#5f7f9c`), **modulation teal** (`#5d8f80`), **delay violet** (`#8175a0`), and **reverb clay** (`#a8785c`): Family identifiers for signal blocks and sound categories.

### Neutral
- **Graphite ground** (`#141719`) and **graphite recess** (`#191c1f`): Page canvas and recessed screen areas.
- **Slate plate** (`#212528`) and **slate panel** (`#2a2f33`): Main planes and raised module bodies.
- **Bone engrave** (`#e2e4e3`): Primary headlines, active labels, and high-priority values.
- **Secondary engrave** (`#8d9499`) and **disabled engrave** (`#697278`): Supporting labels, legends, and inactive states.
- **Rule** (`#3b4247`) and **strong rule** (`#687178`): Hairlines, brackets, frames, and focus-visible boundaries.

### Named Rules

**The One Lamp Rule.** Live red means what is running or selected; do not spend it on decoration, general calls to action, or ordinary success states.

**The Family Bar Rule.** Category colors identify amp, cab, utility, modulation, delay, and reverb. They are structural labels, not a rainbow accent system.

## Typography

**Display Font:** Saira Condensed (with Arial Narrow, sans-serif)
**Body Font:** Saira (with system-ui, sans-serif)
**Label/Mono Font:** IBM Plex Mono (with ui-monospace, monospace)

**Character:** Saira Condensed carries the engraved, instrument-panel voice in uppercase names and actions. Saira keeps measurements, explanatory copy, and numeric readouts open and calm. IBM Plex Mono is reserved for true labels, identifiers, and technical metadata.

### Hierarchy
- **Display** (600, `clamp(3.4rem, 2.2rem + 5.4vw, 7rem)`, `0.98`): The homepage promise and largest section statements.
- **Headline** (600, `clamp(2rem, 1.55rem + 1.65vw, 3rem)`, `0.98`): Section titles and the closing invitation.
- **Title** (600, `clamp(1.2rem, 1.08rem + 0.5vw, 1.48rem)`, `0.98`): Feature rows, effect names, and navigation labels.
- **Body** (400, `clamp(0.98rem, 0.94rem + 0.16vw, 1.08rem)`, `1.55`): Explanatory copy, held to a readable measure.
- **Label** (400, `clamp(0.74rem, 0.72rem + 0.1vw, 0.82rem)`, `0.14em`, uppercase): Rails, metadata, families, and captions.

### Named Rules

**The Engraved Hierarchy Rule.** Use condensed uppercase for what a player scans; use Saira for values and prose; use mono only when the text is truly a label or identifier.

## Layout

The site uses a centered `1240px` maximum canvas with `1.5rem` inline padding, generous `7rem` section spacing, and 1px separators between major bands. The homepage alternates a large demonstration with quieter text rails: physical pedal first, screen showcase second, then feature rows, signal flow, effect names, and the open-source close.

The hero is a two-column composition that collapses to a single column below `1000px`. Screen frames use a two-column grid until `760px`, then stack. Feature rows use four columns on wide screens and collapse to an indexed two-column rhythm on narrow screens. At mobile widths, the hero facts stack and the final action panel becomes vertical.

## Elevation & Depth

The Panel UI is flat by construction: tonal planes, 1px rules, brackets, and hard module headers do the depth work. The website adds only a restrained black contact shadow to the physical/product screen frames so they separate from the canvas; screen interiors themselves never glow or use decorative gradients.

### Shadow Vocabulary
- **Product contact shadow** (`0 24px 34px rgba(0, 0, 0, 0.45)`): The fallback pedal silhouette's physical grounding.
- **Screen frame shadow** (`0 18px 34px rgba(0, 0, 0, 0.28)`): Separates a display bezel from the page, never used inside the LVGL representation.

### Named Rules

**The Flat Plate Rule.** A screen or UI surface is flat at rest. If depth is needed, change the plane or add a rule; do not add a glow, gradient, glass card, or soft shadow inside the device language.

## Shapes

All website panels, buttons, screen bezels, module blocks, and effect groups use square corners (`0`). Borders are mostly 1px; thicker color lines are reserved for family headers and the live signal. Bracket corners and calibration lines are recurring geometry, especially around the hero stage and content rails.

## Components

### Buttons
- **Shape:** Square, engraved plate geometry (`0` radius).
- **Primary:** Bone foreground on graphite canvas, `0.75em 1.2em` padding, condensed uppercase lettering.
- **Hover / Focus:** Hover switches to live red; `:focus-visible` uses a 2px live-red outline with a 3px offset.
- **Secondary / Ghost:** Transparent graphite ground with a strong rule; hover changes only the rule and label color.

### Cards / Containers
- **Corner Style:** Square (`0` radius); no nested rounded cards.
- **Background:** Slate plate or slate panel, separated by a 1px rule.
- **Shadow Strategy:** No shadow for UI content; the screen frame uses the documented product contact shadow.
- **Internal Padding:** `1rem` to `1.5rem` for content surfaces; screen bezel padding is `0.65rem`.

### Navigation
- **Style:** Sticky graphite rail with a 1px bottom rule. Ardor uses a red square mark, condensed uppercase links, and a compact GitHub action.
- **States:** Links rest in secondary engrave, then move to bone on hover; the mark remains the live red anchor.
- **Mobile:** Links collapse behind the existing menu control at `820px` while the primary actions remain available.

### Panel Screen Showcase

The `PresetScreen`, `EditScreen`, `EqScreen`, and `TunerScreen` components reproduce the device's 1280 × 720 Panel grammar inside a scalable bezel. The preset map is intentionally arranged `1 / 3` above `2 / 4` to mirror the physical footswitch corners; the running preset is the only full live-red state. Signal blocks use family header colors, patch points are circles on a rule, the EQ exposes its selected-band strip, and the tuner is center-zero with a muted-output warning.

## Do's and Don'ts

### Do:
- **Do** let the product and its on-pedal interface appear before the implementation details.
- **Do** use hard rules, flat tonal planes, and family colors to explain structure.
- **Do** keep the live red rare and meaningful.
- **Do** use the Panel screen components as the visual authority for device UI imagery.
- **Do** preserve the distinction between player-facing clarity on the pedal and depth in the docs/manager.

### Don't:
- **Don't** bring back the retired teal glow, decorative grid, gradient, or rounded-card language.
- **Don't** turn every technical fact into a homepage metric; route deeper material to documentation.
- **Don't** use family colors as generic calls to action or neutral decoration.
- **Don't** use a gray fault state without a distinct border and explicit recovery text.
