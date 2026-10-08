# POG3 real-data FFT checkpoint — 2026-10-07

This increment tests prepared real-input/inverse-Hermitian FFTW transforms in
the existing production processor, against the preceding magnitude-optimized
Pi baseline at commit `23f4f9248a382aa2721d32767f3c36f9311fd640`.
The original 25% goal remains a chain-budget planning target. Target average
demand, callback tails and intended-chain endurance determine feasibility.

## Transform design and preserved behavior

`SpectralPlan` prepares r2c/c2r plans alongside its existing complex forward and
inverse plans for N=1024/2048/4096. Each direction has the same planned-alignment
and unaligned fallback policy as before. Construction/destruction remains
serialized off the callback. Execution is single-threaded and uses each caller's
existing N-complex workspace, not mutable scratch in the shared plan.

The workspace contains 2N floats, enough for the N+2 physical padding needed by
an even-sized in-place real transform. Forward packing runs in ascending order,
consuming each original real component before any later packed write overlaps it.
FFTW writes the nonnegative N/2+1 bins; the remaining bins are restored by complex
conjugation. Inverse execution writes N packed real samples; descending expansion
restores the full complex-vector format without overwriting unread samples.
Inverse gain is applied exactly once as 1/N.

The final synthesis path keeps this real output packed and feeds it directly into
the existing transactional overlap-add loops, avoiding the complex expansion.
The public transform method retains its full complex-vector output. Internal
packed synthesis is checked sample for sample against that public inverse for
both Hermitian and general complex frames at N=32/1024/2048/4096; this also covers
the original shared fallback. No additional callback buffer is allocated.
See FFTW's
[real-data interface](https://www.fftw.org/fftw3_doc/Real_002ddata-DFTs.html),
[array format](https://www.fftw.org/fftw3_doc/Real_002ddata-DFT-Array-Format.html) and
[new-array execution requirements](https://www.fftw.org/fftw3_doc/New_002darray-Execute-Functions.html).
The installed documentation currently describes 3.3.11; this implementation is
compiled and tested against the pinned 3.3.10 headers/archive.

The public transform API still accepts arbitrary complex input. Forward real
execution requires every imaginary component to be zero. Inverse real execution
requires real DC/Nyquist bins and exact conjugate symmetry across the remaining
bins. General complex spectra, including one-sided deviations and imaginary
DC/Nyquist components, retain the existing complex transform. The selector and
packing costs are included in production timing. Nonproduction FFT sizes retain
the original `RealtimeFft` implementation and exact fallback contract.

The first version fails the existing float-maximum impulse fixture: the real
forward transform returns negative infinity at bin 1 where the reference and
old complex transform remain finite. Real codelets can double intermediates.
The final selector therefore also retains complex arithmetic for extreme input
components, using a conservative FLT_MAX/(2N) headroom bound prepared with each
plan. This includes the initial failing fixture without changing its tolerance.
Ordinary audio-range spectra retain the real path. This is a DSP arithmetic
compatibility decision, not a new rejection policy for the public transform.

Both real and complex plans keep `FFTW_NO_BUFFERING`. The pinned 3.3.10 RDFT
buffered solvers honor that flag; actual prepared full-path C allocation/free
counts are also checked, rather than inferring allocation behavior from flags.
Each shared plan gains four FFTW plan handles. Full processor C++ preparation
increases by **96 bytes**, from 2026198 to **2026294 bytes** in the expression
fixture. Additional FFTW plan storage is not included in that C++ counter;
complete target memory admission remains open.

## DSP validation

The unchanged production audio thresholds pass in all **nine Release CTests**
(75.11 s for the final packed-synthesis implementation): controls, numerical/stream foundations, pitch/low-chord quality,
Attack, voice stages, expression, freeze/gliss and required-preload callback
allocation checks. Concurrent host validation affects elapsed times; these
are correctness receipts, not a matched host performance comparison.

The foundation oracle now compares **2227136 complex bins**, covering every
supported power-of-two size and both directions. Active FFTW arithmetic retains
the existing -110 dB relative L2 error limit and subnormal absolute floor;
fallback sizes retain bit/classification comparisons. Additional fixtures break
only a negative-frequency partner, DC imaginary part or Nyquist imaginary part
to catch an incorrectly unconditional real inverse. Shared-plan concurrency now
covers real and complex inputs, both directions, two simultaneous workspaces
and 64 repeats for each mode at each production size. Round-trip gain/phase
retains the existing -110 dB limit.

Foundation ASan+UBSan/leak checking passes, including the overlapping packing
and expansion, concurrency, stream reset/chunking and transactional overlap-add
fixtures. Changed source/test units pass `-Wall -Wextra -Wpedantic -Werror`.
The Pi passes the complete updated foundation/reference suite and the full
pitch suite: tuning/spurs/gain, chord/alias behavior, Focus, staged deadlines,
Warp, track continuity, partitioning, overload/drain and envelope latency.

## Uninstrumented target comparison

The immutable baseline ELF is copied before the FFT source change. Candidate and
baseline use the same Buildroot GCC 13.4.0, ordinary -O3, static single-precision
NEON FFTW 3.3.10 archive, unchanged benchmark fixtures and profiling OFF.
The full foundation/pitch numerical checks finish before the timing runner.
The four sequential probes run baseline/candidate/candidate/baseline on CPU 2,
SCHED_OTHER, 48 kHz and 64/128 frames, with a four-second measured timeline per
fixture after warmup/reset. All fourteen aggregate workload rows per invocation
are retained. The runner stops/restores the live service; no firmware, presets,
clock policy, live buffers or unrelated DSP quality settings change.

Offline unpaced timing measures standalone DSP demand and callback tails, not
ALSA xruns, paced FIFO behavior or full-chain endurance. Numerical and profiling
probes do not run concurrently with this final comparison.

## Intermediate real FFT result

The first real FFT version expands inverse output back into complex vectors
before synthesis. Its matched four-run target comparison saves **5.06–6.05%**
in full-workload mean time. Static/expression mean demand falls below one period,
but freeze/gliss remains at **106.71–107.70%**, and full-path p99s still exceed
their periods. Those 56 intermediate rows are retained separately; the final
packed-synthesis comparison below uses a fresh sequential run against the same
immutable baseline. Do not combine runs from the two comparisons into one
confidence estimate or attribute an unmatched difference entirely to packing.

## Final measured result and admission

The final packed-synthesis version saves **5.83–7.48%** in full-workload mean
time, comparing the average of two candidate means with the average of two
baseline means for each workload/buffer pair. This is observed repeated-run
evidence, not a confidence interval or a universal workload claim.

| Full workload | Frames | Baseline mean period demand | Final mean period demand | Two-run mean saving | Final highest p99 µs | Final worst µs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Static filter/space/pan/Attack | 64 | 100.47–101.25% | 93.28–93.58% | 7.37% | 1863.667 | 2060.334 |
| Expression/filter/space/pan/Attack | 64 | 104.61–105.13% | 97.12–97.30% | 7.30% | 2045.555 | 2330.796 |
| Freeze/gliss/capture/assignment | 64 | 113.37–113.70% | 106.48–107.34% | 5.83% | 2166.000 | 2551.908 |
| Static filter/space/pan/Attack | 128 | 100.82–100.91% | 93.18–93.45% | 7.48% | 2805.056 | 3022.870 |
| Expression/filter/space/pan/Attack | 128 | 104.33–104.56% | 97.24–97.53% | 6.76% | 3300.129 | 3483.204 |
| Freeze/gliss/capture/assignment | 128 | 113.02–113.21% | 105.67–105.82% | 6.52% | 3204.722 | 3607.111 |

Static/expression average demand is now below one period, but freeze/gliss still
requires **105.67–107.34%**. Every tested full-path p99 exceeds its period
(1333.333/2666.667 µs). **Standalone/full-chain feasibility is still open**;
these means do not authorize public integration or a claim of live usability.
All tails remain retained in the raw comparison, including scheduler outliers.

The uninvolved granular reference's two-run mean changes range from
0.03% to 0.18% reduction. The spectral identity fixture
also uses the real FFT optimization and is not an uninvolved control.
The [final 56 timing rows and evidence](../benchmark-results/pog3-pi4-real-fft-packed-2026-10-07)
include numerical logs, source patch, ELF/artifact hashes, service and telemetry
receipts and reproducible summary arithmetic. All four invocations return zero,
with zero C++ callback allocations. The runner exits zero and restores service
with PID receipt 8867. Final comparison boundary temperatures range
58.913–60.374°C; these are samples, not a continuous peak.
Every recorded clock/governor receipt remains 1.5 GHz/performance. Firmware,
presets and live buffer settings remain preserved. Target C allocation and
additional FFTW memory are not measured by these receipts.

The [intermediate 56 rows](../benchmark-results/pog3-pi4-real-fft-expanded-2026-10-07)
remain separate. Numerical/profiling work does not overlap either comparison;
no timings from the two variants are pooled to estimate the final saving.

## Final target attribution and next work

A separate diagnostic run follows all performance comparisons. Its generated
scopes include the new private synthesis inverse so it does not disappear from
FFT attribution. All fourteen workload dispatch sums match their FFT scope
counts. Every full-feature fixture records **5247 r2c and 14984 c2r executions**,
with **zero complex or unaligned fallback executions**. This confirms use of
aligned real plans, not the instruction-level SIMD composition of each plan.
The diagnostic percentages below include instrumentation overhead and are not
admission or optimization-gain measurements.

| Workload (128 frames) | FFT | Live render excluding FFT/held | Interpretation | Attack | Freeze update | Held render | Residual |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Static | 17.63% | 31.52% | 17.75% | 18.05% | 2.66% | 0.00% | 12.38% |
| Expression | 17.23% | 28.56% | 17.16% | 17.37% | 2.71% | 5.69% | 11.28% |
| Freeze/gliss | 15.96% | 21.38% | 15.73% | 15.41% | 3.48% | 17.96% | 10.09% |

All fourteen diagnostic timing rows observe zero C++ callback allocations.
The runner and probe return zero, and the restored-service PID matches an
independent external process check. [Diagnostic logs and receipts](../benchmark-results/pog3-pi4-real-fft-profile-2026-10-07)
are retained. Retrieved temporary probes are removed after verification.

The next CPU experiment should target Hann-lobe lookup layout and held rendering.
Consecutive destination taps currently read table locations separated by 512
floats for a fixed fractional phase. Test a phase-major layout that keeps those
taps together, retaining the same prepared coefficient bits and interpolation.
For the existing 8193-entry table, logical indices 0–8191 can map to
`(index % 512) * 16 + index / 512`, with index 8192 retained as a separate final
endpoint. This is a cache-locality hypothesis, not a measured saving. It needs
independent coefficient/lookup comparisons, unchanged complete audio gates and
a fresh target A/B; do not accept only a faster microbenchmark.

Keep staged deadlines, timestamp ownership, all warm paths and buffers intact.
Rendering/held work, Attack and interpretation remain substantial target costs.
If mean capacity becomes adequate, reduce bursts and then test paced FIFO/ALSA,
intended-chain margin and thermal endurance before public M8 integration.
