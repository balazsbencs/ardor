# POG3 Attack CPU experiment — 2026-10-07

**Decision: retain the compact birth-slot workspace.** Two matched Pi ABBA
rounds repeat lower ordinary mean demand at both callback sizes. Combined mean
reductions are **2.080% at 64 frames and 2.364% at 128**. At 64, pooled
over-period callbacks fall **862 → 277 / 12,000**, a **67.865% reduction**.
The current core still exceeds callback periods and has no intended-chain or
paced live admission. Four-source rendering, the refined due-age schedule and
default-disabled freeze/gliss remain in place.

## Attribution before implementation

Baseline is `34bf267c533570aad65ab2106ef8d23a5e1b469d`. The new standalone
`ARDOR_POG3_ATTACK_PROFILE` generator instruments copies of the current DSP;
production builds contain no timers or diagnostic records. It adds six child
stages to the existing callback profile and records observations, candidate and
family counts, birth attempts, input frame stamps and actual Attack control bits.
Fixed event/record storage is touched outside processing. The optional
`ARDOR_POG3_ATTACK_PROFILE_DETAILS` adds nested per-partial timers. All three
profiling modes are mutually exclusive; details require Attack profiling.

The coarse Pi primary-window profile measures these exclusive means per update:

| Attack child stage | µs |
| --- | ---: |
| Stereo observations/sort | 77.019 |
| Group candidate scoring | 93.500 |
| Group family assignment | 11.094 |
| Binding reservation | 17.017 |
| Partial binding/birth/ownership/envelope/canonical matching | 190.846 |
| Canonical index | 25.436 |

The inclusive primary Attack mean is 418.016 µs. Short Attack is 216.467 µs and
low Attack 64.350 µs. These are **instrumented attribution**, not admission or
ordinary baseline CPU costs. The nested detail run separates primary per-update
owner/envelope/canonical costs (68.035/40.390/18.804 µs) and the binding/birth
remainder (181.525 µs). Its partial parent event grows to 326.166 µs versus
190.846 in the coarse run: frequent clocks and stores materially perturb it.
Do not subtract that overhead to invent a corrected cost or use these numbers
as measured optimization gains.

| Resolution | Updates per callback partition | Mean observations | Mean birth attempts | Maximum birth attempts |
| --- | ---: | ---: | ---: | ---: |
| Primary | 749 | 255.222 | 69.248 | 190 |
| Short | 1499 | 144.692 | 8.265 | 34 |
| Low | 374 | 12.543 | 0.952 | 6 |

Birth counts include attempts that exhaust available slots. The detailed owner
and envelope call counts therefore can be smaller than observation counts.
The support-classification estimate is observations × accepted candidates;
it is a source-derived work count, not a counter inserted into each harmonic
test. Primary scoring averages 23.124 accepted candidates and 5915.260 such
classifications per update.

Both diagnostic levels record 58,816 events and 2622 Attack updates per measured
core partition. Parsers verify ordered 64/128 event/count identity, control and
input timestamps, six-child exclusive accounting, nested micro accounting,
render deadlines, callback totals and count bounds. Diagnostic-active full-core
traces equal the ordinary baseline on host and Pi (384,000 floats). All six
generated files reproduce from the base revision and retained generator
snapshots. Host/AArch64 strict warnings pass; both Pi profile wisdom exports
retain the same 57-plan multiset. Diagnostic BSS is excluded from the processor
preparation counter and none of these timed runs enters the CPU comparison.

## Change and reviewed ordering

Previously, every birth attempt scanned up to 256 comparatively large partial
histories to select a slot. `AttackBirthSlots<256>` instead snapshots compact
`seen`/slot keys once, lazily at the first birth of an update. Empty slots retain
ascending slot order. If they are exhausted, generated entries sort once by
`seen`, then by slot. Taking a slot skips entries used in the meantime.
The member workspace resets counters rather than clearing its entries and
adds **4136 bytes** to one bank. No heap storage is introduced during processing.

The equivalence argument is specific to this loop:

1. Surviving bindings are reserved before any birth. Snapshot only unreserved,
   unused slots, visiting them in ascending index order.
2. The original selector always chooses the first empty slot, even if generated
   entries have older timestamps. Empty entries therefore have unconditional
   priority; an empty slot with `seen == INT64_MAX` remains eligible.
3. Otherwise it minimizes `seen`, breaking ties by ascending slot index.
   Generated entries at `INT64_MAX` are excluded because the original comparison
   was strictly `seen < oldest`, initialized to `INT64_MAX`.
4. Unselected, unreserved partial histories do not change during the loop.
   Every selected or independently bound history is marked used before the next
   selection. Used flags only advance to true within this update. Skipping used
   entries thus preserves the snapshot's validity without rescanning histories.
5. Exhaustion returns the original sentinel and follows the original capacity
   event/continue path. Reset and each subsequent update discard the old keys.

Reservation/binding rules, family ownership and updates, generation counters,
excitation history, envelope arithmetic, canonical matching, ties, input/control
timestamps and publication order remain unchanged. A birth-free update builds
no list and performs no sort. Snapshotting and sorting are not assumed to win
for every sparse workload; the measured claim applies to the retained fixture.

## Output and DSP checks

The actual helper is compared with an independent copy of the original scan for
**528,384 selections** across 2048 scenarios. Cases cover empty/generated slots,
equal and signed/extreme timestamps, reservations, already used slots,
intervening binding uses, exhaustion and workspace reuse. Mutations after the
snapshot affect only used histories, matching the production invariant. The
test passes on host, Pi and ASan/UBSan and is registered in both build systems.

Default baseline/candidate traces match byte for byte on host and Pi across
**1,966,080 automation floats plus 384,000 moving full-core floats**. Automation
includes two reset/startup/drain passes, 14,160 control events, Attack off/on,
Focus/Warp and available expression modes around control/frame boundaries.
Host opt-in freeze automation also matches all 1,966,080 floats. Cross-architecture
hashes are not compared. Finite traces support, but do not prove universal,
equivalence.

All **nine default suites** pass (44.83 s), as do **ten opt-in suites** (73.15 s),
including Attack independence, expression and full freeze/gliss tests. The host
allocation suite requires the C malloc/free probe. The automation trace passes
ASan/UBSan with leak checking. Changed Attack units pass strict host/AArch64
warnings with both freeze definitions, and the differential test passes strict
warnings on both architectures. The final ordinary library/test targets are
rebuilt with freeze OFF; nine suites are registered. Pi pitch/Focus/Warp/overload
checks precede timing.

The first automation comparison was invalid: its immutable baseline archive was
linked to a probe compiled with candidate headers. The new member shifts later
`PolyphonicPitchBank` fields accessed by inline controls. Both baseline probes
were rebuilt with original headers archived at `34bf267c` before any CPU timing.
The existing baseline full-benchmark ELF already had matching original headers.
The correction required no DSP change. Reproduction must use each build's own
headers. Detailed-profile initial builds also hit host tmpfs exhaustion; the
session's own build cache moved to disk before successful probes. These failed
build/comparison attempts are not timing samples.

## Ordinary matched hardware results

Both rounds use the same hashed ELFs, fixtures and 57-plan shared wisdom.
Baseline is the previous retained schedule's ordinary candidate ELF. Buildroot
GCC 13.4.0 uses -O3/-DNDEBUG and static single-precision NEON FFTW 3.3.10.
Freeze and all profiling options are OFF. Every round runs baseline-forward,
candidate-forward, candidate-reverse, baseline-reverse; reverse swaps callback
size order. Each fixture warms four seconds, resets, then measures four seconds.
The stereo full core exercises all voices, processed dry, Focus, Attack, filter,
detune and Spread in Off expression mode. Granular and spectral controls remain.

The Pi 4 uses CPU 2, unpaced SCHED_OTHER, 48 kHz and 64/128 frames. Recorded
governor/frequency snapshots remain performance/1.5 GHz. First-round initial
temperature is 54.53°C; final confirmation is 57.45°C. These offline loops are
not the live ALSA/FIFO chain and their over-period counts are not measured xruns.

| Core mean reduction | 64 frames | 128 frames |
| --- | ---: | ---: |
| First ABBA | 2.400% | 2.731% |
| Confirmation ABBA | 1.760% | 1.995% |
| Combined, four runs per build | **2.080%** | **2.364%** |

| Build | Frames | Mean period demand range | Highest p99 µs | Worst µs | Over-period callbacks per run |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original Attack | 64 | 86.24–86.87% | 1478.537 | 1620.315 | 194–232 / 3000 |
| Compact birth slots | 64 | 84.13–85.24% | 1379.352 | 1529.314 | 61–87 / 3000 |
| Original Attack | 128 | 86.24–87.45% | 2647.740 | 2817.982 | 2–9 / 1500 |
| Compact birth slots | 128 | 84.11–85.40% | 2627.740 | 2785.760 | 2–8 / 1500 |

Combined mean callback times are 1153.752 → 1129.755 µs at 64 and
2316.608 → 2261.840 µs at 128. The 64-frame period is 1333.333 µs and the
128-frame period 2666.667 µs. Pooled 128-frame over-period counts are 26 → 21
per 6000; such small tail counts are mixed per pair and do not establish robust
128-frame admission. There is no formal significance claim from four repeats.

Combined granular mean reductions are +0.038%/+0.060% at 64/128, and spectral
control reductions −0.023%/−0.504%. Their full distributions and outliers are
retained; no corrected CPU result is computed by subtracting controls.
The core gain repeats in both rounds and is materially larger than these mean
control changes, supporting retention of this small bounded optimization.

All 48 timing rows show zero callback C++ allocations. FFT maxima remain
9/16 at 64/128 for both core builds. Requested C++ preparation bytes increase
1618560 → **1622696**, exactly the workspace size; control preparation is
unchanged. This counter excludes FFTW internals and is not complete memory
admission. No executable, configuration, governor, buffer or NAM quality is
deployed or changed. Each runner restores the existing service; independent PID
readbacks pass. Probe files/float blobs are removed after retrieval.

## Next Attack work for Luna

The deferred-gain step below was subsequently implemented and measured, then
rejected for insufficient ordinary CPU benefit. Production birth-slot behavior
remains as documented here. See
[the gain experiment and current primary-interpretation next steps](pog3-attack-gain-results.md).

Keep this birth-slot change and the current scheduling/batching as the baseline.
The next small candidate is **avoid evaluating a partial gain that canonical
matching immediately replaces**, before revisiting harmonic scoring.

1. Inspect `envelope`: its state mutation completes before its final `gainAt`.
   Split state update from gain evaluation without changing any update arithmetic
   or order. Preserve the exact Attack-zero state update and unity result.
2. Run ownership and envelope-state updates exactly where they run now. Keep the
   canonical lookup in its original position after those updates. Select either
   the current partial or the original canonical match, then evaluate `gainAt`
   only for that selected history. Do not skip envelope state updates when using
   another resolution's gain: later frames and Focus changes need those histories.
3. Preserve the strict distance predicate, lowest-original-slot tie, timestamp
   release checks and short/low/primary order. Avoid approximate sine, harmonic
   pruning, reduced partial limits or gain caches in this first candidate.
4. Count actual canonical replacements with fixed standalone diagnostics before
   claiming avoided gain work. Update generator anchors when changing the
   envelope signature; retain the existing baseline diagnostic snapshots.
5. Compare actual bank/processor automation against this retained baseline with
   matching headers, separately for host/Pi default and host opt-in. Run the
   nine/ten DSP suites, meaningful state/reset/partition checks, allocation checks
   and affected sanitizer checks. Retain a candidate only after ordinary matched
   ABBA plus confirmation improves repeatable mean/tails at an acceptable memory
   cost. Archive rejected patches without leaving unused production paths.

If that gain work is small, profile the candidate × observation harmonic scorer
and primary interpretation next. Preserve accumulation order and all strict
threshold/tie behavior; compact cached magnitudes/keys may help, but per-pair
diagnostic timers and speculative numerical shortcuts can mislead. Broad dense,
sparse, Attack-zero/long, expression, callback-offset and thermal workloads are
still required before paced intended-chain admission and public integration.

## Reproduction and evidence

[All retained artifacts](../benchmark-results/pog3-pi4-attack-2026-10-07/) include
the production patch against the immutable base, original-header/source hashes,
build context/scripts, staged/target binary hashes, exact trace receipts, strict
warnings and DSP/sanitizer logs. `profile/` and `detail/` contain the generators,
compressed raw logs, accounting parsers, reproduction and wisdom receipts.
`numerical-check/` records the successful pre-timing gate. Root and `confirmation/`
hold the independent ordinary ABBA runs, controls, all percentiles/maxima,
device/service/cleanup receipts and wisdom exports. `summarize.py`,
`summarize_confirmation.py` and `check_wisdom.py` regenerate the result tables.
The recursive SHA256 manifest excludes itself.
