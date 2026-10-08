# POG3 current playing-feel audition

The temporary audition is staged at `/tmp/pog3-audition-20261008` on the pedal
at `192.168.88.12`. The normal pedal service remains active until the audition
is explicitly started. This is a development executable; it does not install
the effect or alter saved presets/settings.

The purpose is to decide whether the **current octave algorithm's response feels
acceptable before investing in worker-pipeline integration**. Attack is zero.
Filter, Detune, Spread and expression processing are disabled. Focus starts off.
The current source already contributes approximately 24–48 ms of main spectral
delay; low-note envelope-centroid measurements are approximately 60–72 ms.
These are algorithm measurements, not the complete physical input/output latency.

## Start and controls

Connect the guitar to the usual input and monitor the usual output. The audition
uses input channel 0, as the installed application does, and sends the mono input
to both sides of POG3. Both hardware outputs are active. **The signal is clean
POG3 without the normal NAM/EQ chain**, so use an external amp or monitoring
appropriate for clean guitar. Master is 0.7x (−3.10 dB) in every mode.

From the POG3 worktree, with SSH authentication available:

```sh
sh scripts/pog3-audition.sh start
sh scripts/pog3-audition.sh status
```

Tell the assistant when ready to play and it can start/read the session remotely.
The session runs for at most **15 minutes** and restores the normal pedal service
on completion, stop, or an audio error. It starts in dry mode.

| Footswitch | Sound |
| --- | --- |
| 1 | Immediate dry path |
| 2 | Octave up only |
| 3 | Octave down only |
| 4 | Dry + half-level octave up + half-level octave down |

The rotary encoder chooses Focus: negative movement off, positive movement on.
The assistant can also select it explicitly. Wait about a second after changing
Focus before judging a phrase. The normal touchscreen preset UI is paused during
this temporary session; the four switches directly select these sounds.

Remote commands are also available:

```sh
sh scripts/pog3-audition.sh dry
sh scripts/pog3-audition.sh up
sh scripts/pog3-audition.sh down
sh scripts/pog3-audition.sh blend
sh scripts/pog3-audition.sh focus-on
sh scripts/pog3-audition.sh focus-off
sh scripts/pog3-audition.sh stop
```

`POG3_AUDITION_HOST` overrides the default SSH target.
`POG3_AUDITION_SSH_SOCKET` overrides the session's SSH control socket. If the
socket has expired, normal SSH authentication is required.

## Playing comparison

1. Play a short repeated phrase in dry mode to establish its normal response.
   Include a high-string single-note line and palm-muted low notes.
2. Repeat the same picking pattern with octave up only and Focus off. Judge
   the connection between the pick and the sound, separately from its tone.
3. Try octave down only, then the blend. Check whether the immediate dry signal
   makes the blend comfortable or whether the octave still feels behind the beat.
4. Repeat the most revealing phrase with Focus on. Include low E, chord changes
   and a few sharp stops/re-plucks. Avoid intentional volume swells for this test.

Useful feedback is **comfortable / slightly soft / clearly delayed** for each
mode and Focus setting, plus the phrase/register where it becomes distracting.
Note pitch wobble, rough transients or clicks separately; they do not establish
the cause of a latency impression. The assistant can read xrun/timing counters
while the session runs. If the sound returns to the normal preset, inspect the
session log and return code before treating it as a subjective response problem.

The decision remains a listening result. A dry-path identity check, offline
envelope measurements and a quiet hardware check cannot determine playing feel.
The 2026-10-08 live comparison is complete. The user reported that everything
felt okay, then clarified that there was a slight delay in the tone, barely
noticeable. All four modes and both Focus settings appear in the session log;
the feedback is an overall judgment, not a separate rating for each combination.
This supports continuing to the CPU worker experiment. It does not establish
acceptance of additional pipeline latency, Attack/freeze, or the full NAM/EQ chain.

The corrected live run completed 46,397 callbacks (approximately 123.7 seconds)
with zero ALSA xruns. Mean measured DSP/conversion time was 2058.66 µs, maximum
2744.54 µs, with six callbacks above the 2666.67 µs period. These counters are
not a physical input/output latency measurement or long-term chain admission.
The audition was stopped cleanly and the normal app restored with codec outputs
and relay enabled. [Feedback and raw live receipts](../benchmark-results/pog3-live-playing-feel-2026-10-08/README.md).

The first live attempt produced no audible output: stopping the normal app
muted the codec's `Headphone Switch`, and the original runner only enabled the
output relay. Footswitch events and nonzero DSP output were recorded, but those
did not establish that the analog output was audible. The runner now explicitly
unmutes the codec after PCM readiness, verifies both output switches are on,
and then enables the relay. Cleanup mutes both before restoring the normal app;
when no app was originally running, it restores the previous codec mute state.
The silent preparation checks above did not cover this live unmute step.

## Implementation and validation

`pedal-pog3-audition` is an optional target under
`ARDOR_BUILD_POG3_HEADROOM_PROBE=ON`, excluded from the default build/install.
Its dedicated FIFO70 audio thread on CPU2 owns the current POG3 processor.
The ordinary 48 kHz / 128-frame / 384-frame device settings are retained.
The probe uses ALSA RW_INTERLEAVED; the installed application uses MMAP.
There is no additional worker pipeline in this audition.

The control thread handles footswitch/encoder input, publishes existing lock-free
parameter targets and writes telemetry. It applies no reset when switching modes;
the pitch bank remains warm. The processor's existing 10 ms level slews smooth
sound changes. The audition's fixed output soft knee uses the normal −1 dB
ceiling without adding lookahead.

Device self-checks cover signed PCM conversion, the output ceiling, immediate
dry identity and all four modes at both Focus settings. Strict target compilation
and shell syntax checks pass. The quiet hardware check exercises actual capture,
DSP, output conversion and command changes while sending zero samples to physical
playback. The runner restores the normal service and its original audio settings.
Raw receipts are in
[the audition preparation artifacts](../benchmark-results/pog3-live-audition-2026-10-08/README.md).

At an ALSA xrun the audition ends and records an error; it does not conceal the
xrun by entering the codec's slow recovery loop. Its short preparation checks
are not CPU/endurance admission, a physical latency measurement or listening approval.
