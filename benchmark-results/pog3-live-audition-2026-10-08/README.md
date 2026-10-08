# POG3 live-audition preparation — 2026-10-08

[Controls and listening procedure](../../docs/pog3-live-audition.md).

The current processor is staged on the device as a **temporary, inactive live
audition**. The user will play later. No subjective playing-feel result has been
collected, no audible audition has been started, and the normal service is active.
All preparation runs below send zero samples to physical playback.

The diagnostic uses the existing DSP, 48 kHz, 128-frame callbacks and a 384-frame
ALSA buffer. Input channel 0 is copied to both effect channels and hardware
outputs. Attack, modulation, spread, expression and filtering are disabled;
Focus initially off. Footswitches select dry, octave up, octave down and blend.
The encoder selects Focus. There is no NAM or additional worker pipeline.

## Retained final checks

- `final-device-check.txt`: the staged executable's offline device self-check
  and successful final silent runner. Immediate dry identity has maximum error
  **3.42673e-09** against the independent 0.7x input reference. All four modes at
  both Focus settings have finite nonzero output and healthy DSP. Signed PCM
  conversion and the fixed output ceiling pass.
- `status.json`: final **4500 callbacks / 12 seconds**, mean **2074.19 µs**,
  maximum **2756.48 µs**, **four over-period callbacks**, **zero ALSA xruns**.
  These are live-capture preparation timings, not a deterministic CPU comparison
  or an endurance admission. There is no claim about guitar material or feel.
- `silent-control-trace.txt`, `audition.log`: actual command changes to octave
  up, down, blend and Focus on while the audio stream runs. Physical footswitch
  event devices are opened and identified; a user's actual switch presses await
  the later audition.
- `return-code.txt`: successful zero exit. `service-before.txt`,
  `service-restored.txt`, `device-readback.txt` and `service-restart.log`: automatic
  normal-service restoration, original 128/384-frame MMAP settings, relay enable
  and independent process/readiness readback.
- `session-sha256.txt`, `source-sha256.txt`: exact staged executable/wisdom and
  source/script fingerprints. The program remains staged under the owned
  `/tmp/pog3-audition-20261008` directory; its executable is not committed.
- `strict-warnings.txt`: final target strict warnings pass. `build.log` and
  `build-flags.txt`: ordinary SDK target build, freeze OFF, no stage profiling.
- `envelope-latency.txt`: existing production pitch-suite diagnostic repeats
  **24.009 / 48.001 ms** at 659.3 Hz and **60.022 / 72.006 ms** at 82.4 Hz
  for Focus off/on. Envelope-centroid delays are not physical round-trip latency.

`device-self-test.txt` and `silent-runner.txt` are earlier setup receipts; the
final-device check and final status above identify the retained executable.
No production DSP changed, so unrelated quality suites were not repeated.

## Reproduce / use later

Use the same SDK/container/FFTW setup as the
[headroom probe](../pog3-pi4-headroom-2026-10-08/README.md), enable
`ARDOR_BUILD_POG3_HEADROOM_PROBE=ON`, and explicitly build
`pedal-pog3-audition`. Stage the executable as `audition`, the existing wisdom as
`shared.wisdom`, and `scripts/pog3-audition-remote.sh` as `run.sh` in an owned
device directory. The current target paths are already prepared.

Run `./audition --self-test` without audio hardware. `check-silent.sh` starts the
silent hardware mode, waits for readiness, exercises commands and allows the
timeout to restore the normal service. Never overlap device runners. The silent
mode executes output conversion and then zeroes the physical playback block.

For the user's later playing comparison, use `scripts/pog3-audition.sh start`,
then its mode/status/stop commands. The live runner activates the output relay
only after audio readiness and restores normal operation at exit. The normal
application's preset UI is paused during the comparison. The default audition
expires after 15 minutes or ends immediately on an audio error.

Build/runtime files remain in the local disk cache. The preparation container
is removed. SHA-256 manifest covers retained artifacts, excluding itself.
