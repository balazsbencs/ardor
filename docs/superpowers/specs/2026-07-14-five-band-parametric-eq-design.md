# Five-Band Parametric EQ Design

**Date:** 2026-07-14

## Goal

Add an Ardor-native five-band parametric equalizer effect with immediate,
smoothed audio preview and a touchscreen-friendly response-curve editor. Every
band is an identical peaking filter with editable frequency, Q, gain, and
enable state.

## Product behavior

The block drawer gains an **EQ** category containing **Five Band EQ**. Adding
it inserts one enabled EQ block with five enabled, neutral-gain bands:

| Band | Frequency | Q | Gain |
| --- | ---: | ---: | ---: |
| 1 | 80 Hz | 1.0 | 0 dB |
| 2 | 250 Hz | 1.0 | 0 dB |
| 3 | 800 Hz | 1.0 | 0 dB |
| 4 | 2.5 kHz | 1.0 | 0 dB |
| 5 | 8 kHz | 1.0 | 0 dB |

Each band supports:

- frequency from 20 Hz to 20 kHz;
- Q from 0.1 to 18;
- gain from -18 dB to +18 dB; and
- an independent enabled state.

The existing block-level `enabled` field remains the whole-effect bypass.
Multiple EQ blocks may appear in a preset and serial ordering remains
meaningful. The processor preserves stereo, so an EQ may appear before or
after a stereo-producing effect without collapsing the signal.

Edits are audible immediately. Saving persists the current preset but is not
required for preview. Reset Band restores the selected band's default
frequency, Q, gain, and enabled state.

## Preset contract

The block type is `eq`, the mode is `parametric_eq_5`, and the block has no
asset path:

```json
{
  "id": "block-6",
  "type": "eq",
  "enabled": true,
  "asset": "",
  "params": {
    "mode": "parametric_eq_5",
    "bands": [
      { "enabled": true, "frequency_hz": 80, "q": 1.0, "gain_db": 0.0 },
      { "enabled": true, "frequency_hz": 250, "q": 1.0, "gain_db": 0.0 },
      { "enabled": true, "frequency_hz": 800, "q": 1.0, "gain_db": 0.0 },
      { "enabled": true, "frequency_hz": 2500, "q": 1.0, "gain_db": 0.0 },
      { "enabled": true, "frequency_hz": 8000, "q": 1.0, "gain_db": 0.0 }
    ]
  }
}
```

One typed EQ-parameter parser owns defaults, normalization, and canonical
serialization. Loading begins with the five defaults and overlays as many as
five supplied band objects. Missing bands or fields keep their defaults.
Wrong-type or non-finite numeric values use defaults; valid numeric values are
clamped to the supported range. A non-boolean enabled value uses its default.
Extra bands are ignored. Saving always emits exactly five complete band
objects in canonical order. An unknown EQ mode is unsupported rather than
silently substituted.

## DSP architecture

Add an Ardor-owned `ParametricEqProcessor`, separate from the hosted Daisy
tree. It contains five stereo RBJ peaking biquads using transposed direct-form
II sample processing. Coefficients are calculated from sample rate, frequency,
Q, and gain by one shared calculator used by both processing and visualization.

The configured processor owns no runtime JSON. Its audio path performs no
allocation, locking, file access, logging, or container mutation. It processes
left and right independently with identical coefficients and separate filter
state.

The runtime chain gains an EQ block kind parallel to the native compressor
kind. `ChainPlan` recognizes `eq` / `parametric_eq_5` as an asset-free native
block. `EngineLoader` constructs it outside the audio callback, and
`PedalEngine`/`RuntimeChain` expose a control-thread method that targets an EQ
by stable preset block ID and band index. The lookup does not mutate the chain;
it only writes the processor's atomic target values. This allows more than one
EQ block without relying on its current chain position.

## Realtime parameter updates

Each band exposes atomic targets for enabled state, frequency, Q, and gain.
The UI updates its preset JSON and sends the validated typed band state to the
running processor in the same interaction. UI and hardware-encoder edits use
the same update path.

At the start of each processed audio block, the processor reads targets,
advances smoothed working values, and calculates one coefficient set per band.
Frequency is smoothed in logarithmic space; Q and gain are smoothed linearly.
Smoothing uses a 15 ms one-pole time constant calculated from the current
sample rate and frame count, so its behavior is independent of block size.
Disabling a band smoothly drives its effective gain to 0 dB, then leaves the
neutral filter available for a click-free re-enable. No coefficient or
parameter calculation occurs per sample.

If a live update arrives with an invalid band index, a non-finite value, or a
block ID that is no longer in the active chain, the update returns false and
does not change audio state. This can happen harmlessly at a preset-switch
boundary. The UI's canonical preset state remains saveable, and the next full
preset load applies it normally.

## EQ editor

The EQ uses a purpose-built layout instead of the generic seven-knob pages.
It occupies the existing 1240 x 286 bottom parameter panel:

- The existing title, whole-block bypass, and close control remain in the
  header.
- A full-width response graph fills the main area.
- Five numbered nodes identify the bands. Tapping a node selects its band.
- Dragging a node horizontally changes frequency on a logarithmic scale.
- Dragging a node vertically changes gain on a linear scale.
- A bottom strip shows the selected band, its enabled state, and Frequency, Q,
  and Gain value pads.
- Tapping a value pad focuses it. Vertical drag or the hardware encoder edits
  the focused value; Q is edited through this path.
- Tapping the selected band's enabled state toggles that band.
- Reset Band restores all four selected-band defaults.
- Disabled nodes and individual response curves are dimmed.

The graph shows 20 Hz through 20 kHz on a logarithmic x-axis and -18 dB through
+18 dB on a fixed linear y-axis. A combined response beyond that viewport is
clipped only at the graph boundary; the audio is not clipped. The graph
evaluates 256 logarithmically spaced points. Every individual-band response
appears as a faint trace; disabled traces are further dimmed and omitted from
the bright orange combined response. The response evaluator consumes the same
coefficients as the audio processor, preventing audible and visual behavior
from diverging.

Touch interaction updates the affected node, curve, and value text directly
instead of rebuilding the complete LVGL object tree. Graph rendering is
coalesced to at most 30 frames per second during continuous dragging,
uses preallocated line-point storage, and stays entirely on the UI thread.
Audio targets may still receive every validated interaction event because the
audio-side smoothing coalesces them safely.

## Components and boundaries

The implementation keeps responsibilities separate:

- **EQ parameter model:** owns typed defaults, JSON parsing, validation,
  clamping, and canonical serialization.
- **Coefficient/response calculator:** creates RBJ coefficient sets and
  evaluates their magnitude response without owning filter state.
- **Parametric EQ processor:** owns stereo filter state, atomic targets,
  smoothing, and realtime processing.
- **Runtime integration:** creates, orders, addresses, and updates EQ blocks.
- **EQ editor:** owns selection and gesture behavior and renders graph/value
  state; it does not implement DSP equations independently.
- **Generic parameter controls:** remain unchanged for non-EQ blocks. The EQ
  editor is selected only for `eq` / `parametric_eq_5`.

These boundaries allow the editor or filter implementation to change without
altering the preset contract.

## Verification

### Parameter and preset tests

- Default insertion creates the five specified bands.
- Partial band arrays and partial band objects receive missing defaults.
- Wrong-type and non-finite values fall back; finite out-of-range values clamp.
- Extra input bands are ignored and save output contains exactly five complete
  bands.
- Save/load round trips preserve all five enabled, frequency, Q, and gain
  values.
- Unknown EQ modes are unsupported.

### DSP tests

- A neutral five-band EQ is effectively transparent.
- Each band produces its requested gain at its center frequency within a small
  numerical tolerance.
- Measured bandwidth changes consistently with Q.
- Disabling a band converges smoothly to that band's neutral response.
- Stereo channels retain independent state and identical transfer behavior.
- Output remains finite at range boundaries and under overlapping boosts.
- Reset is deterministic.
- Repeated target changes converge to the final values without a discontinuous
  output jump.

### UI tests

- Frequency-to-x and x-to-frequency mappings are logarithmic and inverse within
  rounding tolerance.
- Gain-to-y and y-to-gain mappings are linear, clamped, and inverse within
  rounding tolerance.
- Node tap selection, two-axis drag, value-pad focus, encoder adjustment,
  enable toggle, and Reset Band update canonical UI state.
- Disabled nodes and traces render dimmed; the selected node renders with the
  focus ring.
- The combined response uses all and only enabled bands.
- The 1280 x 720 render contains the complete EQ panel without overlap or
  clipping.

### Integration and performance tests

- Stable block-ID updates affect the intended EQ when multiple EQ blocks are
  present.
- Serial EQ ordering, whole-block bypass, preset save/apply, and preset
  switching work through the existing engine path.
- The five-band processor runs in the DSP benchmark at 48 kHz with 64-frame
  callbacks and does not violate the existing callback budget.
- The complete desktop test suite and Pi-oriented build configuration pass.

## Non-goals

- Live input or output spectrum analysis.
- Selectable bell, shelf, high-pass, or low-pass band shapes.
- EQ output trim or automatic gain compensation.
- Band solo.
- Mid/side processing.
- A general native-effect registry refactor.
- Rebuilding or swapping the whole runtime chain for each parameter gesture.
