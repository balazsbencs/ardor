# Ardor Pedal POC

Standalone proof-of-concept for a Raspberry Pi guitar pedal platform.

The first target is deliberately small: load a Neural Amp Modeler `.nam` file, load a cabinet IR `.wav`, process mono guitar input, output stereo audio with low latency, and prototype the pedal UI on desktop with LVGL.

Deferred for now: display integration on Raspberry Pi, footswitches, encoders, OTA updates, and plugin formats.

## Requirements

- CMake 3.20+
- C++20 compiler
- Git access during CMake configure, for `miniaudio` and `NeuralAmpModelerCore`
- SDL2 for the LVGL desktop simulator
- macOS for desktop testing, or Linux for target-style builds
- Local test assets:
  - `models/test.nam`
  - `irs/test.wav`
  - optional dry input WAV for offline rendering

Real `.nam` and IR files are ignored by git. Keep licensed/user-provided assets local unless redistribution is allowed.

## Preset Storage

Preset files live under the data root in bank/slot folders, for example:

- `presets/bank-000/preset-0.json`

Block assets inside preset JSON stay relative to that same data root, such as `models/clean.nam` or `irs/open-back.wav`. Absolute paths and `..` traversal are rejected. Real `.nam` models and IRs stay local and are not committed unless redistribution is allowed.

### Supported V1 Parameters

Preset globals:

- `global.inputGainDb`: input gain before NAM.
- `global.outputGainDb`: output gain after cab.
- `global.safetyLimitDb`: limiter ceiling, where `-1.0` is the default. Stored and applied, but not editable from the UI — it is a protective clipper, not a tone control.

Cab block params:

- `params.levelDb`: cab level before output gain.
- `params.mix`: `0.0` dry after-NAM signal, `1.0` full cab signal.

Other block params may be stored in JSON, but modulation, delay, and reverb are not processed yet.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This builds:

- `build/pedal-poc`
- `build/pedal-offline-smoke`
- `build/pedal-ui-sim`

## Test

```sh
ctest --test-dir build --output-on-failure
```

The current automated test is a small offline smoke test for the DSP plumbing. Hardware latency and real NAM model checks are manual for now.

## List Audio Devices

```sh
./build/pedal-poc --devices
```

On macOS with a Behringer U-Phoria UMC22, it usually appears as `USB Audio CODEC`.

## Offline Render

Preset files use relative asset paths under `--data-root`.

Preset-driven offline render:

```sh
./build/pedal-poc \
  --offline \
  --preset ./presets/bank-000/preset-0.json \
  --data-root . \
  --input ./dryguitar.wav \
  --output ./wet.wav
```

Bypass NAM and test only IR/output rendering:

```sh
./build/pedal-poc \
  --offline \
  --ir irs/test.wav \
  --input /path/to/dry-guitar.wav \
  --output /tmp/ardor-wet.wav \
  --bypass-nam
```

Run NAM plus IR:

```sh
./build/pedal-poc \
  --offline \
  --model models/test.nam \
  --ir irs/test.wav \
  --input /path/to/dry-guitar.wav \
  --output /tmp/ardor-wet.wav
```

Input and IR WAV files must be 48 kHz for this POC.

## Realtime Run

List devices first:

```sh
./build/pedal-poc --devices
```

Example output:

```text
Playback devices:
  [1] USB Audio CODEC
Capture devices:
  [1] USB Audio CODEC
```

Run with explicit UMC22 routing:

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
  --input-gain-db -12 \
  --output-gain-db -6 \
  --safety-limit-db -1
```

Preset-driven realtime run:

```sh
./build/pedal-poc \
  --realtime \
  --preset ./presets/bank-000/preset-0.json \
  --data-root . \
  --capture-device 1 \
  --playback-device 1 \
  --input-channel left \
  --output-channel both \
  --block-size 64 \
  --ir-samples 8192
```

Stop with `Ctrl-C`.

### Realtime preset slot switching

Slot-based realtime mode loads presets from `--data-root`:

```sh
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

While it is running, type `0`, `1`, `2`, or `3`, then Enter, to switch presets in the current bank. The app reloads outside the audio callback, restarts the realtime device, and resumes telemetry.

Use `--input-channel left` for input 1 and `--input-channel right` for input 2. On the UMC22, the instrument input is commonly the right/second capture channel.

Realtime mode uses the full IR by default through partitioned convolution. Use `--ir-samples N` to cap long IRs when comparing performance or testing slower hardware.

The output safety limiter is on by default at `-1 dBFS`; adjust it with `--safety-limit-db DB` or disable it with `--no-safety-limit`.

If the sound is overloaded, reduce `--input-gain-db` first. That lowers the signal before NAM. If the amp character is right but the final output clips, reduce `--output-gain-db`.

Realtime status prints once per second:

```text
callbacks=28125 over=0 over%=0.00 max=0.41ms avg=0.23ms budget=1.33ms
```

`over` counts callbacks that took longer than the audio budget. For `--block-size 64` at 48 kHz, the callback budget is about `1.33 ms`.

Realtime telemetry is shared between CLI and UI. The known-good baseline remains `--block-size 64 --ir-samples 8192`. If the overload bypass latches, the CLI prints `bypassed=1` and the UI shows `BYPASS`.

First target settings:

- sample rate: `48000`
- preferred block size: `64`
- fallback block size: `128`
- input: mono
- output: stereo
- round-trip latency goal: under `10 ms`

## UI Mockup

The first UI mockup is static HTML:

```sh
open mockups/preset-ui/index.html
```

It uses the same preset shape as `docs/superpowers/specs/2026-07-07-preset-ui-architecture-design.md`.

## LVGL UI Simulator

The first LVGL UI target is a desktop simulator for preset and edit screens:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target pedal-ui-sim
./build/pedal-ui-sim
```

It validates screen layout and UI state only. It does not wire footswitch GPIO, the encoder, Codec Zero, or realtime audio yet.

### LVGL simulator with preset files

The simulator can load the same preset files used by `pedal-poc`:

```sh
./build/pedal-ui-sim --data-root . --bank 0
```

It reads:

- `presets/bank-000/preset-0.json`
- `presets/bank-000/preset-1.json`
- `presets/bank-000/preset-2.json`
- `presets/bank-000/preset-3.json`

Assets are discovered from `models/*.nam` and `irs/*.wav`. Editing the chain only changes memory until the Save button is pressed.

## Buildroot Firmware Seed

This repo contains a Buildroot external tree under `buildroot/external`. It does not vendor Buildroot.

From a separate Buildroot checkout:

```sh
make BR2_EXTERNAL=/Users/bbalazs/Documents/Ardor/buildroot/external raspberrypi4_ardor_pedal_defconfig
make
```

The package installs:

- `/usr/bin/ardor-pedal`
- `/etc/init.d/S99ardor-pedal`
- `/opt/ardor-pedal/` for local `model.nam` and `cab.wav`

## Hardware Validation

See `docs/hardware-validation.md` for the macOS UMC22 test notes and Raspberry Pi Codec Zero checklist.
