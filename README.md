# Ardor Pedal

Ardor is a standalone Raspberry Pi guitar-processing platform with a realtime
audio engine, touchscreen/footswitch UI, preset storage, a browser-based manager
served by the device, and a reproducible Buildroot firmware image.

The signal chain supports Neural Amp Modeler `.nam` files, cabinet impulse
responses, 35 hosted modulation/delay/reverb effects, compression, noise
gating, five-band parametric EQ with high-pass and low-pass filters, global gain,
and a safety limiter. The same
LVGL interface runs in the SDL desktop simulator and on the Raspberry Pi Touch
Display 2.

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
- Optional: Go for the manager daemon, Node.js 24 via `nvm use` for the manager
  web app, and Docker for the Buildroot firmware image
- Local test assets:
  - `models/test.nam`
  - `irs/test.wav`
  - optional `reverb-irs/test.wav`
  - optional dry input WAV for offline rendering

Real `.nam` and IR files are ignored by git. Keep licensed/user-provided assets local unless redistribution is allowed.

## Guitar Input Buffer

A passive guitar must pass through the Ardor input buffer before it reaches
the Codec Zero. The repository contains the separate, minimal KiCad project
in [`buffer/`](buffer/).

You can order a board with the SMD parts fitted through
[JLCPCB](https://jlcpcb.com/). Upload these files from
[`buffer/jlcpcb/production_files/`](buffer/jlcpcb/production_files/):

- [`GERBER-buffer.zip`](buffer/jlcpcb/production_files/GERBER-buffer.zip) for the PCB files.
- [`BOM-buffer.csv`](buffer/jlcpcb/production_files/BOM-buffer.csv) for the parts list.
- [`CPL-buffer.csv`](buffer/jlcpcb/production_files/CPL-buffer.csv) for the part placement list.

The production files do not include audio jacks, wires, connectors, or the
enclosure. Add these parts after the board arrives. Connect the guitar jack to
`GUITAR_IN` and connect `GUITAR_OUT` to the Codec Zero input. Follow the buffer
schematic for power and ground connections.

## Preset Storage

Preset files live under the data root in bank/slot folders, for example:

- `presets/bank-000/preset-0.json`

Block assets inside preset JSON stay relative to that same data root, such as `models/clean.nam`, `irs/open-back.wav`, or `reverb-irs/small-room.wav`. Cabinet IRs and convolution reverb IRs use separate directories. Existing convolution presets that reference an installed `irs/` asset remain compatible, while new reverb selections use `reverb-irs/`. Absolute paths and `..` traversal are rejected. Real `.nam` models and IRs stay local and are not committed unless redistribution is allowed.

### Supported Parameters

Preset globals:

- `global.inputGainDb`: input gain before NAM.
- `global.outputGainDb`: output gain after cab.
- `global.safetyLimitDb`: limiter ceiling, where `-1.0` is the default. Stored and applied, but not editable from the UI — it is a protective clipper, not a tone control.

NAM block params:

- `params.inputMode`: stereo-to-mono routing at the NAM input: `"sum"` averages left and right (default), `"left"` uses the left/mono channel, and `"right"` uses the right channel.
- `params.useNano`: when `true`, selects the embedded nano submodel to reduce CPU usage; missing or `false` selects the full model.

Cab block params:

- `params.levelDb`: cab level before output gain.
- `params.mix`: `0.0` dry after-NAM signal, `1.0` full cab signal.

Dual Amp is one draggable `type: "dualAmp"` block with two fixed parallel
lanes. Both lanes receive the selected mono input; the left NAM → IR lane feeds
only the left output and the right NAM → IR lane feeds only the right output.
It uses:

- `params.inputMode`: `"sum"` (default), `"left"`, or `"right"`.
- `params.leftNamAsset` / `params.rightNamAsset`: relative `.nam` asset paths.
- `params.leftIrAsset` / `params.rightIrAsset`: relative IR asset paths.
- `params.leftUseNano` / `params.rightUseNano`: per-model nano selection.
- `params.leftCabLevelDb` / `params.rightCabLevelDb`: `-60..12` dB.
- `params.leftCabMix` / `params.rightCabMix`: `0.0..1.0`.
- `params.leftPolarityInvert` / `params.rightPolarityInvert`: whole-lane polarity.

An enabled Dual Amp block cannot be combined with standalone NAM or cabinet
blocks in the same preset. Existing version-1 serial presets remain compatible.

Dual Rig generalizes that fixed block to two independent child chains. It is a
version-2 preset block and remains one draggable item in the outer chain:

```json
{
  "version": 2,
  "blocks": [{
    "id": "rig-1",
    "type": "dualRig",
    "enabled": true,
    "asset": "",
    "params": {
      "inputMode": "sum",
      "leftLevelDb": 0,
      "leftPolarityInvert": false,
      "rightLevelDb": 0,
      "rightPolarityInvert": false
    },
    "lanes": {
      "left": { "blocks": [
        {"id":"left-nam","type":"nam","enabled":true,"asset":"models/clean.nam","params":{}},
        {"id":"left-cab","type":"cab","enabled":true,"asset":"irs/open.wav","params":{}},
        {"id":"left-chorus","type":"mod","enabled":true,"asset":"","params":{"mode":"chorus"}}
      ]},
      "right": { "blocks": [
        {"id":"right-nam","type":"nam","enabled":true,"asset":"models/crunch.nam","params":{}},
        {"id":"right-cab","type":"cab","enabled":true,"asset":"irs/closed.wav","params":{}},
        {"id":"right-delay","type":"delay","enabled":true,"asset":"","params":{"mode":"digital"}}
      ]}
    }
  }]
}
```

Both lanes receive the same selected mono input. Each lane is processed in its
declared order; the left lane's left output and the right lane's right output
are retained at the fixed merge. Child block IDs are globally unique, both
lanes must be non-empty, and split blocks cannot be nested. Version-1 presets
and the fixed `dualAmp` block remain supported.

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

Noise Gate blocks use `type: "dynamics"` with mode `noise_gate`. Their numeric
parameters are `threshold_db`, `reduction_db`, `attack_ms`, `hold_ms`,
`release_ms`, `hysteresis_db`, and `sidechain_hpf_hz`. Detection is
stereo-linked and the gate adds no lookahead latency.

The Ardor-maintained Daisy effect engine lives under `src/daisyfx/hosted/`.
Its upstream origin and license are preserved there; host adaptation and effect
implementation now live together under `src/daisyfx/`.

Five-band EQ blocks use `type: "eq"`, mode `parametric_eq_5`, and five entries
in `params.bands`. Each band stores `enabled`, `frequency_hz` (20–20,000 Hz),
`q` (0.1–18), and `gain_db` (-18 to +18 dB). Missing fields receive indexed
band defaults; saved presets always contain exactly five complete bands. The
surrounding `high_pass` and `low_pass` filters store `enabled`, `frequency_hz`,
`q`, and `slope_db_per_octave` (6, 12, 18, or 24; default 12).

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
Daisy effects and automation, compressor, noise gate, parametric EQ, the
four-track looper and its persistent store, tuner and control gestures, UI
models/LVGL interaction, audio-device enumeration, and reload stress. The
looper timing benchmark is intentionally separate from CTest:

```sh
cmake --build build-sdl --target pedal-looper-bench
./build-sdl/pedal-looper-bench --block-sizes 64,128 --warmup 100 --iterations 1000
```

Host results are useful for regressions, but do not replace the Raspberry Pi 4
acceptance benchmark and ten-minute production-backend soak. Hardware latency,
Codec Zero routing, and subjective audio validation remain device-level checks.

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

Dual Amp and Dual Rig always have a sequential fallback: the callback processes
the complete left lane and then the complete right lane. On a four-core Pi,
enable the optimized path with
`--parallel-rigs --audio-cpu 2 --rig-worker-cpu 3`. This keeps the callback at
`SCHED_FIFO/70` on CPU 2 and runs a persistent right-lane worker at
`SCHED_FIFO/69` on CPU 3; the callback processes the complete left lane
concurrently and joins the worker once at the fixed merge. Effects before and
after the parallel block still run serially on the callback thread. The two CPU
arguments are required and must differ. If the worker cannot obtain its
requested realtime policy or affinity, the rig reports the failure and uses
sequential processing. The older `--parallel-dual-amp` and
`--dual-amp-worker-cpu` spellings remain accepted.

The Buildroot image enables that layout by default through
`PARALLEL_RIGS=1`, `AUDIO_CPU=2`, and `RIG_WORKER_CPU=3` in
`/etc/ardor-pedal.env`. The UI and non-audio work remain normal-priority Linux
tasks; the realtime DSP threads preempt them on their assigned cores.

Measure that layout on a running pedal over SSH:

```sh
./scripts/measure-device-performance.sh --duration 30 192.168.88.17
```

The probe reports whole-core utilization and headroom, per-thread CPU usage,
last CPU, affinity, realtime policy and priority, migrations, temperature, and
CPU clock range. Production builds also publish an atomic snapshot at
`/run/ardor-pedal.telemetry`; the probe uses its before/after counters to check
callback overruns, scheduling gaps, parallel-worker deadline misses,
non-finite DSP blocks, block-size mismatches, and overload bypass. It checks
that the callback is `SCHED_FIFO/70` on CPU 2. With a Dual Amp or Dual Rig
preset active, require and validate the `SCHED_FIFO/69` worker on CPU 3:

```sh
./scripts/measure-device-performance.sh \
  --duration 60 --require-worker 192.168.88.17
```

The local wrapper streams
`scripts/device-performance-remote.sh` to BusyBox `sh`; it does not install
anything or modify the pedal. Use `ARDOR_PI_HOST`, `ARDOR_SSH_USER`, and
`ARDOR_SSH_OPTS` for repeatable lab configuration. Passwords are handled by
OpenSSH and are never stored by the scripts. The final pass/fail verdict covers
thread placement, realtime scheduling, and audio/DSP fault counters;
utilization, headroom, clock, and thermal values remain measurements to compare
between presets and builds.

Realtime telemetry is shared between CLI and UI. The known-good baseline remains `--block-size 64 --ir-samples 8192`. If the overload bypass latches, the CLI prints `bypassed=1` and the UI shows `BYPASS`.

First target settings:

- sample rate: `48000`
- preferred block size: `64`
- fallback block size: `128`
- input: mono
- output: stereo
- round-trip latency goal: under `10 ms`

Chain layout contract: NAM and cabinet blocks are mono stages. A NAM block may follow
stereo effects and folds its input according to `params.inputMode`; its mono output is
copied to both channels. Cabinet blocks must still precede stereo effects. Dual Amp
and Dual Rig establish stereo with their hard-left/hard-right lanes. Inside a Dual
Rig, the same ordering rules apply independently to each lane. Modulation preserves
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

### Four-Track Looper

Select a preset, then press **Looper** on the preset screen. That preset remains
locked until the loop session is closed, and all recorded audio captures its
processed stereo output before master volume and the safety limiter. The four
large track plates select the working track. The four footswitches keep fixed
performance roles:

- FS1 toggles one-level undo/redo for the selected track;
- tap FS2 to select the next track, or hold it to clear the selected track;
- FS3 starts/stops the first recording or an overdub, and FS4 mutes/unmutes the
  selected track;
- hold FS1 + FS2 to pause and enter the tuner; leaving the tuner resumes a loop
  that was previously running;
- later empty tracks arm and begin recording together at the next master-loop
  boundary, so all four layers stay synchronized;
- the touchscreen adds global pause/resume, New/Save/Load/Close, track
  selection, and per-track level/balance controls.

Save and Load are available only while paused. A saved set contains the four
effective stereo tracks, mix settings, loop length, and a snapshot of the
source preset under `loops/<32-character-id>/`. Saving runs outside the audio
callback and publishes the manifest last, so a failed replacement preserves
the previous durable revision. Closing a session releases its preset lock and
restores the preset that was active before a recalled loop.

The planned TRS-A MIDI and expression-pedal interface, message mapping, GPIO
allocation, and preset expression schema are documented in
[`docs/midi-expression-control.md`](docs/midi-expression-control.md). Its
native KiCad schematic is under [`hardware/control-io`](hardware/control-io/README.md).

## Manager Daemon

The REST manager daemon lives in `services/managerd`. It manages `.nam`, `.wav`,
and preset files without doing management work in the realtime process. Asset
uploads queue a catalog refresh for the pedal UI. Applying a saved slot queues
a live engine swap, handled by the pedal management loop without restarting the
audio process; a short muted transition protects the active audio callback.

The hosted HTTPS manager architecture and delivery plan are specified in
[`docs/hosted-manager-architecture.md`](docs/hosted-manager-architecture.md).
Its Phase 1 foundation includes the shared UI transport contract, durable
device identity, versioned wire schemas, and a reconnecting outbound cloud
agent. Phase 2 adds a SQLite-backed hosted control plane, account recovery,
device presence, and physically confirmed claiming. Phase 3 adds hosted preset
list/read/save/apply operations with durable idempotency. Phase 4 adds bounded
asset streaming and server-side TONE3000 integration. Phase 5 adds device-local
accounts, recoverable reset flows, and mDNS discovery.

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

The daemon also serves a device-hosted build of the React manager from `/`.
When loaded from the daemon, the manager connects to the same origin
automatically, so any browser on the network can manage the pedal:

```sh
cd apps/manager
nvm use
npm run build:device

cd ../../services/managerd
ARDOR_API_AUTH=off ARDOR_DATA_ROOT=../.. go run ./cmd/ardor-managerd
```

Open `http://127.0.0.1:8080` for local development, or the corresponding
device address on the local network. On supported networks, the daemon also
advertises `_ardor-manager._tcp` and a stable hostname derived from the device
identity. With the default port, use
`http://ardor-<first-8-device-id-characters>.local:8080`; a production image
that binds the daemon to port 80 can omit the port. Set `ARDOR_MDNS=off` only
when discovery must be disabled. The generated bundle is embedded in the
`ardor-managerd` binary so it remains available without internet access.

For repeat LAN deployments, `scripts/deploy-lan.sh` uses a local Docker builder
image (`ardor-builder:ubuntu-24.04`) after its first run, avoiding a fresh APT
installation each time. Normal deploys reuse Buildroot's package output; set
`ARDOR_PEDAL_DIRCLEAN=1` only when a clean pedal-package rebuild is needed.
Use `ARDOR_APT_MIRROR=http://hu.archive.ubuntu.com/ubuntu` to build the cached
image from the Hungarian Ubuntu mirror.

Auth is enabled by default when no environment override is supplied. On first
start, the pedal displays a short-lived setup code. Open the device-hosted
manager on a trusted LAN and enter that code to choose one local username and
password. Passwords are stored as Argon2id hashes; browser sessions use an
`HttpOnly`, `SameSite=Strict` cookie and mutation requests require a matching
local Origin and Host. Direct LAN access uses HTTP, so the UI clearly warns
that it is intended for trusted networks. `ARDOR_API_AUTH=off` is for explicit
development/testing use and no static API token is required.

Set `TONE3000_CLIENT_ID` on the device to enable **Browse TONE3000** under
Assets → NAM models. Ardor supports NAM A2 captures only. The device starts
TONE3000's LAN OAuth relay using its Wi-Fi address, so the browser or phone
used to sign in must be on the same trusted Wi-Fi network as the pedal. OAuth
credentials remain in device memory only for the one 15-minute selection and
the selected model is saved through the normal local asset path. The
publishable `t3k_pub_…` client ID may be deployed on the device; never deploy a
TONE3000 secret key. `TONE3000_BASE_URL` defaults to
`https://www.tone3000.com` and is useful only for development overrides.

The Security & reset settings can sign out, reset only local access, or request
a factory reset. Resetting local access removes the username, password hash,
and all sessions while preserving presets, assets, Wi-Fi, and cloud state. A
factory reset must be approved on the pedal with footswitch 1 (footswitch 4
cancels); it clears device-local user data while preserving the stable device
identity and its reset audit record. A durable reset marker makes an interrupted
factory reset resume deterministically after restart.

The outbound cloud agent is disabled by default. A control-plane deployment can
enable it with `ARDOR_CLOUD_ENABLED=on` and an HTTPS origin in
`ARDOR_CLOUD_URL`. Preset reads are available over an authenticated claimed
device connection. Preset save/apply remains opt-in per pedal with
`ARDOR_CLOUD_REMOTE_MUTATIONS=on`; only the four allowlisted preset operations
are accepted. The generated Ed25519 identity is stored under
`<ARDOR_DATA_ROOT>/identity/device.json` with mode `0600`.

## Hosted Control Plane

The initial control plane in `services/controlplane` is a single Go service
with an embedded migration set and a pure-Go SQLite driver. SQLite is the
intentional first-deployment choice: the server has one active instance and a
single writer, so PostgreSQL would add operations without adding useful
capacity yet. The repository boundary leaves a future database migration open.

Run the API locally over explicitly enabled development HTTP:

```sh
cd services/controlplane
ARDOR_INSECURE_HTTP=on \
ARDOR_PUBLIC_ORIGIN=http://127.0.0.1:8090 \
go run ./cmd/ardor-controlplane
```

Build the hosted browser application with the repository Node version:

```sh
cd apps/manager
nvm use
npm run build:hosted
```

Production deployment must expose that static bundle and `/v1` from the same
public HTTPS origin. Account sessions are `HttpOnly`, `SameSite=Strict`, secure
cookies; state-changing browser requests also require the exact public Origin.
Hosted preset save/apply requests carry UUID idempotency keys; completed
responses and audit events are persisted in the same SQLite transaction.

## Manager Web App

The React manager in `apps/manager` connects to `ardor-managerd` and provides
bank/slot browsing, drag-and-drop chain editing, block validation and
inspection, undo/redo, Save/Apply, asset upload/rename/delete, and light/dark
themes. The device serves it over LAN HTTP, so no installation is needed. There
is no desktop application.

Run the app against a device during development:

```sh
cd apps/manager
npm install
npm run dev
```

TONE3000 browsing of NAM captures is part of the hosted control-plane build.
The control plane holds the TONE3000 credentials; see
`services/controlplane/README.md`.

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

- A four-slot preset screen with bank controls, master-volume status, Edit,
  direct Tuner entry, and a global Settings gear between **Bank+** and **Edit**.
- A touch-first Settings screen for changing Wi-Fi credentials without
  reflashing, choosing the panel palette, configuring MIDI/expression input,
  and selecting a 32, 64, or 128-sample audio buffer. Audio-buffer changes
  restart the supervised engine process before they take effect.
- A persistent footer with centered master volume and one-second audio-buffer
  headroom feedback calculated from recent callback time versus its deadline.
- A single left-to-right signal canvas with horizontal touch scrolling,
  per-preset scroll memory, dedicated block drag handles, and Input/Output
  jump controls.
- An asset drawer with separate All, Amps, Cabs, Utility, Modulation, Delays,
  and Reverbs filters. Utility contains compressor, Noise Gate, and EQ blocks.
- `+` insertion points between every top-level effect and between effects in
  each split lane. Top-level insertion offers `Split Left / Right`; lane
  insertion prevents nested split regions.
- Compact lane handles reorder effects within a rail or move them across the
  Left and Right rails; moving the final block out of a lane is rejected.
- Dual Rig presets render as an explicit `SPLIT`, separate color-coded `LEFT`
  and `RIGHT` rails, and a stereo `JOIN` instead of disguising routing as an
  ordinary effect card.
- Long chains remain on the same horizontal signal line and auto-scroll when a
  dragged block reaches either screen edge.
- Two-row, three-column parameter pages using large horizontal sliders with
  inline labels/values, plus a matching rectangular bypass control.
- A dedicated five-band parametric EQ editor with high-pass and low-pass stages,
  selectable 6–24 dB/oct slopes, and a live response graph.
- A muted tuner with note/frequency/cents guidance and both touchscreen Exit
  and footswitch exit.

Build and launch the desktop simulator with:

```sh
./scripts/build-sim.sh
```

In Edit mode, tap any `+` on the mono rail and choose **Split Left / Right** to
create a version-2 split region. The Split starts with NAM and IR in both
lanes, using the first installed model and IR when available. Tap a lane's `+`
to add another effect directly to that rail. Only one active Split is
supported, split regions cannot be nested, and existing standalone NAM/IR
blocks must be removed before inserting the Split.

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

Built-in algorithms are listed first, then assets discovered from
`models/*.nam`, `irs/*.wav`, and `reverb-irs/*.wav`. Cabinet and
convolution reverb impulses stay in their own drawer categories. Compressor,
Noise Gate, EQ, and Daisy effects come from built-in catalogs. The simulator
saves chain edits only when Save is pressed.

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
