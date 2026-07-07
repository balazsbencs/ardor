# Ardor Pedal POC

Standalone proof-of-concept for a Raspberry Pi guitar pedal platform.

The first target is deliberately small: load a Neural Amp Modeler `.nam` file, load a cabinet IR `.wav`, process mono guitar input, and output stereo audio with low latency. The same C++ app builds on macOS for desktop testing and Linux/Buildroot for Raspberry Pi.

Deferred for now: LVGL UI, display integration, footswitches, encoders, presets, OTA updates, and plugin formats.

## Requirements

- CMake 3.20+
- C++20 compiler
- Git access during CMake configure, for `miniaudio` and `NeuralAmpModelerCore`
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

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This builds:

- `build/pedal-poc`
- `build/pedal-offline-smoke`

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

Stop with `Ctrl-C`.

Use `--input-channel left` for input 1 and `--input-channel right` for input 2. On the UMC22, the instrument input is commonly the right/second capture channel.

Realtime mode uses the full IR by default through partitioned convolution. Use `--ir-samples N` to cap long IRs when comparing performance or testing slower hardware.

The output safety limiter is on by default at `-1 dBFS`; adjust it with `--safety-limit-db DB` or disable it with `--no-safety-limit`.

If the sound is overloaded, reduce `--input-gain-db` first. That lowers the signal before NAM. If the amp character is right but the final output clips, reduce `--output-gain-db`.

Realtime status prints once per second:

```text
callbacks=28125 over=0 over%=0.00 max=0.41ms avg=0.23ms budget=1.33ms
```

`over` counts callbacks that took longer than the audio budget. For `--block-size 64` at 48 kHz, the callback budget is about `1.33 ms`.

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
