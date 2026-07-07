# Raspberry Pi Guitar Pedal POC Design

## Goal

Build the smallest useful proof of concept for a Raspberry Pi 4B 1GB guitar pedal platform: a standalone real-time audio app that runs Neural Amp Modeler A2 plus a cabinet impulse response, first on macOS with a Behringer U-Phoria UMC22, then on Raspberry Pi with Codec Zero and a Buildroot image.

The POC succeeds when the Pi processes mono guitar input through NAM A2 and an IR `.wav` to stereo output with measured round-trip latency under 10 ms, or produces a clear bottleneck report showing why it cannot.

## Scope

Included:

- Standalone C++ audio app.
- Portable DSP core shared by macOS and Raspberry Pi builds.
- macOS real-time test path using CoreAudio and UMC22.
- Raspberry Pi real-time test path using ALSA and Codec Zero.
- Buildroot external tree for the Pi firmware image.
- Offline DSP smoke test using a short mono `.wav`.
- Runtime status logging for device, sample rate, block size, DSP time, CPU load, xruns, model path, and IR path.

Deferred:

- LVGL UI.
- 5 inch display integration.
- Footswitches and encoders.
- Presets and parameter editing.
- Plugin formats such as LV2/VST.
- OTA updates, A/B partitions, factory reset, and product hardening.
- Hardware-in-CI.

## Architecture

The system has one app with a narrow platform boundary.

```text
audio input
  -> input gain
  -> NAM A2 processor
  -> IR convolution
  -> output gain
  -> mono-to-stereo output
```

Components:

- `PedalEngine`: platform-independent DSP core. Loads a `.nam` model and IR `.wav`, then processes fixed-size audio blocks.
- `AudioBackend`: small interface implemented by CoreAudio on macOS and ALSA on Raspberry Pi.
- `pedal-poc`: command-line runtime. Parses paths and audio settings, opens the backend, runs the DSP callback, and prints status.

No UI code runs in Project 1. LVGL can be added after audio latency is proven.

## Runtime Defaults

Initial settings:

```text
sample rate: 48000 Hz
preferred block size: 64 samples
fallback block size: 128 samples
input channels: 1
output channels: 2
latency target: <10 ms round-trip
```

The app should run models at the model's expected sample rate, initially 48 kHz. It should not add resampling in the first POC.

## Repository Shape

```text
apps/pedal-poc/
  main.cpp
src/dsp/
  PedalEngine.cpp
  PedalEngine.h
src/audio/
  AudioBackend.h
  CoreAudioBackend.mm
  AlsaBackend.cpp
buildroot/
  external/
    configs/
    package/ardor-pedal/
models/
  .gitkeep
irs/
  .gitkeep
```

This is a proposed implementation shape, not a requirement to create empty scaffolding before code needs it.

## Build System

Use CMake for the app and DSP code.

macOS:

```sh
cmake -S . -B build -DARDOR_AUDIO_BACKEND=coreaudio
cmake --build build
```

Raspberry Pi:

```sh
cmake -S . -B build -DARDOR_AUDIO_BACKEND=alsa
cmake --build build
```

Buildroot should package the same app by setting `ARDOR_AUDIO_BACKEND=alsa`.

Use upstream NAM C++ code for model loading and processing. Do not write a model runner. For IR convolution, start with the smallest implementation that passes the offline and real-time tests; only add a stronger partitioned convolution implementation if profiling shows the simple path cannot meet latency or CPU targets.

## Buildroot Firmware

Use a Buildroot external tree owned by this repo. Do not vendor Buildroot itself.

Firmware contents:

- Linux kernel and Raspberry Pi firmware.
- ALSA userspace tools.
- Codec Zero / IQaudio codec configuration.
- `ardor-pedal` standalone app.
- Model and IR copied into `/opt/ardor-pedal/`.
- Init script that starts the app on boot.
- Optional SSH for debugging.
- No desktop, X11, or Wayland.

Boot flow:

```text
power on
  -> Linux boots
  -> ALSA device appears
  -> ardor-pedal starts
  -> app loads /opt/ardor-pedal/model.nam
  -> app loads /opt/ardor-pedal/cab.wav
  -> audio processing starts
```

## Testing

Offline DSP smoke test:

- Input: short mono `.wav`.
- Chain: NAM A2 plus IR.
- Output: stereo `.wav`.
- Pass: output renders, contains no NaNs, and is not silent.

macOS real-time test:

- Device: Behringer U-Phoria UMC22.
- Settings: 48 kHz, 64 samples preferred, 128 samples fallback.
- Pass: no dropouts for 10 minutes.

Raspberry Pi real-time test:

- Device: Raspberry Pi Codec Zero.
- Settings: 48 kHz, 64 samples preferred, 128 samples fallback.
- Pass: app autostarts and runs without ALSA xruns for 10 minutes.

Latency measurement:

- Method: loopback click or pulse measurement.
- Pass: measured round-trip latency under 10 ms.

CI later:

- Configure and build the CMake app.
- Run the offline DSP smoke test with tiny checked-in fixture files.
- Build the Buildroot image only on manual or nightly workflow unless CI runtime is acceptable.

Real user-supplied `.nam` and IR files should stay out of git unless their license explicitly allows redistribution.

## References

- NAM A2 release: https://www.neuralampmodeler.com/post/a2-is-released
- Neural Amp Modeler LV2 A2 support notes: https://github.com/mikeoliphant/neural-amp-modeler-lv2
- Raspberry Pi Codec Zero specs: https://www.raspberrypi.com/products/codec-zero/
- LVGL Linux DRM driver docs: https://lvgl.io/docs/open/integration/embedded_linux/drivers/drm
- Buildroot manual: https://buildroot.org/downloads/manual/manual.html
