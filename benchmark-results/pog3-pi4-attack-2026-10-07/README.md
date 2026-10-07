# Retained Attack CPU evidence

Base: `34bf267c533570aad65ab2106ef8d23a5e1b469d`.

- Apply `candidate.patch.gz` (decompressed) to the base for the three production
  DSP files. `harness.patch.gz` supplies the build/test/diagnostic changes.
- Build each baseline probe with original headers: archive `src` and
  `benchmark-results/pog3-pi4-render-schedule-2026-10-07/automation-trace.cpp`
  from the base into a separate source directory before compiling it. Never link
  an old bank archive to an automation probe compiled with the new bank layout.
- `host-numerical.sh`, `host-validation.sh`, `build-arm.sh` and `build-context.txt`
  retain the exact local toolchain/cache paths; adapt mounts and paths to a new
  environment. Baseline full ELF provenance is in `baseline-provenance.txt`.
- `profile/` and `detail/` are old-Attack diagnostic runs, not optimization
  measurements. Their retained generator versions reproduce against base
  source. To regenerate parsing, decompress each `profile.log.gz` and pass it
  plus an output prefix to that directory's `summarize_phases.py`.
- `numerical-check/` is the successful pedal pre-timing gate. Root timing is
  the first ABBA and `confirmation/` is the second, using unchanged ELFs/wisdom.
  Numerical logs copied into confirmation refer to the preceding gate; numerical
  tests were not rerun in the timing-only confirmation.
- Run `python3 summarize.py .`, `python3 summarize.py confirmation`, and
  `python3 summarize_confirmation.py .` to reproduce tables. Run
  `python3 check_wisdom.py .` and `python3 check_wisdom.py confirmation` for the
  four exports per timing round; use a final `1` argument for either diagnostic.
- `sha256sum -c artifact-sha256.txt` audits every retained file recursively.
  Float blobs and executables are not committed; hashes, byte counts, complete
  raw timing distributions and numerical receipts are retained.

Read [the report](../../docs/pog3-attack-cpu-results.md) for ordering review, overhead limits,
all result tables, memory/runtime limits and the next Attack experiment.
