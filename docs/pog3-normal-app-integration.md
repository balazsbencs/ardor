# Poly Octave 3 normal app integration

2026-10-12. This milestone adds the existing POG3 core to the normal fbdev app
and manager, preserving the FFT/pitch/Attack algorithms. It follows the
[two-block worker experiment](pog3-worker-pipeline-results.md).

## Prepared host and ownership

`Pog3Hosted` owns one core and one CPU3 worker. CPU2 remains the audio callback.
Normal `--parallel-rigs --rig-worker-cpu 3` requests CPU3 affinity and FIFO69;
a worker that cannot acquire these settings is rejected during preparation.
Direct/offline hosting retains the existing sample processor. The worker path
accepts complete prepared 32/64/128-frame blocks, with exactly 256 samples of
common added output delay (8/4/2 blocks). This is **5.333 ms at 48 kHz**, in
addition to converter, ALSA and existing spectral latency; it is not total
physical latency. Current live qualification is at 128 frames.

UI/MIDI/scene targets publish to a separate atomic 33-control bank. Each input
block gets its own fixed-size target snapshot; the worker alone advances DSP
state with those dated targets. Completed generations release snapshot slots
before reuse. Reset occurs with callbacks quiesced and joins/flushes work before
resetting histories. Destruction joins before releasing worker-owned state.
The callback never waits, allocates, replans or performs FFTW file access.

Startup emits exactly the prepared common delay of silence. A missing due
output, rejected submission or wrong quantum increments the exposed worker
fault count and latches silence until reset/preset replacement. No stale replay
or direct callback fallback is accepted as a passing performance run. This
conservative fault policy remains a draft limitation: automatic user-visible
recovery from a worker deadline failure is future work. Full-app validation
checks absolute zero faults as well as performance-script deltas.

Local block bypass keeps the common delay, crossfades and continues draining
prepared blocks. POG3 uses modulation cut/crossfade rather than the scalar
reverb/delay let-ring path. Global effects bypass retains the app's existing raw
input policy and can suspend the chain; it does not preserve pipeline latency.

## App contract

Both editors expose `mod/pog3` as **Poly Octave 3**, with the core registry's
33 stable keys/indexes, defaults, physical units and discrete choices. POG3 has
its own manager constraint group so it can coexist with a legacy modulation
block. A serial chain admits one POG3, including initially bypassed blocks;
Dual Amp/Rig, WDW and a second POG3 are rejected before preparation. Ordinary
serial NAM remains supported. These constraints reserve worker ownership;
they do not certify arbitrary additional effects under the CPU budget.

The default build exposes Off, Volume, Crossfade, Warp and Filter expression
choices. Freeze/gliss stays opt-in and OFF; reserved normalized choices are
rejected. Expression mode is a preset-level choice rather than a scene target;
other eligible numeric controls use the existing stable-index scene machinery.

The ARM64 app imports the packaged FFTW 3.3.10 NEON wisdom once under the planner
mutex during preparation. Other platforms plan normally. Missing/incompatible
wisdom still permits preparation, but changed plans require a new hardware
performance check. Buildroot installs both the seed and existing FFTW license.

## Checks and measured result

- Eight affected app smoke suites and the new hosted quality suite pass.
  Preset activation also covers serial factory admission, initially disabled
  duplicate rejection, split/WDW rejection and deferred freeze rejection.
- Exact direct/worker output at 32/64/128 frames, input-dated automation,
  48-sample cadence crossings, startup/drain/reset and deliberate fault recovery
  pass on host and Pi. Worst audio error is zero. The global libc interposer
  observes zero allocation/free calls across caller and worker processing.
- The manager's catalog/value suites pass 392 tests; the device bundle builds.
  The new hosted adapter/test compile with `-Wall -Wextra -Wpedantic -Werror`.
- The full **fbdev UI + MMAP_INTERLEAVED S32_LE stereo** app runs at
  48 kHz / 128-frame period / 384-frame buffer on the Pi 4. Four 30-second
  samples use dry/up/down/blended voice presets, retaining the device's
  Vibe + Supro NAM + EQ chain. Each actual preset selection is captured.
  The complete run ends at 47,455 callbacks with zero callback overruns,
  gaps, worker misses, nonfinite output and block mismatches. Callback mean is
  1.17–1.18 ms and cumulative maximum 1.74 ms against a 2.667 ms period.
  Core CPU2 utilization is 47.4–49.7%; CPU3 is 82.9–85.2%. These are procfs core
  busy percentages, not per-job worker duration. Sampled temperature peaks at
  62.8 C and the clock remains 1500 MHz. Both audio FIFO70/CPU2 and worker
  FIFO69/CPU3 checks pass in every case.
- The initial muted attempt was interrupted by a bank change to an empty
  preset. The performance check correctly rejected the absent worker. The
  repeated unmuted run verifies bank 0 and the selected test presets. The user
  reported “I think it sounds great” after this run, accepting the integrated
  sound/feel in this test. Attack and Focus extremes were not exhaustively
  assessed in this listening run.

[Raw timing, selected presets, adapter checks and deployment receipt](../benchmark-results/pog3-normal-app-2026-10-12/README.md)
contain the evidence. Presets/settings and the original workspace are preserved.
This remains a draft: long thermal endurance, broader effects/parameter load,
32/64-frame live throughput and deferred freeze/calibration work are not certified.
