# Pedal Platform Master Implementation Roadmap

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this roadmap phase-by-phase. Each referenced phase plan contains task-level checkbox steps, tests, and commit commands.

**Goal:** Execute the next six pedal-platform phases in a safe order, turning the current POC into a preset-driven UI/audio/runtime stack ready for Raspberry Pi validation.

**Architecture:** Keep the existing detailed phase plans as the source of truth. This roadmap defines dependency order, integration gates, and stop/go checks between phases so each piece lands cleanly before the next one depends on it.

**Tech Stack:** C++20, CMake, LVGL, SDL simulator, miniaudio, Buildroot external tree, Linux input events, SysV init.

## Global Constraints

- No new dependencies unless a phase plan explicitly names one already justified there.
- Keep one serial chain for v1.
- Keep `48000 Hz`, `--block-size 64`, `--ir-samples 8192` as the known-good realtime baseline.
- Keep preset assets relative to `--data-root`.
- Do not load presets, NAM files, WAV files, or allocate new engines inside the realtime audio callback.
- Commit after each task in the referenced plans.
- Run the phase gate before starting the next phase.

---

## Source Plans

- `docs/superpowers/plans/2026-07-08-ui-save-load-wiring.md`
- `docs/superpowers/plans/2026-07-08-runtime-preset-switching.md`
- `docs/superpowers/plans/2026-07-08-effect-parameters-v1.md`
- `docs/superpowers/plans/2026-07-08-realtime-robustness.md`
- `docs/superpowers/plans/2026-07-08-hardware-controls-integration.md`
- `docs/superpowers/plans/2026-07-08-pi-buildroot-target.md`

---

## Execution Order

```text
1. UI Save/Load Wiring
2. Runtime Preset Switching
3. Effect Parameters V1
4. Realtime Robustness
5. Hardware Controls Integration
6. Pi Buildroot Target
```

Why this order:

- UI Save/Load makes the UI own real preset files.
- Runtime Preset Switching makes preset slots audible in the engine.
- Effect Parameters needs real save/load so edits persist.
- Realtime Robustness needs slot switching to stress reloads and display runtime state.
- Hardware Controls should feed the already-working slot-switching boundary.
- Pi Buildroot Target should package the integrated app shape, not a half-wired one.

---

### Phase 1: UI Save/Load Wiring

**Plan:** `docs/superpowers/plans/2026-07-08-ui-save-load-wiring.md`

**Purpose:** Make the LVGL simulator load preset JSON, select slots from files, and manually save edited chains.

**Run These Tasks:**

- [ ] Task 1: Add UI File Helpers
- [ ] Task 2: Wire LVGL Actions To The Store
- [ ] Task 3: Document Simulator Preset Files

**Phase Gate:**

Run:

```bash
cmake --build build --target pedal-ui-sim pedal-ui-model-smoke
ctest --test-dir build -R pedal-ui-model-smoke --output-on-failure
SDL_VIDEODRIVER=dummy ./build/pedal-ui-sim --data-root . --bank 0
git diff --check
```

Expected:

- UI model smoke passes.
- Simulator starts from `--data-root`.
- Four preset slots are loaded from disk or shown as empty.
- Save writes active preset JSON back to disk.

**Stop If:**

- The simulator still depends on `makeDemoUiState()` as its only source of presets.
- Manual save cannot round-trip edited chain order.

---

### Phase 2: Runtime Preset Switching

**Plan:** `docs/superpowers/plans/2026-07-08-runtime-preset-switching.md`

**Purpose:** Make `pedal-poc` load bank/slot presets and switch realtime slots outside the audio callback.

**Run These Tasks:**

- [ ] Task 1: Reuse Preset-Slot Engine Loading
- [ ] Task 2: Add Slot-Based Offline And Realtime CLI Loading
- [ ] Task 3: Add Realtime Switch Boundary
- [ ] Task 4: Document Realtime Switching

**Phase Gate:**

Run:

```bash
cmake --build build --target pedal-poc pedal-preset-cli-smoke
ctest --test-dir build -R pedal-preset-cli-smoke --output-on-failure
git diff --check
```

Manual check with the UMC22:

```bash
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 \
  --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

Expected:

- `--preset FILE` still works.
- `--data-root DIR --bank N --slot N` works offline and realtime.
- Typing `0`, `1`, `2`, or `3` plus Enter requests a slot switch.
- Reload work happens outside the miniaudio callback.

**Stop If:**

- Realtime switching tries to parse JSON, read WAV/NAM files, or allocate a new engine in the audio callback.
- Failed preset reload leaves audio stopped when the previous engine could have continued.

---

### Phase 3: Effect Parameters V1

**Plan:** `docs/superpowers/plans/2026-07-08-effect-parameters-v1.md`

**Purpose:** Persist and edit global params and minimal cab params.

**Run These Tasks:**

- [ ] Task 1: Round-Trip UI Globals And Block Params
- [ ] Task 2: Add LVGL Global Param Drawer
- [ ] Task 3: Apply Cab Level And Mix
- [ ] Task 4: Edit Cab Params In The Drawer
- [ ] Task 5: Document V1 Params

**Phase Gate:**

Run:

```bash
cmake --build build --target pedal-ui-sim pedal-ui-model-smoke pedal-engine-contract-smoke pedal-preset-smoke pedal-preset-cli-smoke
ctest --test-dir build -R "pedal-ui-model-smoke|pedal-engine-contract-smoke|pedal-preset-smoke|pedal-preset-cli-smoke" --output-on-failure
SDL_VIDEODRIVER=dummy ./build/pedal-ui-sim --data-root . --bank 0
git diff --check
```

Expected:

- Preset globals round-trip through UI save/load.
- Block `params` round-trip through UI save/load.
- Cab `levelDb` and `mix` are applied by the engine.
- Modulation, delay, and reverb remain stored-only or unsupported.

**Stop If:**

- Editing a parameter auto-saves without pressing Save.
- Unsupported effect params start changing audio behavior.

---

### Phase 4: Realtime Robustness

**Plan:** `docs/superpowers/plans/2026-07-08-realtime-robustness.md`

**Purpose:** Add reload stress coverage and make realtime overrun/bypass status visible in UI.

**Run These Tasks:**

- [ ] Task 1: Add Shared Runtime Telemetry Snapshot
- [ ] Task 2: Show Telemetry In LVGL UI State
- [ ] Task 3: Add Preset Reload Stress Test
- [ ] Task 4: Document Robustness Baseline

**Phase Gate:**

Run:

```bash
cmake --build build --target pedal-preset-smoke pedal-ui-model-smoke pedal-preset-reload-stress pedal-ui-sim pedal-poc
ctest --test-dir build -R "pedal-preset-smoke|pedal-ui-model-smoke|pedal-preset-reload-stress" --output-on-failure
SDL_VIDEODRIVER=dummy ./build/pedal-ui-sim --data-root . --bank 0
git diff --check
```

Manual realtime soak:

```bash
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 \
  --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

Expected:

- CLI telemetry keeps the existing once-per-second shape.
- UI shows `LIVE` or `BYPASS`.
- Reload stress test passes.
- Overrun bypass still latches after repeated overloads.

**Stop If:**

- UI telemetry requires realtime audio to be running inside the simulator.
- Telemetry code adds work to the audio callback.

---

### Phase 5: Hardware Controls Integration

**Plan:** `docs/superpowers/plans/2026-07-08-hardware-controls-integration.md`

**Purpose:** Map Pi footswitches and encoder events to preset slots and master output volume.

**Run These Tasks:**

- [ ] Task 1: Add Hardware-Neutral Control Events
- [ ] Task 2: Add Linux Input Reader
- [ ] Task 3: Wire Controls Into Realtime Slot Mode
- [ ] Task 4: Document Pi Input Setup

**Phase Gate:**

Run:

```bash
cmake --build build --target pedal-poc pedal-control-smoke pedal-preset-cli-smoke
ctest --test-dir build -R "pedal-control-smoke|pedal-preset-cli-smoke" --output-on-failure
git diff --check
```

Linux/Pi manual check:

```bash
evtest /dev/input/eventX
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --control-device /dev/input/event-footswitches \
  --control-device /dev/input/event-encoder \
  --block-size 64 --ir-samples 8192
```

Expected:

- `KEY_F1` through `KEY_F4` select preset slots 0 through 3.
- Encoder relative events change master volume from `0%` to `100%`.
- macOS build still works without compiling Linux input implementation.

**Stop If:**

- Control polling enters the audio callback.
- The simulator grows hardware-control emulation before real Pi input works.

---

### Phase 6: Pi Buildroot Target

**Plan:** `docs/superpowers/plans/2026-07-08-pi-buildroot-target.md`

**Purpose:** Package the integrated app into a Raspberry Pi 4 Buildroot image with startup services and asset directories.

**Run These Tasks:**

- [ ] Task 1: Package Both Runtime Binaries And Asset Layout
- [ ] Task 2: Boot Audio Service From Config
- [ ] Task 3: Boot Fullscreen LVGL UI
- [ ] Task 4: Codec Zero Verification Checklist

**Phase Gate:**

Run from repo:

```bash
sh -n buildroot/external/package/ardor-pedal/S98ardor-ui
sh -n buildroot/external/package/ardor-pedal/S99ardor-pedal
git diff --check
```

Run from a Buildroot checkout:

```bash
make BR2_EXTERNAL=/Users/bbalazs/Documents/Ardor/buildroot/external raspberrypi4_ardor_pedal_defconfig
make ardor-pedal-dirclean
make ardor-pedal
make
```

Pi first-boot check:

```bash
cat /etc/ardor-pedal.env
ls -R /opt/ardor-pedal
/etc/init.d/S98ardor-ui restart
/etc/init.d/S99ardor-pedal restart
aplay -l
arecord -l
tail -f /var/log/messages
```

Expected:

- `/usr/bin/ardor-pedal` and `/usr/bin/ardor-ui` are installed.
- `/opt/ardor-pedal/models`, `/irs`, and `/presets/bank-000` exist.
- UI starts fullscreen.
- Codec Zero appears in ALSA.
- Guitar input produces stereo output.

**Stop If:**

- The image requires user assets to be committed into git.
- The service hardcodes one model/IR path instead of using preset slot mode.

---

## Final Integration Gate

Run on desktop:

```bash
cmake --build build --target pedal-poc pedal-ui-sim pedal-offline-smoke pedal-preset-smoke pedal-engine-contract-smoke pedal-ui-model-smoke pedal-preset-cli-smoke pedal-preset-reload-stress pedal-control-smoke
ctest --test-dir build --output-on-failure
SDL_VIDEODRIVER=dummy ./build/pedal-ui-sim --data-root . --bank 0
git status --short
```

Run manually with UMC22:

```bash
./build/pedal-poc --realtime --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 \
  --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

Final expected state:

- UI edits and saves real preset files.
- Runtime loads and switches preset slots.
- Globals and cab params persist and affect audio.
- Telemetry is visible in CLI and UI.
- Footswitch/encoder path is ready for Linux input devices.
- Buildroot package installs services and asset layout for Pi validation.

---

## Deferred Until After Pi Validation

- Bank up/down footswitch combinations.
- Multiple NAM/cab stereo chains.
- Modulation, delay, and reverb DSP.
- Automatic Codec Zero mixer configuration.
- Lock-free live engine swap.
- Buildroot CI image publishing.
