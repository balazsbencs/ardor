---
version: 1
slug: "website-src-pages-user-manual-astro"
primary_target: "website/src/pages/user-manual.astro"
related_targets: ["website/src/components/manual/InteractivePedal.astro","website/src/components/manual/LooperGuide.astro"]
---

# User manual surface

- Scope and mode: `/user-manual`, primarily Read with an Operate simulator. It extends the established Ardor website world.
- Audience and job: a guitarist learning the physical pedal at a desk before relying on it on stage. Operate a faithful control, see the resulting device state, and understand mode-dependent actions and holds.
- Approved direction: Version A, “Instrument + sidecar,” corrected so the encoder is mounted through the top panel. Approved comp: `.impeccable/mocks/user-manual-a-sidecar-approved.png`.
- Memorable moment: holding FS1 + FS2 draws a live route into the explanation, fills a real one-second meter, mutes the simulated output, and replaces the preset screen with the tuner.
- Beginner Looper strategy: optimize for the first successful phrase. Teach one action at a time in a seven-step path—enter, record, close, layer, perform, pause/manage, tune/return—then expose timing rules and the complete control reference.
- Simulator handoff: the guide’s practice action opens Looper mode in the existing pedal simulator and scrolls it into view, keeping explanation and experimentation connected.
- Constraints: preserve the physical `1 / 3` above `2 / 4` switch map; red means live only; accurate control claims; multi-touch plus keyboard parity; responsive whole-pedal view; no final product photography claim.
- Content authority: Looper behavior and timing claims follow `docs/superpowers/specs/2026-08-30-looper-design.md`; keep this guide synchronized with firmware changes.

## Implementation inventory

| Comp ingredient | Grammar / commitment | Medium |
| --- | --- | --- |
| Physical pedal | Near-life-size black enclosure; four metal corner switches; corrected top-panel encoder | Generated raster derived from the existing pedal study |
| Touchscreen | Crisp live Preset, Tuner, Looper and Edit states inside the bezel | Semantic HTML/CSS |
| Footswitch and encoder hit areas | Aligned to real hardware; pressed/turning feedback | Accessible HTML buttons and pointer/keyboard events |
| Context sidecar | One continuous technical plate, not stacked cards | Semantic HTML/CSS |
| Control-to-manual route | Thin hard-edged red path for current input | SVG |
| Hold timing | Full-width calibrated progress strip with live elapsed seconds | Native progress plus JavaScript timing |
| Combination ledger | Dense mode-by-mode table continuing below the simulator | Semantic table |
| Looper first-run path | Seven selectable lessons with one clear outcome per step | Accessible buttons and progressive panels |
| Looper state diagrams | Deterministic four-track displays for empty, recording, playing, armed, and paused states | Semantic HTML/CSS |
| Practice handoff | Opens Looper mode and returns focus to the interactive hardware | Custom browser event plus anchored scroll |
| Detailed Looper reference | Clock behavior, switch roles, management actions, troubleshooting, and signal capture boundary | Semantic prose, definition groups, and disclosure elements |

Unresolved for production: replace the render if real product photography becomes available; keep Looper wording synchronized with the firmware control contract.
