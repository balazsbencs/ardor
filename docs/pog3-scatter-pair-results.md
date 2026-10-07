# POG3 two-source accumulation experiment — 2026-10-07

**Decision: retain four-source batching.** Replacing it throughout the live
renderer with two-source batching is byte exact on the tested host/Pi traces,
but does not show a consistent CPU improvement. At the device's current
64-frame callback size, the candidate is 0.731% slower on the two-run mean and
has more over-period callbacks. At 128 frames it is 0.516% faster on mean, but
has more over-period callbacks and a higher maximum. The prototype is retained
as a reproducible patch, not as the production implementation.

## Experiment and arithmetic

The baseline is the four-source implementation at
`039686625620fca743b2e75e21e4553c3ec3ab15`. Both builds use the reduced core:
experimental freeze OFF and profiling OFF. This comparison does not use the
older unbatched processor as baseline or pool its timing results.

The prototype replaces each eligible four-source traversal with a two-source
traversal. Each pair has 24 taps per source and shares 25 destinations. The
first and last destination each receive one contribution; the middle 23 each
receive both in original source order. Eligible batches reduce destination
read/write pairs from 48 to 25 (47.917%). Four-source batches reduce 96 to 27
(71.875%). These are algorithmic access counts, not measured memory bandwidth.

Eligibility still requires one region and fully interior support. Integer
translation, endpoints/reflection and single leftovers retain the original
paths. The tested AArch64 complex-multiply contraction and NaN library fallback
are preserved. FFTs, gain/phase histories, Attack, Warp/Focus, latency, control
cadence and job timing are unchanged. There is no new prepared heap workspace.
The compiled ARM renderer stack frame is 512 bytes, versus 528 for four-source
batching. Annotated assembly and compiler vectorization diagnostics are retained.

## Eligibility explains the tradeoff

A separate diagnostic inspects the exact live-region geometry before the
source traversal, after the normal alias/gain/fully-held skip logic. For each
region it models the greedy two-source and four-source traversal independently.
It runs **after** all uninstrumented timing probes. Its timing CSV is retained
only as a receipt and is excluded from timing summaries.

The changing stereo chord/noise core fixture yields identical geometry at
64 and 128 frames:

| Modeled traversal | Batches | Batched interior sources | Interior sources batched | Interior destination read/write pairs | Reduction from original interior scatter |
| --- | ---: | ---: | ---: | ---: | ---: |
| Two sources | 1,274,833 | 2,549,666 | 86.957% | 41,049,049 | 41.667% |
| Four sources | 459,225 | 1,836,900 | 62.648% | 38,683,683 | 45.028% |

Both classify the same 2,932,092 fractional interior sources, 81,872 edge
sources and 637,725 integer sources across 897,496 visited regions. The
unbatched interior model has 70,370,208 destination read/write pairs. Broader
pair coverage therefore does not imply fewer accesses: the pair traversal
models about **6.11% more interior pairs than the four-source traversal**.
This supports retaining four-source batching, but is not a causal measurement
of the CPU difference or a generalization to every input. Zero geometry in
the granular/spectral identity controls is expected because those fixtures
bypass shifted live rendering. The diagnostic's reproducible parser checks
source accounting, width arithmetic and callback-size invariance.

## Exact output and DSP validation

The candidate matches baseline byte for byte across **552,960 renderer samples
and 384,000 complete-processor float samples on both host and Pi**. Equality is
per architecture. The retained 192-case renderer fixture uses actual
`PitchFrame`/`PitchRenderer` APIs and deterministic small FFT plans; it covers
wide/short regions, edges, optional low analysis, eight shift/Warp patterns,
zero/changing gains and resets. Its printed four-source geometry is potential
fixture coverage, not an actual candidate batch counter. The complete trace
uses production FFT sizes and shared imported wisdom. These finite fixtures
establish tested compatibility, not equality for every possible input/compiler.

The prototype passes all **eight default DSP suites** (45.81 s) and all **nine
opt-in freeze suites** (72.70 s), including full freeze/gliss and the required
preload callback C allocation/free checks. Its actual renderer trace passes
ASan/UBSan with leak checking. Renderer/trace units pass strict host and
Buildroot AArch64 warnings with both freeze option values. The Pi passes the
complete default pitch/Focus/Warp/overload suite before timing. After rejecting
the prototype, the production source is restored exactly to the already
validated four-source implementation and the host build is rebuilt with
experimental freeze OFF. No new production DSP code is retained here.

## Matched Pi timing

Both probes use Buildroot GCC 13.4.0, ordinary -O3/-DNDEBUG and the same static
float NEON FFTW 3.3.10 archive. The immutable baseline ELF matches the preceding
four-source receipt. Source and uploaded executable hashes are checked. Input,
settings and benchmark source are unchanged: changing stereo chords/noise, all
six voices, Focus, Attack=.11, processed dry, filter/detune/Spread and Off
expression mode. Each fixture uses a four-second warmup, reset and four-second
measurement at 48 kHz, on CPU 2 with SCHED_OTHER and the existing performance
governor. Ordering is baseline-forward, candidate-forward, candidate-reverse,
baseline-reverse; reverse also reverses the callback-size order.

| Build | Frames | Mean period demand | Highest p99 (µs) | Worst (µs) | Over-period callbacks per run |
| --- | ---: | ---: | ---: | ---: | ---: |
| Four-source baseline | 64 | 85.96–86.49% | 1696.371 | 1881.333 | 1112–1118 / 3000 |
| Two-source candidate | 64 | 86.38–87.32% | 1692.685 | 1908.055 | 1170–1207 / 3000 |
| Four-source baseline | 128 | 86.42–87.11% | 2651.852 | 2785.648 | 4–7 / 1500 |
| Two-source candidate | 128 | 85.89–86.74% | 2621.889 | 2877.741 | 6–8 / 1500 |

Two-run mean reductions at 64/128 frames are **−0.731% / +0.516%**.
Granular control reductions are −0.036% / −0.137%; spectral control reductions
are −1.364% / −0.841%. This variation prevents attributing the observed timing
change precisely to the traversal. Two repetitions per build are not a
statistical proof. Both versions still have callback peaks beyond their
1333.333/2666.667 µs periods; a small 128-frame average change cannot establish
live suitability. Offline over-period counts are unpaced DSP wall durations,
not ALSA xruns, paced FIFO or intended-chain endurance.

All processes import the same shared wisdom. All four timing exports retain
its header and 57 records with no additions/removals; this controls planning
records without proving instruction-level plan identity. All 24 uninstrumented
aggregate rows report zero C++ callback allocations. Core preparation remains
**1,618,560 bytes**, excluding FFTW and immutable diagnostic storage as before.
The runner, numerical probe, four timing probes and final diagnostic all return
zero. No heavy target work overlaps them. Boundary telemetry spans 52.6–57.0°C
from the first to last receipt and shows 1.5 GHz at those boundaries; it is not
a continuous thermal or throttling measurement.

The live service is restored and independently checked (PID **1052**). Target
probes and temporary float blobs are removed after byte/hash checks. No firmware,
configuration, callback buffer, governor or unrelated DSP quality is changed.

## Next CPU work for Luna

Keep the four-source path. The next bounded memory experiment is a **hybrid
traversal**: take four eligible sources first, then use a pair only when fewer
than four eligible sources remain in the same region/interior support. This
preserves the stronger four-source savings and may reduce the existing single
remainder work. The diagnostic here models uniform widths only; it does not
measure hybrid eligibility or prove a hybrid CPU gain.

For that experiment, model hybrid coverage first, retain original contribution
ordering and the tested ARM contraction, then repeat exact host/Pi traces and
matched ABBA controls against the retained four-source baseline. Keep it only
if device results justify it. Do not expand into FFT, Attack or parameter
changes within the same comparison.

If the memory candidates remain marginal, prioritize the clustered frame-job
schedule causing 64-frame peaks. Before moving work, establish exact traces
for original control samples, input timestamps, history/update ordering,
complete-frame publication, N+H latency and renderer deadlines. Scheduling may
improve tails without reducing mean demand. Freeze/gliss remains opt-in;
public M8 integration still depends on standalone headroom followed by paced
intended-chain, thermal/xrun and listening checks.

## Reproduction

[Raw experiment evidence](../benchmark-results/pog3-pi4-scatter-pair-2026-10-07)
retains the rejected patch, source/ELF/artifact hashes, annotated assembly,
numerical logs, wisdom, all timing tails and restoration receipts. Check out
the source base in `source.txt` and apply the **decompressed** `candidate.patch.gz`
in a separate checkout to recreate the prototype. Its patch SHA refers to
the uncompressed bytes. Build with the retained CMake context; `build-traces.sh`
compares libraries from the base and patched checkouts.

`coverage_sources.py CHECKOUT OUTPUT` generates only diagnostic source copies
from the patched checkout. Compile its `bench.cpp` and `pitch.cpp` with the
candidate library and hosted pitch shifter, using the same toolchain/FFTW
configuration. Generated-source hashes are retained for checking reproduction.
Use `numerical.sh` and the four timing wrappers via `run-remote.sh`; run
`coverage.sh` last. Preserve numerical failure receipts and service restoration
before retrying. Target `/tmp` paths must be unique for each new experiment.

Run `summarize.py DIRECTORY`, `check_wisdom.py DIRECTORY` and
`summarize_coverage.py DIRECTORY` to regenerate summaries and audit wisdom/
geometry. The artifact SHA-256 manifest covers the evidence recursively,
excluding the manifest itself. The retained [four-source report](pog3-scatter-batch-results.md)
continues to describe the production optimization and its earlier baseline.
