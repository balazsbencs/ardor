# POG3 four-source plus pair leftovers experiment — 2026-10-07

**Decision: retain the existing four-source implementation.** The hybrid
prototype passes exact audio and DSP checks, and models fewer destination
accesses. Its small initial CPU gain weakens in a second matched comparison.
The extra path is not retained in production; its patch and full evidence remain
reproducible. The next priority is identifying and reducing callback peaks.

## Bounded memory change

Baseline source is `01de14a2f5dcd0c1bec6603cf665c82e542f09b9`, whose renderer is
the retained four-source implementation. The prototype leaves that branch first
and adds a pair branch only when four-source eligibility fails but two adjacent
sources in the same region have completely interior support. Each pair retains
all 48 contributions and their per-destination addition order, but shares 25
destination read/write pairs instead of 48. Integer shifts, reflected/endpoints
and single leftovers retain their original paths.

The second source uses the already tested AArch64 complex-multiply contraction
and NaN library fallback. FFTs, histories, gains, Attack, Warp/Focus, latency,
control samples and job scheduling are unchanged. GCC vectorizes both batch
interiors with 16-byte operations. The compiled ARM renderer stack stays
**528 bytes**, and the prototype adds no prepared heap storage.

## Geometry improves; access counts are not CPU time

A separate diagnostic copy models baseline and hybrid traversal before the live
source loop, after ordinary skips. It runs after the first round's uninstrumented
timing. Its CPU timings are excluded from every optimization summary. On the
Pi's changing stereo chord/noise core fixture, both callback sizes have:

| Traversal | Four-source batches | Pair batches | Single interior sources | Interior sources batched | Modeled interior destination read/write pairs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | 459,225 | 0 | 1,095,192 | 62.648% | 38,683,683 |
| Hybrid | 459,225 | 356,383 | 382,426 | 86.957% | 30,486,874 |

The same 897,496 visited regions classify the same 2,932,092 fractional interior,
81,872 edge and 637,725 integer sources. Every modeled four-source batch survives.
Each extra pair saves 23 destination access pairs, for **8,196,809 fewer accesses
(21.189% below baseline)**. The reproducible parser checks these identities,
source accounting and callback-size invariance. Identity controls have zero
shifted-render geometry. Host geometry is retained separately and is not pooled
with Pi counts; host/ARM interpretation can differ slightly.

This is an algorithmic access model, not a memory-bandwidth measurement. It
does not remove interpolation arithmetic, source rotations, frame interpretation,
Attack or FFT work. The measured CPU results below show why access reductions
alone are insufficient to select this optimization.

## Exact output and DSP gates

Baseline/candidate output matches byte for byte across **552,960 renderer samples
and 384,000 complete-processor float samples on both host and Pi**, per
architecture. The 192-case actual renderer fixture uses deterministic small FFT
plans and covers wide/short regions, DC/Nyquist support, optional low analysis,
eight pitch/Warp patterns, changing/zero gains and resets. Its geometry model
includes **264,054 potential pair batches** and retains four-source, single,
edge and integer cases. This is potential fixture coverage, not a production
batch counter. The complete trace uses production FFT sizes and shared wisdom.
These finite checks are compatibility evidence, not proof for every input or
compiler.

The prototype passes all **eight default DSP suites** (45.16 s) and all **nine
opt-in freeze suites** (74.06 s), including full freeze/gliss and required-preload
callback C allocation/free checks. Its renderer trace passes ASan/UBSan with
leak checking. Changed renderer/trace units pass strict host and AArch64 warnings
with both freeze option values. The complete Pi default pitch/Focus/Warp/
overload suite passes before timing. Source is restored exactly to baseline
after rejecting the experiment; the host is rebuilt with experimental freeze
OFF. Existing validation of that unchanged production implementation remains
applicable.

The container package install completed as a retry began, briefly overlapping
two builds. Both were stopped; a clean rebuild completed before any probes were
staged. All measured executables come from the clean build. No overlapping
target probe or build occurs during target measurement.

## Two matched hardware rounds

Both uninstrumented probes use Buildroot GCC 13.4.0, ordinary -O3/-DNDEBUG,
static float NEON FFTW 3.3.10, experimental freeze OFF and profiling OFF. The
immutable baseline ELF matches the preceding four-source receipt. Input and
settings remain unchanged: changing stereo chords/noise, all six voices, Focus,
Attack=.11, processed dry, filter/detune/Spread and Off expression mode. Every
fixture has a four-second warmup, reset and four-second measured timeline.

CPU 2 uses SCHED_OTHER and the existing performance governor at 1.5 GHz. Each
round runs baseline-forward, candidate-forward, candidate-reverse,
baseline-reverse; reverse also swaps callback-size order. The second round
reuses exactly the same executable and imported wisdom hashes, after independently
checking first-round output and service restoration. It repeats timing only.

| Mean reduction versus four-source baseline | 64 frames | 128 frames |
| --- | ---: | ---: |
| First ABBA round | +0.752% | +0.234% |
| Confirmation ABBA round | +0.130% | −0.063% |
| Both rounds, four runs per build | +0.440% | +0.085% |

Only the two identical-fixture rounds of this experiment are combined. Earlier
uniform-pair or unbatched experiments are excluded. Combined granular control
reductions are −0.032%/+0.077% at 64/128 frames; spectral reductions are
+0.049%/−0.343%. Their individual round variations are retained. The 128-frame
sign changes and overlap between individual means make a durable CPU gain
uncertain; four repeats per build do not establish statistical significance.

| Build, four runs each | Frames | Mean period demand range | Highest p99 (µs) | Worst (µs) | Over-period callbacks per run |
| --- | ---: | ---: | ---: | ---: | ---: |
| Four-source baseline | 64 | 86.29–87.02% | 1705.166 | 1868.037 | 1133–1168 / 3000 |
| Rejected hybrid | 64 | 85.58–86.72% | 1699.296 | 1903.555 | 1079–1158 / 3000 |
| Four-source baseline | 128 | 86.14–86.92% | 2625.629 | 2872.111 | 4–8 / 1500 |
| Rejected hybrid | 128 | 85.61–86.95% | 2616.778 | 2771.741 | 2–5 / 1500 |

Some aggregate tails improve, particularly at 128 frames, but the candidate's
worst 64-frame callback is higher. Its 64-frame over-period rate is still
35.97–38.60%, and both sizes retain maxima beyond their periods. The retained
baseline remains unsuitable for claiming reliable standalone callbacks. These
unpaced offline wall durations are **not ALSA xruns**, paced FIFO or intended-chain
endurance. The experiment does not establish that memory bandwidth is irrelevant;
it establishes that this traversal does not provide enough repeatable benefit
to justify adding another production branch now.

All **48 uninstrumented aggregate rows** report zero C++ callback allocations.
Core preparation stays **1,618,560 bytes**, with FFTW and immutable diagnostic
storage outside that counter as before. Shared wisdom bytes are identical across
rounds; all eight timing exports retain the header and 57 records without
additions/removals. This constrains planning records without proving instruction-level
plan identity or removing process/code-placement variation. All ten probes and
both runners return zero. Boundary clock/thermal receipts are retained; they
do not constitute continuous thermal or throttling measurements.

First and confirmation runners restore the service, independently checked as
PIDs **4941** and **5411** respectively. Both unique target directories and
temporary float blobs are removed after byte/hash checks. Only this experiment's
container is stopped/removed. No firmware, configuration, governor, live buffer
or unrelated DSP quality change is made.

## Next CPU step for Luna: explain the peaks before moving jobs

Keep the production four-source renderer. Uniform pairs and pair leftovers have
now been measured; further access-count tuning alone has diminishing measured
returns. Add a diagnostic copy of the **current reduced core** that records
cost/event distributions by 64-frame callback phase over the 512-sample low hop.
Do this before selecting a scheduling change. Existing transform counts and
the single worst callback do not identify every expensive job combination.

Read `PolyphonicPitchBank::process` and verify its actual input-sample stamps.
The current steady-state render due ages, relative to a long-frame event, are:

| Long-frame age interval | Long render jobs | Short render jobs |
| --- | --- | --- |
| 0–63 | 17, 38 | none |
| 64–127 | 81, 102 | 65, 81, 97, 113 |
| 128–191 | 129, 145, 161, 177 | none |
| 192–255 | 193, 209, 225, 241 | 193, 209, 225, 241 |

Short analysis repeats every 128 samples. Both analyses split left/right work
over adjacent samples. Long Attack completes at age ≥1; short Attack at age ≥8;
low Attack waits for its right frame and preserves low-before-primary ordering.
These relative ages are **not** a direct mapping to the benchmark's callback-end
modulo 512. Verify startup/alignment and actual entry samples before mapping the
table to callback buckets. The table counts renderer calls, not equal-cost jobs
or total transforms. It suggests collisions worth measuring, not a proven cause.

1. Generate diagnostic source copies or use the existing compile-time profiling
   workflow. Keep timers/event counters out of production builds. Allocate fixed
   counters off the callback; print results after the measured stream. Reset
   counters after warmup and processor reset.
2. Record each analysis/interpretation, low/long/short Attack and long/short
   renderer event with input sample, frame timestamp and voice/channel identity.
   Summarize stage cost/counts across all eight 64-frame phases; preserve p99,
   maximum and every over-period count. Separate nested FFT cost from rendering
   to avoid double counting. Treat instrumented timing as attribution only.
3. Run the unchanged moving chord/noise core fixture, including Off, all six
   voices, Focus and Attack; retain controls. Check healthy state, deadlines,
   allocation counts and normal output before profiling the pedal. Keep the
   ordinary uninstrumented baseline as the admission/comparison probe.
4. Select a bounded scheduling candidate from the measured costly phases. Begin
   with renderer due ages whose publication offset can preserve window onset;
   do not move Attack/analysis/control evaluation in the same experiment. Keep
   `H - age` staging, complete stereo frames, original voice/channel/history order
   and completion strictly before the next relevant frame changes. Check low
   frame ownership and both arbitrary callback partitions and Focus paths.
5. Before retaining any schedule change, extend the exact oracle to rapid
   Attack/Focus/Warp changes around original control and frame boundaries. Preserve
   original control samples, input stamps, shared Attack update order, N+H
   latency and output publication. If Attack work is later moved, sample its
   seconds at the original evaluation point; moving a timestamp alone does not
   preserve the control contract. Earlier execution cannot consume future values.
6. Repeat exact host/Pi output, default/opt-in quality/allocation checks, then
   uninstrumented matched ABBA with shared wisdom. A schedule optimization must
   reduce the relevant tails without hiding worse means or new deadline misses.
   Retain failed numerical attempts separately and restore the live service.

Mean demand still matters: scheduling redistributes work without automatically
creating chain headroom. Freeze/gliss stays opt-in, and public M8 integration
remains blocked on standalone feasibility followed by paced intended-chain,
thermal/xrun and listening checks. Do not change device buffers, governor or
unrelated DSP quality to make this comparison pass.

## Reproduction

[Raw source, numerical, geometry and both timing rounds](../benchmark-results/pog3-pi4-scatter-hybrid-2026-10-07)
retain the rejected patch, hashes, annotated assembly and restoration receipts.
Apply the decompressed `candidate.patch.gz` to `source.txt`'s base in a separate
checkout. Its SHA refers to uncompressed bytes. Build with the retained CMake
context; `build-traces.sh` builds baseline/candidate traces. The added fixture
geometry changes counters only, not the input/audio timeline.

`coverage_sources.py CHECKOUT OUTPUT` creates diagnostic source copies from the
patched checkout; `build-coverage.sh` compiles them with the candidate library.
Generated-source hashes are checked by independent regeneration. Run numerical
checks first, uninstrumented ABBA next and geometry last, using the retained
runner/wrappers and unique `/tmp` paths. The confirmation subdirectory retains
the second round, identical wisdom, executable readback and separate receipts.

`summarize.py DIRECTORY` and `check_wisdom.py DIRECTORY` audit either round;
`summarize_coverage.py DIRECTORY` audits Pi geometry, or add `host-coverage.log`
for host geometry. `summarize_confirmation.py ROOT_DIRECTORY` combines only these
two rounds and checks all 48 rows, preparation and transform-count bounds.
Derived summaries regenerate byte for byte. The recursive artifact manifest
excludes itself. See the [uniform-pair experiment](pog3-scatter-pair-results.md)
and [retained four-source optimization](pog3-scatter-batch-results.md) for the
preceding memory comparisons.
