# POG3 nonoverflow bin sort — Pi 4, 2026-10-08

**Retain skipped nonoverflow sort:** combined ordinary mean reductions
0.912%/0.562% at 64/128, with unchanged memory and exact output. All 10/11 suites
pass. 64-frame maxima/over-period counts worsen and the uninvolved spectral
control also improves; no isolated stage attribution or live admission is claimed.

[Full review, timings and Luna next step](../../docs/pog3-bin-sort-results.md).
Baseline: `816d1b8603b07ae7b58c2930bae124fcb185c8bd`. Its DSP/header and ordinary
full-core behavior are identical to retained HEAD35b after phase-cache rejection.
Immutable original HEAD35b libraries/full-core ELFs are reused with HEAD816
matching headers; `source.txt` and original/candidate hashes identify this reuse.

The candidate moves the final ascending-bin sort inside the overflowing top-256
partition branch. Accepted scan order already supplies ascending bins for at most
256 candidates. No header/layout/storage, phase arithmetic or track/history
changes. `candidate.patch.gz` is the standalone production patch against HEAD816.

Root files contain first ordinary ABBA, `confirmation/` its separate repeat;
`combined-*` pools these rounds only. CPU probes use unchanged ELFs/wisdom,
48k, four-second warmup/reset and four-second measured fixture, CPU2,
SCHED_OTHER unpaced and existing performance governor. All profiles/freeze OFF.
Every raw control distribution, p99/max/outlier and zero-exit receipt is retained.
No timing correction or statistical significance is claimed.

`host-OFF/ON-ctest.log` saves 10/11 passing DSP suites. Numerical receipts/logs
save exact 2498-frame/173482-region field histories, default host/Pi automation
and full-core output plus host opt-in automation. Existing 810-frame original
full-sort and 464-frequency Cartesian history oracles run in pitch quality.
Strict host/AArch64 warnings, affected sanitizers and callback allocation checks
pass. `final-default-build.log`/`final-config.txt` record final freeze-OFF targets.
Ordinary rows retain 1625912-byte core C++ preparation, zero callback allocations
and 9/16 maximum FFTs at 64/128; FFTW planning memory is excluded.

## Reproduction

Scripts record absolute session paths; adapt them to isolated source/cache/SDK
locations. Start from HEAD816, retain an immutable original header/source tree,
and apply the single archived candidate patch to another isolated tree. Use each
build's matching headers for its automation/direct probes. Production DSP state
is defined by the documented decision; do not assume final source contains a
rejected patch. Build flags and source/ELF receipts are included.

Use the cached Buildroot2025.02.15 SDK, GCC13.4/CMake3.31, GNU Make4.4.1 and
static NEON float FFTW3.3.10 in an isolated Ubuntu24.04 container. Source/SDK
mounts are read-only, own disk cache read-write, compiler TMPDIR on disk. Host
uses GCC14.2/CMake and distro shared float FFTW3.3.10. Ordinary Release
-O3/-DNDEBUG profiles/freeze OFF; separate host opt-in library/suite freeze ON.

1. Build full/cpu/pitch probes with candidate scripts and separately linked
   original/candidate automation/direct traces. Run host tests, exact numerical,
   affected sanitizer and strict warnings before selecting a result. Trace helper
   serializes public fields individually rather than reading struct padding.
2. Stage hashed probes in a unique target `/tmp` directory and run numerical/pitch
   tests before ordinary ABBA, then repeat ABBA with unchanged binaries and the
   same seed. Existing service stop/restore runner supplies receipts. Never run
   multiple hardware runners concurrently; do not deploy an app or change settings.
3. Regenerate with `summarize.py DIR` for each round and
   `summarize_confirmation.py ROOT` for both. Audits assert 48 rows, identical
   wisdom, zero C++ callback allocations, unchanged preparation/FFT bounds.
4. Run `check_wisdom.py DIR` on both round directories: exported text order can
   differ, but header plus 57-plan record multiset must remain equal. Seed SHA-256
   is `964fddf2f8a7f7744905961489a90ceaecbb244bfd8d2d5617491c040efaa545`.
5. Read back service PID after every runner and retrieve all receipts before
   removing only owned target probes/container. No installed app/firmware,
   buffer/governor/NAM-quality or unrelated-checkout changes.

No additional interpretation instrumentation is used for this small candidate;
ordinary timing and exact ordering/history validation determine the decision.
Executables and full float/field blobs are not committed. Immutable original and
candidate libraries/ELFs remain in the local disk cache. Recursive
`manifest-sha256.txt` covers committed artifacts, excluding itself and caches.
