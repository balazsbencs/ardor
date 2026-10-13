# Parametric NAM models

Ardor supports self-describing `ConcatWaveNet` and `ConcatLSTM` `.nam` models
using the parametric fork's `1.0.0` format, and Gene Ko's `ParametricWaveNet`
FiLM schema 1 models in `0.7.0` files, including their `SlimmableContainer`
exports and complete top-level `embedded_parametric` payloads. Ordinary snapshot models continue to
use the pinned upstream NAM runtime and its existing file-version rules.

## Research findings

A snapshot NAM learns a function of audio history at one set of hardware
settings. A parametric model also receives the physical control settings. Its
training data pairs aligned input/output audio with the settings used for each
capture. At playback, those control values change the neural computation.
Putting a gain or EQ after a snapshot does not reproduce this behavior.

Steve Atkinson's [ParametricOD announcement](https://www.neuralampmodeler.com/post/the-first-publicly-available-parametric-neural-amp-model)
demonstrates two modeled knobs and two switches, with a customized architecture
and C++ runtime. The output knob was omitted because the physical potentiometer
was damaged. The current [official downloads](https://www.neuralampmodeler.com/users)
provide Windows/macOS plugin installers. Research did not locate separately
published weights, runtime source, or a redistribution license for that model.
A free installer alone does not establish that its embedded model can be bundled
in Ardor. This implementation therefore includes an independently trained open
demo, with the user's approval; it does not extract or redistribute ParametricOD.

The [open parametric trainer](https://github.com/phillipmself/neural-amp-modeler-parametric)
and [companion inference runtime](https://github.com/phillipmself/NeuralAmpModelerCoreParametric)
provide a usable file contract:

- `ConcatWaveNet` and `ConcatLSTM` concatenate encoded controls with audio input.
  Continuous controls map their physical range to `[-1, 1]`; a switch contributes
  one one-hot channel per choice. The network still exposes mono audio to the host.
- `HyperWaveNet` generates network weights from controls. It needs different
  runtime handling; the trainer documents audible artifacts during weight changes.
  Ardor does not support it in this release.
- `config.params` gives ordered control names, ranges, defaults, types and switch
  choices. Hosts must retain that order for input-channel encoding.

The concat inference code under `src/namparam` is adapted from the MIT-licensed fork at
commit `fddcbc91e74ba79066a43c3b73cc690073529232`. Ardor keeps its existing pinned
NAM core rather than replacing it wholesale. Explicit parser calls avoid static
registration/linker surprises and keep the parametric version namespace separate.

Gene Ko's [NAMix](https://github.com/mrgeneko/NAMix) publishes reusable FiLM
models and a [companion core fork](https://github.com/mrgeneko/NeuralAmpModelerCore).
Its models condition each A2 layer with control-dependent scale and shift before
activation, then sum the skip outputs for the head. Ardor adapts only that fork's
parametric A2 kernels and adds a host container adapter; it preserves stock NAM
loading. The source is pinned to `419bb5712e651fb7fb548f8aa54431207727aa59`, with its
MIT notice in `src/namparam/LICENSE-Gene-Ko`.

For hardware training, capture representative knob settings and every switch
state, align each recording, record settings and input calibration, then reserve
unseen settings and different audio for validation. Space-filling designs and
corner captures reduce the number of recordings compared with a full factorial
grid. Interpolation quality must be measured rather than assumed. The linked
trainer provides this workflow; Ardor currently imports the trained result.

## Use in the manager

1. Open **Assets → NAM models** and upload a `.nam` file. Both local and hosted
   managers use the same device asset store, so metadata survives either route.
2. Open **Add block**. Supported parametric models appear by filename under
   **Amp and drive**, with their control names in the description.
3. Select the model. A file with `metadata.gear_type: "pedal"` creates a
   **Neural Pedal** (`namPedal`) block. Other files create a Neural Amp (`nam`).
4. For a supported container, **Model size → Nano** selects its smallest tier;
   **Standard** selects the full tier. Both retain the same learned control values.
5. Adjust **Model controls** in the block drawer. Save and apply the preset to
   hear changes. Controls are shared across scenes in this first version.

Neural Pedal blocks can precede an amp without consuming the lane's one-amp slot.
They also work in Dual Rig and Wet / Dry / Wet child chains. A fixed Dual Amp
block loads parametric files at their exported defaults; edit model controls in a
standalone NAM block or a Dual Rig child block instead.

The touchscreen module drawer also recognizes supported parametric models and
shows learned controls. Touchscreen changes queue a prepared preset preview.
Definitions come from the actual model asset, not from untrusted preset metadata.

The Assets **Try in preset** shortcut uses an existing matching Neural Amp or
Neural Pedal block. It does not replace an amp with a pedal model automatically;
if no matching block exists, add the model by name from **Add block**. Unsupported
models retain their error message and cannot be selected by this shortcut.

Changing the selected model clears the old model controls. Reset to defaults
removes saved control overrides. Model filenames still follow normal asset
rename, usage, backup and restore behavior.

## Supported file contract

```json
{
  "version": "1.0.0",
  "architecture": "ConcatLSTM",
  "sample_rate": 48000,
  "metadata": { "gear_type": "pedal", "license": "MIT" },
  "config": {
    "hidden_size": 12,
    "num_layers": 1,
    "params": [
      { "name": "Drive", "type": "continuous", "min": 0, "max": 10, "default": 5 },
      { "name": "Mode", "type": "switch", "min": 0, "max": 1,
        "default": 0, "enum_names": ["Soft", "Hard"] }
    ]
  },
  "weights": ["architecture-specific numeric weights, not this placeholder"]
}
```

This example describes the envelope, not a loadable network. A trainer must
export the correct numeric weight layout for its architecture. Concat models support
up to 24 control definitions; FiLM supports up to 16. Names must be unique and
nonempty. Switch values are integer
indices into `enum_names`. Continuous values must be finite and in range.

Ardor requires mono audio input/output and 48 kHz models; it does not resample
other training rates. ConcatWaveNet excludes packed or slimmable inner networks
and condition DSPs, matching the adapted runtime. Nano selection does not resize
an individual concat model; a concat container selects its bundled child instead.
Unknown architectures and unsupported parametric versions are
reported in Assets; their controls are not advertised as playable modules.

FiLM files require `config.layers` to be an A2 channel width from 3 through 9,
`config.parametric.schema_version: 1`, `type: "film"` (or omitted),
`head_mode: "skip"`, and `condition_size` equal to the number of continuous
`parameters`. Parameters use the same name/min/max/default fields shown above,
with their exported values fed directly into FiLM (no concat normalization).
If `film_layers` is present it must list all 23 layers in order. Legacy residual
heads, LoRA, newer schemas and discrete FiLM parameters are rejected. Containers
accept 1–8 direct ParametricWaveNet children, identical control definitions,
strictly ascending `max_value` boundaries ending at 1.0, and matching versions and
48 kHz rates. A dual export uses its embedded model's metadata and calibration.

Preset values are keyed by model control name:

```json
{
  "id": "overdrive",
  "type": "namPedal",
  "enabled": true,
  "asset": "models/Ardor-Parametric-Overdrive-Demo.nam",
  "params": { "inputMode": "sum", "modelParameters": { "Drive": 7, "Tone": 3 } }
}
```

Values are checked and applied before reset/prewarm. The mono host interface,
input calibration and existing output normalization remain intact. Invalid
control names, ranges, switch indices, versions, channel counts and truncated
weight blobs fail model loading. Inference buffers are prepared before audio
processing; no model parsing or file access happens in the audio callback.

## Companion trainer compatibility

The [Parametric capture app](https://github.com/balazsbencs/parametric-nam-trainer)
defaults new projects to **NAM A2 · Standard + Nano (one file)**. It trains two
independent networks using the same captures and controls, then packages the
3-channel Nano and 8-channel Standard into one `SlimmableContainer` 1.0.0 file.
Both use the 23-layer A2 topology with concat conditioning. Upload that file once;
it appears as one module with a **Model size → Standard / Nano** selector.
Individual A2 Standard, A2 Nano and Current recipe profiles remain available.

Choose the profile in **Train → Model architecture**, train, and save
`model_parametric.nam` from Review. Upload that file in Ardor's Assets manager.
The trainer adds the project name, `metadata.gear_type` (`amp` or `pedal`) and
`metadata.training_architecture` without modifying weights or calibration.
Ardor uses the gear type for the modules drawer and the child's `config.params`
for the shared model controls. Older exports without a gear type default to an amp.
The trainer now rejects recording rates other than 48 kHz and more than 24 controls.

The [trainer fixture notes](../tests/fixtures/parametric-trainer-a2.md) describe
real Standard, Nano and switch-conditioned exports and the initialized
24-control contract fixture. C++ tests compare Ardor with the trainer's Python
inference at 13- and 64-frame buffer sizes, including control extrema and
switch positions. Asset-store tests upload those exact model documents;
native drawer checks verify every control and the amp/pedal classification.

Concat containers require 1–8 direct `ConcatWaveNet` children with version 1.0.0,
48 kHz rates, identical ordered controls, gear type and input calibration.
Their ascending `max_value` boundaries must end at 1.0; the trainer exports
Nano at 0.5 and Standard at 1.0. Nested, mixed-family and inconsistent tiers are
rejected. Each child retains its complete weights and metadata. The container
preserves each child's standalone loudness normalization; switching sizes keeps
the saved controls. FiLM containers retain their existing 0.7.0 contract.
This concat container extension requires Ardor; it does not imply compatibility
with stock NAM or other parametric players.

The companion trainer also offers **NAM A2 FiLM** profiles: Standard (8 channels),
Nano (3), and Standard + Nano in one file. These train a local PyTorch FiLM A2
network using the same capture data and the pinned trainer's losses/checkpoints.
They export Gene Ko's `ParametricWaveNet` 0.7.0, FiLM schema 1 with a skip head,
or a two-tier 0.7.0 `SlimmableContainer`. FiLM supports up to 16 continuous
controls; switch projects use concat. Physical ranges and defaults are retained.
The training model includes the playback core's DC filter, and exported matrices
absorb the internal control normalization to consume raw physical values.

Combined FiLM packaging rescales copies of the children's output heads to −18 dB
loudness, following the existing FiLM container's parent-gain convention and
matching each child's standalone normalized playback. Original individual exports
retain their calibration. The [FiLM fixture notes](../tests/fixtures/parametric-trainer-film.md)
describe actual one-epoch exports, Python/C++ audio comparisons for both sizes,
size switching, native controls, and asset-store import/rename/list checks.

## Published Clone Overdrive demo

[`assets/nam/clone-overdrive.nam`](../assets/nam/clone-overdrive.nam) is the
unchanged release asset from [NAMix v0.6.6](https://github.com/mrgeneko/NAMix/releases/tag/v0.6.6).
It has Gain and Tone controls (0–1, default 0.5), with 4-channel compact and
8-channel full models. It is labeled a published parametric demo; hardware
capture provenance is not established here.

[NAMix's model-specific NOTICE](https://github.com/mrgeneko/NAMix/blob/cf6d7eb14a3ae87e740a13aed3dee2139459b4fe/NOTICE)
explicitly grants MIT terms for `clone-overdrive.nam`. The full relevant notice
is preserved in `assets/nam/Clone-Overdrive-LICENSE.txt`, including Gene Ko's
2026 copyright, and installed alongside the model. Model SHA-256:
`61bb3f25d07920d0fbe8711542df2e585c21ab20d77332816a0de0d66ea148bb`.
Both bundled demos are seeded on new images and installed when missing during
startup on existing data partitions. Development users upload them via Assets.

Example Clone preset block:

```json
{
  "id": "clone",
  "type": "namPedal",
  "enabled": true,
  "asset": "models/clone-overdrive.nam",
  "params": { "useNano": true, "modelParameters": { "Gain": 0.8, "Tone": 0.3 } }
}
```

## Open overdrive demo

[`assets/nam/Ardor-Parametric-Overdrive-Demo.nam`](../assets/nam/Ardor-Parametric-Overdrive-Demo.nam)
is an MIT-licensed, Ardor-owned model with **Drive** and **Tone** controls from
0 to 10. It is trained on a synthetic soft clipper and a one-pole tone filter,
not a physical pedal capture or a replica of ParametricOD. The label and embedded
metadata state that limitation. It is small: one 12-unit LSTM, 805 weights.

New firmware images seed it into the writable model library. The startup script
also installs it when missing on an existing data partition, without overwriting
an existing file. For a development device, upload the `.nam` through Assets.

Reproduce the model with Python and PyTorch:

```sh
python3 scripts/train-parametric-overdrive.py
```

The script fixes its random seed, generates its own synthetic training data and
writes the NAM weight layout directly. It records a held-out synthetic validation
ESR in the model metadata; that number is not a claim about hardware accuracy.
PyTorch versions and CPU kernels can cause small training differences.

## Validation and remaining work

`pedal-parametric-nam-smoke` verifies both published Clone tiers against the
independent generic Eigen runtime from Gene Ko's fork, audible changes for Gain
and Tone, FiLM schema/weight rejection, and embedded dual exports. It also verifies
audible differences for the synthetic demo's Drive, Tone and a
one-hot switch; both concat architectures; block-size independence; reset and
preset restoration; a pedal before a snapshot amp; native drawer metadata; and
malformed controls/weights. Asset-store tests verify upload, list and rename
metadata. Manager tests verify discovery, edits, reset and model replacement;
browser tests cover concat combined exports, the open demo, published FiLM
pedals and a FiLM amp on desktop and phone, including learned values, tier
restoration and the amp save/load acknowledgement. Web validation rejects invalid
control ranges, fractional switch values, unknown controls, malformed control maps
and unsupported models. Replacing a parametric file with a fixed model also blocks
stale learned values before Apply.
Assets shortcut tests verify matching amp/pedal blocks, Undo, and preservation of
the other block.

The web-manager follow-up passed 755 unit tests and 16 mocked browser cases
across desktop and phone, plus typechecking, hosted/device builds and the manager
daemon tests. These browser cases complement the real Pi manager workflows in
the hardware report; they do not establish additional audio performance limits.

Still outside this release: HyperWaveNet, FiLM LoRA/schema 2 and residual heads,
other forks' parametric architectures,
per-scene learned control values, MIDI/expression control of learned parameters,
live manager parameter messages, arbitrary model-rate resampling, and hardware
capture training in Ardor.

[Pi 4 hardware validation](parametric-nam-pi4-validation-2026-10-11.md) covers
ARM numerical compatibility, real manager uploads/apply, full-chain capacity,
Codec Zero timing and stability. Capacity depends on the model: the tested
Standard parametric A2 chains fit one model; compact tiers allow more. Read the
report for complete configurations, headroom and measurement limits.
