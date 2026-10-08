# POG3: actual-device headroom and hardware profile

Date: 2026-10-08. DSP baseline: `81b6187408e141c71f520c507c41d6d5d6afc85a`.
Production DSP is unchanged by this investigation. The new executable is an
opt-in diagnostic, excluded from the default build and pedal installation.

**Decision: pause the compact Attack support-key experiment.** The current
dense POG3 configuration cannot run serially before the reference NAM chain at
the device's existing audio settings. Both attempts encounter a playback xrun
after 11 callbacks. The next experiment should test an explicit POG3 worker
pipeline, with measured generation deadlines and added latency, before doing
more small layout changes or public effect integration.

## What actually runs on the pedal

The device is a Raspberry Pi 4B, 48 kHz, **128 frames per callback**, with a
384-frame ALSA buffer. One callback period is **2666.667 µs**. The installed
audio thread uses CPU2 and FIFO priority 70. The existing governor is performance
at 1.5 GHz. Neither the buffer size, governor nor NAM quality was changed.

The saved boot preset is a serial **vibe → full-size NAM → EQ** chain, with its
compressor disabled. Its model is `Supro_1695TJ-P12Q_M201b_421738.nam`, with
`useNano: false`. Preset/model hashes and a copy of the preset are archived.
The saved preset is the reference workload; a live UI selection was not read
back, so the installed-app observation alone does not prove its selected preset.

The installed application reports about **0.98 ms** average processing during
a 20-second observation, without new over-budget callbacks or gaps. Its audio
thread occupies about 38.6% CPU. The probe's reference-chain measurements agree
closely, although this agreement is not a proof that both application states match.

## Measured results

All final modes use the **same executable**, FFTW wisdom and deterministic input.
Core configuration enables all six dry/voice levels at 0.25, Focus 1, Attack 0.11,
dry Attack/filter/detune, Detune/Spread/Filter Q 1, and expression Off.
Experimental freeze/gliss is compiled out. Four seconds of continuous processing
precedes each complete measurement; there is no reset at the measured boundary.

The following are ordinary, unprofiled measurements:

| Mode | Measured callbacks | Mean µs | p99 µs | Maximum µs | Over period | Actual xruns |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Reference chain, first | 11250 | 974.992 | 1015.000 | 1140.870 | 0 | 0 |
| POG3 alone, first | 11250 | 2154.634 | 2416.944 | 2725.222 | 1 | 0 |
| POG3 alone, repeat | 11250 | 2174.451 | 2417.315 | 2554.167 | 0 | 0 |
| Reference chain, repeat | 11250 | 955.684 | 983.759 | 1125.259 | 0 | 0 |
| Combined, **unpaced**, first | 4500 | 3416.230 | 3738.185 | 3875.130 | 4500 | Not applicable |
| Combined, **unpaced**, repeat | 4500 | 3486.757 | 3820.722 | 4019.648 | 4500 | Not applicable |

Each standalone/chain row covers 30 seconds after warmup. Both complete POG3
runs produce the same checksum, as do both chain runs and both unpaced combined
runs. A paced no-op measurement averages 4.577 µs, without xruns or missed periods.

The two **ALSA-paced combined attempts fail during warmup**, after 11 callbacks
and roughly 35 ms of loop time. Each records one playback xrun and six
over-period callbacks. Their partial means are deliberately excluded from the
steady-state table. A first-xrun stop preserves the lead-up without entering
codec recovery. These are real ALSA xruns, not inferred from a timing threshold.

Core alone uses about **81.17% of a period on average**. No xruns in two short
runs is encouraging for isolated operation, but it is not a thermal/endurance
qualification. Its occasional period overrun and approximately 0.25 ms p99
margin leave little room for callback overhead or another substantial effect.
The combined unpaced rows measure CPU demand only; their zero xrun fields do
not mean the chain works in real time.

Temperature was 53.6 °C before the final batch and 54.5 °C afterward. Recorded
frequency stayed 1.5 GHz. The normal application was restored and its PID and
128/384-frame ALSA configuration independently read back.

## A useful budget, rather than another tiny candidate

The repeated paced means average **2164.542 µs for POG3** and **965.338 µs for
the chain**. Subtracting the latter from the available period gives a first
planning estimate:

| Proposed remaining margin | POG3 allowance µs | Reduction from isolated POG3 mean |
| --- | ---: | ---: |
| 0% — fits the average only | 1701.329 | 21.4% |
| 10% | 1434.662 | 33.7% |
| 20% | 1167.995 | 46.0% |

These are planning scenarios, not a newly imposed 20% requirement. The old 25%
whole-effect target remains nonbinding. Passing depends on the intended chain's
actual deadlines, sound and acceptable latency.

The measured combined mean is **3451.494 µs**, above the sum of isolated means.
Different input to the downstream chain, memory/cache interaction, and paced
versus unpaced execution have not been separated. Do not assume additive costs
or call this difference a measured cache penalty. Using the observed combined
mean directly requires **22.74% less total serial work merely to fit on average**,
or **38.19% less for a 20% margin**. Tail and endurance requirements come on top.

This scale of reduction is the reason to stop treating sub-percent differences
as the next feasibility milestone. The previous skipped-sort experiment's
uninvolved spectral control improved more than its full-core mean; its reported
0.562% at 128 frames is an observation, not an isolated, established sort benefit.
Its small semantics-preserving patch remains retained; this investigation makes
no new claim about its speedup.

## Fresh hardware profile

Linux PMU access works without installing `perf`. A separate 20-second paced
run samples user-space CPU cycles every 1000003 events. Of **24141 samples**,
**24116** fall inside measured DSP windows and 25 outside. There are **zero lost
samples**. Non-PIE executable symbols and the target libm dynamic symbols resolve
sample PCs. Timestamps use the same monotonic clock as callback records.

| Sampled symbol group | Share of DSP-window samples |
| --- | ---: |
| Pitch renderer | 19.93% |
| Polyphonic Attack and its named sorting helpers | 17.77% |
| Spectral synthesis / overlap-add, excluding called FFT codelets | 12.78% |
| Pitch-frame interpretation and its named sorting helpers | 10.21% |
| libm routines, callers not distinguished | 9.39% |
| Spectral analysis, excluding called FFT codelets | 4.00% |
| Remaining FFT codelets, orchestration, voice stages and other work | 25.92% |

These are **exclusive sampled instruction locations**, not inclusive stage
timings or a call graph. In particular, do not charge all libm work to Attack,
or interpret the synthesis row as including FFT execution. `Attack::group`
itself is only **4.00%**; compact support-scoring read keys would affect a subset
of its work. `Attack::update` is 7.09%, and `Attack::owner` is 2.82%. The largest
individual function is the pitch renderer, not Attack grouping. Inlining, PMU
skid and sampling variance limit finer interpretation.

A separate 20-second paced counter run gives:

| Hardware measurement | Result |
| --- | ---: |
| User cycles | 24183395034 |
| Retired instructions | 29669222509 |
| Instructions per cycle | 1.227 |
| L1 data-cache accesses | 8779264884 |
| L1 data-cache refills | 154100264 |
| Refills / accesses | 1.755% |
| Branch mispredictions per 1000 instructions | 5.262 |

All counters have equal enabled/running time: no measured multiplexing.
Counter scope includes the measured loop's user-space I/O/recording overhead;
kernel work and other threads are excluded. These figures **do not establish
memory bandwidth or cache misses as the dominant bottleneck**. An L1 refill
ratio is not a stall-cycle attribution; L2/DRAM latency and joint NAM execution
remain unmeasured. Further memory work needs a demonstrated hot loop and useful
projected payoff, not just a smaller structure.

The sampling and counting runs have identical checksums at equal duration.
Their means are 2187.827 and 2174.315 µs; ordinary timings above remain the
feasibility evidence. Profiling results are not substituted for ordinary tails.

## Next bounded experiment for Luna

1. **Keep this DSP baseline.** Make a diagnostic mode that pipelines the entire
   stereo POG3 block on CPU3, while CPU2 processes the reference chain from the
   previous completed POG3 generation. Prepare all storage and POG3 state before
   streaming. One worker exclusively owns that state. This tests a substantial
   change in the serial critical path without changing the pitch/Attack algorithm.
2. Inspect and reuse `ParallelStereoStageExecutor` where its generation and
   ownership behavior fits. It already provides preallocated stereo buffers,
   worker CPU/priority setup and submission/underflow counters. Do not assume its
   latest-completed-output policy guarantees a fixed one-block delay. Require
   generation `n-1` at callback `n`, or count a miss; no stale replay or bypass
   may silently pass the benchmark. The selected serial preset leaves CPU3 free;
   general routing may already own that core, so this is not a public integration.
3. Price **one additional 128-sample quantum: 2.667 ms** at this device setting.
   Preserve the POG3 dry/wet alignment by delaying its complete output together.
   Compare direct output against the worker output with exactly that delay,
   including startup/drain. If the executor needs a larger fixed delay, report
   and test it explicitly. Do not change ALSA periods or NAM quality to make it pass.
4. First validate offline generation order, identical samples after latency
   compensation, zero callback allocations, ownership/reset/shutdown and
   parameter snapshots dated to submitted input. Then measure the actual worker
   run with the same preset, fixture, executable and wisdom, alongside direct
   controls. Report CPU2 callback tails, CPU3 work/wakeup time, accepted/completed
   generations, submission misses, late/missing outputs and physical xruns.
5. The idealized critical-path estimate becomes closer to `max(POG3, chain)`
   than their serial sum, approximately 2.16 ms versus 3.45 ms here. This is a
   hypothesis, not a measured speedup: shared caches, dispatch and the observed
   POG3 tail can consume its limited margin. Only extend to a thermal soak and
   representative material/control sweeps if the short paced test passes.
6. If it fails, use measured worker deadline slack and sampled renderer/synthesis
   costs to choose a larger DSP change. An Attack support-key layout is not the
   default next candidate. Keep freeze/gliss and public factory/catalog/scene
   integration pending demonstrated feasibility and latency acceptance.

## Scope and reproduction limits

The probe uses real ALSA capture/playback as its clock, FIFO70 on CPU2, and the
real preset loader, NAM and engine. Capture samples are discarded in favor of
a deterministic synthetic fixture; hardware playback receives silence. Combined
mode places POG3 before the preset and averages its stereo output into the
existing engine's mono input. It is not yet the general public block routing.

The probe uses ALSA **RW_INTERLEAVED** while the installed application uses
**MMAP_INTERLEAVED**. It omits the UI and production backend wrapper. Initial
chain agreement and the no-op control support the measurement, but a future
admission still needs the actual application backend and intended inputs.
No listening judgment or all-presets/all-controls feasibility is inferred.

The initial probe link omitted NAM static registrars and failed to load the
reference model. Direct linking of `ardor_dsp` objects, as the application does,
corrected it. A subsequent automatic-xrun-recovery attempt exposed codec prepare
sleeps that repeatedly overran the opposite stream; it was terminated and the
service restored. Final runs stop at the first xrun. A missing `taskset` utility
was resolved by setting affinity in the probe itself. These setup attempts are
archived separately and excluded from the final table.

[Raw results, receipts, symbol tables, wisdom and reproduction scripts](../benchmark-results/pog3-pi4-headroom-2026-10-08/README.md).
The probe passes the target Release build and strict GCC warnings. No production
DSP changed, so earlier DSP quality suites were not rerun for this diagnostic.
