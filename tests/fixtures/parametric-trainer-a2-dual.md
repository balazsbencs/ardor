# Combined Standard + Nano trainer fixture

`parametric-trainer-a2-dual.json` embeds the actual combined export produced by
the companion trainer's `test_combined_training_exports_both_tiers_and_switches`.
Both networks were trained for one CPU epoch on synthetic audio, with Drive and
a Soft/Hard switch. The container contains unchanged 3-channel Nano and
8-channel Standard child documents. Each child retains its own loudness.

The fixture has 512 input samples and 14 Python audio references: seven control
settings for each tier. `slimmable_size` is 0 for Nano and 1 for Standard. The
reference generator reloads each child's exported weights into the pinned
Python ConcatWaveNet and applies that child's loudness normalization to -18 dB,
independently of the container's C++ implementation.

Regenerate by running the companion trainer suite with a retained `--basetemp`,
then invoking from the trainer repository:

```sh
.venv/bin/python scripts/export-ardor-reference.py \
  /path/to/combined/run/model_parametric.nam \
  --output ../ardor/tests/fixtures/parametric-trainer-a2-dual.json
```

The environment and upstream revision match `parametric-trainer-a2.md`.
The C++ smoke compares both tiers at 13- and 64-frame buffer sizes, switches
tiers on a loaded processor while retaining controls, checks native selector
and preset restoration, and rejects malformed controls, calibration, versions,
families, boundaries and weights. Go tests upload/list/rename one asset and
validate its shared controls and two-tier metadata. Manager browser tests verify
one module, Standard/Nano selection, switch editing and saved-state restoration.
Synthetic one-epoch training establishes interoperability, not gear accuracy.
