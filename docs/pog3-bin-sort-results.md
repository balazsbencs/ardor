# POG3 nonoverflow bin-sort experiment — 2026-10-08

**Decision: retain the skipped nonoverflow sort.** Two ordinary Pi ABBA rounds
repeat lower mean demand: **0.912%/0.562% combined reductions at 64/128**.
Exact audio/history and all 10/11 DSP suites pass with unchanged storage.
The 64-frame maximum and pooled period overruns worsen; this is a small mean
improvement with mixed tails, not live/chain admission.

Baseline is `816d1b8603b07ae7b58c2930bae124fcb185c8bd`, whose production DSP
matches retained HEAD35b after rejection of both sparse phase caches. Exact
bounded top-peak selection, both birth snapshots, refined due ages and four-source
rendering remain. Freeze/gliss is opt-in, default OFF.

## Ordering and storage review

`PitchFrame::update` scans bins from zero through the frequency ceiling in
ascending order, appending at most one candidate for each accepted bin. For
**256 or fewer candidates**, no partition has changed their order: the final
ascending-bin sort is redundant. Move that sort into the **overflow branch**,
after the original `nth_element` selection and truncation. More than 256 peaks
still select the original descending-magnitude/ascending-bin top set and then
sort those bins before association.

Zero/one peaks, exact 256 boundary, tied magnitudes and cutoff ties preserve
the original sequence. Every next frame overwrites the consumed prefix before
reading it, so transitions between overflow and nonoverflow cannot consume stale
partitioned candidates. The unused suffix is never read as track history. Plans cap N at 32768,
so stored 16-bit bin numbers preserve the scanned ordering without wrapping.
All phase/log frequency calculations remain before selection, even for dropped
peaks. Track aging, prediction keys and tie handling, ascending-bin association,
generations, births, capacity counts, region partitioning and Cartesian history
copy remain unchanged. No class layout, frequency, timestamp or FFT changes.

Prepared core C++ storage is unchanged at **1625912 bytes** (excluding FFTW
planning/storage). Processing performs no new allocation. This exact ordering
argument is supported by finite traces, not a proof of every DSP property.

## Validation

- Existing independent original-full-sort oracle checks **810 published frames**:
  zero/one/255/256/257 candidates, larger overflows up to 8193, equal, cutoff-tied,
  monotone and random magnitudes, repeated history and both ceilings. Existing
  independent **464-frequency original Cartesian history** and frame/Attack
  birth-selector oracles remain.
- Separately linked immutable original/candidate **2498-frame / 173482-region /
  4226018-byte** traces match exactly on host and Pi. They include explicit cutoff
  histories, broad overflow/nonoverflow transitions, dense/sparse/migrating/silent
  peaks, reset/reprepare, invalid frames, phase histories and capacity events.
  Each public field is serialized without struct padding.
- Default host/Pi automation matches all **1966080 floats**, plus **384000 full-core
  floats**. Host opt-in automation also matches all 1966080 floats. Original
  probes compile against their own immutable matching headers/library.
- **10 default / 11 opt-in DSP suites pass**, including Attack/expression,
  freeze/gliss and required-preload C callback allocation/free checks. Affected
  automation/frame/phase-history ASan/UBSan/leak checks and strict host/AArch64
  warning builds pass. Full target pitch/Focus/Warp/overload checks precede timing.
- Ordinary target rows have zero measured C++ callback allocations, unchanged
  preparation and stable 9/16 core maximum FFTs per 64/128 callback. All wisdom
  exports preserve the same header and 57 planning records. Final ordinary host
  targets are rebuilt with freeze OFF. The final retained source is exactly the archived candidate patch.

## Ordinary Pi timing

Release -O3/-DNDEBUG, Buildroot GCC13.4, static NEON float FFTW3.3.10; Pi4B1.5,
48k, CPU2, SCHED_OTHER unpaced, existing performance governor at 1.5 GHz. Freeze
and every diagnostic profile are OFF. Two separate ABBA rounds use unchanged
hashed original/candidate ELFs and the same imported FFTW wisdom. Each row has a
four-second warmup/reset and four-second measured fixture, 3000/1500 callbacks
at 64/128. Order is original forward / candidate forward / candidate reverse /
original reverse. Matched fixtures exercise all voices, processed dry, Focus,
Attack, Warp, filter/doubling/Spread and Off expression; granular/spectral controls
are retained.

| Ordinary round | Mean reduction at 64 | Mean reduction at 128 |
| --- | ---: | ---: |
| First | 0.764% | 0.614% |
| Confirmation | 1.060% | 0.510% |
| Combined | 0.912% | 0.562% |

| Four runs per build | Frames | Mean period demand | Highest p99 µs | Worst µs | Pooled over-period callbacks |
| --- | ---: | ---: | ---: | ---: | ---: |
| baseline | 128 | 79.510–80.111% | 2443.611 | 2549.278 | 0/6000 |
| baseline | 64 | 79.712–80.320% | 1296.519 | 1378.482 | 16/12000 |
| candidate | 128 | 79.153–79.508% | 2426.463 | 2517.092 | 0/6000 |
| candidate | 64 | 78.883–79.460% | 1287.648 | 1457.204 | 25/12000 |

Combined original/candidate means are **1066.1025 → 1056.376 µs** at 64 and
**2126.66125 → 2114.714 µs** at 128. Highest p99 falls at both sizes, but worst
64 time rises **1378.482 → 1457.204 µs**. Pooled 64 period overruns rise
**16 → 25/12000**, with **0 → 0/6000** at 128. All four individual forward/reverse
pairs have lower core mean at both sizes, but no across-the-board tail benefit.

Granular control combined changes are **−0.104%/+0.060%** reductions at 64/128.
Spectral control reductions are **+2.699%/+1.091%**. That workload executes
transform-only renderers, excluding `PitchFrame`; its improvement is unrelated
to the removed sort and occurs in both rounds. The experiment therefore supports
an observed ordinary core mean reduction, not an isolated attribution of its
full size to sorting. Code layout and runtime variability are not separately
attributed. No control-adjusted benefit is calculated.

Keep all raw distributions and outliers; no adjusted timing or statistical
significance claim. Changes are relative to this experiment's baseline and are
not added to earlier improvements. Finite unpaced comparisons do not establish
paced ALSA/FIFO, intended-chain, thermal/xrun or listening admission. Public
integration and freeze/gliss promotion remain deferred while CPU feasibility
is open. The initial 25% planning goal is not a hard rejection threshold.

## Reviewed Luna next CPU step

Keep this skipped sort and the preceding exact bounded selection, both birth
snapshots, due ages and four-source rendering as the measured production baseline.
The deferred gain split and both sparse phase caches remain rejected.

Attack's support scorer is the next separate memory-layout candidate. In
`PolyphonicAttack::group`, each accepted fundamental scans the retained
observations in their existing magnitude order. It reads only `frequency` and
`magnitude` from observation records that also hold stereo track/generation keys.

1. Keep original observation construction, stereo pairing, magnitude/frequency
   sort and top-256 truncation. Compact keys must be filled **after** that sort
   and truncation, only from its consumed prefix, when grouping actually runs.
   Do not sort keys by frequency or change support accumulation order.
2. Try a bounded frequency/magnitude workspace, with exact float copies and a
   measured preparation delta. A single reusable workspace is enough because
   resolution updates are sequential; its lifetime must cover the whole group
   call. Prefer a compact fixed representation over callback allocation. Measure
   key-gather traffic against saved strided reads; do not assume a cache win.
3. Keep candidate filters/duplicate checks, observation index provenance,
   fundamental order, harmonic division and strict threshold, float multiply /
   division / accumulation order, score/frequency sort ties, family IDs and onset /
   release dates. Avoid reciprocal substitutions, relaxed math or reordered sums.
   Do not change the owner predicate or family/partial lifetime in the same patch.
4. Use an independent original scorer comparison for exact candidate scores,
   sort order and family/partial outcomes. Cover 40/2000 Hz candidate boundaries,
   tiny magnitudes, harmonic tolerance/half ties, duplicates, dense/empty input,
   both stereo pairing cases, capacity and reset; retain full Attack/expression
   quality and exact default/opt-in processor automation. Compare actual scoring
   behavior, not just a new helper against itself.
5. Compare ordinary target ABBA and confirmation with identical fixture/wisdom,
   controls, raw tails and measured memory/allocation. Keep the representation
   only for useful repeatable CPU benefit. If traffic-only changes fail, separately
   examine conservative support-frequency pruning, with exact division/threshold
   boundary analysis and the original final harmonic predicate; do not merge it
   with the layout experiment.
6. Continue CPU feasibility first. Preserve retained rendering/scheduling and
   defer public integration and freeze restoration until paced device/chain
   measurements warrant them.

[Complete evidence and reproduction](../benchmark-results/pog3-pi4-bin-sort-2026-10-08/README.md).
