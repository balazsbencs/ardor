# POG3 four-source live accumulation experiment — 2026-10-07

The live renderer now batches four consecutive source coefficients when their
complete interpolation support is inside the positive-frequency interior.
The baseline remains the default reduced core at `8cf40397`, with freeze/gliss
OFF. This experiment follows the user's preference to investigate memory
access before changing frame-job scheduling.

## Change and arithmetic contract

Four 24-tap scatters share 27 destination bins. The new traversal loads one
destination, retains its sum through the applicable source contributions, then
stores it once. Eligible batches therefore reduce destination read/write pairs
from **96 to 27** (71.875%). This counts algorithmic accesses, not measured DRAM
traffic or a claimed overall renderer speedup. Source rotations, weights and
all 96 contributions remain present. The central destinations receive the same
four additions in original source order; triangular head/tail destinations
receive only their applicable contributions.

The first destination must be above DC and the final destination below Nyquist.
The four coefficients must belong to the same region. Integer shifts, incomplete
batches, DC/Nyquist and reflected support retain the existing paths. Regions
retain their original order. Low-band/held synthesis, phase histories, Warp,
Focus, Attack, FFTs, gain logic, control cadence and job timing stay unchanged.
There is no epsilon threshold, heap workspace or change to preparation counts.

The first prototype passed exact host traces but failed the Pi oracle before
timing. GCC had contracted the opposite summand of some batched imaginary
complex multiplies. Across 552,960 samples, 119,844 differed; maximum absolute
error was 7.450580596923828e-09. Timing did not start, and the runner restored the
service (PID 19493, independently checked). The failed patch and receipts remain
in `attempt-1/` rather than being mixed into the successful comparison.

For batched sources 1–3 on AArch64, explicit `std::fma` now preserves the tested
baseline contraction: real input × real rotation minus the separately rounded
imaginary product, and real input × imaginary rotation plus the separately
rounded other imaginary product. NaN results retain the existing library
complex multiplication fallback. Other architectures keep the existing complex
expression. Source zero retains the original expression. These are the tested
GCC 13.4.0 ARM arithmetic choices; output gates remain necessary for other
compilers/targets rather than assuming C++ contraction is universally fixed.

The ARM compiler vectorizes the central loop with 16-byte NEON operations.
Assembly shows destination sums retained through all four contributions between
the load and store, rather than a scratch array of 27 accumulating values.
Interpolation weights still use stack storage/register loads. The renderer's
compiled stack frame grows from **496 to 528 bytes**; the bounded extra 32 bytes
is separate from prepared heap storage. Compressed annotated assembly for both
versions and the final vectorization receipt are retained.

## Exact output and DSP checks

A standalone trace drives the actual `PitchFrame`/`PitchRenderer` APIs at
N=64/128/256/512 using the deterministic FFT fallback. Its 192 cases combine
sparse wide regions, dense noise/short regions, DC/Nyquist ownership, optional
low analysis and eight shift patterns: −24/−12/0/+7/+12/+24/−5.3 semitones and
continuous Warp. Partial gains include zero and changing values; mid-run resets
exercise startup/history behavior. Each case must become audible and every
sample must be finite. Fixture bounds include four-source interior batches,
edge sources, remainders and integer translations; those counts describe
potential fixture coverage, not production hardware instrumentation.

After correcting ARM contraction, baseline/candidate output matches byte for
byte across **552,960 renderer samples and 384,000 complete-processor float
samples on both host and Pi**. The complete trace uses production FFT sizes and
the same imported wisdom before identical warmup/reset/input/settings. Equality
is per architecture, not a claim that host and ARM FFT outputs are identical.
These finite workloads are compatibility evidence, not every possible input.

The final default Release build passes all **eight DSP suites** (45.62 s).
The opt-in freeze build passes all **nine suites** (73.33 s), including full
freeze/gliss and required-preload callback C allocation/free checks. The actual
renderer trace passes ASan/UBSan with leak checking. Changed renderer/trace units
pass strict warnings on host and AArch64 with both option values. The Pi passes
the complete default pitch/Focus/Warp/overload suite before timing. The host
build is restored to OFF after the opt-in checks.

## Matched Pi comparison

Both uninstrumented probes use Buildroot GCC 13.4.0, ordinary -O3/-DNDEBUG,
the same static float NEON FFTW 3.3.10 archive and experimental freeze OFF.
The baseline probe/library is the retained reduced-core build from the previous
checkpoint, whose ELF hash is checked against that receipt. The candidate uses
the retained source patch. The benchmark source and fixtures are unchanged.

The four-second warmup and measured timeline use changing stereo chords/noise,
all six voices, Focus on, Attack=.11, processed dry, filter/detune/Spread settings,
and Off expression mode. CPU 2 uses SCHED_OTHER. Runs are baseline-forward,
candidate-forward, candidate-reverse, baseline-reverse; reverse also swaps the
callback size order. Granular and spectral identity controls remain included.
Every process imports the same prepared wisdom; all four exports retain its
header and 57 records with no additions/removals. Numerical work finishes before
timing, and no other heavy target probes overlap it.

| Build | Frames | Mean period demand | Highest p99 (µs) | Worst (µs) | Over-period callbacks per run |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | 64 | 87.10–87.74% | 1691.315 | 1884.926 | 1129–1172 / 3000 |
| Four-source batching | 64 | 85.94–86.66% | 1688.444 | 1884.796 | 1120–1161 / 3000 |
| Baseline | 128 | 86.94–87.45% | 2652.797 | 2797.629 | 9–13 / 1500 |
| Four-source batching | 128 | 86.37–86.39% | 2622.241 | 2958.518 | 4–7 / 1500 |

Two-run mean reductions are **1.280% / 0.938%** at 64/128 frames. Granular control
reductions are 0.062% / −0.218%; spectral control reductions are 0.691% / 0.769%.
The candidate's core means are below both corresponding baseline means, but
uninvolved spectral timing also changes. This is a modest observed build gain,
not a precise attribution of all the improvement to memory traffic. Shared
wisdom controls planning records without proving instruction-level plan identity
or removing process/code-placement variation. No earlier fixture timings are
pooled into these reductions.

The change remains retained as a bounded, validated optimization. It does not
establish reliable callback performance. At 64 frames, **37.33–38.70%** of measured
callbacks still exceed their period. At 128 frames, the rate is **0.267–0.467%**;
both candidate p99s fit, but both maxima exceed the period, and the worst observed
128-frame callback is higher than baseline. All tails remain recorded.

All 24 aggregate timing rows observe zero C++ callback allocations; preparation
stays **1,618,560 bytes** in the matched core fixture. FFTW storage and immutable
diagnostic storage remain outside that counter. The numerical probe, four timing
probes and runner return zero. Offline over-period counts are unpaced DSP wall
durations, **not ALSA xruns**, paced FIFO or intended-chain endurance.

The successful runner restores PID **20173**, independently checked. Temporary
target probes and float blobs are removed after retrieval/hash/byte checks.
Firmware, configuration, governor, live callback buffers and unrelated DSP
quality remain unchanged. Boundary telemetry is retained with every invocation.

## Next memory experiment

Measure how often real input supplies four consecutive coefficients inside one
eligible region. Dense frames and short regions may leave much of the scatter
on the original path. A two-source batch is the next small experiment: broader
coverage and lower register pressure, at the cost of a smaller access reduction
per batch. Compare it against this implementation and the original loop under
the same exact-output and matched-control gates; do not generalize from one
input's observed gain.

After meaningful average reduction, revisit the frame-job schedule to address
the still-dominant 64-frame peaks. Scheduling can reduce tails without changing
average demand. Preserve original Attack control samples, input stamps, update
ordering, N+H latency and renderer deadlines. Public M8 integration remains
blocked until standalone headroom and paced intended-chain/thermal/xrun tests
establish feasibility.

[Raw source, assembly, numerical and timing evidence](../benchmark-results/pog3-pi4-scatter-batch-2026-10-07)
includes hashes and reproducible summary/wisdom scripts. Source patch hashes
refer to the original uncompressed bytes of each `candidate.patch.gz`.
