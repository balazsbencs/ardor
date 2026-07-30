# MIDI and expression control

This is the implementation contract for the control-I/O companion circuit in
[`hardware/control-io`](../hardware/control-io/README.md).

## Pin allocation

| Function | Interface | GPIO | Physical pin | Linux endpoint |
| --- | --- | ---: | ---: | --- |
| MIDI receive | UART4 RX | GPIO9 | 21 | `/dev/ttyAMA4` |
| MIDI transmit reserve | UART4 TX | GPIO8 | 24 | `/dev/ttyAMA4` |
| Expression samples | I2C1 SDA/SCL | GPIO2/3 | 3/5 | ADS1115 IIO device |
| ADC data-ready reserve | GPIO input | GPIO25 | 22 | future interrupt use |

GPIO2/GPIO3 are intentionally shared with the Codec Zero. ADS1115 address
`0x48` does not conflict with the codec, and the companion circuit does not
duplicate the bus pull-ups.

The Buildroot boot configuration enables `uart4`, the Raspberry Pi MIDI clock
overlay, and the upstream `ads1115` overlay. With that clock overlay, userspace
opens `/dev/ttyAMA4` at `38400` baud to obtain the physical MIDI rate of
`31250` baud.

## Initial MIDI contract

MIDI input is channel-omni by default. The parser supports running status and
ignores interleaved realtime bytes.

| MIDI message | Ardor action |
| --- | --- |
| Program Change `0`..`3` | Select preset slot 1..4 in the active bank |
| CC `0` or CC `32`, value `0`..`99` | Select bank 0..99 |
| CC `20`, value `0`..`63` | Tuner off |
| CC `20`, value `64`..`127` | Tuner on |

CC 20 is an Ardor default, not a universal tuner convention. The mapping is
represented by `MidiControlMapping` so it can become user-configurable without
changing the byte parser.

## Expression assignment in a preset

Expression assignment is optional and additive, so existing version-1 and
version-2 presets remain valid:

```json
{
  "expression": {
    "blockId": "delay-1",
    "parameter": "mix",
    "minimum": 0.15,
    "maximum": 0.85,
    "inverted": false
  }
}
```

`blockId` is the stable block identifier, not the block's position in the
chain. `parameter` is its runtime parameter key. The normalized pedal position
is clamped to `0..1`, optionally inverted, then scaled into
`minimum..maximum`. Loading rejects assignments to missing blocks and invalid
ranges.

## Remaining runtime work

The current code establishes and tests the MIDI parser/action mapping and
preset schema. The next integration slice is:

1. Open `/dev/ttyAMA4`, feed bytes into `MidiStreamParser`, and queue mapped
   actions onto the existing management loop.
2. Read and calibrate the ADS1115 IIO channel, with endpoint capture,
   disconnect detection, smoothing, and a small dead band.
3. Resolve the active preset's assignment against the live parameter catalog
   and use the existing non-realtime parameter setters.
4. Add manager and on-device controls for MIDI channel/CC selection,
   expression target, range, inversion, and calibration.

MIDI and ADC I/O must remain outside the realtime audio callback.

## Primary references

- [MIDI Association TRS connector specification announcement](https://midi.org/specification-for-trs-adapters-adopted-and-released)
- [Raspberry Pi UART documentation](https://www.raspberrypi.com/documentation/configuration/computers/raspberry-pi.html#configure-uarts)
- [Raspberry Pi overlay reference](https://github.com/raspberrypi/firmware/blob/master/boot/overlays/README)
- [Texas Instruments ADS1115 data sheet](https://www.ti.com/lit/ds/symlink/ads1115.pdf)
