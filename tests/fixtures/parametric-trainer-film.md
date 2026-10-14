# FiLM companion trainer fixture

`parametric-trainer-film.json` contains a real combined pedal export and a real
individual Nano amp export from the companion trainer's
`test_actual_film_dual_training_and_individual_export`. Each network was trained
for one CPU epoch on synthetic audio. Controls are Drive (0–10) and Tone (−5–5).
Training includes joint target normalization, checkpoint selection, and final
output-scale compensation. These fixtures establish format interoperability;
they do not establish accuracy on physical gear.

The local trainable PyTorch FiLM A2 follows Gene Ko's public schema 1 contract:
23 A2 layers, modulation before LeakyReLU, a summed skip head, 16-tap output
convolution, and the same 20 Hz DC filter as the playback runtime. Training
normalizes control ranges internally; exported matrices absorb that mapping
and take raw physical values, as Gene Ko's player expects.

The combined 0.7.0 SlimmableContainer carries a 3-channel Nano and an 8-channel
Standard. Packaging copies and rescales each child's head to −18 dB loudness,
matching its standalone normalized playback. Individual files are retained
with their original output calibration. This follows the existing FiLM
container's parent-level gain convention.

The fixture includes 512 input samples and 18 Python audio cases, with defaults,
individual extrema and joint extrema, for both bundled tiers and the individual
model. Reference inference reloads exported weights into the companion trainer's
Python model, independently of Ardor's C++ kernels. Ardor compares at 13- and
64-frame buffers, switches sizes while retaining controls, restores native
preset controls, and rejects malformed schema, controls, versions, tiers and
weights. Go tests import, rename and list the actual model documents.

Regenerate from the trainer repository after retaining a full pytest run:

```sh
.venv/bin/python scripts/export-ardor-reference.py \
  /path/to/film-dual/run/model_parametric.nam \
  /path/to/film-nano/run/model_parametric.nam \
  --output ../ardor/tests/fixtures/parametric-trainer-film.json
```

The pinned NAM trainer revision and environment match `parametric-trainer-a2.md`.
