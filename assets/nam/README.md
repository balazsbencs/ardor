# Bundled parametric overdrive models

`Ardor-Parametric-Overdrive-Demo.nam` is an Ardor-owned, MIT-licensed synthetic
neural overdrive demonstration. Drive and Tone are learned control inputs.
This is not Steve Atkinson's ParametricOD or a physical pedal capture.

The training source is `scripts/train-parametric-overdrive.py`; the file contains
its architecture, settings, weights and synthetic validation ESR. See
`docs/parametric-nam.md` for importing, inference compatibility and limitations.

Copyright (c) 2026 Ardor contributors. Distributed under the repository's MIT
license; a copy is included here for redistribution with the model.

## Clone Overdrive — Gene Ko

`clone-overdrive.nam` is Gene Ko's published parametric overdrive demo from
[NAMix v0.6.6](https://github.com/mrgeneko/NAMix/releases/tag/v0.6.6), bundled
unchanged. It provides **Gain** and **Tone**, each 0–1 (default 0.5), with
4-channel compact and 8-channel full FiLM models at 48 kHz. Its release identifies
it as a demo; Ardor does not claim a physical hardware capture or ParametricOD
provenance. Select **Use nano model** for its compact tier.

The model-specific MIT permission explicitly lists `clone-overdrive.nam` in
[NAMix's NOTICE](https://github.com/mrgeneko/NAMix/blob/cf6d7eb14a3ae87e740a13aed3dee2139459b4fe/NOTICE).
`Clone-Overdrive-LICENSE.txt` preserves that model notice and Gene Ko's copyright;
it is shipped beside the weights, including in firmware data seeds and updates.
The `LICENSE` file here applies to the Ardor-owned synthetic demo above.

Release asset: https://github.com/mrgeneko/NAMix/releases/download/v0.6.6/clone-overdrive.nam

SHA-256: `61bb3f25d07920d0fbe8711542df2e585c21ab20d77332816a0de0d66ea148bb`

The runtime adapter has its own MIT notice at
`src/namparam/LICENSE-Gene-Ko`. See `docs/parametric-nam.md` for supported schemas
and import limitations.
