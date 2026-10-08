# POG3 fixed-delay worker feasibility — 2026-10-08

The fixed **two-block worker pipeline passes the tested serial POG3 + NAM/EQ
workload** on the pedal. Both 30-second runs and a 180-second run finish with zero
ALSA xruns, late outputs, submission misses or wrong output generations. Direct
serial processing still xruns after 11 callbacks; the one-block worker fails its
strict generation deadline. Keep the current pitch/Attack DSP and continue with
listening validation of **256 added samples / 5.333 ms**.

This is a diagnostic feasibility result, not deployment or all-routing admission.
The normal app and its saved settings remain installed; its service and audio
configuration were restored and independently read back after each batch.

## Fixed conditions and results

Device `192.168.88.12`, 48 kHz, 128-frame / 2.667 ms periods, 384-frame ALSA
buffers, CPU2 FIFO70 for the callback and CPU3 FIFO69 for POG3. The unchanged
reference preset is serial vibe → full NAM → EQ, with its original model.
The preset's CPU3 is available; no dual-amp worker competes for that core.
FFTW remains single-precision static NEON with the previous seed wisdom.
No NAM quality, device block size, governor or production DSP change is used.

The final batch uses one Release executable for direct/core/chain/one-block/
two-block modes. Every run has four seconds of warmup. Successful statistics
exclude warmup; failures include all completed callbacks up to the first failure.

| Mode | Measured callbacks | CPU2 mean / max, µs | CPU3 mean / max, µs | Outcome |
| --- | ---: | ---: | ---: | --- |
| Direct combined, repeat 1 | 11, partial warmup | 2563.385 / 3142.629 | — | Playback xrun |
| Direct combined, repeat 2 | 11, partial warmup | 2569.609 / 3142.333 | — | Playback xrun |
| One block, repeat 1 | 16, partial warmup | 1140.895 / 1188.000 | 1782.651 / 2682.500 | Required generation 16 late |
| One block, repeat 2 | 3688, includes warmup | 1180.619 / 1254.167 | 2305.264 / 2666.426 | Required generation 3688 late |
| Two blocks, 30 s, repeat 1 | 11250 | 1175.078 / 1261.315 | 2308.258 / 2652.741 | Pass |
| Two blocks, 30 s, repeat 2 | 11250 | 1182.989 / 1264.500 | 2309.757 / 2681.815 | Pass |
| Two blocks, 180 s | 67500 | 1160.527 / 1280.407 | 2324.920 / 2878.704 | Pass |
| Core only, 15 s | 5625 | 2167.098 / 2514.074 | — | Pass |
| Chain only, 15 s | 5625 | 959.513 / 1079.963 | — | Pass |

The passing combined runs have **zero CPU2 over-period callbacks**, zero ALSA
xruns and zero pipeline misses. Partial failure means are not steady-state CPU
comparisons. The one-block version fails even when ALSA reports no xrun: stale
output is not an acceptable substitute for a required generation.

In the longer run, CPU2 uses about **43.5%** of the callback period; CPU3's
mean POG3 job uses **87.2%**. The worker maximum submission-to-finish time is
2904.315 µs, including wakeup/copy time. It exceeds one quantum but fits within
the tested two-quantum allowance. There are **219 worker jobs exceeding one
nominal period**; none misses the required two-block output generation. Mean
throughput stays below one period, so the queue is not hiding sustained overload.
After warmup the three-minute run completes 67500 jobs; all 69000 submitted jobs
including warmup are completed at shutdown. The final two outputs remain beyond
the stopped playback timeline and are explicitly accounted for as unconsumed.

Both equal-duration short runs produce the same checksum. Their delay-compensated
audio contract is established separately by exact sample comparisons below.
The soak's thermal readbacks are 55.017°C before and 59.887°C after, both at
1.5 GHz / performance governor. Endpoint reads do not establish peak temperature
or full thermal equilibrium; three minutes is finite evidence, not endurance
certification.

## Architecture and ownership review

`ParallelStereoStageExecutor` retains its original default latest-completed
behavior. An opt-in `fixedOutputDelayBlocks` selects the exact older generation;
configuration rejects a delay larger than its prepared input ring. A nonblocking
`completedGeneration()` acquire reads published completion. Existing callers
leave the option at zero. No application routing selects the new option yet.

The diagnostic `pog3_probe::Worker` requires generation `n-delay` before each
submission `n`. A missing required output stops the measurement immediately.
It then verifies accepted submission identity and the returned output generation;
no waiting, stale replay, skipped input or bypass can qualify as success. Initial
fallback from the generic executor is replaced with explicit silence for exactly
the configured startup delay. Failures return 3 with partial traces intact.

The existing executor has two prepared input slots and three publication slots.
For two-block delay, the callback copies generation `n-2` before enqueueing `n`.
It advances the publication-consumed marker only after finishing the output copy;
the worker uses that marker before recycling a publication slot. Input-slot
recycling requires published worker completion. The adapter's two parameter
snapshot slots are reused only after the required old generation has completed.
Submission's semaphore handoff orders each complete snapshot with its input.

Only CPU3 processes the POG3 state. Each submitted block carries all 33 normalized
targets, applied by the owner before processing that block; controls are dated
to input submission, not to the later output callback. The processor retains its
existing 48-sample cadence and smoothing. Snapshot publication in a future live
host must use a separate control-target bank: do not let the worker's dated
targets overwrite newer UI targets in a shared publication bank.

CPU2 copies the complete delayed stereo POG3 output, averages it to the probe's
existing mono input boundary, then runs the reference chain. Dry and wet are
delayed together. The algorithm, NAM and chain ordering are unchanged. Callback
and worker storage is prepared before streaming. Lifecycle reset waits for
completion, resets the owner state and generations, and clears histories.
Shutdown joins the worker before its buffers/context/core are destroyed. Health
and processor counters are queried only after joining; there are no concurrent
reads of mutable core diagnostics.

Per-generation timing arrays are diagnostic storage sized before streaming.
They record submission, worker start/end, CPU time, transforms and actual CPU.
The callback never grows them. A production implementation should use bounded
counters instead of retaining a trace for the whole session. Worker job timing
includes target application and DSP; wakeup time includes dispatch/input copy.
Output publication finishes just after its recorded end, so **generation checks**,
not the work-duration column alone, decide whether a deadline passed.

## Validation

The optional Linux `pedal-pog3-pipeline-quality` checks 64/128-frame blocks with
one/two-block delays on both host and pedal. **2060 compared stereo blocks**
match direct processing byte for byte after exactly the selected delay. Cases
include distinct channels, changing Focus/Attack/Master/pan/filter targets,
control-cadence crossings, zero startup, draining and repeated reset.

A held worker forces an exact due-generation miss: the callback must reject it
before enqueuing new input. Recovery/reset must remove old generations, and
shutdown joins an in-flight job. The glibc C allocation interposer checks both
worker processing and callback exchange: **zero allocations and frees**. It also
covers C++ allocations that use libc. Preparation, thread creation and lifecycle
operations are intentionally outside this realtime scope.

Strict ARM warnings pass. The three existing flexible routing graph/program/
engine smoke suites pass with the legacy default executor policy. The retained
POG3 algorithm sources are unchanged, so its already-passed full DSP suites are
not rerun. The artifact audit independently checks summaries against raw timing
traces, complete/partial exits, every generation, CPU assignment and job totals.
Original failure traces and the initial one-block exploration are retained.

## Next steps for Luna

1. **Check playing feel with the actual 256-sample added delay.** The user accepted
   the preceding unpipelined octave audition with a slight, barely noticeable
   delay. That does not accept the new 5.333 ms. Repeat dry/up/down/blend and Focus
   comparison with Attack zero; retain a clean, immediate dry reference. Clearly
   distinguish the added pipeline delay from total physical or spectral latency.
2. If acceptable, validate this topology with the **normal MMAP backend and UI**,
   live mono guitar, the reference NAM/EQ chain and mode/parameter changes. Use
   separately published control snapshots. Track actual submitted/completed/
   consumed generations and xruns; do not treat generic bypass as a passing run.
   Prepare FFT plans and storage before playback and keep all core ownership on
   CPU3. Do not change the installed app until the temporary build is reviewable.
3. Define reset/preset-swap/stop behavior with callbacks quiesced and the worker
   drained or joined. Preserve the graph's dry/wet timing and report **256 samples
   of additional pipeline latency** at 128 frames. Public normal routing needs a
   deliberate deadline-failure policy; the probe's fail-fast policy is a diagnostic.
4. Resolve **CPU3 ownership** before offering general routing. Dual-amp and other
   workers may already reserve it. This serial preset is the measured topology;
   moving workers or admitting different chains needs its own measurement. Expand
   to live control/material sweeps and a longer thermal run after the backend test.
5. Keep freeze/gliss opt-in and public factory/catalog/scene integration pending
   these gates. Resume lower-level DSP optimization only if measured CPU3 margin
   or real-backend tails require it. The current result supports the architecture;
   it does not justify further speculative Attack layout changes.

[Raw timing, generation traces, receipts and reproduction](../benchmark-results/pog3-pi4-pipeline-2026-10-08/README.md).
