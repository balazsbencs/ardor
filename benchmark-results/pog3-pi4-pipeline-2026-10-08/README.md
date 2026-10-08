# POG3 worker pipeline on the pedal — 2026-10-08

[Results, ownership review and detailed Luna next steps](../../docs/pog3-worker-pipeline-results.md).

The tested serial POG3 + NAM/EQ workload passes with **two fixed 128-frame
delay blocks / 5.333 ms added latency**. Both 30-second cases and the 180-second
case have zero xruns, late generations, submission misses and wrong outputs.
The one-block worker deliberately fails on the first late generation; direct
serial processing fails on the first playback xrun. Exit 3 is a failure result,
even when ALSA itself has not yet underrun.

## Retained files

- `*.summary.csv`, `receipt.csv`, `soak-receipt.csv`: exact final case counters.
  Passing cases exclude four seconds of warmup; failed cases include their
  partial warmup/measurement lead-up. Do not compare partial failure means to
  steady-state means. Runner exit 0 means collection/restoration succeeded;
  per-case receipt codes decide feasibility.
- `*.callbacks.csv.gz`: lossless callback wall/CPU time, timestamps, xruns,
  submitted and returned output generations. Every pipeline row must consume
  `submitted-delay`, or zero during the explicit startup silence.
- `*.worker.csv.gz`: lossless job submission/start/end times, CPU time, wakeup,
  transforms and actual CPU. `worker_over_period` counts submission-to-finish
  above **one nominal period**, even for the two-block case. It is not a count
  of missed two-block deadlines; strict due-generation checks provide that count.
  Passing worker means omit jobs submitted during warmup. The final one/two
  jobs complete during drain but are beyond the consumed playback timeline.
- `*.maps.gz`: process mapping receipts. The diagnostic executable is non-PIE.
- `target-quality.log`, `host-quality.log`: exact sample/delay/automation and
  lifecycle checks at 64/128 frames and both delays, with C/C++ allocation/free
  checks on callback and worker paths. Preparation/lifecycle are outside scope.
- `legacy-routing-checks.log`, `strict-target-warnings.log`: unchanged default
  executor behavior and strict ARM compile checks. The latter is empty on success.
- `input-sha256.txt`, `soak-input-sha256.txt`, `source-sha256.txt`, `build-flags.txt`:
  executable/wisdom/preset/model identities and final source/build conditions.
  Model and executable binaries are not committed. Production POG3 DSP is unchanged.
- `device-*.txt`, `soak-device-*.txt`, service logs/PIDs and
  `restored-final-device.txt`: environment and independent restoration readbacks.
  The restored app uses MMAP / S32_LE / 48 kHz / 128 / 384, both codec output
  switches on, output relay enabled. Normal app/presets/settings were not deployed.
- `initial-one-block/raw/`: earlier exploration with the original one-block
  adapter and a different executable. Both late-generation failures and their
  full traces are retained separately. The final root-level batch is the matched
  comparison of direct and both delay configurations in a single executable.
- `audit.py`, `audit.txt`: independent arithmetic and generation-accounting
  validation. `sha256-manifest.txt` covers this directory except itself.

## Reproduction

Use the SDK/cache mounts from the preceding
[headroom build](../pog3-pi4-headroom-2026-10-08/build.sh), current worktree sources
and the same seed wisdom. The target uses Buildroot GCC 13.4, Release `-O3
-DNDEBUG`, freeze OFF, static single-precision FFTW/NEON, ordinary NAM quality.
No DSP stage profiling is enabled.

```sh
sh /ardor/benchmark-results/pog3-pi4-headroom-2026-10-08/build.sh
/buildroot/output/host/bin/cmake --build /probe/headroom-arm \
  --target pedal-pog3-pipeline-quality pedal-pog3-malloc-probe -j4
```

On a Linux host with this repository's usual dependencies:

```sh
cmake -S . -B build-ci
cmake --build build-ci --target pedal-pog3-pipeline-quality pedal-pog3-malloc-probe -j4
LD_PRELOAD="$PWD/build-ci/libpedal-pog3-malloc-probe.so" \
  build-ci/pedal-pog3-pipeline-quality --allocation
```

Stage the two target executables, allocation interposer and seed wisdom in an
owned device directory. Adapt `run-remote.sh` paths and inspect the unchanged
reference preset/model before running. Its default directory is
`/tmp/pog3-pipeline2-20261008`. Never overlap runners. The normal app is stopped
temporarily, actual ALSA capture/playback clocks the diagnostic, deterministic
synthetic audio feeds DSP, and physical playback receives silence.

Run `run-soak-remote.sh` only after both short two-block cases pass and the first
runner has exited/restored the app. Read back service, ALSA settings and codec
outputs independently after restoration. Collect receipts, gzip raw timing/maps
losslessly, and run:

```sh
python3 audit.py .
```

RW_INTERLEAVED diagnostics omit the normal UI and production MMAP wrapper.
The fixture exercises changing stereo pitches/chords and the current heavy
reduced-core settings; actual controls are static during timing runs. Automation
is checked separately by quality tests. This is not general routing, live
control-sweep, physical round-trip or subjective added-delay acceptance.
