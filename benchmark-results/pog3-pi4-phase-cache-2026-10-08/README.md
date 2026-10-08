# POG3 phase-cache experiment — Pi 4, 2026-10-08

**Both candidates rejected; production DSP restored.** Per-bin validity regresses
combined mean CPU 0.540%/0.497% at 64/128; bulk-cleared validity regresses
0.559%/0.786%. Actual diagnostic phase arguments fall 31.566%, without useful
ordinary benefit. Baseline `35b64bca2aca6fe6ec65bd1fca8c3eac3736fa6e`
retains top-256 partitioning, both birth snapshots, refined scheduling and
four-source rendering. Freeze/gliss is opt-in, default OFF.

Read [the decision, exactness review, all timing tables and Luna next step](../../docs/pog3-phase-cache-results.md).

## Artifact layout

- Root ordinary files: first **per-bin** candidate (+16028 prepared bytes),
  ABBA first round. `confirmation/` is its second round; `combined-*` pools only
  these eight runs. `first-candidate.patch.gz` archives its production diff.
- `bulk-validity/`: independent **bulk-cleared double-validity** candidate
  (+19320 bytes), its own first/confirmation/combined files and `candidate.patch.gz`.
  Never pool the root and bulk datasets or compare candidates using these
  independent round baselines as a direct head-to-head test.
- Each `diagnostic/`: original and candidate generated instrumentation,
  actual phase-argument counters, independent validity shadow assertions,
  active-profile exact-audio receipts, compressed raw logs, parsed callbacks,
  stage events, 5247 frame records and accounting checks. Diagnostic timings
  are excluded from ordinary CPU comparisons. Raw frame-count records now have
  **22 fields**; use this experiment's updated parser.
- `numerical-*`, host logs, pitch checks: immutable original/candidate matching
  headers and libraries; exact 2498-frame/173482-region (4226018-byte) histories,
  1966080-float automation, 384000-float core output, host opt-in automation.
  First/core hashes differ between host and target; comparisons are separately
  exact within each platform. Standalone active-profile Pi output also equals
  the ordinary original Pi output exactly.
- `host-OFF/ON-ctest.log`: 10/11 DSP suites for each candidate. Independent
  original phase-history test, affected sanitizers/strict warnings and target
  pitch tests also pass. Empty warning logs indicate successful compiler exits.
- Source, patch, target ELF and build-flag hashes identify candidates. Ordinary
  target ELF hashes before/after both rounds match. Final source/header hashes
  match original; final rebuilt ARM full-core ELF equals immutable baseline
  `d6352129bcd9b7742db904d996f387a1f5bbaa0efa6deac31314fb817d9d5299`.
  `final-host-*` records freeze-OFF rebuild and all 10 passing suites after removal.
- Wisdom checker compares header plus planning-record multiset (not export text
  order). All exports retain the same 57 plans. Seed SHA-256:
  `964fddf2f8a7f7744905961489a90ceaecbb244bfd8d2d5617491c040efaa545`.
- Device metadata, zero-exit receipts, service PID readbacks and final cleanup
  are retained. Existing pedal service is restored after every runner. Only four
  session-owned `/tmp` probe directories and its build container are removed.
  Requested C++ storage excludes FFTW/libc planning memory. No deployed app changes.

## Reproduction

Use an isolated source tree rooted at HEAD35b and this commit's tests and drivers.
The **final production tree has no phase cache**: to reproduce a candidate, apply
its archived production patch to that baseline before building. Each gzip patch
contains only `PolyphonicPitchBank.cpp/.h`; verify its source hashes. Keep an
immutable unpatched baseline tree and use its matching headers for every probe.
The new phase-history test itself also passes against that original library.

Scripts record this session's absolute repository/cache/SDK paths. Adapt those
paths to isolated trees; do not apply both patches cumulatively or run build
scripts against the restored final DSP expecting candidate timings. The cache
baseline source alias for bulk must be a **relative symlink** when mounted into
the container (`phasebulk-baseline-source -> phasecache-baseline-source`).

Buildroot 2025.02.15 SDK volume, GCC 13.4, CMake 3.31, Python generator, GNU Make
4.4.1, static NEON float FFTW 3.3.10, Release -O3/-DNDEBUG. Cached original ordinary
full-core ELF was built from retained HEAD35b. Baseline diagnostic source overlays
only the standalone profile drivers for actual argument counters; DSP/header
remain HEAD35b. `ordinary-build-flags.txt` records candidate configuration;
`*-diagnostic-build-flags.txt` records original/candidate instrumented builds.
Generators run off-thread and compile separate copies; ordinary profiling is OFF.

1. Build `device_pog3`, full/cpu/pitch probes with the recorded candidate script.
   Build separately linked automation and direct-field traces with matching
   original/candidate headers. Validate exact host traces, 10/11 suites, affected
   sanitizers and warnings with the retained scripts. Compare float/field bytes,
   not struct padding. Expanded traces have identical semantics for both layouts.
2. Upload hashed probes into a unique pedal `/tmp` directory. Use the existing
   service stop/restore runner, CPU2, SCHED_OTHER, 48k and existing performance
   governor. Check numerical/pitch results before timing. Do not run hardware
   runners concurrently. Do not deploy an application or change runtime settings.
3. Run ordinary ABBA, then confirmation with unchanged probes/seed. Parse each
   round with `summarize.py`, combined with its own `summarize_confirmation.py`
   (16028-byte assertion at root; 19320 in bulk). Check all returns, allocation,
   FFT/preparation and unchanged ELF receipts. Keep raw tails and controls.
4. Build separate interpretation-profile probes. Check active-profile output
   against original ordinary full-core before counting work. Parse logs with
   `summarize_phases.py DIR profile-baseline` and `... profile-candidate`, then
   `audit_phase_counts.py DIR`. Each layout independently saves 533169 previous
   args from 844543 phase peaks; shadow validity equals actual hit flags.
5. Check each ordinary/confirmation wisdom directory with `check_wisdom.py DIR`;
   diagnostic directories use `check_wisdom.py DIR 2`. Restore/read back the
   existing service and retrieve receipts before removing only owned probes.

Executables, full float/field comparison blobs and compiler cache are not
committed. Immutable original/candidate libraries, source snapshots and ELFs are
preserved in the local disk cache for later analysis. `manifest-sha256.txt` covers
all committed artifacts recursively, excluding itself and Python caches.
