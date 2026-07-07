# Hardware Validation

## macOS UMC22 Realtime Test

Command:

```sh
./build/pedal-poc --realtime --model models/test.nam --ir irs/test.wav --sample-rate 48000 --block-size 64
```

Pass:

- Runs for 10 minutes.
- No audible dropouts.
- Status prints increasing callback count.

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

## Latency Measurement

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
