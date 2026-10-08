# Current POG3 live playing-feel result

Device: `192.168.88.12`. Corrected live runner with codec output unmute/readback;
48 kHz, 128-frame periods, 384-frame ALSA buffer, CPU2/FIFO70. Real guitar input
and audible stereo output; no added worker pipeline, NAM or EQ. Attack is zero
and freeze is disabled. Source/binary identity is recorded in `session-sha256.txt`.

User feedback after confirming audible output:

> everything feels okay

Follow-up clarification:

> although I hear a just a slight delay in the tone, but barely noticeable

Record this as acceptable overall playing feel with slight, barely noticeable
delay. The log includes dry, octave up, octave down and blend, plus Focus changes.
There are no separate ratings for each mode, register or Focus combination.
There is no recorded audio or measured physical round-trip latency.

| Final counter | Value |
| --- | ---: |
| Callbacks | 46,397 |
| Audio frames / 48 kHz | Approximately 123.7 seconds |
| Mean DSP + output conversion | 2058.66 µs |
| Maximum DSP + output conversion | 2744.54 µs |
| Callbacks exceeding 2666.67 µs | 6 |
| ALSA xruns | 0 |
| Exit code | 0 |

`status.json` is final, with `finished:1`; `audition.log` records controls.
`service-restored.txt` and `restored-device-readback.txt` independently confirm
the normal app resumed with its MMAP hardware settings, codec outputs on and
relay enabled. The initial muted attempt is documented separately and excluded.

Next: bounded CPU3 worker feasibility with the reference NAM/EQ chain, retaining
the current algorithm. Its added latency must be tested separately for playing
feel. This short standalone run does not establish full-chain or thermal endurance.
