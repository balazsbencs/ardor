# POG3 silent held reconstruction checkpoint — 2026-10-07

This increment follows the phase-major Hann-lobe checkpoint. It skips held
carrier reconstruction when the resolved amplitude is exactly zero, after
updating identity, center, frequency and phase on their original timeline.
Complementary crossover bands, zero-magnitude gliss slots and zero held gain
can therefore avoid sin/cos, coefficient lookup and spectral scatter while
retaining phase continuity when they become audible again.

No epsilon threshold is introduced: small nonzero amplitudes still render.
Capture, target assignment, gliss timing, alias/edge policy, FFT plans, support,
staging and allocation bounds retain their existing behavior. Silent phases
remain warm. The 25% goal remains a chain-budget planning target, with target
mean demand, callback tails and intended-chain endurance determining admission.

## Audio and arithmetic validation

The retained `held-trace.cpp` drives the actual frame, held renderer and
synthesis APIs in 36 cases: N=64/128/256, main-only/low-only/combined bands,
and -24/0/7/+24 semitone settings. It repeatedly moves partials between silent
and audible crossover bands with stable identities, restores zero magnitude
and zero gain, and exercises DC/reflection and upper alias behavior. Each case
must become audible, so equality cannot be satisfied by silence alone.

Each architecture compares the baseline and candidate's **1,376,256 output
samples (5,505,024 bytes)** byte for byte. Both host and Pi comparisons pass.
The small plans deliberately use the deterministic shared FFT fallback so
independently measured FFTW plans cannot obscure exact equality. Host and ARM
hashes are compared within their architecture; equality across architectures
is not asserted. This is finite-fixture evidence, not a proof for every input.
Trace sources, logs and hashes are retained; temporary float blobs are removed.

All **nine Release DSP CTests pass** (73.92 s), including complete freeze/gliss,
expression, pitch, Attack and callback allocation gates. Changed production
source and trace units pass strict warnings. The full Pi pitch suite passes
before timing. Production-size FFTW paths, actual capture/retarget events and
staged health are exercised by the unchanged complete processor benchmark.

## Uninstrumented Pi comparison

The immutable baseline full ELF matches the layout comparison's final candidate
SHA256. Both builds use Buildroot GCC 13.4.0, ordinary -O3/-DNDEBUG, the same
static single-precision NEON FFTW 3.3.10 archive and profiling OFF. Numerical
work finishes before four sequential baseline/candidate/candidate/baseline
probes on CPU 2, SCHED_OTHER, at 48 kHz and 64/128 frames. Each fixture has warmup,
reset and a four-second measured audio timeline. No other heavy target probe
runs during timing. These are standalone unpaced DSP measurements, not ALSA
xruns, paced FIFO or combined-chain endurance.

| Full workload | Frames | Baseline mean period demand | Candidate mean period demand | Two-run mean reduction | Highest candidate p99 (µs) | Candidate worst (µs) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Static filter/space/pan/Attack | 64 | 92.58–92.62% | 91.57–92.15% | 0.79% | 1877.352 | 2070.482 |
| Expression/filter/space/pan/Attack | 64 | 95.89–96.10% | 95.08–95.68% | 0.64% | 2049.704 | 2270.315 |
| Freeze/gliss/capture/assignment | 64 | 103.89–103.93% | 102.44–102.92% | 1.18% | 2135.575 | 2547.167 |
| Static filter/space/pan/Attack | 128 | 92.71–92.88% | 91.75–92.17% | 0.91% | 2787.575 | 2885.056 |
| Expression/filter/space/pan/Attack | 128 | 95.91–96.21% | 95.53–95.80% | 0.41% | 3199.092 | 3385.944 |
| Freeze/gliss/capture/assignment | 128 | 103.77–104.03% | 102.86–103.15% | 0.87% | 3157.741 | 3492.944 |

The observed mean reduction in freeze/gliss is **0.87–1.18%**. Final mean demand
remains **102.44–103.15%** of a period; all full-path p99s exceed the
1333.333/2666.667 µs periods. This does not establish standalone live admission.
The branch is retained as an exact-zero work elimination with matching finite
trace evidence, but its CPU benefit is small and its precise magnitude uncertain.

The uninvolved static path also improves by 0.79–0.91%, and the spectral identity
control by 0.67–0.78%. Granular controls change by -0.08 to -0.10% reduction.
Process/planner variation and code placement can contribute to build differences;
these controls prevent attributing the whole observed reduction to the new branch.
No cache or instruction counters are collected. Some tails worsen and all raw
outliers remain retained. Numerical, diagnostic and performance timings are not
pooled, nor are the preceding layout results pooled with this comparison.

All four timing invocations and the numerical probe return zero. All **56 aggregate
rows** remain retained, with zero C++ callback allocations and unchanged matched
C++ preparation-byte counts. Runner exit is zero. Restored service PID **12381**
matches independent readback; sampled clock/governor remains 1.5 GHz/performance.
Boundary temperature samples are **55.504–59.400°C**, not a continuous peak.
Target C/FFTW allocations and complete memory admission remain unmeasured.

[All A/B and trace receipts](../benchmark-results/pog3-pi4-held-zero-2026-10-07)
include sources, compressed raw core patch, ELF/artifact hashes, numerical logs,
service/telemetry receipts and reproducible summary arithmetic. The candidate
patch hash refers to the uncompressed patch. Generated audio blobs are temporary;
the recorded hashes and successful byte comparison retain the equality receipt.

## Final diagnostic attribution and next CPU work

A separate profile runs after both uninstrumented comparisons, using generated
source copies with nested exclusive scopes. Its 128-frame full fixtures attribute:

| Workload | FFT | Live rendering excluding FFT/held | Interpretation | Attack | Freeze update | Held rendering | Residual |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Static | 17.56% | 30.61% | 17.96% | 18.16% | 3.20% | 0.00% | 12.51% |
| Expression | 16.97% | 28.48% | 17.33% | 17.59% | 3.19% | 5.07% | 11.38% |
| Freeze/gliss | 15.93% | 21.37% | 16.12% | 15.80% | 3.93% | 16.28% | 10.57% |

These are instrumented cost shares, not optimization-gain or admission timings.
All fourteen FFT dispatch totals match their scope-call totals. Every full
fixture still executes 5247 r2c and 14984 c2r transforms, with zero complex or
unaligned fallbacks. The final profile returns zero, observes zero C++ callback
allocations and restores service with PID **12644**, independently verified.
[Diagnostic receipts](../benchmark-results/pog3-pi4-held-profile-2026-10-07)
retain normalized stage/dispatch CSVs, raw output, generated-source/ELF hashes
and service/telemetry receipts. No profiling overlaps either A/B comparison.

The next CPU increment should target rendering cost and retain the scalar/audio
contract before changing production code:

1. Improve comparison control by reusing the same prepared FFTW wisdom for both
   builds in the isolated benchmark. Check actual plan/dispatch behavior and
   uninvolved spectral controls; do not introduce callback planning or treat
   existing small build differences as uniquely causal.
2. Prototype batched lobe evaluation for interior held partials. Avoid repeated
   function/index work while retaining the historical float subtraction and
   interpolation results. Keep the scalar edge/reflection path. Hoisting a
   fractional phase can change rounding near the origin; require a bit-exact
   scalar oracle across destinations, table seams and all production sizes.
3. Investigate live scatter memory traffic, the largest remaining measured
   rendering stage. Its current 24-tap ARM loop is already vectorized. Compare
   accumulation layouts that reduce repeated destination loads/stores rather
   than adding duplicate SIMD work. Account for all prepared scratch memory,
   accumulation order, partition boundaries, reflections and conjugate symmetry.
4. Run complete unchanged DSP gates and an uninstrumented target comparison after
   any retained implementation. Preserve timestamps, snapshots, complete-frame
   publication and staging deadlines. Once average headroom exists, callback
   tails still need work before a paced intended-chain/thermal/xrun test.

Public M8 integration remains blocked. No buffer-size or unrelated effect-quality
changes, application/firmware deployment, or claim of live usability follows
from these small CPU improvements.
