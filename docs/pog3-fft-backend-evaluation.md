# POG3 FFT backend and CPU admission

Updated 2026-10-06. Production POG3 resolutions now use single-precision FFTW.
The user explicitly accepts GPL-linked builds. This replaces the compact scalar
FFT introduced at `59be448c`; the shared convolver FFT is unchanged. CPU admission
is still open, so the block remains absent from the selectable catalog.

## Execution and ownership

`SpectralPlan` owns immutable forward/inverse complex FFTW plans for N = 1024,
2048 and 4096. Each analysis/renderer owns its existing full complex vector;
new-array execution transforms that vector in place. Plans contain no shared
mutable Ardor execution scratch. Other supported sizes, 32–512 and 8192–32768,
continue through `RealtimeFft`.

Preparation allocates a dummy vector and uses `FFTW_MEASURE | FFTW_NO_BUFFERING`.
The planner may overwrite the dummy; it never plans on live analysis/history.
Plan construction and destruction serialize through a preparation-only mutex.
Execution acquires no Ardor mutex, performs no planning and uses no FFT worker
threads. Destruction, like configuration, belongs off the audio callback.
A shared plan remains alive through its analysis/renderer owners. Destruction
never calls global `fftwf_cleanup`, which could invalidate another live plan.
See the official [thread-safety guidance](https://www.fftw.org/fftw3_doc/Thread-safety.html).

Plans are made with the ordinary vector allocator's alignment class. Execution
checks the address class and chooses the aligned plan or an already prepared
`FFTW_UNALIGNED | FFTW_NO_BUFFERING` alternative. That alternative permits a
valid vector with different alignment without copying, allocating, locking or
replanning. Normal aligned execution retains SIMD. FFTW 3.3.10's alignment query
is pure address arithmetic (`kernel/align.c`). Same-size, contiguous, in-place
new-array execution satisfies the [FFTW array requirements](https://www.fftw.org/fftw3_doc/New_002darray-Execute-Functions.html).

Forward gain is unchanged; inverse output receives exactly one multiplication
by 1/N. Windows, hop sizes, deferred stereo timestamps, complete-frame publication,
renderer deadlines, all warm Focus paths, low crossover, double phase histories,
24-tap interpolation and wet latency are retained.

## The buffered-plan finding

Ordinary FFTW MEASURE selected `dft-buffered` algorithms that allocate/free a
C temporary on each execution. The earlier C++ `operator new` counter reported
zero because it cannot observe FFTW's C allocations. The isolated full identity
workload demonstrated **19,500 allocation/free pairs** during its 64-sample run.
Its apparent callback allocation result was therefore incomplete.

`FFTW_NO_BUFFERING` excludes that solver. Direct probes execute each active
size in both directions 10,000 times and observe zero C allocation/free calls.
The complete processor is checked independently by the new glibc-only CTest
`pedal-pog3-realtime-allocation`, across all seven workloads at both callback
sizes. It interposes malloc/calloc/realloc, aligned allocation and free, including
processing, reset and control helpers. All 14 rows have **zero C allocation calls
and zero free calls**, as well as zero C++ new calls. The interposer is a test
shared library and is never linked into the DSP/application. Its required CLI
mode fails if the preload was not loaded; this negative check also passes.
Recheck allocation behavior when changing FFTW version, platform or planner flags.
These host observations do not substitute for target execution checks.

## Numerical and audio acceptance

FFTW uses a different butterfly/coefficient ordering. Requiring identical float
bits to the previous algorithm would reject a valid optimized transform. The
foundation test explicitly changes the active-size contract to **relative L2
error below -110 dB** against the independent shared FFT, with a small absolute
rounding floor for subnormal fixtures. It still compares all **1,834,112 complex
bins** across every supported size and direction. Other sizes retain exact bits
and nonfinite classification. An impulse independently checks DC/inverse gain;
complex round trips check gain/phase, and two concurrent execution threads check
shared plans with independent mutable vectors.

Overflow/nonfinite butterfly classification can differ between FFT algorithms.
The public audio gate remains whole-frame rejection with existing WOLA history
preserved, followed by clean recovery. Existing identity delay, arbitrary callback
partition, deferred-window exactness, compact synthesis, spectral pitch, low chord,
Attack, voice stages, all expression modes, 60-second tonal/chord freeze holds and
warm-live release checks continue to pass. Exact comparisons between two paths
using the same prepared backend remain exact; historical scalar-backend WAVs are
no longer claimed byte-identical to FFTW output.

All **nine POG3 release CTests pass (74.63 s)**, including the allocation test.
The final required-preload invocation separately passes in **27.70 s**. Final
foundation/pitch ASan+UBSan checks with leak detection pass in **121.75 s**,
followed by a passing freeze warm-live check. The stream, foundation and C probe
compile with ordinary -O3 and strict warnings; the benchmark's intentional
malloc-backed replacement new/delete requires disabling GCC's existing
`-Wmismatched-new-delete` diagnostic for its standalone warning check.

## Build and distribution

CMake requires `fftw3.h` and `fftw3f`, supplied by the platform development package:
`libfftw3-dev` on Debian/Ubuntu or `brew install fftw` on macOS. Explicit
`ARDOR_FFTW_INCLUDE_DIR` / `ARDOR_FFTW_LIBRARY` cache paths support SDKs and sysroots.
Only `ardor_pog3` links the imported single-precision target; the shared FFT and
other effect implementations retain their existing backend. Linux CMake CI jobs
install the new development dependency.

The tested host runtime is **FFTW 3.3.10 (Debian 3.3.10-2+b1)**. Ardor's pinned
Buildroot 2025.02.15 also supplies 3.3.10. The pedal package selects/depends on
`fftw-single`. Its external make override enables AArch64 NEON without the
32-bit-only -mfpu option, uses ordinary -O3 and disables FFT threads/OpenMP/combined
threads. Buildroot's speed-over-accuracy/fast-math option remains disabled.
Both generated-defconfig checking and inspection of effective make variables
pass; the saved defconfig stays current because dependencies are selected by the
Ardor package. No image or tracked device binary was regenerated.

The pinned [Buildroot single-precision recipe](https://github.com/buildroot/buildroot/blob/2025.02.15/package/fftw/fftw-single/fftw-single.mk)
otherwise disables NEON on AArch64 through its 32-bit ARM condition. Actual
Pi execution and CPU/memory admission still require device measurements.

FFTW is GPL-2.0-or-later; the user's acceptance permits this dependency. Original
Ardor source licensing stays as recorded in LICENSE, while redistribution of
FFTW-linked binaries must satisfy GPL terms, including corresponding source/build
instructions. The dependency is not covered by Ardor's MIT license. Buildroot
records FFTW's upstream license and the pedal installation copies its COPYING
to `/usr/share/licenses/fftw-single/COPYING`. Review corresponding-source packaging
before distributing linked release binaries. See [FFTW licensing](https://fftw.org/).

## Memory accounting

The existing C++ preparation counter reports **2,026,198 bytes** for the all-mode
expression processor, down 13,920 from the preceding 2,040,118. It includes
preparation temporaries but **excludes FFTW's internal C allocations**, so this
number alone no longer proves the complete 2 MiB host goal.

A fresh standalone default-processor glibc `mallinfo2` observation, including live
arena allocations and mmap storage, measures a **2,436,144-byte heap increase**
with normal allocator caches. With glibc tcache disabled it measures **2,319,504
bytes**, of which 132,416 remain after processor destruction. These figures
include FFTW planner-global storage and allocator overhead; they are not precise
per-instance requested bytes or target ABI measurements. The fuller memory goal
must be rechecked, after CPU feasibility, rather than reporting the old C++-only
count as complete FFTW admission.

## Final CPU comparison

Four complete release runs execute sequentially in **control → FFTW → FFTW →
control** order after builds and validation finish. The immutable control is
`59be448c`, using its matching private plan header and the already corrected
shared granular reader. Both sides use ordinary **-O3 -DNDEBUG**, GCC 14.2.0,
an Intel Core i3-8100T at 3.10 GHz, the same seven workloads and 64/128-sample
callbacks at 48 kHz. No relaxed math flags, instrumentation or allocation
interposer is used for these final timings. A short initial control overlapped
a warning compilation; it was stopped, retained separately as
`fftw-discarded-build-overlap.csv`, and the whole four-run sequence restarted.
No completed run/outlier is discarded from this comparison.

Full-path mean demand improves **24.12–27.66%**. It now consumes
**31.67–35.27%** of the callback period, still above the **25% goal**.
The unchanged granular workload varies **-1.12% to +5.60%**, exposing host drift.
All 56 rows finish with zero C++ processing/control/reset allocations and
healthy pre-reset analysis/render counters. Full-path transform bursts remain
11/16 at 64/128 samples; the identity comparison retains 20. All outliers remain
in means, percentiles and maxima. The second FFTW pair retains a **5670.750 µs**
64-sample static maximum, above the **1333.333 µs** period; its 33.11% mean
also includes that outlier. The first scalar pair retains a **4910.204 µs**
128-sample expression maximum. Tails and maxima do not improve uniformly.
These measurements improve the CPU blocker but do not certify target deadlines
or make the block selectable.

| Pair | Path | Frames | Mean scalar → FFTW (µs) | Mean change | FFTW period use | p99 scalar → FFTW (µs) | Max scalar → FFTW (µs) |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | Granular | 64 | 97.447 → 96.695 | -0.77% | 7.25% | 656.514 → 656.094 | 676.245 → 740.041 |
| 1 | Identity | 64 | 207.290 → 65.269 | -68.51% | 4.90% | 717.179 → 220.600 | 741.866 → 454.511 |
| 1 | Bank | 64 | 524.188 → 369.206 | -29.57% | 27.69% | 766.341 → 543.763 | 813.104 → 717.171 |
| 1 | Attack bank | 64 | 532.652 → 377.359 | -29.15% | 28.30% | 809.331 → 651.610 | 854.872 → 697.816 |
| 1 | Static | 64 | 579.347 → 423.041 | -26.98% | 31.73% | 862.269 → 704.225 | 1007.672 → 1271.277 |
| 1 | Expression | 64 | 588.851 → 434.952 | -26.14% | 32.62% | 900.530 → 747.573 | 957.994 → 912.471 |
| 1 | Gliss | 64 | 626.505 → 469.353 | -25.08% | 35.20% | 939.814 → 780.415 | 1092.863 → 1200.795 |
| 1 | Granular | 128 | 194.468 → 193.568 | -0.46% | 7.26% | 677.494 → 681.585 | 686.103 → 2116.058 |
| 1 | Identity | 128 | 412.596 → 129.771 | -68.55% | 4.87% | 720.543 → 226.273 | 754.910 → 269.912 |
| 1 | Bank | 128 | 1048.858 → 738.411 | -29.60% | 27.69% | 1211.591 → 826.036 | 1268.775 → 1316.158 |
| 1 | Attack bank | 128 | 1064.893 → 753.431 | -29.25% | 28.25% | 1186.842 → 869.519 | 1276.473 → 914.532 |
| 1 | Static | 128 | 1157.275 → 844.682 | -27.01% | 31.68% | 1267.330 → 968.144 | 1497.240 → 1146.288 |
| 1 | Expression | 128 | 1200.768 → 871.402 | -27.43% | 32.68% | 1502.362 → 1082.651 | 4910.204 → 1167.181 |
| 1 | Gliss | 128 | 1247.689 → 935.829 | -25.00% | 35.09% | 1389.953 → 1095.060 | 1523.304 → 1243.057 |
| 2 | Granular | 64 | 97.418 → 102.871 | +5.60% | 7.72% | 657.040 → 727.966 | 673.123 → 2685.082 |
| 2 | Identity | 64 | 206.197 → 65.881 | -68.05% | 4.94% | 712.492 → 250.104 | 743.316 → 416.295 |
| 2 | Bank | 64 | 524.156 → 374.729 | -28.51% | 28.10% | 766.771 → 602.384 | 816.628 → 756.133 |
| 2 | Attack bank | 64 | 535.618 → 376.485 | -29.71% | 28.24% | 819.171 → 649.393 | 878.949 → 689.640 |
| 2 | Static | 64 | 581.702 → 441.408 | -24.12% | 33.11% | 869.249 → 740.527 | 933.494 → 5670.750 |
| 2 | Expression | 64 | 593.187 → 433.710 | -26.88% | 32.53% | 913.720 → 747.454 | 971.194 → 802.119 |
| 2 | Gliss | 64 | 625.698 → 470.299 | -24.84% | 35.27% | 937.678 → 781.721 | 1113.637 → 1026.979 |
| 2 | Granular | 128 | 194.698 → 192.509 | -1.12% | 7.22% | 678.497 → 676.728 | 693.730 → 685.257 |
| 2 | Identity | 128 | 412.572 → 129.131 | -68.70% | 4.84% | 723.040 → 223.993 | 750.146 → 235.083 |
| 2 | Bank | 128 | 1062.646 → 741.751 | -30.20% | 27.82% | 1261.193 → 824.067 | 2398.897 → 919.272 |
| 2 | Attack bank | 128 | 1076.214 → 754.059 | -29.93% | 28.28% | 1197.747 → 879.682 | 3033.881 → 1000.805 |
| 2 | Static | 128 | 1167.418 → 844.568 | -27.66% | 31.67% | 1295.517 → 973.150 | 1432.107 → 1050.105 |
| 2 | Expression | 128 | 1188.798 → 867.975 | -26.99% | 32.55% | 1406.504 → 1078.039 | 2020.694 → 1155.219 |
| 2 | Gliss | 128 | 1262.610 → 935.110 | -25.94% | 35.07% | 1431.611 → 1092.403 | 3317.509 → 1232.961 |

The complete CSVs, including median/p95/p999, reset cost and worst-callback
position/transform counts, are ignored local artifacts:
`build-ci/pog3-artifacts/fftw-{control,production}-{1,2}.csv`.
Earlier `fftw-complex-preliminary.csv` used allocating buffered plans; it is an
isolated comparison, not the final production/allocation evidence.


Recorded CSV SHA-256 identities:

| File | SHA-256 |
| --- | --- |
| `fftw-control-1.csv` | `928599a268424112d505d8f19953d800daaa260b3d393207d6bef29df12fd62f` |
| `fftw-production-1.csv` | `3320bc23b54650407f5918419a4d11a6f9da7bb07963c053292b2eac76c76576` |
| `fftw-production-2.csv` | `e7cc8c42ccc8968fc38a9c5171549218b782e4178dae32b05efa54bc246197d6` |
| `fftw-control-2.csv` | `d4c5128d76d239c333731a58e43b1e64b9fec8c815af1d7f7062ac7f345dabc5` |

## Remaining CPU work, for the implementing agent

A separate temporary scoped-timer build profiles the final FFTW algorithm after
the four uninstrumented runs. Timers exist only in copied development sources
under `/tmp/pog3-fftw-trial/profile`; production code contains none. The experiment
measures one 64-sample expression run and one gliss run, after warm-up/reset,
with inclusive/exclusive accounting so renderer child FFT/held work is not
counted twice. These are approximate diagnostic shares from one host, with
timer overhead; they are not admission timings or library-only speedup claims.

| DSP scope | Expression exclusive share | Gliss exclusive share |
| --- | ---: | ---: |
| FFT execution including inverse scaling | 6.30% | 5.86% |
| Live renderer and WOLA, excluding FFT/held children | 28.26% | 21.65% |
| Pitch-frame interpretation | 23.54% | 21.97% |
| Polyphonic Attack update | 22.50% | 20.25% |
| Freeze update/capture/assignment | 2.96% | 3.74% |
| Held carrier/lobe rendering | 5.17% | 16.39% |

Rendering totals approximately **33.43% / 38.05%**, with interpretation and
Attack each over 20%. Remaining FFT work is about 6%; even removing it entirely
would save only about two callback-period percentage points. Reaching 25% from
the current full paths requires a further **approximately 21–29% reduction**
in processor demand. Prioritize the other measured DSP costs now.

1. **Interpretation magnitude path:** `PitchFrame::update` evaluates complex
   magnitude for every positive bin, then another previous-bin magnitude at
   each peak. Prototype a squared-magnitude/double-intermediate sqrt helper
   that preserves the complete finite float range without float-product
   overflow/underflow. Measure this helper against `std::abs`/hypot, including
   subnormals, near-limit floats, axes, cancellation and threshold-neighbor
   cases. Do not use fast-math or flush-to-zero to obtain a favorable result.
   Check peak ties, ownership, frequency estimates, noise-floor decisions and
   recovery through the independent DSP suites, not just the magnitude loop.
   Retain peak-only phase evaluation and double oscillator histories.
2. **Attack ownership/grouping:** instrument `group`, `owner`, `envelope` and
   the slot/binding loops separately before changing the matcher. The existing
   sorted right/canonical indexes already remove much unrelated scanning.
   Cache reusable bounded harmonic candidate work, if the profile justifies it,
   while retaining strict tolerance, score/tie order, family/onset dates,
   reserved surviving bindings, excitation epochs and canonical age transfer.
   Compare boundary/random matcher cases, automated frame/voice results and
   staggered-note/held-chord Attack gates before taking timing evidence.
3. **Live/held rendering:** the combined share is largest. Keep the 24-tap
   kernel initially; evaluate vectorized contiguous complex multiply/add and
   repeated lobe/coefficient work using the existing immutable tables. Preserve
   edge reflection, DC/Nyquist endpoints, per-destination accumulation order,
   low crossover/phase alignment, held/live history and continuous Warp.
   A shorter interpolation kernel is a separate audible tradeoff and must pass
   existing tuning, spur/alias, chord and freeze gates before it is considered.
4. **Evaluate one change at a time:** retain an immutable current FFTW control,
   build ordinary Release, finish builds/tests, then run two opposite-order
   timing pairs for all 14 workloads. Compare means, p99/max, the unchanged
   granular drift indicator, allocation/free calls and renderer health before
   keeping a prototype. Preserve all completed callbacks/outliers. Repeat the
   diagnostic profile after a meaningful change so optimization follows the
   remaining costs.
5. **Admission remains the integration gate:** when host demand meets the goal,
   measure the actual AArch64 NEON backend, full C/C++ plan/storage footprint,
   target callbacks and combined-chain endurance. Until then defer catalog,
   factory, editor, scene and manager work. Host success alone does not establish
   device feasibility. Keep the original worktree and tracked device binaries
   outside this DSP work.

## Reproduction

```sh
# Supply the platform FFTW development package or explicit include/library paths.
cmake -S . -B build-ci -DARDOR_UI_BACKEND=none -DCMAKE_BUILD_TYPE=Release
cmake --build build-ci -j4 --target pedal-pog3-controls pedal-pog3-quality pedal-pog3-pitch-quality pedal-pog3-attack-quality pedal-pog3-voice-stages pedal-pog3-expression-quality pedal-pog3-freeze-quality pedal-pog3-bench pedal-pog3-malloc-probe
ctest --test-dir build-ci --output-on-failure -R '^pedal-pog3-'
build-ci/pedal-pog3-bench --csv build-ci/pog3-artifacts/fftw-retained.csv
# Run the allocation CTest separately from timing; the interposer affects setup.
ctest --test-dir build-ci --output-on-failure -R '^pedal-pog3-realtime-allocation$'
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-pog3-sanitize --output-on-failure -R '^pedal-pog3-(quality|pitch-quality)$'
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 build-pog3-sanitize/pedal-pog3-freeze-quality --warm-live
```

The preload target/CTest exists only on Linux/glibc. Other platforms retain
C++ allocation checking and need their own C-library execution-allocation probe.
To reconstruct the old CPU control, build only `pedal-pog3-bench` at `59be448c`
in a separate checkout with matching headers, the same compiler/flags and the
same corrected reader. Benchmark each side sequentially, outside build/test or
profiling activity. Do not substitute an unoptimized build or discard slow rows.
