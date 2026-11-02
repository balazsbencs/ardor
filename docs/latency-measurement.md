# Latency measurement

This procedure measures the real latency of an Ardor pedal, from the guitar
input jack to the output jack. It uses an audio interface with two inputs and
`scripts/measure-latency.py`. No oscilloscope is necessary.

`audio-probe --latency` (5.35 ms at 64 frames in
`benchmark-results/audio-digital-loopback-pi4.txt`) measures a different thing.
It runs on the Pi, uses a raw codec loop, and does not include the buffer
board, the `ardor-pedal` engine or the preset blocks. The commit does not say
whether that run used a cable or the codec's internal digital loopback.

## How it works

The interface plays a click on output 1. A Y cable sends the click to
interface input 1 (the reference) and to the Ardor input. The Ardor output
goes to interface input 2. The interface adds the same delay to both inputs,
so the time between the two recorded clicks is the Ardor latency alone.
Playback and recording do not need to be in sync.

## Wiring

Turn direct monitoring OFF on the interface, and turn software playthrough
OFF in the DAW. If monitoring is on, input 1 hears output 1 twice, and the
Ardor output feeds back into the Ardor input.

1. Connect interface output 1 to a Y cable (or a passive splitter).
2. Connect one leg to interface input 1.
3. Connect the other leg to the Ardor guitar input. Use a passive pad if the
   interface output is too hot.
4. Connect the Ardor left output to interface input 2.
5. Set the interface input gains so that no input clips. The clicks play at
   -20 dBFS peak, near a guitar level.

## Run on macOS

The `run` command records with `ffmpeg` and plays with `afplay`. `afplay`
always uses the macOS default output, so set the interface as the default
output first.

```sh
python3 -I scripts/measure-latency.py run --list-devices   # find the interface audio index
python3 -I scripts/measure-latency.py run --device <index> --keep ~/ardor-latency/bypass-64
```

The first run can record silence if the terminal has no microphone
permission. Give it permission in System Settings > Privacy & Security >
Microphone, then run again. `--keep` saves `clicks.wav` and `recording.wav`
for a later check. Keep that folder outside the repository.

## Run with any DAW

```sh
python3 -I scripts/measure-latency.py generate clicks.wav
```

1. Put `clicks.wav` on a track that plays to interface output 1.
2. Record interface inputs 1 and 2 at the same time, as one stereo file or
   two tracks. Record the full length of the clicks.
3. Export the recording as one WAV file with input 1 first.
4. Analyze it:

```sh
python3 -I scripts/measure-latency.py analyze recording.wav --verbose
```

Use `--ref-channel` and `--dut-channel` if the channels are in a different
order. The script reads 16-, 24- and 32-bit PCM and 32- or 64-bit float WAV
files at any sample rate.

## Read the result

The script gives two values for each run:

- **Cross-correlation** finds the delay where the output matches the
  reference best. Use it for the bypass value and for the declared-latency
  check below.
- **Onset** finds the first half-level sample of each click. It is the "feel"
  value for high-gain presets. A filtered or smeared output starts before its
  centre, so onset can be smaller than cross-correlation.

The script warns if the two values disagree by more than 0.25 ms, if the
latency changes by more than 0.1 ms between clicks, if clicks have no output
(for example, a closed noise gate), if a channel clips, and if the polarity
changes.

## What to measure

Do each run after a preset reload, and again after a reboot. The ALSA period
alignment can change at each start.

| Preset | Buffer 32 | Buffer 64 | Buffer 128 |
| --- | --- | --- | --- |
| All blocks bypassed |  |  |  |
| NAM + cab IR |  |  |  |
| Gig preset (oversampled drive, WDW, delay, reverb) |  |  |  |
| Heaviest preset that does not overload |  |  |  |

Put the text reports in `benchmark-results/`. Do not commit the WAV
recordings: each run is about 4 MB.

## Checks

- **Bypass.** The bypassed value is the floor for each buffer size. Compare
  it with the `audio-probe` value. A difference near a whole number of
  buffers (1.33 ms at 64 frames) points to an extra buffer in the engine or
  in ALSA. A smaller difference is the buffer board and, if the probe used a
  digital loopback, the Codec Zero ADC and DAC filters.
- **Declared latency.** For a preset whose added blocks all declare their
  latency (EQ, the oversampled drives), the preset value minus the bypassed
  value must equal the sum of their `latencyFrames()` divided by the sample
  rate. A difference is a bug. Do not use this check for NAM captures and cab
  IRs: they add their own phase and pre-delay.
- **Goal.** `PRODUCT.md` sets a round-trip goal under 10 ms. Check the gig
  preset at 64 frames against it.
