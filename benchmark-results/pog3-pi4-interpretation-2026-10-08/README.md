# Frame interpretation attribution and compact birth-slot evidence

2026-10-08. **Retain the production frame-birth snapshot.** Combined ordinary
mean reductions are 4.933%/4.527% at 64/128 frames, with exact frame/audio
histories and 10/11 default/opt-in DSP checks passing. See
[the full report and reviewed Luna handoff](../../docs/pog3-interpretation-cpu-results.md).

## Provenance and scope

Baseline is `c5f4b1b1a81ff28d8a219398bd924e745c6b3e7f`; production DSP matches
`4de167c`. Original source/headers and ordinary libraries/ELFs were preserved
before editing. Candidate changes frame-birth allocation only, using five ordered
tiers with fixed uint16 indices. Preparation grows 3216 bytes across six aligned
frames; original phase, track, region and control/audio behavior remains.

Raspberry Pi 4B 1.5, AArch64, Linux 6.18.37-v8, 48 kHz, existing performance
1.5 GHz, CPU 2, SCHED_OTHER unpaced standalone probes. GCC 13.4, Release
-O3/-DNDEBUG, static NEON float FFTW 3.3.10, freeze and every profile OFF for
ordinary timing. Four-second warmup/reset/four-second measurement, ABBA with
forward/forward/reverse/reverse workload order. Same ELFs and 57-plan seed wisdom
in both rounds. Raw control rows and outliers remain; no normalized CPU or
significance claim. Over-period timings are not live xruns or chain admission.

## Files

- Root `profile.csv/log.gz`: baseline diagnostic run, six ordinary-shaped CSV
  rows plus stage/event/record logs. `profile-*.csv` and `profile-check.txt` audit
  all callbacks, 74566 events, 5247 frames, metadata/timestamps and nested stages.
  Diagnostic counters/timers affect costs and are excluded from CPU retention.
- `candidate-run/`: numerical checks, **first ordinary ABBA round**, candidate
  diagnostic run, CSV summaries, receipts, wisdom and service metadata.
- `candidate-run/confirmation/`: **second ordinary ABBA round** using unchanged
  ELFs; no numerical rerun. Some numerical receipts copied from the still-existing
  directory are duplicates of the first run. Its runner receipt lists only four
  timing probes. `combined-*.csv` in the parent combine these two rounds only.
- `frame-trace.cpp`: separately linkable original/current frame-field oracle,
  explicit serialization without padding. Exact hashes/byte counts and logs are
  retained for 1088 histories and 79066 regions. Default bank/processor automation
  has 1966080 floats; core has 384000; opt-in host automation has 1966080.
- `check_work_counts.py`: immutable original/candidate frame records and exact
  removed scan/snapshot-read counts. `summarize_phases.py` accepts original
  18-field and new 20-field record formats. `snapshots`/`snapshot_reads` are zero
  for the original; candidate reads count both 256-track snapshot passes. Used
  checks during snapshot consumption are additional bounded work.
- `*.cpp.gz`, `callback_profile.h.gz`, and candidate-prefixed counterparts:
  exact generated copies used for measured diagnostics. Source SHA files and
  `generated-source-check.txt` prove both measured variants regenerate exactly.
  `baseline-interpretation-profile.py` is the original measured 18-field driver;
  `interpretation_profile_sources.py` is the final dual-shape 20-field driver.
  The final driver is also checked against original production source.
- `build-*.sh`, host runners, `host-numerical.sh`, `host-tests.sh`, validation and
  warning scripts: exact local build/probe commands with environment-specific
  cache/SDK paths. `host-tests.sh` runs OFF/ON suites and reconfigures OFF;
  `final-default-build.log` records the subsequent ordinary OFF rebuild.
- `source.txt`, source/header/build/ELF hashes and patches: original/candidate
  identity. `final-target-binary-sha256.txt` independently verifies all nine target
  probe ELFs unchanged through both rounds. No executable blobs or compared float
  blobs are committed; the hashes, lengths, commands and source remain.
- `cleanup-readback.txt`, container cleanup and independent service readbacks:
  restored existing service, removed only two session-owned remote directories
  and the session-owned container. Local comparison blobs are removed; immutable
  baseline libraries/source and build caches remain for the next experiment.

## Reproduction

Use the recorded paths/toolchain/flags or equivalent relocated paths. Baseline
compilation must use original headers, not candidate headers: this workspace
changes inline bank-control offsets. Link `frame-trace.cpp` separately to each
ordinary library; compare byte-for-byte per architecture. Do the same for the
saved automation helper from `pog3-pi4-render-schedule-2026-10-07` and `--freeze-trace`.

For baseline diagnostic generation, run the archived baseline driver with the
immutable baseline root and an output directory; candidate generation uses the
final driver and current root. Both import the archived callback generator.
Overlay the standalone diagnostic CMake/generator harness onto a baseline source
checkout if using the provided cross-build commands; the new standalone helper
and unit source are also needed for CMake's target declarations. This does not
add a birth workspace to the baseline `PitchFrame`. The compressed generated
files are the authoritative measured diagnostic inputs.

Run numerical and pitch checks before ordinary timing. Use the existing CPU
wrapper/restore runner only on the authorized device with unique `/tmp` probe
paths. Capture logs/ELF hashes/wisdom/receipts and independently read back the
restored service before cleanup. All production/profile comparison results here
were collected before remote cleanup.

Regenerate summaries from repository root:

```sh
r=benchmark-results/pog3-pi4-interpretation-2026-10-08
python3 "$r/summarize_phases.py" "$r"
python3 "$r/summarize_phases.py" "$r/candidate-run" profile-candidate
python3 "$r/check_work_counts.py" "$r"
python3 "$r/summarize.py" "$r/candidate-run"
python3 "$r/summarize.py" "$r/candidate-run/confirmation"
python3 "$r/summarize_confirmation.py" "$r/candidate-run"
python3 "$r/check_wisdom.py" "$r" 1
python3 "$r/check_wisdom.py" "$r/candidate-run" 5
python3 "$r/check_wisdom.py" "$r/candidate-run/confirmation" 4
(cd "$r" && sha256sum -c artifact-sha256.txt)
```

The recursive manifest excludes itself. Diagnostic BSS and FFTW internal storage
are excluded from C++ requested preparation bytes. Intended-chain memory/CPU,
paced deadline/endurance and listening admission remain open.
