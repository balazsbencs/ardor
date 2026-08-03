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

Per-preset learned CC mappings take priority over these fixed actions when the
same message is used. Program Change remains available for preset selection.

## MIDI Learn and scenes

In the on-device parameter drawer, tap **MIDI** beside a parameter, then move a
pedal or press a footswitch. Ardor captures the CC number and channel. Press
**Save** for automatic behavior, **Advanced** to choose Continuous or
Toggle/Scene and edit endpoints **1** and **2**, or **Cancel** to leave the
preset unchanged. The Bypass header has its own MIDI learn target.

Automatic learn treats a CC with several values across a useful span as a
continuous controller. A single press is treated as a latched toggle. Toggle
bindings react only to the high edge (`>=64`), so the low message sent when a
momentary footswitch is released does not undo the scene.

Mappings are additive preset data:

```json
{
  "midiMappings": [
    {
      "channel": 0,
      "controlChange": 11,
      "mode": "continuous",
      "actions": [
        {
          "target": "parameter",
          "blockId": "wah-1",
          "parameter": "position",
          "value1": 0.05,
          "value2": 0.95
        }
      ]
    },
    {
      "channel": 0,
      "controlChange": 64,
      "mode": "toggle",
      "actions": [
        { "target": "blockEnabled", "blockId": "boost-1", "value1": 0, "value2": 1 },
        { "target": "blockEnabled", "blockId": "chorus-1", "value1": 1, "value2": 0 },
        { "target": "parameter", "blockId": "drive-1", "parameter": "gain", "value1": 0.5, "value2": 0.7 }
      ]
    }
  ]
}
```

`channel` uses zero-based MIDI channels (`0`..`15`); `-1` is omni. Continuous
bindings interpolate every action from `value1` to `value2` over CC `0`..`127`.
Toggle bindings apply every action together, making the mapping a small scene
inside the preset. Relearning the same footswitch on another control adds that
control to the scene. Every preset activation resets toggle bindings to setting
1 before MIDI input is processed. Blocks controlled by `blockEnabled` are
prepared even when setting 1 bypasses them, so enabling them does not rebuild
the audio engine. Live MIDI targets are currently limited to top-level blocks;
Dual Rig lane children remain preset-load-only targets.

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

## Editing

Ardor Manager exposes the expression assignment as preset data: enable Expression Pedal,
choose a compatible effect and numeric parameter, set the minimum/maximum
values, and optionally invert the sweep. The assignment participates in the
editor's normal undo, validation, save, and apply flow.

Ardor Manager preserves and validates `midiMappings`; MIDI Learn itself runs on
the pedal so it can listen to the attached controller. MIDI channel, tuner CC,
and ADC calibration are also available in the on-device Control I/O settings.

MIDI and ADC I/O must remain outside the realtime audio callback.

## Primary references

- [MIDI Association TRS connector specification announcement](https://midi.org/specification-for-trs-adapters-adopted-and-released)
- [Raspberry Pi UART documentation](https://www.raspberrypi.com/documentation/configuration/computers/raspberry-pi.html#configure-uarts)
- [Raspberry Pi overlay reference](https://github.com/raspberrypi/firmware/blob/master/boot/overlays/README)
- [Texas Instruments ADS1115 data sheet](https://www.ti.com/lit/ds/symlink/ads1115.pdf)
