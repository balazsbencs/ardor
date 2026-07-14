# Complete Multi-FX Import and Compressor Design

**Date:** 2026-07-14

## Goal

Make every portable effect in the Daisy multi-fx project available as a real
Ardor preset block, with meaningful editable parameters, saved defaults, and
realtime-safe hosted processing. Add a native compressor because the upstream
project contains no compressor implementation.

## Scope

Import all 35 Daisy effects:

- **Modulation (13):** Chorus, Flanger, Rotary, Vibe, Phaser, Vintage Trem,
  Poly Octave, Pattern Trem, Auto Swell, Filter, Formant, Quadrature, and
  Destroyer.
- **Delay (10):** Digital, Tape, Dual, Filter, Lo-fi, Bucket Brigade, Duck,
  Pattern, Swell, and Tremolo.
- **Reverb (12):** Room, Hall, Plate, Spring, Bloom, Cloud, Shimmer, Chorale,
  Nonlinear, Swell, Magneto, and Reflections.
- **Dynamics (1):** Ardor Compressor.

The result is 36 effect descriptors. NAM and cab processing are unchanged.

## Architecture

### Hosted Daisy source

`third_party/daisy-multi-fx-hosted/` will contain the complete portable source
closure needed by the 35 modes: all mode sources, portable audio/config/params
sources, and their DSP dependencies. Vendor sources remain functionally
unchanged. Ardor-only adaptations live in `compat/` and `src/daisyfx/`.

The original Daisy registries cannot be used as Ardor's source of truth because
they omit several newer mode classes. Ardor therefore owns one complete static
registry, with one entry per supported hosted mode.

### Registry and descriptors

Extend the effect catalog into the single source of truth for:

- preset block type and stable mode identifier;
- display name and UI category;
- processor factory and reset/process dispatch;
- complete parameter schema; and
- normalized default values.

Each parameter descriptor will include a JSON key, display label, normalized
default, normalized step, physical range, curve, and formatter/unit. Daisy
presets continue to persist values normalized to `0.0..1.0`; the UI converts
them to user-facing units such as Hz, milliseconds, seconds, dB, or percent.
Mode-specific controls will be named semantically. The existing generic Daisy
slots are retained only internally where a mode genuinely uses the common
parameter set; the user-facing labels come from the per-mode schema.

### Daisy processor adapter

`DaisyFxProcessor` becomes registry-driven instead of branching only for
Vintage Trem, Digital Delay, and Room Reverb. A configured processor holds one
selected mode and its mapped parameter set. Configuration maps normalized JSON
values through the upstream per-mode range functions, initializes/prepares the
mode outside the audio callback, and atomically hands the completed chain to
the engine using the existing loading path.

Processing and reset dispatch use the selected registry entry. Each entry
declares its intended mono/stereo handling so the adapter preserves stereo
where the upstream mode produces stereo output and uses the existing mono-to-
stereo wet mixing convention where appropriate.

### Compressor

Add an Ardor-owned `dynamics` block with mode `compressor` rather than putting
new DSP into the vendored Daisy tree. It will use a feed-forward detector,
smoothed gain reduction, and equal-power wet/dry mix. Its preset contract is:

- `threshold_db`: -60 to 0 dB;
- `ratio`: 1:1 to 20:1;
- `attack_ms`: 0.1 to 200 ms;
- `release_ms`: 10 to 2000 ms;
- `knee_db`: 0 to 24 dB;
- `makeup_db`: 0 to 24 dB;
- `input_gain_db`: -24 to 24 dB;
- `mix`: 0 to 100%;
- `sidechain_hpf_hz`: 20 to 500 Hz;
- `detector`: peak or RMS; and
- `auto_makeup`: false or true.

The processor is configured and coefficients are calculated before entering the
runtime chain. Its sample processing performs no dynamic allocation, JSON
access, file I/O, or parameter-map lookup. Invalid/missing numbers fall back
to descriptor defaults and are clamped to their supported ranges.

## UI and preset flow

The block drawer is generated from catalog entries and groups effects into
Dynamics, Modulation, Delay, and Reverb. Selecting an effect inserts its
descriptor defaults. The parameter drawer is schema-driven and paginates as
needed, rendering physical values and units rather than a universal percentage
control. Existing preset files remain compatible: on load, missing known
parameters are populated from the descriptor defaults before editing/saving.

Preset loading continues to prepare a complete chain outside the realtime
callback. Unknown modes, invalid modes, or malformed parameters cause only the
offending block to be marked unsupported/bypassed; the rest of the chain stays
safe and operational.

## Verification

- Catalog coverage test asserts exactly the 35 Daisy modes plus the compressor,
  with unique identifiers and complete parameter schemas/defaults.
- Per-mode adapter tests configure each mode, render deterministic input, check
  finite bounded output, reset determinism, and a mode-specific non-pass-through
  response.
- Preset/UI tests cover insertion defaults, meaningful formatted controls,
  clamp behavior, pagination, old-preset default migration, and save/load
  round-trips for every descriptor.
- Compressor tests cover threshold and ratio gain-reduction behavior, attack and
  release envelope behavior, knee, makeup gain, mix, detector choice, sidechain
  filter, clamping, reset determinism, and finite output.
- Runtime-chain tests cover ordering, enable/disable bypass, and graceful
  rejection of an invalid hosted mode or compressor configuration.
- Build and full test verification run on the desktop host and the Pi-oriented
  build configuration. A DSP benchmark iterates the full registry at the target
  `48 kHz / 64-frame` callback size to identify effects that violate the
  realtime budget before enabling them on hardware.

## Non-goals

- Importing Daisy hardware audio, GPIO, display, MIDI, QSPI, or preset-store
  code.
- Changing NAM or cab behavior.
- Creating effects inside the audio callback.
- Silently substituting an effect when a preset requests an unknown mode.
