# Hardware Validation

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
  --output-channel both \
  --ir-samples 8192
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
