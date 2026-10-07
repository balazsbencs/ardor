# POG3 Pi 4 CPU checkpoint — 2026-10-07

The hardware test was worth running now. **The tested full FFTW processor uses
101.85–115.97% of a callback period on the pedal; the ERB Attack/freeze hybrid
uses 186.92–192.55% overall and 233.53–242.21% in its held segment.**
Neither tested full-feature configuration has standalone real-time headroom at
48 kHz / 64 or 128 frames. This is an actual capacity problem, independent of
whether the initial 25% planning target is relaxed. The bare eight-path ERB
bank uses 60.84–62.65% with no measured overruns, but lacks required Attack/freeze
and full live Focus alignment, and retains its documented audio failures.
It is not an admitted complete POG3 block.

## Device, build and measurement scope

The connected pedal reports Raspberry Pi 4 Model B Rev 1.5, aarch64,
927620 kB RAM and Linux 6.18.37-v8 SMP PREEMPT. Its running configuration is
48 kHz, 64 frames and audio CPU 2. The governor is `performance`; before/after
and monitoring samples report **1.5 GHz**. Boundary temperatures are
55.017/58.913°C; additional sampled monitoring reaches 60.374°C. These are
sampled readings, not a continuous thermal peak or a completed chain soak.

The isolated [device project](../tests/pog3-library-trial/device/README.md)
uses the existing Buildroot 2025.02.15 SDK, GCC 13.4.0 and ordinary `-O3`,
without fast-math. FFTW 3.3.10 is single-precision, static, NEON enabled and
has no worker threads/OpenMP. Its source hash matches Buildroot's manifest.
Temporary probes run only under `/tmp`; FFTW is not installed into the image.
The DSP/benchmark source corresponds to **`abbb569a`**, with the retained file
and ELF hashes in [the evidence directory](../benchmark-results/pog3-pi4-2026-10-07/).
The committed runner additionally polls for the post-start PID receipt; the
uploaded runner's actual hash is retained separately from the matching DSP.

The live pedal service is stopped during numerical/timing work and restored
by the remote EXIT trap. No firmware, preset, governor or live callback setting
is changed. All benchmark processes inherit affinity **CPU 2**. The recorded
final run spans 08:57:32–09:13:16 UTC. Other OS activity is not claimed eliminated.

These are **unpaced standalone CPU measurements under SCHED_OTHER**. A FIFO
process that runs offline continuously can hit Linux RT-bandwidth throttling;
normal scheduling avoids that artificial unpaced-loop failure. All observed
OS/preemption tails are retained. Results do not measure ALSA xruns, actual
FIFO callback behavior or a NAM/cabinet/reverb chain. A paced runtime test could
change scheduling tails; this data does not demonstrate the headroom needed
for the tested full-feature configurations.

## Numerical checks and ARM correction

The existing FFTW foundation/reference suite passes on the Pi, including its
active transform accuracy/identity and independent-plan checks. The ERB
Attack/freeze feature suite passes with `--short-hold`: the held recurrence
fixture is four seconds, with all other feature fixtures retained. It checks
exact Attack-off output, resolved-note Attack, actual stationary freeze under
replacement input, moving gliss, Volume/dry eligibility, reset and partitioning.
The two-minute renderer-only result remains a host Release observation.

The first ARM attempt fails **exact Attack-off equality** before timing.
The fix routes zero-second Attack through the raw `ErbCadenceBank` specialization,
which removes that demonstrated difference and passes the unchanged equality
check on ARM and host. Different contraction/rounding in a multiply-by-one
specialization is a plausible explanation; it is not independently proven by
an instruction-level experiment. No equality tolerance is weakened. The first
attempt and its failure log are retained in the local artifact directory.

The host's three earlier foundation suites remain unchanged. All four optional
host suites pass after adding the device build (16.37 s); the final exact-off
correction passes the feature suite again (11.39 s). Both changed C++ units and
the affinity launcher pass strict warnings. The separate host feature sanitizer
result precedes this small exact-off correction and uses the shortened hold.

## All retained timing results

There are **72 stage invocations**, two passes in fully opposite case order,
plus **28 full-suite rows** from two complete fourteen-workload invocations.
Every stage receipt returns zero, every stage fault counter is zero, and paired
stage output checksums match. All feature cases perform 5247 transforms;
freeze/gliss capture once, and event gliss assigns four targets. All analysis
and all eight live ERB paths continue through held work. Capacity fallback
counters are retained in paired logs; zero faults does not mean zero fallback.
For example, ARM event active stages record 23397 capacity events, compared
with 23400 on the host. This is not complete audio/family admission.

Each stage invocation warms four seconds, resets state, then measures the
same four-second timeline. Static/dynamic feed the dense chord/noise fixture;
events use repeated alternating chord onsets. Freeze/gliss are at heel for one
second, capture/hold or glide for two seconds, then release for one second.
Dynamic Warp updates once per callback, as in the host comparison.

Ranges below span the two passes and all three workloads for that buffer size.
Each stage row aggregates six runs. Overruns mean measured callback durations
above 1333.333 µs (64 frames) or 2666.667 µs (128 frames); they are **not ALSA
xrun counts**. Highest p99 is the largest per-case p99, not a pooled percentile.

| Backend | Frames | Mean period demand | Highest case p99 µs | Worst callback µs | Overruns / callbacks |
| --- | ---: | ---: | ---: | ---: | ---: |
| ardor | 64 | 94.81–97.01% | 1941.630 | 2143.370 | 7674 / 18000 |
| ardor | 128 | 94.90–97.37% | 3100.740 | 3384.300 | 2987 / 9000 |
| erb-cadence-count-32 | 64 | 60.84–62.53% | 850.037 | 904.445 | 0 / 18000 |
| erb-cadence-count-32 | 128 | 60.95–62.65% | 1693.630 | 1730.720 | 0 / 9000 |
| erb-effects-ownership-32 | 64 | 93.41–95.78% | 1906.650 | 2055.320 | 7499 / 18000 |
| erb-effects-ownership-32 | 128 | 92.38–95.53% | 2915.410 | 3046.680 | 1403 / 9000 |
| erb-effects-attack-32 | 64 | 136.08–138.18% | 3563.830 | 3750.200 | 12313 / 18000 |
| erb-effects-attack-32 | 128 | 134.87–137.69% | 4567.520 | 4777.190 | 8973 / 9000 |
| erb-effects-freeze-32 | 64 | 186.92–192.55% | 5051.430 | 5233.000 | 15319 / 18000 |
| erb-effects-freeze-32 | 128 | 187.57–191.25% | 7434.810 | 7611.980 | 8974 / 9000 |
| erb-effects-gliss-32 | 64 | 187.77–192.38% | 5087.810 | 5353.330 | 15304 / 18000 |
| erb-effects-gliss-32 | 128 | 187.39–192.30% | 7507.940 | 7621.980 | 8974 / 9000 |

| Full processor workload | Frames | Mean period demand | Highest case p99 µs | Worst callback µs |
| --- | ---: | ---: | ---: | ---: |
| granular_reference_10_shifters | 64 | 7.01–7.07% | 349.518 | 361.574 |
| granular_reference_10_shifters | 128 | 7.00–7.01% | 413.130 | 443.463 |
| spectral_identity_focus_transition | 64 | 27.56–27.72% | 1307.889 | 1655.593 |
| spectral_identity_focus_transition | 128 | 27.52–27.57% | 1325.889 | 1557.741 |
| spectral_pitch_bank_warp_focus | 64 | 96.84–96.85% | 1932.704 | 2105.630 |
| spectral_pitch_bank_warp_focus | 128 | 96.57–96.83% | 3051.945 | 3281.259 |
| spectral_pitch_bank_attack_warp_focus | 64 | 93.34–93.79% | 1882.944 | 2002.593 |
| spectral_pitch_bank_attack_warp_focus | 128 | 93.56–93.77% | 2884.611 | 3144.759 |
| static_sound_path_filter_space_pan_attack | 64 | 102.07–102.50% | 2004.222 | 2183.871 |
| static_sound_path_filter_space_pan_attack | 128 | 101.85–102.44% | 3066.852 | 3221.685 |
| expression_modes_filter_space_pan_attack | 64 | 106.01–106.08% | 2205.315 | 2394.204 |
| expression_modes_filter_space_pan_attack | 128 | 105.74–106.68% | 3567.074 | 3887.148 |
| freeze_gliss_capture_assignment_filter_space | 64 | 114.50–115.48% | 2297.370 | 2713.907 |
| freeze_gliss_capture_assignment_filter_space | 128 | 114.78–115.97% | 3504.204 | 3882.204 |

| Held trial | Frames | Middle-two-second mean demand |
| --- | ---: | ---: |
| erb-effects-freeze-32 | 64 | 233.53–241.85% |
| erb-effects-freeze-32 | 128 | 234.17–240.67% |
| erb-effects-gliss-32 | 64 | 234.83–242.21% |
| erb-effects-gliss-32 | 128 | 234.61–242.21% |

The `ardor` stage control is the **pitch bank**, with six long plus two short
paths and zero-second ownership; it does not enable full filter/space/expression
or active freeze. The separate full-suite rows do exercise the existing complete
processor. Their source fixtures and control timelines differ from the ERB
stage cases, so this is a capacity comparison, not an identical-feature A/B.
Full-suite order repeats unchanged; only the stage cases reverse order.
Full-suite timing consumes output and verifies staged health/capture/assignment;
all 28 rows observe zero **C++** callback allocations. C/FFTW allocation and
complete target heap usage are not measured in this device run.

The production processor's three complete workloads exceed 100% mean demand
at **both** callback sizes. Larger buffers reduce some relative burst costs,
but do not remove the measured average deficit. The ERB hybrid is more expensive
in these configured trials despite omitting post-filter/space/output stages.
Its middle-two-second demand is substantially higher than the overall mean.
Neither a 25% goal change nor dismissal of rare outliers explains these averages.

## Receipts and service restoration

The runner's overall exit is **1**, after all numerical and timing work completes:
the init script returns before the background pedal child has exec'd, so its
immediate `pidof` receipt fails. The service-start log and external follow-up
confirm restoration, PID 25573, a sleeping live process and refreshed telemetry.
The committed runner fixes that receipt race with a bounded ten-second poll;
that orchestration change does not alter DSP timing. No timing failure is hidden
as a successful whole-run exit. The initial numerical-failure attempt also
restores the service.

Raw CSVs, receipts, numerical logs, source/ELF hashes, restoration verification
and run notes are checked in under
[`benchmark-results/pog3-pi4-2026-10-07`](../benchmark-results/pog3-pi4-2026-10-07/).
All individual logs/CSVs and the original uploaded runner remain locally in
`build-pog3-library-trial/pi4-20261007/final`; the first attempt is retained in
its parent directory. No binary is committed. Validate/summarize retrieved data:

```sh
python3 tests/pog3-library-trial/device/summarize.py build-pog3-library-trial/pi4-20261007/final build-pog3-library-trial/pi4-20261007/summary
```

## Next CPU work

Keep the existing full FFTW processor as the baseline for **target profiling**.
The host profile identifies rendering, interpretation and Attack as candidates;
measure their exclusive costs on the Pi before assuming identical bottlenecks.
Use the retained full processor and stage fixtures unchanged as controls.
Separate average work reduction from staging of frame bursts; both are needed.

For the ERB hybrid, separately price active ownership versus gain projection and
held-carrier work on the Pi. The repeated per-partial/per-band response calculation
is an optimization candidate: investigate prepared/cached transfer weights with
explicit moving-frequency and gain-error bounds. Do not equate the total active
Attack increment with mapping cost without profiling. Evaluate SIMD/shared voice
work or suitable multirate tiers against the cycle/phase and transient controls.
The raw 61% bank saving alone does not establish a complete backend advantage.

Do not silently drop voices, stop warm analysis during holds, reduce unrelated
NAM quality or increase the live buffer. A deliberate supported lower-cost
configuration can be assessed separately, with its feature limits documented.
Preserve the DSP contracts while optimizing; raw ERB close-note/stereo/spur
and capture/release coherence work remains open.

The 25% figure remains a **chain-budget planning target**, not a universal
usability limit. After a candidate has credible standalone margin, test the
intended real chain under its actual scheduler and perform the thermal/xrun soak.
That test is deferred here because the measured full-feature configurations
already lack standalone capacity. M8 public integration remains blocked on
CPU feasibility, not on satisfying 25% specifically.
