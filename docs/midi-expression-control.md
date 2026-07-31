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

## Runtime configuration

The Buildroot service enables both inputs by default:

```sh
MIDI_DEVICE=/dev/ttyAMA4
MIDI_CHANNEL=omni
MIDI_TUNER_CC=20
EXPRESSION_DEVICE=auto
EXPRESSION_MIN_RAW=0
EXPRESSION_MAX_RAW=26400
EXPRESSION_SMOOTHING=0.25
EXPRESSION_DEADBAND=0.002
```

`MIDI_CHANNEL` accepts `omni` or MIDI channels `1`..`16`.
`EXPRESSION_DEVICE=auto` finds the Linux IIO device named `ads1115`; an IIO
device directory or its `in_voltage0_raw` file may be supplied explicitly.
The ADC is sampled in the management loop at 125 Hz, normalized between the
calibration endpoints, smoothed, and dead-band filtered. MIDI and ADC I/O stay
outside the realtime audio callback.

The first live expression path supports Daisy mod/delay/reverb parameters,
compressor and noise-gate parameters, and cab `mix` or `levelDb`. A missing,
disabled, unsupported, or nested Dual Rig target is left unchanged and logged
once. Preset changes reset the expression filter and load the new assignment.

## Editing and remaining UI work

Ardor Manager exposes the assignment as preset data: enable Expression Pedal,
choose a compatible effect and numeric parameter, set the minimum/maximum
values, and optionally invert the sweep. The assignment participates in the
editor's normal undo, validation, save, and apply flow.

The on-device editor still needs equivalent assignment controls and endpoint
calibration capture. MIDI channel, tuner CC, and ADC calibration are currently
service configuration rather than on-device preferences.

MIDI and ADC I/O must remain outside the realtime audio callback.

## Primary references

- [MIDI Association TRS connector specification announcement](https://midi.org/specification-for-trs-adapters-adopted-and-released)
- [Raspberry Pi UART documentation](https://www.raspberrypi.com/documentation/configuration/computers/raspberry-pi.html#configure-uarts)
- [Raspberry Pi overlay reference](https://github.com/raspberrypi/firmware/blob/master/boot/overlays/README)
- [Texas Instruments ADS1115 data sheet](https://www.ti.com/lit/ds/symlink/ads1115.pdf)
