# Hardware Validation

Assembly, pin assignments, and first-power checks live in `docs/hardware-assembly.md`.

## macOS UMC22 Realtime Test

Command:

```sh
./build/pedal-poc \
  --realtime \
  --model models/test.nam \
  --ir irs/test.wav \
  --sample-rate 48000 \
  --block-size 64 \
  --capture-device 1 \
  --playback-device 1 \
  --input-channel right \
  --output-channel both
```

Pass:

- Runs without xruns.
- No audible dropouts.
- Status prints increasing callback count.

Current result:

```text
date: 2026-07-07
device: Behringer U-Phoria UMC22, shown as USB Audio CODEC
sample rate: 48000
block size: 64
IR samples: 8192
input channel: right
output channel: both
xruns: 0
notes: sounds good in realtime; 12000 eventually produced xruns, 14000 produced a few xruns, 16000 was unusable
```

Partitioned convolution result:

```text
date: 2026-07-07
device: Behringer U-Phoria UMC22, shown as USB Audio CODEC
sample rate: 48000
block size: 64
IR samples: 24000 full irs/test.wav
input channel: right
output channel: both
over-budget callbacks: 0
max callback: about 0.86 ms
average callback: about 0.27 ms
budget: 1.33 ms
notes: short smoke run; full IR became practical after partitioned convolution
```

## DSP Microbenchmark Baseline

Per-component cost at `48000 Hz / block 64 / IR 8192`, Release build, measured with:

```sh
cmake --build build --target pedal-dsp-bench
./build/pedal-dsp-bench          # uses models/test.nam if present
```

| Host | Component | avg µs/block | % of 1333 µs budget |
| --- | --- | ---: | ---: |
| macOS dev machine (2026-07-09) | IrConvolver | 22.8 (24.6 before precomputed twiddles; max 125→78) | 1.7% |
| macOS dev machine (2026-07-09) | NamProcessor (test.nam, SlimmableContainer/WaveNet) | 50.4 | 3.8% |
| Pi 4B (`performance` governor) | IrConvolver | TBD (Phase 0) | — |
| Pi 4B (`performance` governor) | NamProcessor | TBD (Phase 0) | — |

The Pi rows gate the optimization tasks in
`docs/superpowers/plans/2026-07-09-ir-convolver-performance.md`: if the Pi
convolver row is under 200 µs/block, those tasks do not run. Expect roughly
5–10× the macOS per-core numbers on the Cortex-A72.

## Raspberry Pi Codec Zero Realtime Test

Command on Pi:

```sh
/etc/init.d/S99ardor-pedal restart
tail -f /var/log/messages
```

Pass:

- App starts on boot.
- Guitar input produces stereo output.
- No ALSA xrun messages for 10 minutes.

## Latency Measurement Later

Method:

- Send a click from output to input with a loopback cable.
- Record the processed click.
- Measure input-to-output sample offset.
- Convert samples to milliseconds: `samples / 48000 * 1000`.

Pass:

- Round-trip latency is under 10 ms.

Result:

```text
date:
device:
sample rate:
block size:
measured samples:
measured ms:
notes:
```
