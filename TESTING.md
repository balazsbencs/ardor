# Testing Ardor on macOS

This guide covers local testing of the Raspberry Pi pedal software without
updating or connecting the device. The macOS build can run the same DSP engine
and LVGL interface, while the local manager daemon and Manager app exercise the
same REST and runtime-command paths used on the pedal.

The available workflows are:

| Workflow | UI | Audio | REST API | Manager |
| --- | --- | --- | --- | --- |
| LVGL simulator | Yes | No | No | No |
| Integrated pedal simulator | Yes | Realtime CoreAudio | No | No |
| REST daemon | No | No | Yes | Optional |
| Full local stack | Yes | Realtime CoreAudio | Yes | Yes |

## Safety

Do not test live audio with the Mac's microphone feeding the Mac's speakers.
That can create immediate acoustic feedback. Prefer a guitar audio interface
and headphones, begin with its output level down, and raise it gradually.

The Manager can overwrite presets and rename or delete assets under its data
root. Use an isolated test data root unless modifying the repository's local
data is intentional.

## Requirements

- macOS and the Xcode command-line tools
- CMake 3.20 or newer
- SDL2
- Go, for `ardor-managerd`
- Node.js 24, selected by the repository `.nvmrc`
- Local `.nam` models and cabinet IR WAV files for presets that reference them

With Homebrew and `nvm`, the relevant setup is typically:

```sh
brew install cmake sdl2 go
cd /path/to/ardor
nvm use
```

The first CMake configure requires network access to fetch the pinned
miniaudio, NeuralAmpModelerCore, and LVGL sources.

## Choose a data root

All components in a full-stack test must use the same data root. Preset asset
paths are relative to this directory.

For quick testing against the repository's current local presets and assets:

```sh
export ARDOR_REPO=/absolute/path/to/ardor
export ARDOR_TEST_DATA="$ARDOR_REPO"
```

For isolated testing, create a fresh temporary directory and seed it from the
repository:

```sh
export ARDOR_REPO=/absolute/path/to/ardor
export ARDOR_TEST_DATA="$(mktemp -d /tmp/ardor-e2e.XXXXXX)"

mkdir -p \
  "$ARDOR_TEST_DATA/presets" \
  "$ARDOR_TEST_DATA/models" \
  "$ARDOR_TEST_DATA/irs" \
  "$ARDOR_TEST_DATA/settings" \
  "$ARDOR_TEST_DATA/wifi"

rsync -a "$ARDOR_REPO/presets/" "$ARDOR_TEST_DATA/presets/"
rsync -a "$ARDOR_REPO/models/" "$ARDOR_TEST_DATA/models/"
rsync -a "$ARDOR_REPO/irs/" "$ARDOR_TEST_DATA/irs/"
rsync -a "$ARDOR_REPO/settings/" "$ARDOR_TEST_DATA/settings/"
rsync -a "$ARDOR_REPO/wifi/" "$ARDOR_TEST_DATA/wifi/"

echo "$ARDOR_TEST_DATA"
```

Export the printed `ARDOR_TEST_DATA` value in every terminal used for that test
session. Creating a new temporary directory is also the safest way to reset a
test run.

## Build the desktop pedal applications

From the repository root:

```sh
cmake -S . -B build-sdl \
  -DARDOR_UI_BACKEND=sdl \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-sdl --target pedal-poc pedal-ui-sim -j
```

If CMake cannot find a Homebrew SDL2 installation, configure with:

```sh
cmake -S . -B build-sdl \
  -DARDOR_UI_BACKEND=sdl \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix sdl2)"
```

## LVGL UI without audio

Use `pedal-ui-sim` when testing layout, touch interaction, navigation, chain
editing, settings, and preset serialization without opening an audio device:

```sh
cd "$ARDOR_REPO"
./build-sdl/pedal-ui-sim --data-root "$ARDOR_TEST_DATA" --bank 0
```

The simulator reads the four slots in the selected bank and discovers assets
from `models/` and `irs/`. Chain changes remain drafts until **Save** is
pressed. It does not run DSP, produce telemetry, or simulate the physical
footswitches and encoder.

The convenience script builds and starts the same simulator against the
repository root:

```sh
./scripts/build-sim.sh
```

## Integrated LVGL UI and realtime audio

First enumerate CoreAudio devices:

```sh
cd "$ARDOR_REPO"
./build-sdl/pedal-poc --devices
```

Capture and playback devices have separate index lists. Re-run this command
after connecting or disconnecting an interface because the indexes can change.

Start the integrated pedal simulator with suitable indexes:

```sh
./build-sdl/pedal-poc \
  --realtime \
  --ui \
  --data-root "$ARDOR_TEST_DATA" \
  --bank 0 --slot 0 \
  --capture-device 1 \
  --playback-device 1 \
  --input-channel right \
  --output-channel both \
  --block-size 64 \
  --ir-samples 8192 \
  --telemetry-file /tmp/ardor-pedal-test-telemetry.txt
```

Use the device numbers printed on the local machine. `--input-channel left`
selects the first capture channel and `right` selects the second. On a UMC22,
the instrument input is commonly the right/second channel.

macOS may ask for microphone permission the first time the process opens a
capture device. Grant access to the host application that launched
`pedal-poc`, such as Terminal or the development IDE.

This mode exercises:

- the production DSP chain, including NAM, cabinet IR, dynamics, EQ, and Daisy
  effects;
- the production LVGL UI and preset editing model;
- live structural engine replacement and continuous parameter updates;
- tuner input and host-level output muting;
- preset and asset runtime commands; and
- callback timing and overload telemetry.

Pi-specific evdev footswitches, the encoder, serial MIDI, the expression ADC,
Linux realtime scheduling, framebuffer presentation, and touch calibration are
not reproduced on macOS.

## Run the REST API locally

Start the real manager daemon with authentication disabled for local testing:

```sh
cd "$ARDOR_REPO/services/managerd"

ARDOR_API_AUTH=off \
ARDOR_DATA_ROOT="$ARDOR_TEST_DATA" \
ARDOR_API_BIND=127.0.0.1 \
ARDOR_API_PORT=8080 \
go run ./cmd/ardor-managerd
```

Verify it from another terminal:

```sh
curl http://127.0.0.1:8080/api/device
curl http://127.0.0.1:8080/api/presets
curl http://127.0.0.1:8080/api/assets/models
curl http://127.0.0.1:8080/api/assets/irs
```

The API supports preset read/write/apply, model and IR upload, asset rename and
delete, and Wi-Fi settings. Upload and apply requests create command files
under `runtime/commands` in the shared data root. A running `pedal-poc` polls
and consumes those files.

To exercise authentication, restart the daemon with a test token:

```sh
ARDOR_API_AUTH=on \
ARDOR_API_TOKEN=local-test-token \
ARDOR_DATA_ROOT="$ARDOR_TEST_DATA" \
ARDOR_API_BIND=127.0.0.1 \
ARDOR_API_PORT=8080 \
go run ./cmd/ardor-managerd
```

Then supply `local-test-token` in the Manager connection dialog or use:

```sh
curl \
  -H 'Authorization: Bearer local-test-token' \
  http://127.0.0.1:8080/api/presets
```

The Wi-Fi API writes only beneath the selected test data root on macOS. Its
attempt to run the Pi Wi-Fi restart script will fail harmlessly and log an
error; it does not reconfigure the Mac's Wi-Fi connection.

## Run the Manager app

Select the repository's Node version and install locked dependencies:

```sh
cd "$ARDOR_REPO"
nvm use
cd apps/manager
npm ci
```

Start the Manager in the browser:

```sh
npm run dev
```

Then open `http://127.0.0.1:1420`.

The default Manager address is `http://127.0.0.1:8080`. Leave the token empty
when the daemon is running with `ARDOR_API_AUTH=off`.

## Full end-to-end test

Use three terminals. Export the same `ARDOR_REPO` and `ARDOR_TEST_DATA` values
in each one.

### Terminal 1: simulated pedal

```sh
cd "$ARDOR_REPO"

./build-sdl/pedal-poc \
  --realtime --ui \
  --data-root "$ARDOR_TEST_DATA" \
  --bank 0 --slot 0 \
  --capture-device 1 \
  --playback-device 1 \
  --input-channel right \
  --output-channel both \
  --block-size 64 \
  --ir-samples 8192 \
  --telemetry-file /tmp/ardor-pedal-test-telemetry.txt
```

### Terminal 2: REST daemon

```sh
cd "$ARDOR_REPO/services/managerd"

ARDOR_API_AUTH=off \
ARDOR_DATA_ROOT="$ARDOR_TEST_DATA" \
ARDOR_API_BIND=127.0.0.1 \
ARDOR_API_PORT=8080 \
go run ./cmd/ardor-managerd
```

### Terminal 3: Manager

```sh
cd "$ARDOR_REPO"
nvm use
cd apps/manager
npm run dev
```

Open `http://127.0.0.1:1420` and connect the Manager to
`http://127.0.0.1:8080`.

### Verify preset Save and Apply

1. Select a preset in the Manager.
2. Make a recognizable change, such as bypassing an effect or changing an
   effect mode.
3. Press **Save**. This writes the preset through the REST API.
4. Press **Apply**. The REST API queues an `apply_preset` command.
5. Confirm that the pedal terminal prints `Switched to preset BANK:SLOT`.
6. Confirm that the LVGL window selects the preset and reports it as active.
7. Confirm the audible change with a safe input and output level.

If preflight fails because a model or IR is unavailable, the pedal terminal
prints the rejection and the previously valid engine remains audible.

The current Apply response confirms that the command was queued, not that the
audio engine accepted it. Until runtime acknowledgements are added, the pedal
log and LVGL status are the authoritative result.

### Verify asset upload and refresh

1. Open **Assets** in the Manager.
2. Upload a licensed local `.nam` model or IR WAV.
3. Confirm that it appears under `models/` or `irs/` in `ARDOR_TEST_DATA`.
4. Confirm that the LVGL window reports **Assets reloaded**.
5. Add the asset to a preset, Save, and Apply it.

Uploading an asset refreshes the catalog; it does not modify the active chain
until a preset that references the asset is applied.

### Verify changes made in LVGL

1. Edit and save a preset in the LVGL window.
2. Refresh or reconnect the Manager.
3. Load that bank and slot and confirm the saved chain.

There is currently no push notification from the runtime to the Manager, so a
Manager refresh or reconnect is required after an out-of-band LVGL save.

### Queue Apply directly through REST

```sh
curl -X POST \
  http://127.0.0.1:8080/api/presets/banks/0/slots/0/apply
```

The response should be HTTP `202`, followed by the preset-switch message in the
pedal terminal.

## Automated test suites

### C++ engine, UI, and integration tests

```sh
cd "$ARDOR_REPO"
ctest --test-dir build-sdl --output-on-failure
```

### Manager daemon tests

```sh
cd "$ARDOR_REPO/services/managerd"
go test ./...
```

### Manager frontend tests

```sh
cd "$ARDOR_REPO"
nvm use
cd apps/manager
npm ci
npm run typecheck
npm test
```

`npm run test:e2e` is reserved for Playwright tests, but the repository does
not yet contain a Playwright configuration or E2E specifications. The manual
three-process procedure above is currently the full-stack test.

## Current limitations

- `GET /api/device` reports static device capabilities but not runtime health,
  callback telemetry, or the authoritative active bank and slot.
- REST Apply returns when the command is queued. It does not wait for engine
  activation or return a preflight/activation error.
- The Manager does not currently poll for external preset changes or runtime
  status.
- Automated tests do not open real CoreAudio devices or validate subjective
  audio output and latency.
- macOS does not reproduce the Pi's physical controls, Codec Zero routing,
  scheduling policy, framebuffer, or touchscreen behavior.

These limitations do not prevent manual Manager-to-REST-to-audio testing, but
they should be considered when interpreting an Apply response or an automated
test result.

## Troubleshooting

### Manager fails with a `node:util` or `styleText` import error

The selected Node version is too old for the pinned Vite version. Run `nvm use`
from the repository root; `.nvmrc` selects Node.js 24.

### Manager cannot connect

Check the REST endpoint independently:

```sh
curl http://127.0.0.1:8080/api/device
```

Confirm that the Manager and daemon use the same port and that a token is
provided only when authentication is enabled.

### Port 8080 or 1420 is already in use

Inspect the listener before starting another process:

```sh
lsof -nP -iTCP:8080 -sTCP:LISTEN
lsof -nP -iTCP:1420 -sTCP:LISTEN
```

Use another `ARDOR_API_PORT` only if the Manager connection address is changed
to match it. The Vite development server intentionally requires port 1420.

### Realtime audio does not start

- Re-run `pedal-poc --devices` and check both device indexes.
- Grant microphone permission to Terminal or the launching IDE.
- Set the audio interface to 48 kHz when possible.
- Check whether another application is holding the interface.
- Try block size 128 to distinguish device setup problems from CPU pressure.

### Preset Apply is queued but nothing changes

- Confirm `pedal-poc` and `ardor-managerd` use exactly the same data root.
- Confirm the pedal is running in realtime bank/slot mode.
- Inspect the pedal terminal for missing model, missing IR, or validation errors.
- Confirm command JSON files under `runtime/commands` are being consumed.
- Use a fresh isolated test data directory if commands from an older session
  make the result ambiguous.

### The preset loads as pass-through

The initial preset failed to load. Inspect the pedal log and verify every asset
path in the preset exists relative to `ARDOR_TEST_DATA`. Real `.nam` and IR
files are intentionally ignored by git and must be supplied locally.

## Ending a session

Stop the Manager, daemon, and pedal processes with `Ctrl-C` in their respective
terminals. An isolated temporary data directory remains available for log and
preset inspection; move it to Trash when it is no longer needed.
