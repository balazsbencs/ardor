# POG3 actual-device headroom — 2026-10-08

[Results, limits and next experiment](../../docs/pog3-headroom-results.md).
DSP source is HEAD `81b6187408e141c71f520c507c41d6d5d6afc85a`; this change adds
only a diagnostic executable, analysis and documentation. No DSP optimization
or deployment is part of these measurements.

The pedal runs 48 kHz / 128-frame callbacks, 384-frame ALSA buffers, CPU2 FIFO70,
performance governor 1.5 GHz. The final batch uses one ELF across all modes.
`reference-preset.json` is the saved boot preset; its NAM model is not copied
into the repository. `input-sha256.txt` identifies the ELF, wisdom, preset and
model. `probe-sha256.txt` also hashes the diagnostic source.

## Files and interpretation

- `*.summary.csv` and `runner.log`: final ordinary/profile results. Exit 3 in
  `receipt.csv` means first real ALSA xrun, with partial trace preserved. Both
  combined paced runs fail during warmup; `complete=0,warmup_included=1` makes
  that explicit. Do not compare their partial means as steady-state results.
- `*.callbacks.csv.gz`: losslessly compressed per-callback wall/CPU time,
  start timestamp/gap, observed xruns and transform count. Successful traces
  exclude four seconds of warmup. Failed traces include completed warmup blocks
  through the first xrun. A capture xrun is recorded in the summary/log because
  it occurs before the next callback begins.
- `combined-offline-*`: CPU2 SCHED_OTHER **unpaced** combined demand, with
  actual DSP but no audio I/O. Zero xrun fields are not a passing audio result.
- `core-sample.*`, `pc-attribution.*`: CPU-cycle PC samples, zero lost samples,
  symbol attribution inside DSP timing windows. Profile timings are separate
  from ordinary admission evidence. Symbols are exclusive, not call stacks.
- `core-count.counters.csv`: user-only PMU cycles, instructions, L1 accesses,
  refills and branch misses. Enabled/running fields check multiplexing.
- `executable-symbols.txt.gz`, `libm-symbols.txt.gz`: exact-ELF `nm -S -n -C`
  output, with `-D` for the device's libm. Its executable LOAD segment has equal
  file/virtual offsets, permitting the mapping-relative lookup in `analyze.py`.
- `*.maps.gz`: lossless per-process mappings for PC resolution; probe ELF is non-PIE.
- `transform-timing.csv`: timing grouped by observed transforms per callback;
  this is a workload correlation, not causal FFT stage attribution.
- `device-*.txt`, `service-*.txt/log`, `restored-service-pid.txt`: environment
  and restoration receipts. Installed app uses MMAP_INTERLEAVED; diagnostic uses
  RW_INTERLEAVED. Both use identical rate/period/buffer settings.
- `installed-chain-baseline.txt.gz`: lossless installed-application telemetry.
- `initial-link-failure/`, `recovery-attempt/`, `taskset-preflight-failure/`:
  superseded setup attempts, excluded from final analysis. The recovery attempt
  was terminated after repeated codec prepare sleeps; its service was restored.
- `headroom-warnings.log`: strict target syntax/warning check exit code.
  `audit.txt` validates retained final artifacts. No production DSP suite was
  rerun because no production DSP source changed.

Both paced combined attempts deliberately return 3, not a passing exit status.
Final runner success means all planned cases were collected and service restored,
not that the combined effect passed. All final complete runs pass DSP health and
finite-checksum checks. Raw outliers are retained.

## Reproduction

Adapt absolute paths in scripts to an isolated worktree and owned device `/tmp`
directory. The session used Ubuntu 24.04, Buildroot 2025.02.15 SDK, GCC 13.4,
CMake 3.31 and GNU Make 4.4.1. `/ardor` and `/buildroot` were read-only mounts,
`/probe` a writable disk cache, and compiler TMPDIR `/probe/compiler-tmp`.
FFTW is static single-precision NEON 3.3.10 with the existing 57-plan seed wisdom.
NAM dependency commit is `4c0ee78b71abd5eb20aec58562e7540f43caac3b`.

1. Build the optional probe using `sh build.sh` inside the SDK container. It
   uses ordinary `-O3 -DNDEBUG`, freeze OFF, and no DSP stage instrumentation.
   Direct DSP object linking preserves NAM's static architecture registrars.
2. Hash the new executable and source, generate its symbol table, and copy it
   with the seed wisdom into an owned target directory. Use that executable for
   every mode. The executable/model themselves are not committed here.
3. Review the reference preset/model and service paths. Run `run-host.sh` with
   an authenticated SSH connection. It runs `run-remote.sh` serially, restores
   the previously running app on exit and collects artifacts. Never overlap
   runners. No buffer, governor, NAM quality or installed app changes are needed.
4. `alsa` mode uses CPU2 FIFO70, real capture/playback and silent physical output.
   `offline` uses CPU2 SCHED_OTHER and executes the same frame sequence without
   pacing. Both include `ScopedDenormalGuard` around DSP processing.
5. Resolve samples and validate the retained final set:

   ```sh
   python3 analyze.py . executable-symbols.txt.gz libm-symbols.txt.gz
   python3 audit.py .
   ```

   Gzip raw callback/sample CSVs losslessly for storage; analysis reads either
   form. `analyze.py` writes `pc-attribution.csv` and `transform-timing.csv`.
   The original `pc-attribution.txt` captures its printed output.
6. Read back the installed service and ALSA parameters independently before
   removing only the owned target probes/container. Session executables and
   libraries remain in the local disk cache for later same-build comparisons.

The recursive SHA-256 manifest covers this artifact directory except the manifest
itself. Historical setup artifacts do not override the final root-level results.
