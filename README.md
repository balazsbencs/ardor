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
  --ir-samples 8192
```

Stop with `Ctrl-C`.

Use `--input-channel left` for input 1 and `--input-channel right` for input 2. On the UMC22, the instrument input is commonly the right/second capture channel.

Realtime mode trims long IRs to `4096` samples by default because the current POC uses a simple FIR convolver. On macOS with the UMC22, `--block-size 64 --ir-samples 8192` is the current known-good setting. Use `--ir-samples N` to choose a different limit. `0` currently means "use the default realtime limit"; full-length realtime IRs need partitioned convolution.

First target settings:

- sample rate: `48000`
- preferred block size: `64`
- fallback block size: `128`
- input: mono
- output: stereo
- round-trip latency goal: under `10 ms`

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
