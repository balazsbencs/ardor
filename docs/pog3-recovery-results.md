# POG3 recovery, CI and hardware endurance

2026-10-12. Source `44f4c950` is deployed in the normal pedal app and manager.
The DSP algorithms and healthy-path output are unchanged; this adds a bounded
worker failure/recovery policy to the prepared host.

## Recovery contract

The callback continuously retains a fixed 256-sample dry history. On a missing
due generation or rejected submission it stops dispatch, increments the fault
count and fades to delayed dry over 128 samples. A short transition from the
last output sample avoids a hard output discontinuity; it does not replay a
stale processed block. Once an acquire read proves every old job completed, a
new input publication marks the restart generation. The worker applies that
input's latest target snapshot and resets its own core at that generation.
Executor generation numbers never rewind during automatic recovery. Historical
publications before the restart generation are consumed/discarded during
startup, then a 960-sample crossfade returns to fresh processed output.

The callback does not wait, reset the core, allocate, destroy a thread or
rebuild the chain. Scenes, other effects, looper state and target publication
remain intact. The worker's core reset retains all prepared capacities.
Completion of all old jobs also makes snapshot reuse and publication of the
restart marker ordered before any new worker access.

One automatic restart is allowed per explicit reset/preset load. A second
failure remains in delayed dry bypass and emits an on-device status asking for
preset reload. An invalid quantum bypasses without an automatic retry. A
permanently stalled worker therefore keeps the dry path audible without a
callback wait or an infinite retry loop. Normal teardown/reset still requires
the existing quiesced lifecycle boundary. Fault counters remain cumulative
within the processor instance; successful recovery does not erase them.

Telemetry exposes `pog3_recoveries` and `pog3_bypassed`, and the normal UI reports
recovering, recovered or bypassed status. The performance script rejects POG3
bypass and any new worker fault during the sample. A recovery is an audible
failure policy, not evidence that the original deadline was met.

## Tests and CI

The production hosted adapter is tested at 32/64/128 frames with a real worker
held by a test-only, thread-directed signal handler. The caller keeps running;
there is no production stall hook. Tests verify delayed dry against an
independent changing-input history, latest controls after restart, fresh
processed output rather than old publications, one restart, repeated-failure
bypass, no further dispatch after bypass, and explicit-reset recovery.
A global libc interposer observes zero allocations/frees during the initial
fault, worker reset and fade back. Healthy output remains exactly equal to the
direct core shifted by 256 samples (worst error zero), including automation.
Both host and Pi pass. Strict new-adapter/test warnings pass.

All **75 host tests** pass. CI passes for the C++ engine, LVGL UI, both managers,
documentation, Mac/Linux desktop and Linux/Mac/Windows CLAP ABI/validator checks.
The generic Daisy tests now use actual supported choice endpoints; POG3 input
headroom accommodates its summed voices and input/master gain without relaxing
output bounds. Freeze remains unavailable in the default build.
Desktop/plugin CI builds pinned FFTW 3.3.10 as a static float/PIC library with
threads/OpenMP disabled. Windows also matches the plugin's static MSVC runtime.
Packages include the FFTW notice. Two annotations on local benchmark-selected
output filenames are recorded as false positives; the operator's intended file
output contract is unchanged. All checks on `44f4c950`, including that gate,
are green. The raw check snapshot is retained with the evidence.

## Actual MMAP/UI hardware result

A temporary data root retains the device's compressor, Vibe, Supro NAM and EQ,
with one POG3 prepended. The full fbdev app uses MMAP_INTERLEAVED S32_LE stereo
at 48 kHz / 128-frame period / 384-frame ALSA buffer. Outputs are muted during
automatic stress tests and the installed app/output state is restored afterward.
Saved presets/settings are never used as test output destinations.

Four steady samples total **600 seconds**: 120 seconds baseline, 120 seconds
maximum Attack/doubling/Spread and resonant filtering, 120 seconds Focus/Warp,
and 240 seconds Focus/filter expression/envelope extremes. All six levels are
0.7 and master is 0.2; normalized profile overrides are recorded. Every sample
checks actual CPU2 FIFO70/CPU3 FIFO69 scheduling and selected presets. The final
steady sample ends at **230,327 callbacks**, with zero overruns, gaps, worker
misses/recoveries/bypass, nonfinite output or quantum mismatches. Mean callback
time is 1.18–1.19 ms; cumulative maximum is 1.63 ms against 2.667 ms. CPU2 busy
is 47.7–48.5%; CPU3 is 86.6–88.5%. Maximum sampled temperature is 64.8 C, with a
constant 1500 MHz clock. Core busy percentages are not worker per-job timing.

Captured input peaks during this automatic run range from **−38.8 to −34.6
dBFS**. This checks endurance and these settings under the observed quiet live
input; it does not certify worst-case dense, loud playing. Dense-input DSP
correctness remains covered separately by the existing core suites.

The first scene command was incorrectly sent to the preset command directory,
so the combined runner returns 1 after all four passing steady samples. This
harness failure and log are preserved. A separate corrected run uses the live
command directory and verifies B/C/D/A recalls, including POG3 local bypass and
resume. It finishes at **18,503 callbacks**, with zero faults and return code 0.
No steady stress run is repeated merely to hide that harness failure.

## Deployment and remaining scope

Normal app and manager report `0.0.0-pog3.dev.20261012 (44f4c950)`. Exact binary
hashes, manager assets, 48k/128/384 MMAP settings, advancing clean callbacks,
codec `on,on`, relay `255`, unchanged preset/settings hashes and read-only root
are verified. Previous executables remain backed up at
`/opt/ardor-pedal/pog3-deploy-backup-20261012-44f4c950`. Base image/update metadata
is unchanged. No test preset is installed in the user's normal data root.

Keep 128 frames. One serial POG3 is supported; Dual Amp/Rig, WDW and a second
instance remain excluded. Dense playing/extra-effect CPU extremes, 32/64-frame
live performance, freeze/gliss and remaining calibration remain separate work.
[Raw checks, timing, scene state and deployment receipts](../benchmark-results/pog3-recovery-2026-10-12/README.md)
record this milestone.
