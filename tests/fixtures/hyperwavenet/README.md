# HyperWaveNet reference fixture

`hyperwavenet.nam` and `hyperwavenet_golden.json` are unchanged MIT-licensed
fixtures from phillipmself/NeuralAmpModelerCoreParametric at
`fddcbc91e74ba79066a43c3b73cc690073529232`, under `tools/test/fixtures`.
See `src/namparam/LICENSE` for the upstream license.

The upstream generator renders its Python trainer at three Gain/Mode settings,
including every switch position. Ardor compares raw audio at 13, 64 and 128 frames,
then tests invalid weights/layouts, live targets and preset restoration. These
synthetic fixture weights are not the user's Marshall captures. The Marshall
models remain external inputs to the test and are not redistributed here.

Run `pedal-hyperwavenet-smoke [fixture-directory [model.nam ...]]` to include real
models. Timings use 128 frames, 1,000 warmup blocks and 10,000 measured blocks,
with all controls changed every 16 measured blocks. C++ new/delete calls are
audited. Linux builds also wrap linked malloc/calloc/realloc/free references,
including Eigen scratch allocation; allocations internal to shared libraries are
not intercepted. An optional leading `--cpu N` pins the Linux benchmark thread.

The smoke also generates a deterministic 16-channel A2 model in memory. It
compares the specialized and generic runtimes with full and low-rank deltas,
including a changed dilated-convolution weight, mixed block lengths, ring wraps
and retargeting. Shape mutations verify that near-matches retain generic playback.
Passing an actual HD model path repeats that comparison with the supplied weights.

`pedal-hyperwavenet-live PRESET DATA_ROOT SECONDS` is a separate hardware helper,
not a CTest. It opens the production 48 kHz / 128-frame audio backend on CPU 2,
mutes physical output, changes all learned controls every 100 ms, and fails on
scheduling, gap, overrun or nonfinite counters. Stop the normal audio service
before running it, then restore that service and its previous selection.
