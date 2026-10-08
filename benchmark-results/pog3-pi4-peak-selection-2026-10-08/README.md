# Exact top-256 peak selection evidence — 2026-10-08

**Retain the partition.** Two ordinary matched Pi ABBA rounds show
1.976%/0.967% combined mean reductions at 64/128 frames, exact frame/audio
histories and unchanged 1625912-byte C++ preparation. p99 and pooled 64-frame
over-period counts improve; recorded maxima worsen slightly. See
[the full results and reviewed Luna next-step plan](../../docs/pog3-peak-selection-results.md).

Baseline `4e262aed0109fa5cfe31483216248fc290677671` retains frame/Attack birth
snapshots, refined due ages and four-source rendering. Original source/headers,
libraries and ELFs were preserved before the candidate. Only the overflowing
magnitude full sort becomes `nth_element`; capacity events and final bin sort
remain. All phase work still runs before selection, including dropped peaks.

## Evidence

- Root `baseline-1`, `candidate-1`, `candidate-2`, `baseline-2` CSV/logs: first
  ordinary ABBA round. `confirmation/`: second round, unchanged ELFs and seed
  wisdom. `receipt.csv` confirms numerical/pitch checks before first timing;
  confirmation receipt lists only the four timing probes.
- `summary/comparison.csv`, confirmation equivalents and `combined-*.csv`:
  mean demand, p99/max, period counts, raw controls and both-round comparisons.
  `summarize.py` and `summarize_confirmation.py` regenerate these exact summaries
  and audit all 48 rows for allocation, preparation and FFT bounds.
- `peaks-trace.cpp`: explicit public-field serialization, independently linked
  to saved original/current libraries and headers. Includes **1538 histories /
  172698 regions**, with 450 added cutoff/tie histories and broad lifecycle cases.
  Original/current 4183202-byte traces match separately on host/Pi. Default
  automation has 1966080 floats and core has 384000; host opt-in automation also
  matches all 1966080. Hashes, byte counts and numerical logs are retained.
- New `peakSelection()` in `tests/pog3_pitch_quality.cpp`: 810 publications
  equal independent original full-sort choices. Normal pitch suite includes it;
  `--peak-selection` runs it alone. `host-original-cutoff.log` verifies the new
  fixture separately against the immutable original library. Pi pitch log includes
  this check before CPU timing. No new ctest is needed: 10 default / 11 opt-in
  suites remain and all pass.
- Affected automation/frame/cutoff ASan/UBSan/leak logs, strict host/AArch64
  warning checks and required-preload C allocation/free suite pass. All ordinary
  timing rows have zero C++ callback allocations and 9/16 core FFT maxima. Final
  host ordinary targets are rebuilt freeze OFF.
- `check_wisdom.py`, seed/exports and checks: all eight timing exports retain the
  header and 57-record multiset. Source/build/ELF hashes and archived candidate
  patch identify exact inputs. All eight target probe ELFs remain unchanged
  through both rounds. Only hash/length evidence is kept for comparison blobs.
- Metadata and independent service PID readbacks: restored existing service after
  each round. Cleanup removes only the unique remote probe directory and own
  container/comparison blobs. No application/firmware, buffer, governor or NAM
  quality changes are made; unrelated worktree edits remain in place.

## Reproduction

Build/probe scripts preserve the actual local cache and SDK paths. Host GCC
14.2, target Buildroot GCC 13.4, Release -O3/-DNDEBUG, target static NEON float
FFTW 3.3.10. All ordinary timing uses freeze and every profile OFF. Original
headers must be used for baseline probes. Archive paths and target build
commands are recorded; adapt mounts/cache paths if reproducing elsewhere.

The standalone harness imports the same 57-plan wisdom for every process. Each
workload warms up for four seconds, resets, then measures the same four-second
48 kHz audio/control timeline. Forward/forward/reverse/reverse ABBA. Target CPU
2, SCHED_OTHER, unpaced loops, existing performance governor at 1.5 GHz. The
core exercises all voices, processed dry, Focus, Attack, Warp, filter, doubling
and Spread with Off expression; granular/spectral controls remain in every run.

Numerical traces and full pitch/Focus/Warp/overload checks precede timing. Use the
restore runner only on the authorized device and unique `/tmp` paths. Independently
read back the service and retrieve all receipts before cleanup. No instrumented
stage timing is used in this experiment. Ordinary runtime results and controls
support retention; no corrected CPU estimate or statistical significance is
claimed. Finite offline period durations do not establish paced FIFO/ALSA,
intended-chain, memory, listening or thermal admission.

Regenerate summaries from repository root:

```sh
r=benchmark-results/pog3-pi4-peak-selection-2026-10-08
python3 "$r/summarize.py" "$r"
python3 "$r/summarize.py" "$r/confirmation"
python3 "$r/summarize_confirmation.py" "$r"
python3 "$r/check_wisdom.py" "$r" 4
python3 "$r/check_wisdom.py" "$r/confirmation" 4
(cd "$r" && sha256sum -c artifact-sha256.txt)
```

The manifest recursively covers every retained artifact except itself.
`host-tests.sh` runs OFF/ON and reconfigures OFF; `final-default-build.log` records
its subsequent ordinary rebuild. Final corrected scripts/logs describe successful
runs; initial local harness flag/name/executable-mode corrections changed no DSP
or measurement ELF. C++ requested preparation excludes FFTW internal storage.
