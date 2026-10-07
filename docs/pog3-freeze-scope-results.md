# POG3 freeze scope decision and matched Pi comparison — 2026-10-07

The default build now defers both freeze expression modes while retaining the
complete implementation behind `ARDOR_POG3_EXPERIMENTAL_FREEZE=ON`. The default
is OFF in both the application and standalone DSP project. The reduced core
supports Off, Volume, Crossfade, Warp and Filter. It preserves the 33 parameter
indexes and all existing normalized expression values; freeze values remain
reserved rather than renumbering persisted controls.

## Default behavior and retained experiment

The default bank allocates neither the mutable freeze state nor the twelve
long renderers' held-phase arrays. It bypasses snapshot capture, stereo matching,
onset/target updates and held reconstruction. Renderer arguments select the
existing live path. FFT sizes, windows, Attack, phase histories, Warp, Focus,
latency, control cadence and frame-job scheduling remain unchanged.

A configuration selecting an unavailable freeze mode returns a clear error
before replacing the working processor. Both key and index setters refuse
unavailable modes without changing targets. These are availability semantics;
unsupported selections do not silently produce another effect. Diagnostics
report an inactive state in the default build. A constant-initialized immutable
descriptor supplies those diagnostic values without callback construction.

The opt-in build retains both freeze modes and their full audio tests. It is
an experimental build, not a CPU-admitted public effect. The normal CTest set
contains eight core DSP suites; enabling the option adds the ninth freeze suite.
The effect still has no public `mod/pog3` catalog entry.

## Matched workload and planning control

The prior static/expression/freeze fixtures use different settings and input,
so their means cannot identify the cost of cutting freeze. This comparison uses
one deterministic changing stereo chord/noise input, all six voices, Focus on,
Attack=.11, processed dry, identical filter/detune/Spread settings and the same
48 kHz / 64- and 128-frame callback sizes. The four-second timeline includes
capture, toe hold, heel release and recapture events.

The enabled build runs three cases: Off with warm preparation, stationary
Freeze Volume at unity held gain, and moving Freeze Gliss with unity held gain.
The default disabled build runs Off with preparation bypassed. Stationary and
gliss control positions differ intentionally to request those behaviors; input
and sound settings are otherwise identical. This measures these particular
performances, not a universal freeze-versus-gliss cost.

Each fixture warms for four seconds, resets, then measures four seconds. Event
receipts require actual captures in both held cases, no target assignments in
stationary hold, and multiple target assignments in gliss. Staged health and
callback allocation checks remain active. Reset validates measured events before
clearing their diagnostics.

Both builds use matching Buildroot GCC 13.4.0, ordinary -O3/-DNDEBUG, static float
NEON FFTW 3.3.10 and profiling OFF. A prepared enabled instance exports FFTW
wisdom before numerical checks. Every timing process imports that same file
before preparation. All four post-process wisdom files retain the same header
and **57 planning records**, with zero added or removed records. Text ordering
can differ; the record-set comparison is retained. This constrains planning
variation without claiming instruction-level plan identity or eliminating all
process/code-placement variation.

Runs are enabled-forward, disabled-forward, disabled-reverse, enabled-reverse.
The second pair reverses callback sizes and enabled mode order. Both builds also
run uninvolved granular and spectral identity controls on the same input.
The runner stops/restores the application, pins CPU 2 with SCHED_OTHER and runs
all probes sequentially. Numerical and diagnostic work does not overlap timing.

## Target results

Ranges retain both runs; reductions average their means. No previous fixture
or checkpoint timing is pooled into this comparison.

| Case | Frames | Mean period demand | Highest p99 (µs) | Worst (µs) | Over-period callbacks per run |
| --- | ---: | ---: | ---: | ---: | ---: |
| Default core, preparation bypassed | 64 | 87.34–87.46% | 1683.629 | 1842.277 | 1152–1170 / 3000 |
| Experimental core, Off/warm preparation | 64 | 90.78–90.91% | 1850.926 | 2069.166 | 1279–1300 / 3000 |
| Stationary hold | 64 | 92.67–93.27% | 1950.537 | 2129.796 | 1412–1425 / 3000 |
| Moving gliss | 64 | 103.30–103.31% | 2141.667 | 2549.722 | 1478–1483 / 3000 |
| Default core, preparation bypassed | 128 | 87.04–87.97% | 2684.370 | 2775.315 | 9–21 / 1500 |
| Experimental core, Off/warm preparation | 128 | 90.84–91.05% | 2757.611 | 2928.203 | 75–77 / 1500 |
| Stationary hold | 128 | 92.37–92.89% | 2978.685 | 3276.222 | 184–215 / 1500 |
| Moving gliss | 128 | 102.97–103.46% | 3176.797 | 3492.112 | 1044–1100 / 1500 |

Bypassing inactive preparation reduces matched live mean time by
**3.78–3.79%**, to **87.04–87.97%** period demand across these cases. The default
64-frame case still exceeds its period in **38.4–39.0%** of measured callbacks;
the 128-frame case does so in **0.6–1.4%**. One 128-frame p99 fits its period,
while the other is slightly above it. Both runs' maxima exceed their periods.
Average headroom now exists in this fixture; reliable live or chain admission
is not established.

The matched results also change the earlier inference about cutting only gliss:
stationary hold averages **92.37–93.27%**, while moving gliss averages
**102.97–103.46%**. The combined profile alone could not distinguish that gap.
Held population/targets and changing reconstruction can contribute; this study
does not isolate their individual costs. Stationary freeze may be a better first
feature to restore later. Both modes remain deferred while the core is optimized.

Uninvolved controls still vary. Disabled-versus-enabled granular mean reductions
are 0.032%/0.110%; spectral reductions are -0.615%/0.858% at 64/128 frames.
The matched Off saving exceeds those observed control differences, but its
precise causal magnitude remains subject to run and code-placement effects.

All four timing invocations return zero and retain **32 aggregate rows** with
zero C++ callback allocations. The runner and numerical probe return zero.
Offline over-period counts use actual per-callback wall duration against the
exact callback period before sorting; they are **not ALSA xruns**. These unpaced
SCHED_OTHER measurements do not measure paced FIFO, physical I/O or intended-chain
endurance. The full input timeline and benchmark source are reproducible.

## DSP and memory evidence

The default build passes all **eight Release core DSP suites** (46.76 s); the
experimental build passes all **nine suites**, including complete freeze/gliss
(73.59 s). Both sets retain the required-preload C allocation/free test. The
default expression/transactional lifecycle suite passes ASan/UBSan with leak
checking. Changed source/test units pass strict warnings with both option values.
The final host build is restored to OFF after validation.

Before timing, both architectures compare an enabled-Off and disabled-Off
four-second measured stereo trace after the same warmup/reset. All **384,000
float samples (1,536,000 bytes)** match byte for byte within each architecture.
The Pi's complete default pitch suite also passes. These finite traces establish
live-path compatibility for the retained workload, not every possible input.
Temporary float blobs are removed; sources, exact hashes, logs and successful
byte-comparison receipts remain retained.

Matched C++ preparation counts fall from **2,024,808 to 1,618,560 bytes**, a
**406,248-byte** reduction: 209,640 bytes of mutable freeze state and 196,608
bytes of held phase arrays. A 209,640-byte immutable inactive diagnostic
descriptor remains in read-only program storage (target ELF symbol size
`0x332e8`); it is not part of that heap counter. FFTW storage and complete resident
memory remain outside these receipts. Do not equate the heap saving with a
complete target-memory admission result.

The timing runner restores PID **14853**, confirmed by external readback.
Boundary temperature samples range **55.017–59.400°C**; all sampled
clock/governor receipts remain 1.5 GHz/performance. Firmware, configuration,
live buffers and unrelated DSP quality settings are preserved.

## Reduced-core profile and next work

A subsequent generated-source profile runs only after timing. Every measured
freeze-update and held-render scope records **zero calls**. All six FFT dispatch
sums match their scope totals. The core retains 5247 r2c and 14984 c2r calls per
fixture with zero complex/unaligned fallbacks. Instrumented 128-frame cost shares
are **32.20% live rendering**, **18.76% interpretation**, **18.73% FFT** and
**18.41% Attack**, with about 11.90% residual. These are diagnostic shares, not
gain/admission measurements. The profile returns zero, observes zero C++ callback
allocations and restores PID **15692**, independently checked.

Next, reduce core callback peaks before restoring optional features. Inspect
work at the 64-frame boundaries: analysis, interpretation and Attack updates
cluster near frame arrivals, while renderer jobs use the previous full-feature
schedule. Test redistributing whole jobs first under an exact audio oracle.
Any postponed Attack update must retain its original control sample, input
stamp and ordering across low/long/short histories. Preserve complete frame
publication, Focus/Warp histories, N+H latency and original render deadlines.
Moving work may reduce tails but cannot reduce average CPU demand.

In parallel with that investigation, prototype live accumulation layouts that
reduce repeated destination loads/stores; the existing 24-tap ARM scatter loop
is already vectorized. Retain tuning/spur/chord and Attack independence gates,
preparation bounds and callback allocation checks. Measure against this matched
reduced-core baseline using the retained wisdom and both callback sizes. After
reliable standalone headroom/tails, run paced intended-chain/thermal/xrun tests.
Public M8 integration remains blocked until feasibility is established.

[Raw comparison, numerical and profile evidence](../benchmark-results/pog3-pi4-freeze-scope-2026-10-07)
retains the patch, build/ELF/artifact hashes, shared wisdom and record-set audit,
all tail/over-period counts, numerical logs, telemetry and restoration receipts.
Run `python3 summarize.py DIRECTORY` to regenerate the summaries and
`python3 check_wisdom.py DIRECTORY` to verify the retained planning records.
The source patch hash refers to the original uncompressed bytes in
`candidate.patch.gz`.
