# Raspberry Pi 4 NAM latency benchmark

Measured on 2026-08-12 on the production Raspberry Pi at 48 kHz, with the CPU
governor fixed to `performance`, the benchmark pinned to CPU 2, and
`SCHED_FIFO/60`. The device is a Raspberry Pi 4 Model B Rev 1.5 (Cortex-A72 at
1.5 GHz) using the RPi Codec Zero.

## Finding that blocked the latency setting

The UI had persisted `audioBlockSize: 32`, but the installed supervisor still
launched the engine with `--block-size 512`. The repository supervisor already
contains the persisted-setting lookup; `scripts/deploy-lan.sh` did not deploy
that file. The deploy script now installs the supervisor atomically alongside
the binaries. On-device verification after installing it showed:

```text
ardor-pedal ... --block-size 32 ...
capture period_size: 32, buffer_size: 96
playback period_size: 32, buffer_size: 96
```

## All-model compute sweep

The sweep covered all 123 real `.nam` files, both slimmable tiers, block sizes
8/16/32/64/128, and 2,000 timed blocks per case: 2.46 million timed NAM calls.

| Tier | Frames | Quantum | Worst mean CPU | Worst p99.9 | Misses |
| --- | ---: | ---: | ---: | ---: | ---: |
| full | 8 | 0.167 ms | 60.13% | 96.61% | 14 |
| full | 16 | 0.333 ms | 45.59% | 59.83% | 5 |
| full | 32 | 0.667 ms | 37.54% | 46.50% | 0 |
| full | 64 | 1.333 ms | 34.97% | 40.73% | 0 |
| full | 128 | 2.667 ms | 35.28% | 48.43% | 0 |
| nano | 8 | 0.167 ms | 9.76% | 28.52% | 2 |
| nano | 16 | 0.333 ms | 8.76% | 22.37% | 0 |
| nano | 32 | 0.667 ms | 8.36% | 14.76% | 0 |
| nano | 64 | 1.333 ms | 8.27% | 10.95% | 0 |
| nano | 128 | 2.667 ms | 8.65% | 11.51% | 0 |

A focused 100,000-block test of the worst observed 32-frame full-tier model had
zero misses, 37.29% mean CPU, 40.91% p99.9, and a 457.11 us maximum. The
corresponding nano test had zero misses, 8.01% mean CPU, 10.23% p99.9, and a
200.96 us maximum.

## Production engine soaks

The active preset contains a NAM model plus a 4,096-sample cabinet IR.

| Configuration | Duration / callbacks | Average | Maximum | Deadline faults | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| 32 frames, full NAM | 60 s / 91,747 | 0.33 ms | 0.75 ms | 8 | fail |
| 64 frames, full NAM | 120 s / 91,017 | 0.54 ms | 0.96 ms | 0 | pass |
| 32 frames, nano NAM | 120 s / 181,651 | 0.12 ms | 0.48 ms | 0 | pass |

All three soaks had zero scheduling gaps, worker overruns, non-finite blocks,
block mismatches, and overload bypasses. The 32-frame full-tier faults were
small processing-time overruns; standalone NAM remained below the deadline, so
the next optimization target is the rest of the production callback/IR path.

## Codec-internal digital loopback

The probe routed playback back into capture inside the codec. It measures the
actual ALSA/driver buffering path, but excludes analog ADC/DAC conversion and
external cable delay.

| Requested frames | Native capture/playback | Measured round trip |
| ---: | ---: | ---: |
| 8 | 32 / 8 | 4.1875 ms |
| 16 | 32 / 16 | 4.3750 ms |
| 32 | 32 / 32 | 2.7083 ms |
| 64 | 64 / 64 | 5.3542 ms |
| 128 | 128 / 128 | 10.7083 ms |
| 512 | 512 / 512 | 42.7083 ms |

The capture driver clamps requests below 32 frames, making 32 the useful
hardware floor. Compared with the accidentally deployed 512-frame setting,
32 frames removes 40 ms from this measured digital round trip.

## Recommendation

- Use 32 frames with nano NAM for the lowest currently verified reliable mode.
- Use 64 frames with full NAM for the currently verified reliable full-quality mode.
- Treat 32 frames with full NAM as experimental until the rare production
  callback tail is removed and a longer soak passes with zero faults.

Raw results and probe output are stored next to this report.
