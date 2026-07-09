# Preset Runtime Testing Instructions

Use this checklist to test the preset-driven audio runtime on macOS with a Behringer U-Phoria UMC22 or another CoreAudio interface.

## Goal

Verify that `pedal-poc` can load a preset JSON file, resolve relative `.nam` and `.wav` assets from `--data-root`, process offline audio, and run realtime guitar input without xruns at the known-good settings.

## Test Branch

Use branch:

```sh
git switch codex/audio-engine-integration
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target pedal-poc pedal-preset-cli-smoke
ctest --test-dir build -R pedal-preset-cli-smoke --output-on-failure
```

Expected:

- `pedal-preset-cli-smoke` passes.
- No build errors.

## Asset Layout

Create or verify this local layout:

```text
models/test.nam
irs/test.wav
dryguitar.wav
presets/bank-000/preset-0.json
```

Real `.nam`, `.wav`, and guitar recordings are local test assets and should not be committed.

## Preset File

Create `presets/bank-000/preset-0.json`:

```json
{
  "version": 1,
  "name": "Test Amp Cab",
  "routing": "serial",
  "global": {
    "inputGainDb": -12.0,
    "outputGainDb": -6.0,
    "safetyLimitDb": -1.0
  },
  "blocks": [
    {
      "id": "amp-1",
      "type": "nam",
      "enabled": true,
      "asset": "models/test.nam",
      "params": {}
    },
    {
      "id": "cab-1",
      "type": "cab",
      "enabled": true,
      "asset": "irs/test.wav",
      "params": {}
    }
  ]
}
```

Asset paths must stay relative to `--data-root`. Do not use absolute paths.

## Offline Test

Run:

```sh
./build/pedal-poc \
  --offline \
  --preset ./presets/bank-000/preset-0.json \
  --data-root . \
  --input ./dryguitar.wav \
  --output ./preset-wet.wav \
  --block-size 64 \
  --ir-samples 8192
```

Expected:

- Command exits with code `0`.
- `preset-wet.wav` is created.
- Output sounds like NAM plus cab IR, not dry guitar.
- No message like `failed to load NAM`, `IR sample rate mismatch`, or `failed to open preset`.

Compare with legacy CLI:

```sh
./build/pedal-poc \
  --offline \
  --model ./models/test.nam \
  --ir ./irs/test.wav \
  --input ./dryguitar.wav \
  --output ./legacy-wet.wav \
  --block-size 64 \
  --ir-samples 8192 \
  --input-gain-db -12 \
  --output-gain-db -6 \
  --safety-limit-db -1
```

Expected:

- `preset-wet.wav` and `legacy-wet.wav` should sound broadly the same.

## Realtime Device Check

List devices:

```sh
./build/pedal-poc --devices
```

Find the UMC22. It usually appears as `USB Audio CODEC`.

Note the playback and capture indexes:

```text
Playback devices:
  [N] USB Audio CODEC
Capture devices:
  [M] USB Audio CODEC
```

## Realtime Test

Start with input channel `right` for UMC22 instrument input. If there is no input signal, retry with `left`.

```sh
./build/pedal-poc \
  --realtime \
  --preset ./presets/bank-000/preset-0.json \
  --data-root . \
  --sample-rate 48000 \
  --block-size 64 \
  --ir-samples 8192 \
  --capture-device M \
  --playback-device N \
  --input-channel right \
  --output-channel both
```

Replace `M` and `N` with the capture/playback indexes from `--devices`.

Expected:

- Guitar signal is audible.
- Tone sounds like the amp model plus cab IR.
- Realtime log prints once per second.
- `over=0` should stay at zero for at least a few minutes.
- `bypassed=0` should stay zero.

Example good log:

```text
callbacks=28125 over=0 over%=0.00 max=0.41ms avg=0.23ms budget=1.33ms bypassed=0
```

Stop with `Ctrl-C`.

## Failure Notes To Record

If something fails, write down:

- Command used.
- Full terminal output.
- Device indexes used.
- Input channel used: `left` or `right`.
- Whether output was silent, dry, distorted, or bypassed.
- `over`, `over%`, `max`, `avg`, and `bypassed` values from the realtime log.
- Whether offline preset output worked.

## Pass Criteria

This phase passes when:

- Automated `pedal-preset-cli-smoke` passes.
- Offline preset render creates a valid wet WAV.
- Realtime preset run produces the expected amp/cab tone.
- `--block-size 64 --ir-samples 8192` runs for several minutes without growing `over`.
- Legacy `--model/--ir` command still works.

## Realtime Robustness Baseline

Known-good realtime baseline:

- sample rate: `48000`
- block size: `64`
- IR samples: `8192`

Automated reload stress:

```sh
cmake --build build --target pedal-preset-reload-stress
ctest --test-dir build -R pedal-preset-reload-stress --output-on-failure
```

Manual realtime soak:

```sh
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 \
  --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

Pass:

- Telemetry prints once per second.
- `over` does not grow continuously.
- UI shows `LIVE` during normal operation.
- UI shows `BYPASS` if overload bypass latches.
- Preset reloads do not crash and do not run inside the audio callback.

## Integrated UI and Audio Checklist

Run with `--ui` to exercise the full integrated loop:

```sh
./build/pedal-poc --realtime --ui --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 \
  --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

- [ ] UI window opens and shows the preset screen.
- [ ] Guitar audio is audible immediately.
- [ ] Touching a different slot card switches the audio engine and highlights the new slot.
- [ ] Pressing Save writes the preset to disk and reloads the engine audibly.
- [ ] Telemetry updates once per second in the UI status line.
- [ ] Master volume adjusted via encoder is reflected in the UI percentage.
- [ ] Forcing overload (tiny block size) causes UI to show `BYPASS`; removing overload returns it to `LIVE`.
