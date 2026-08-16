# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Users

**Primary: the gigging and practicing guitarist.** A player who wants a working
pedal at their feet. Tone, latency, preset switching, and the tuner are what
they judge it by. Building the unit is a means to an end — they would take an
assembled one if it existed. When a tradeoff pits playability against builder
convenience or developer elegance, the player wins.

Secondary audiences exist (DIY builders who flash the image and wire the GPIO,
developers drawn to an open realtime DSP platform) but they are not the audience
design decisions are optimized for.

## Product Purpose

Ardor is a standalone Raspberry Pi guitar-processing platform: a realtime audio
engine, a touchscreen and footswitch interface, preset storage, a manager
application, and a reproducible Buildroot firmware image. It replaces a rack or
pedalboard with one appliance running neural amp captures, cabinet impulse
responses, and a full effects chain.

It is an **appliance, not a plugin host**. Plugin formats remain out of scope.
Signed OTA updates may replace the Ardor device applications and restart them;
kernel, boot, Buildroot, and partition changes still require a full image flash.

Success is people building it, playing through it, and contributing back.

## Positioning

An **open-source guitar processor that runs the whole rig on a Raspberry Pi** —
fully reproducible from source, with a firmware image anyone can build.

The mechanism a neighboring product could not truthfully copy: every layer is
open and inspectable, from the DSP chain to the preset schema to the SD-card
image, under MIT license. Commercial modelers ship a sealed box; Ardor ships the
whole rig as buildable source.

**Project intent: open-source project only.** No sales, no kits, no commercial
offering. Nothing in any surface should sell, price, or imply purchase.

## Operating Context

The pedal lives in three real scenes, all confirmed:

1. **Dark stage, standing, at foot level.** Viewed from roughly 1.5 m above at a
   steep angle, in low light, with possible stage-lighting glare. Footswitches do
   the work; the screen is glanced at, not read.
2. **Rehearsal room with a band.** Moderate light, fast changes between songs, no
   time to read. Preset-switch speed and tuner access dominate.
3. **Desk or studio, alongside a computer.** The manager application is open on a
   laptop; the pedal is within arm's reach. Detailed chain editing happens on the
   larger screen.

Notably **not** a primary scene: seated bedroom practice with the pedal in hand.
Close-range leisurely screen reading is not the design target.

This split is a durable constraint: **the device screen is for glancing under bad
conditions; the manager is for detailed work under good conditions.** Density and
legibility budgets differ accordingly.

## Capabilities and Constraints

Three distinct interface surfaces exist, each with its own technology and rules:

- **Device touch UI** — LVGL in C++, running on a Raspberry Pi Touch Display 2
  via the `fbdev` backend, with an SDL desktop simulator (`pedal-ui-sim`) for
  development. Not web, not iOS, not Android. No web layout, CSS, or animation
  primitives are available; LVGL widgets, styles, and its own animation system
  are the entire vocabulary. Hardware controls are four footswitches (`KEY_F1`–
  `KEY_F4`) and one rotary encoder.
- **Manager application** — React, shipped two ways from one codebase: a
  device-hosted browser build served by `ardor-managerd` over LAN HTTP, and a
  hosted HTTPS control-plane build. There is no desktop application.
- **Marketing and documentation website** — Astro, deployed to GitHub Pages.

Engine and product facts future work must not contradict:

- 48 kHz sample rate; preferred block size 64, fallback 128; round-trip latency
  goal under 10 ms.
- Mono input, stereo output.
- Neural Amp Modeler `.nam` captures with optional embedded nano submodels.
- Cabinet IRs via partitioned convolution.
- 35 built-in effects: 13 modulation/special, 10 delay, 12 reverb.
- Compressor, stereo-linked noise gate with zero added latency, five-band
  parametric EQ (±18 dB) with high-pass and low-pass filters and a live response graph.
- Dual Amp (fixed two-lane block) and Dual Rig (two independent child chains,
  preset version 2). Both merge hard-left/hard-right into stereo.
- Presets in bank/slot folders, four slots per bank, JSON format, relative asset
  paths only.
- A safety limiter at −1 dBFS that is deliberately **not** user-editable — it is
  protection, not a tone control.
- LAN manager access is plain HTTP by design and the UI must keep saying so;
  it is for trusted networks only.
- Factory reset requires physical confirmation on the pedal (footswitch 1
  approves, footswitch 4 cancels).

Terminology used consistently across product and interface: *block*, *chain*,
*rail*, *lane*, *split*, *join*, *bank*, *slot*, *preset*, *rig*.

## Brand Commitments

**Only the name "Ardor" is fixed.** Nothing else is locked.

- `website/public/favicon.svg` and `website/public/og.png` are **placeholders**,
  explicitly free to be replaced.
- The green default UI accent is *not* a binding commitment; the device already
  offers a user-selectable global accent color.
- No wordmark, logo system, typeface, or palette has been committed to.
- There is no established voice document. Existing copy in `README.md` and
  `website/src/data/` is precise, technical, and unhyped — a reasonable starting
  reference, not a ratified standard.

The user has stated they are **open to redesign and want new perspectives**, on
the current website and identity. This is an invitation, not a rejection of what
exists.

## Evidence on Hand

Real:

- **A physically built pedal exists** and can be photographed. No product
  photography has been produced yet.
- **Audio demos exist or can be recorded.** `dryguitar.wav`, `ardor-wet.wav`, and
  `ardor-wet2.wav` are in the repository root; real playing demos are obtainable.
- **Real LVGL UI screenshots are obtainable** from the SDL simulator
  (`./scripts/build-sim.sh`, then `pedal-ui-sim`). The current website renders the
  device screens as hand-built HTML approximations in
  `website/src/components/screens/` rather than captures of the real interface.
- **35 effects with real parameter metadata** — names, semantic labels, physical
  units, defaults, and ranges — generated from `src/daisyfx/DaisyFxCatalog.cpp`
  into `website/src/data/effects.generated.json`.
- **A physical enclosure design** — `enclosure.openscad` and printable `.3mf`
  files in `3d_files/`. A KiCad schematic for the control-I/O board is in
  `hardware/control-io/`.
- Extensive real documentation: `README.md`, `BUILD.md`,
  `docs/hardware-assembly.md`, `docs/hosted-manager-architecture.md`,
  `docs/midi-expression-control.md`.
- MIT license; repository at `https://github.com/balazsbencs/ardor`.

Absent — **must not be fabricated**:

- No product photography exists yet.
- No logo or wordmark beyond the placeholder favicon.
- No testimonials, user quotes, reviews, press coverage, or artist endorsements.
- No user counts, download numbers, star counts, or adoption figures.
- No pricing, availability, kits, or purchase path — and none is planned.
- No measured latency, CPU, or benchmark figures beyond what the repository
  states as goals and targets. "Under 10 ms round-trip" is a **goal**, not a
  measured result, and must be stated as such.

## Product Principles

1. **The player is the customer.** Every surface answers to someone holding a
   guitar, not someone holding a soldering iron or a compiler. Openness is the
   method; playing is the point.
2. **Glanceable on stage, detailed at the desk.** The device screen optimizes for
   a one-second look under bad light at a steep angle. The manager optimizes for
   precision. Never invert these.
3. **Openness is the position, not a feature bullet.** Reproducible from source
   is the claim no sealed competitor can make. It earns prominence, but as
   substance — buildable, inspectable, documented — never as a badge.
4. **Say only what is true.** The repository is unusually precise about goals
   versus measurements, scope versus intent, supported versus planned. Every
   surface inherits that discipline. No invented proof, no borrowed credibility.
5. **The instrument's own vernacular.** Blocks, rails, lanes, banks, slots, dB,
   ms, kHz. Guitarists and audio people have precise language; use it correctly
   rather than translating it into generic software copy.

## Accessibility & Inclusion

No formal standard has been established as a product requirement. Two
product-specific needs are confirmed by the operating context and should be
treated as real constraints:

- **Low-light, steep-angle, high-glare legibility** on the device screen. Contrast
  and target size must survive a dark stage viewed from standing height.
- **Footswitch parity for critical actions.** Preset switching and tuner entry
  are reachable without touching the screen, and must stay that way — a player
  with a guitar in both hands cannot tap.
