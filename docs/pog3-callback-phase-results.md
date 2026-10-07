# POG3 callback-phase profile — 2026-10-07

The Pi profile identifies analysis/Attack plus long-render collisions as the next
bounded CPU experiment. Four-source accumulation remains the renderer. This
checkpoint adds standalone diagnostic copies, not production timers or a CPU
gain claim. Baseline is `2e7efdb35add6e99223d0f48f35078df4387534f`.

## Diagnostic contract and verification

`ARDOR_POG3_CALLBACK_PROFILE=ON` generates separate bank, FFT stream and benchmark
sources. It is OFF by default and mutually exclusive with the older frame-job
profile. Fixed single-owner event storage records sample/frame stamps,
resolution, stereo/voice identity, actual Attack control bits, and nested
inclusive/exclusive nanoseconds. Warmup recording is disabled; records are
formatted after processing. Nested inverse cost is subtracted from exclusive
render cost, avoiding double counting. Attack and interpretation bodies remain
unchanged. The ~3.5 MiB fixed diagnostic BSS is outside the prepared heap counter;
it is not explicitly prefaulted during warmup. Timer, record-store, code-placement
and page effects mean these durations are attribution evidence only.

The unchanged normal ARM benchmark rebuild is byte-identical to the retained
four-source ELF (`34b2090e...`). Host and Pi diagnostic output matches ordinary
output byte for byte across 552,960 actual-renderer samples and 384,000 full
processor float samples, separately per architecture. The full host sanitizer
trace records 43,084 stage events without ASan/UBSan/leak failures. Generated
bank/FFT units pass strict host/AArch64 warnings. The Pi pitch/Focus/Warp suite
passes before profiling. No production DSP source changes at this checkpoint.

For each 64/128-frame core stream the parser independently accounts for all
43,084 records: 5,247 forward transforms and interpretations, 14,984 renders and
inverse transforms, and 2,622 Attack calls. It checks every callback and nested
stage sum, output aggregate, expected job age/deadline, original frame/control
stamp, one inverse per renderer, and identical ordered event metadata across
callback partitions. The processor remains healthy with zero C++ callback
allocations and 1,618,560 prepared bytes. FFTW/shared diagnostic storage remain
outside that counter. Imported/exported wisdom retains its header and all 57
planning records; this does not establish instruction-level plan identity.

## Measured phase costs

On Pi 4B, CPU 2, SCHED_OTHER, performance governor at 1.5 GHz, the unchanged
moving-chord/noise Off core fixture has the following **instrumented** 64-frame
callback distribution. Each phase has 375 callbacks; phase is callback end
sample modulo 512, not renderer age.

| End phase | Mean µs | p99 µs | Maximum µs | Over 1333.333 µs |
| --- | ---: | ---: | ---: | ---: |
| 0 | 1488.427 | 1656.910 | 1709.460 | 363 |
| 64 | 1541.317 | 1808.930 | 1918.740 | 371 |
| 128 | 830.985 | 949.871 | 990.389 | 0 |
| 192 | 969.739 | 1097.610 | 1201.680 | 0 |
| 256 | 1346.234 | 1533.390 | 1643.650 | 212 |
| 320 | 1371.511 | 1628.500 | 1702.350 | 238 |
| 384 | 790.772 | 906.685 | 979.685 | 0 |
| 448 | 954.392 | 1089.870 | 1154.350 | 0 |

All 1,184 over-period callbacks lie in four phases. The worst callback has five
transforms; the maximum transform count is eleven. Counting FFTs alone therefore
misses the costly combinations. Instrumented core means are 1161.672/2320.268 µs
at 64/128 frames; the 128-frame p99 is 2628.463 µs, maximum 2757.259 µs, with 6/1500
over period. These are not comparable optimization gains or ALSA xrun counts.

At phase 64, mean exclusive long Attack is 409.980 µs, short Attack 209.350 µs,
low Attack 61.237 µs and interpretation 363.036 µs. At phase 0, interpretation
is 371.424 µs and long rendering excluding inverses is 307.378 µs. Individual
long-render inclusive means vary from 102.411 to 146.740 µs across voice/channel
jobs. Complete primary interpretation averages 283.113/274.715 µs left/right;
long/short/low Attack averages 414.938/213.981/61.400 µs. The forward/inverse labels
include dispatch packing/checks/scaling, not just FFTW codelet execution. Full
stage, job and callback distributions are retained in the CSV artifacts.

## Selected next experiment and ownership review

The replay subtracts recorded inclusive long-render costs from their original
callbacks and assigns those same costs to new due ages. It preserves total work
and the twelve-job voice/channel order. It enumerates 455 monotonic four-bucket
layouts and ranks over-period count, p99 and maximum. No cache, changed-job-cost,
instrumentation or differing workload behavior is modeled.

The first candidate is `{65,80,96,111,127,129,141,153,166,178,191,193}` versus
`{17,38,81,102,129,145,161,177,193,209,225,241}`. Modeled over-period count falls
from 1184 to 191/3000, p99 to 1481.482 µs and maximum to 1681.129 µs. The remaining
peak shows why scheduling alone cannot establish admission. These are replay
predictions, not observed candidate performance.

Only renderer due ages may change initially. Keep `256 - longAge_` publication
staging; complete jobs before 256, with earliest job after freeze update at age
9. Retain the six voices' left-before-right order and every per-renderer history
order. Primary and low frames/gains remain unchanged throughout this interval;
low Attack completes before long Attack. `frameWarp_` was captured at the long
boundary. Focus is mixed at output pop, independently of job time. Freeze held
bands, mix/dryMix/gain change on its unchanged update, not in intervening control
setters; paired left renderer data must still precede its right reference.

Verify these claims with baseline/candidate byte traces under rapid controls at
48/128/256/512 boundaries, reset/startup/drain and both build modes. Run default
and opt-in DSP suites and healthy/deadline/allocation checks. Only then use
ordinary OFF-profile shared-wisdom ABBA hardware probes and confirmation to judge
actual tails and mean demand. Reject altered output or unstable tail gains;
retain all evidence. Do not move Attack/interpretation or change buffers in this
experiment. If it is insufficient, primary interpretation and Attack remain the
measured next targets; they need separate timestamp/history-preserving designs.

## Receipts and limits

[Retained artifacts](../benchmark-results/pog3-pi4-callback-phase-2026-10-07/)
include the exact generator, build context, generated-source/ELF hashes, full
compressed event logs, linear accounting parser, replay model, numerical receipts,
wisdom and service metadata. The first host attempt imported ARM wisdom and was
rejected before tracing; corrected host evidence uses host wisdom. It contributes
no timing. Both target probes and runner return zero. The live service is restored
and independently read back as PID 7572, unchanged after removing the unique
`/tmp` probe directory. No firmware, configuration, governor, live buffer, NAM
quality or public integration change occurs. Instrumented standalone processing
cannot certify paced FIFO, full-chain endurance or listening quality.
