# Ardor Pedal

Ardor is a standalone Raspberry Pi guitar-processing platform with a realtime
audio engine, touchscreen/footswitch UI, preset storage, desktop manager, and a
reproducible Buildroot firmware image.

The signal chain supports Neural Amp Modeler `.nam` files, cabinet impulse
responses, 35 hosted modulation/delay/reverb effects, compression, five-band
parametric EQ, global gain, and a safety limiter. The same LVGL interface runs
in the SDL desktop simulator and on the Raspberry Pi Touch Display 2.

The pedal is an appliance rather than a plugin host. Plugin formats and an OTA
update workflow are outside the current scope.

## Community

See [CONTRIBUTING.md](CONTRIBUTING.md) before proposing changes. Contributors
are recognized in [CONTRIBUTORS.md](CONTRIBUTORS.md), and suspected
vulnerabilities should be reported privately according to
[SECURITY.md](SECURITY.md).

## Requirements

- CMake 3.20+
- C++20 compiler
- Git access during CMake configure, for `miniaudio` and `NeuralAmpModelerCore`
- SDL2 for the LVGL desktop simulator (`ARDOR_UI_BACKEND=sdl`)
- macOS for desktop testing, or Linux for target-style builds
- Optional: Go for the manager daemon, Node.js/Rust for the Tauri manager, and
  Docker for the Buildroot firmware image
- Local test assets:
  - `models/test.nam`
  - `irs/test.wav`
  - optional dry input WAV for offline rendering

Real `.nam` and IR files are ignored by git. Keep licensed/user-provided assets local unless redistribution is allowed.

## Preset Storage

Preset files live under the data root in bank/slot folders, for example:

- `presets/bank-000/preset-0.json`

Block assets inside preset JSON stay relative to that same data root, such as `models/clean.nam` or `irs/open-back.wav`. Absolute paths and `..` traversal are rejected. Real `.nam` models and IRs stay local and are not committed unless redistribution is allowed.

### Supported Parameters

Preset globals:

- `global.inputGainDb`: input gain before NAM.
- `global.outputGainDb`: output gain after cab.
- `global.safetyLimitDb`: limiter ceiling, where `-1.0` is the default. Stored and applied, but not editable from the UI — it is a protective clipper, not a tone control.

NAM block params:

- `params.useNano`: when `true`, selects the embedded nano submodel to reduce CPU usage; missing or `false` selects the full model.

Cab block params:

- `params.levelDb`: cab level before output gain.
- `params.mix`: `0.0` dry after-NAM signal, `1.0` full cab signal.

Daisy effect blocks use no asset path and store catalog-defined normalized
`0.0..1.0` parameters. The built-in catalog currently contains 35 modes:

- 13 modulation/special effects: chorus, flanger, rotary, vibe, phaser,
  vintage/pattern tremolo, poly octave, auto swell, filter, formant,
  quadrature, and destroyer.
- 10 delays: digital, tape, dual, filter, lo-fi, bucket-brigade, duck,
  pattern, swell, and tremolo.
- 12 reverbs: room, hall, plate, spring, bloom, cloud, shimmer, chorale,
  nonlinear, swell, magneto, and reflections.

Each mode uses semantic labels, physical-value formatting, defaults, and
discrete choices from `src/daisyfx/DaisyFxCatalog.cpp`. Compressor blocks use
`type: "dynamics"` with mode `compressor`.

The Ardor-maintained Daisy effect engine lives under `src/daisyfx/hosted/`.
Its upstream origin and license are preserved there; host adaptation and effect
implementation now live together under `src/daisyfx/`.

Five-band EQ blocks use `type: "eq"`, mode `parametric_eq_5`, and five entries
in `params.bands`. Each band stores `enabled`, `frequency_hz` (20–20,000 Hz),
`q` (0.1–18), and `gain_db` (-18 to +18 dB). Missing fields receive indexed
band defaults; saved presets always contain exactly five complete bands.

## Build

Desktop build with the SDL UI:

```sh
cmake -S . -B build-sdl -DARDOR_UI_BACKEND=sdl -DCMAKE_BUILD_TYPE=Release
cmake --build build-sdl -j
```

Primary executables include:

- `build-sdl/pedal-poc` — offline/realtime engine and integrated UI
- `build-sdl/pedal-ui-sim` — desktop-only LVGL simulator
- `build-sdl/audio-probe` — audio-device probe

For a headless build, configure with `-DARDOR_UI_BACKEND=none`. The Pi firmware
uses the `fbdev` backend through the Buildroot package.

## Test

```sh
ctest --test-dir build-sdl --output-on-failure
```

The CTest suite covers preset parsing and activation, realtime-chain behavior,
Daisy effects and automation, compressor and parametric EQ processing, tuner and
control gestures, UI models/LVGL interaction, audio-device enumeration, and
reload stress. Hardware latency, Codec Zero routing, and subjective audio
validation remain device-level checks.

## List Audio Devices

```sh
./build-sdl/pedal-poc --devices
```

On macOS with a Behringer U-Phoria UMC22, it usually appears as `USB Audio CODEC`.

## Offline Render

Preset files use relative asset paths under `--data-root`.

Preset-driven offline render:

```sh
./build-sdl/pedal-poc \
  --offline \
  --preset ./presets/bank-000/preset-0.json \
  --data-root . \
  --input ./dryguitar.wav \
  --output ./wet.wav
```

Bypass NAM and test only IR/output rendering:

```sh
./build-sdl/pedal-poc \
  --offline \
  --ir irs/test.wav \
  --input /path/to/dry-guitar.wav \
  --output /tmp/ardor-wet.wav \
  --bypass-nam
```

Run NAM plus IR:

```sh
./build-sdl/pedal-poc \
  --offline \
  --model models/test.nam \
  --ir irs/test.wav \
  --input /path/to/dry-guitar.wav \
  --output /tmp/ardor-wet.wav
```

Input and IR WAV files must be 48 kHz.

## Realtime Run

List devices first:

```sh
./build-sdl/pedal-poc --devices
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
./build-sdl/pedal-poc \
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
./build-sdl/pedal-poc \
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
./build-sdl/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

While it is running, type `0`, `1`, `2`, or `3`, then Enter, to switch presets in the current bank. The app reloads outside the audio callback, restarts the realtime device, and resumes telemetry.

Use `--input-channel left` for input 1 and `--input-channel right` for input 2. On the UMC22, the instrument input is commonly the right/second capture channel.

Realtime mode uses the full IR by default through partitioned convolution. Use `--ir-samples N` to cap long IRs when comparing performance or testing slower hardware.

The output safety limiter is on by default at `-1 dBFS`; adjust it with `--safety-limit-db DB` or disable it with `--no-safety-limit`.

If the sound is overloaded, reduce `--input-gain-db` first. That lowers the signal before NAM. If the amp character is right but the final output clips, reduce `--output-gain-db`.

Add `--clip-debug` to print interval peaks at the input-gain boundary, after every effect block, and immediately before the safety limiter. For example:

```text
levels input=-7.2dBFS nam:amp=1.4dBFS CLIP[38] ir:cab=-0.8dBFS output=-1.1dBFS limiter=[122] first=nam:amp
```

`CLIP[n]` counts frames above 0 dBFS during the interval, and `first=` identifies the earliest boundary that crossed full scale. This distinguishes signal-level overload from intentional NAM distortion: if `input` or `nam:...` is first, reduce input gain; if `ir:...` is first, reduce cabinet level; if only `output` or `limiter` is active, reduce output gain or master volume. The meter observes processor boundaries—it cannot label the intentional nonlinear distortion inside a NAM model as clipping.

With `--clip-debug --ui`, the touchscreen status bar shows the same diagnosis: red `CLIP` with the first stage and peak, amber `LIMIT` when only the safety limiter engages, or green `LEVEL OK`. It refreshes once per second.

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

Chain layout contract: NAM and cabinet blocks are mono stages. Modulation preserves
stereo input. The hosted delay and reverb effects preserve the stereo dry field but
sum their wet input to mono before producing their vendor-defined stereo wet output.

Hardware controls on Raspberry Pi use Linux input events:

```sh
./build-sdl/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --control-device /dev/input/event-footswitches \
  --control-device /dev/input/event-encoder \
  --block-size 64 --ir-samples 8192
```

The app maps `KEY_F1` through `KEY_F4` to preset slots and relative encoder
movement to master output volume. Hold the two left switches (`KEY_F1` +
`KEY_F2`) together for one second to mute the output and open the tuner; press
any footswitch to exit without changing presets. The touchscreen provides the
same flow through the preset-screen Tuner button and tuner-screen Exit button.

## Manager Daemon

The REST manager daemon lives in `services/managerd`. It manages `.nam`, `.wav`,
and preset files without doing management work in the realtime process. Asset
uploads queue a catalog refresh for the pedal UI. Applying a saved slot queues
a live engine swap, handled by the pedal management loop without restarting the
audio process; a short muted transition protects the active audio callback.

Run locally without auth:

```sh
cd services/managerd
ARDOR_API_AUTH=off \
ARDOR_DATA_ROOT=../.. \
ARDOR_API_BIND=127.0.0.1 \
ARDOR_API_PORT=8080 \
go run ./cmd/ardor-managerd
```

The device status endpoint is:

```sh
curl http://127.0.0.1:8080/api/device
```

Auth is enabled by default when no environment override is supplied. Set
`ARDOR_API_AUTH=on` and provide `ARDOR_API_TOKEN` for a protected device.

## Desktop Manager

The Tauri desktop manager in `apps/manager` connects to `ardor-managerd` and
provides bank/slot browsing, drag-and-drop chain editing, block validation and
inspection, undo/redo, Save/Apply, asset upload/rename/delete, and light/dark
themes.

Supported desktop-manager targets are Apple Silicon and Intel macOS, and
Windows x64. Linux builds of the Tauri manager are not supported or released;
Linux support applies to the headless engine, manager daemon, and firmware
tooling.

```sh
cd apps/manager
npm install
npm run tauri dev
```

Optional TONE3000 integration can browse and install NAM captures directly on
the connected pedal. Copy `apps/manager/.env.example` to `.env`, add a
publishable TONE3000 client ID, and register the callback URL documented in the
example file.

For local testing, run the Go daemon with auth disabled and use
`http://127.0.0.1:8080` as the manager base URL:

```sh
cd services/managerd
ARDOR_API_AUTH=off ARDOR_DATA_ROOT=../.. ARDOR_API_BIND=127.0.0.1 ARDOR_API_PORT=8080 \
go run ./cmd/ardor-managerd
```

## Legacy UI Mockup

The original static HTML concept remains available for design history:

```sh
open mockups/preset-ui/index.html
```

The LVGL implementation described below is the authoritative interface.

## Touch UI and LVGL Simulator

The touch UI includes:

- A four-slot preset screen with bank controls, master-volume status, Edit, and
  direct Tuner entry.
- A fixed two-row signal-chain editor with drag-and-drop block ordering.
- An asset drawer with separate All, Amps, Cabs, EQ, Dynamics, Modulation,
  Delays, and Reverbs filters.
- Two-row, three-column parameter pages using large horizontal sliders with
  inline labels/values, plus a matching rectangular bypass control.
- A dedicated five-band parametric EQ editor with a live response graph.
- A muted tuner with note/frequency/cents guidance and both touchscreen Exit
  and footswitch exit.

Build and launch the desktop simulator with:

```sh
./scripts/build-sim.sh
```

`pedal-ui-sim` is a desktop-only tool. It does not wire footswitch GPIO, the encoder, Codec Zero, or realtime audio. For the integrated UI+audio experience use `pedal-poc --ui` (see below).

### LVGL simulator with preset files

The simulator can load the same preset files used by `pedal-poc`:

```sh
./build-sdl/pedal-ui-sim --data-root . --bank 0
```

It reads:

- `presets/bank-000/preset-0.json`
- `presets/bank-000/preset-1.json`
- `presets/bank-000/preset-2.json`
- `presets/bank-000/preset-3.json`

Assets are discovered from `models/*.nam` and `irs/*.wav`; compressor, EQ, and
Daisy effects come from built-in catalogs. The simulator saves chain edits only
when Save is pressed.

## Integrated UI and Audio

Pass `--ui` to `pedal-poc` in slot mode to run the LVGL UI alongside the audio engine in a single process:

```sh
./build-sdl/pedal-poc \
  --realtime \
  --ui \
  --data-root . \
  --bank 0 --slot 0 \
  --capture-device 1 \
  --playback-device 1 \
  --input-channel left \
  --output-channel both \
  --block-size 64 --ir-samples 8192
```

The UI starts on the preset screen. On-device effect-chain edits preview
immediately: structural and discrete changes prepare and swap a replacement
engine, while supported continuous parameters are published live. Save only
persists the already-audible draft. Switching preset or bank with unsaved edits
asks whether to Save, Discard, or Cancel. Telemetry (callback count, overruns, bypass state) updates once
per second. Encoder master volume is reflected in the UI. Tuner mode shows the
detected note, frequency, cents offset, and flat/sharp guidance while keeping
the pedal output muted.

`--ui` requires `ARDOR_UI_BACKEND=sdl` (desktop default) or `ARDOR_UI_BACKEND=fbdev` (Pi). It has no effect on the non-slot realtime or offline paths.

## Buildroot Firmware Image

The repository contains a Buildroot external tree for a Raspberry Pi 4 pedal
image. The build pins and verifies Buildroot 2025.02.15, runs in a native Docker
container on Apple Silicon or x86_64, and preserves the validated Raspberry Pi
Linux 6.18 hardware stack.

Build the complete image from the repository root:

```sh
./scripts/build-image.sh
```

The resulting `sdcard.img` contains:

- `/usr/bin/ardor-pedal` and `/etc/init.d/S99ardor-pedal`.
- `/usr/bin/ardor-managerd` and `/etc/init.d/S98ardor-managerd`.
- `/etc/ardor-pedal.env`, `/etc/ardor-managerd.env`, and the Codec Zero mixer
  state.
- A read-only root filesystem and writable `/opt/ardor-pedal` data partition
  seeded with four presets.

See [BUILD.md](BUILD.md) for prerequisites, versioned-volume behavior, flashing,
rollback, troubleshooting, hardware checks, REST verification, and deferred
upgrade work.

## Hardware Validation

See [Hardware Assembly Guide](docs/hardware-assembly.md) for the current Codec
Zero, touchscreen, footswitch, encoder, and GPIO wiring contract. Device-image
verification and first-boot checks are documented in [BUILD.md](BUILD.md).
