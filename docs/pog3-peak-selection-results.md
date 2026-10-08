# POG3 exact top-peak selection — 2026-10-08

**Decision: retain the top-set partition.** Two ordinary Pi ABBA rounds show
**1.976%/0.967% combined mean CPU reductions at 64/128 frames**, with unchanged
1625912-byte C++ preparation, exact audio/frame histories and all 10/11
default/opt-in DSP checks passing. Pooled 64-frame over-period counts fall
**37 → 22 / 12000 (40.541%)**. Both baseline/candidate have zero 128-frame
period overruns in these runs. Recorded maxima are slightly higher in the
candidate, so this is not an across-the-board tail improvement or live admission.

Baseline is `4e262aed0109fa5cfe31483216248fc290677671`. Frame/Attack birth
snapshots, refined render scheduling and four-source rendering remain. Freeze
is still opt-in, default OFF. No public effect integration or installed
application deployment is added.

## Change and ordering review

The original overflowing-frame path sorts every candidate by descending
magnitude / ascending bin, keeps 256, then sorts the kept peaks by bin. Only
that final top set is consumed. Replace the first full sort with
`std::nth_element(begin, begin + 256, end, originalComparator)` when the count
exceeds 256; keep the original capacity event and final bin sort.

Every candidate has a finite magnitude, and each bin appears at most once.
The original comparator therefore gives a unique cutoff choice, including
magnitude ties. Partitioning selects the same first-256 set; the subsequent
bin sort gives the same publication order. `begin + 256` is strictly inside
the range because this branch runs only for more than 256 peaks. At 256 or
fewer, the original path remains.

All peak predicates, DC/Nyquist handling and phase/logarithmic frequency
calculations still run before selection for every candidate, including peaks
later dropped at capacity. Original tracks, predictions, generations, missed
ages/velocities, capacity counts, region partitions and Cartesian history remain.
The candidate vector's unconsumed suffix can differ, but the next update writes
all newly consumed candidates before reading them. No history reads that suffix.
No class layout, prepared vector size, FFT/backend, workspace or control changes.

Partitioning is in place. The host C/C++ callback allocation checks and target
C++ counter confirm no processing allocation. Its average complexity does not
promise a particular worst runtime; prepared bin counts bound the input, and
actual target tails remain part of the admission gate. No approximation or
reduced peak/frequency work is introduced. This ordering argument is supported
by finite exact traces, not a universal numerical proof.

## Validation

- `peakSelection()` in the existing pitch-quality suite compares actual published
  bins and bit-exact magnitudes against an independent **original full-sort**
  oracle. **810 published frames** cover 0/1/255/256/257/258 peaks, larger
  overflows up to 8193, equal/monotone/random and cutoff-tied magnitudes, full
  and 400 Hz ceilings, repeated capacities, and N=1024/2048/32768. The fixture
  also passes separately linked to the immutable baseline library. Available
  alone as `pedal-pog3-pitch-quality --peak-selection`; the normal suite runs it.
- Expanded separately linked original/current frame traces serialize each public
  field without padding. **1538 frames / 172698 regions** match byte for byte
  separately on host and Pi, **4183202 bytes per trace**. These include 450
  explicit cutoff/tie histories and broad dense/sparse/silent/migrating histories,
  sizes 64 through 32768, both ceilings, phase history, reset/reprepare, wrong-size
  and nonfinite frames, boundaries/subnormals and repeated saturation.
- Default bank/processor automation matches **1966080 floats** on host and Pi;
  the full processor matches **384000 floats**. Host opt-in automation matches
  all 1966080 floats. Original headers/libraries are saved and used for baseline
  probes. No tolerance or alternate output is accepted.
- **10 default / 11 opt-in DSP suites pass**, including Attack, expression,
  complete freeze/gliss and required-preload C callback allocation/free checks.
  Ordinary host targets are rebuilt freeze OFF afterward.
- Automation, expanded frame traces and cutoff checks pass ASan/UBSan/leak
  checking. Strict host/AArch64 builds pass. Target full pitch/Focus/Warp/overload
  checks, including the cutoff oracle, run before any timing.
- All **48 ordinary timing rows** have zero C++ callback allocations, unchanged
  1625912-byte core preparation and 9/16 core FFT maxima at 64/128 frames. Both
  controls retain their memory/FFT bounds. All eight wisdom exports preserve the
  same header and 57-record multiset. The same eight hashed probe ELFs remain
  unchanged through both rounds.

The preceding interpretation profile, with long candidate sorting about 47–49
instrumented µs, chose this target. No new instrumented timing is used here;
only ordinary full-processor measurements justify retention. Sorting and all
other substages have not been retimed separately after this change.

## Ordinary hardware results

Same Pi 4B 1.5, AArch64 Linux 6.18.37-v8, 48 kHz, CPU 2, SCHED_OTHER unpaced
standalone probes, existing performance governor at 1.5 GHz. Buildroot GCC
13.4, Release -O3/-DNDEBUG, static NEON single-precision FFTW 3.3.10, freeze
and every profiling option OFF. The existing live service is stopped/restored
for each run and independently read back; no application, firmware, buffer,
governor or NAM-quality changes are made.

Both rounds use unchanged baseline/candidate ELFs and the same 57-plan seed
wisdom. Four seconds warmup, reset, four seconds measured per workload. ABBA
order: baseline-forward, candidate-forward, candidate-reverse, baseline-reverse.
The core exercises all stereo voices, processed dry, Focus, Attack, Warp,
filter/doubling/Spread and Off expression. Granular and spectral controls remain.

| Mean reduction | 64 frames | 128 frames |
| --- | ---: | ---: |
| First round | 1.506% | 0.986% |
| Confirmation | 2.439% | 0.948% |
| Both rounds | **1.976%** | **0.967%** |

| Four runs per build | Frames | Mean period demand | Highest p99 µs | Worst µs | Over-period counts per run |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original full sort | 64 | 80.340–82.254% | 1309.538 | 1417.000 | 7–12 / 3000 |
| Top-set partition | 64 | 79.633–80.363% | 1294.685 | 1429.000 | 4–7 / 3000 |
| Original full sort | 128 | 80.397–81.587% | 2477.519 | 2601.685 | 0 / 1500 |
| Top-set partition | 128 | 79.728–80.693% | 2452.555 | 2610.037 | 0 / 1500 |

Combined means are **1087.093 → 1065.613 µs** at 64 and
**2156.908 → 2136.050 µs** at 128. Reductions are relative mean work, not
percentage points of a callback period. Pooled 64 over-period counts are
37 → 22/12000; 128 is 0 → 0/6000. Both maximum values worsen slightly:
64 by 12 µs (0.847%), 128 by 8.352 µs (0.321%). These observed tails remain
in the decision; no claim that every callback improves is made.

The first round's spectral control also improves, notably at 128; confirmation
reverses that control result while core improvement repeats. Combined control
mean reductions are granular +0.072%/+0.073%, spectral +0.230%/−0.025% at
64/128. Full distributions and outliers are retained. No corrected CPU result,
formal significance or direct comparison to earlier experiments' baselines is
claimed. Do not add this percentage to previous birth/schedule percentages.

The 64-frame candidate still exceeds its 1333.333 µs period. Zero observed 128
period overruns in these finite offline runs does not establish paced FIFO,
ALSA or intended-chain/thermal admission. CPU and memory headroom, listening
and public integration remain open.

## Reviewed Luna next steps: sparse previous-phase reuse

1. **Save this exact baseline before editing.** Retain top-set partition,
   frame/Attack slot snapshots, refined due ages, four-source rendering and
   default-disabled freeze. Keep immutable source, original headers/libraries,
   ordinary ELFs and seed wisdom. Public integration stays blocked on feasibility.
2. **Use already-required phase work only.** The earlier exact work records show
   about 63% same-bin previous-phase reuse in long/short frames. Cache the exact
   float `std::arg(spectrum[k])` only where the existing phase-estimation branch
   already computes it, including candidates later dropped by the top-256 cap.
   Do not compute phase for all bins or change peak predicates. Keep Cartesian
   previous spectrum and its original magnitude gate as the fallback.
3. **Define validity by the previous accepted finite frame.** For each visited
   bin, read prior validity, then clear it before any peak early-continue; mark
   it only after computing the current phase in the existing phase branch.
   Initial/logarithmic/DC/Nyquist branches need no new phase work. Wrong-size
   updates preserve the original history; nonfinite frames set `previous_`
   false as before and must prevent stale reuse. Reset/prepare invalidate all
   entries. Every subsequent accepted finite frame clears absent/nonphase bins,
   so a previously present peak cannot reuse an older frame's phase on return.
4. **Preserve exact arithmetic and ordering.** Store float phase values. On a
   cache miss evaluate original `std::arg(previousSpectrum[k])`. Subtract the
   current/previous **float** operands first, then the original double bin/step
   expression and `principal` remainder. Preserve evaluation precision, clamping,
   timestamps, previous magnitude, peak ordering and history-copy position. Add
   no approximate atan, phase wrapping, sine or relaxed numerical tolerance.
5. **Account for storage and traffic before choosing a representation.** Prepare
   bounded float/valid arrays off-thread (or an explicitly reviewed compact
   validity representation). Include low-ceiling sizes and alignment in measured
   preparation growth. Compare clear/read/write costs against saved atan calls;
   count actual hits/misses with standalone counters, not per-bin timers.
6. **Expand the independent frame oracle.** Explicitly exercise same-bin hits,
   moving-bin misses, absent/returning peaks, accepted silent frames, initial/log
   paths, magnitude threshold crossings, wrong-size/nonfinite interruption,
   reset/reprepare, dropped-overflow peaks and both frequency ceilings. Compare
   every published float/bin/track/generation/capacity/timestamp to immutable
   original output. Retain actual bank/processor default and opt-in automation.
7. **Require useful ordinary benefit.** Run required DSP/allocation/sanitizer and
   strict-warning checks, then profile-OFF Pi ABBA plus confirmation with the same
   wisdom/fixtures/controls. Retain raw mean/p99/max/over-period results and actual
   memory. Reject noise-sized or inconsistent gains; if phase caching lacks useful
   benefit, inspect harmonic-support scoring or avoid the redundant bin sort in
   already-ascending nonoverflow frames as separate candidates. Keep live/chain
   admission distinct from offline improvement.

Artifacts: [complete experiment](../benchmark-results/pog3-pi4-peak-selection-2026-10-08/README.md).
