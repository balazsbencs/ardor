# Pedal Platform Master Implementation Roadmap

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this roadmap phase-by-phase. Each referenced phase plan contains task-level checkbox steps, tests, and commit commands.

**Goal:** Execute the remaining pedal-platform phases in a safe order, turning the current POC into a preset-driven UI/audio/runtime stack validated on the Raspberry Pi 4B (1GB).

**Architecture:** Keep the existing detailed phase plans as the source of truth. This roadmap defines dependency order, integration gates, and stop/go checks between phases so each piece lands cleanly before the next one depends on it.

**Architecture decision (binding for Phases 6–7):** The on-device app is **one process** that owns LVGL and the audio thread. There is no separate UI daemon and no IPC. See `docs/superpowers/plans/2026-07-09-ui-audio-integration.md`.

**Tech Stack:** C++20, CMake, LVGL (SDL on desktop, in-tree fbdev/evdev drivers on the Pi), miniaudio, Buildroot external tree, Linux input events, SysV init.

## Already Implemented

These plans are complete; their checkboxes are historical. Do not re-execute them:

- `docs/superpowers/plans/2026-07-07-pedal-poc.md`
- `docs/superpowers/plans/2026-07-07-preset-ui-architecture.md`
- `docs/superpowers/plans/2026-07-08-lvgl-ui-shell.md`
- `docs/superpowers/plans/2026-07-08-audio-engine-integration.md`
- `docs/superpowers/plans/2026-07-08-preset-driven-cli.md`

## Global Constraints

- No new dependencies unless a phase plan explicitly names one already justified there.
- Keep one serial chain for v1.
- Keep `48000 Hz`, `--block-size 64`, `--ir-samples 8192` as the known-good realtime baseline.
- Keep preset assets relative to `--data-root`.
- Do not load presets, NAM files, WAV files, allocate memory, or reset/prewarm the NAM model inside the realtime audio callback. "Allocate" includes buffer resizes and convolver repartitioning — all buffers are sized at load time.
- Commit after each task in the referenced plans.
- Run the phase gate before starting the next phase.

---

## Source Plans

- `docs/superpowers/plans/2026-07-08-ui-save-load-wiring.md`
- `docs/superpowers/plans/2026-07-08-runtime-preset-switching.md`
- `docs/superpowers/plans/2026-07-08-effect-parameters-v1.md`
- `docs/superpowers/plans/2026-07-08-realtime-robustness.md`
- `docs/superpowers/plans/2026-07-08-hardware-controls-integration.md`
- `docs/superpowers/plans/2026-07-09-ui-audio-integration.md`
- `docs/superpowers/plans/2026-07-08-pi-buildroot-target.md`

---

## Execution Order

```text
0. Pi Feasibility Spike (gate for everything below)
1. UI Save/Load Wiring
2. Runtime Preset Switching
3. Effect Parameters V1
4. Realtime Robustness
5. Hardware Controls Integration
6. UI + Audio Single-Process Integration
7. Pi Buildroot Target
```

Why this order:

- The Pi spike validates the CPU budget assumption **before** five phases of work are stacked on top of it. If NAM at block 64 does not fit on a Cortex-A72, that changes model-tier targets and possibly block size, and every later plan inherits that.
- UI Save/Load makes the UI own real preset files.
- Runtime Preset Switching makes preset slots audible in the engine.
- Effect Parameters needs real save/load so edits persist.
- Realtime Robustness needs slot switching to stress reloads and display runtime state.
- Hardware Controls should feed the already-working slot-switching boundary.
- UI + Audio Integration merges the two halves into the one process the device actually ships; without it, the touchscreen changes nothing audible.
- Pi Buildroot Target packages the integrated app shape, not a half-wired one.

---

### Phase 0: Pi Feasibility Spike

**Plan:** inline (this is a spike, not a feature phase).

**Purpose:** Measure, on the real Pi 4B, whether NAM + partitioned IR convolution fits the 1.33 ms callback budget at block 64 — before building more on that assumption.

**Prerequisite code fixes (these invalidate any benchmark if skipped):**

- [ ] NAM processes per block, not per sample (`NamProcessor::processBlock` calling `model->process(in, out, frames)` once; `PedalEngine::processBlock` currently loops single-sample calls).
- [ ] NAM prewarm-on-reset disabled after load (`SetPrewarmOnReset(false)`); reset/prewarm never runs on the audio thread.
- [ ] All realtime buffers preallocated at load time (`PedalEngine::namBlock_`, backend blocks, `IrConvolver` partitions for the fixed block size).

**Run These Tasks:**

- [ ] Cross-compile `pedal-poc` for aarch64 (any working toolchain; a full Buildroot image is not required for this spike — SSH into Raspberry Pi OS Lite is fine).
- [ ] Set the cpufreq governor to `performance` for the measurement.
- [ ] Build and run the per-component DSP microbenchmark from `docs/superpowers/plans/2026-07-09-ir-convolver-performance.md` Task 1 — it separates NAM cost from IR-convolver cost, which the lumped telemetry cannot.
- [ ] Run `--offline` renders with a representative standard WaveNet `.nam` and a feather/lite `.nam`, timed; compute per-block cost vs the 1.33 ms budget.
- [ ] If audio hardware is available, run `--realtime` and record the once-per-second telemetry for 10 minutes; record `vcgencmd measure_temp` and `vcgencmd get_throttled` before/after.
- [ ] Record results in `docs/hardware-validation.md`, including which model tier fits with >30% headroom.

**Phase Gate:**

- A named model tier (standard or feather) runs with >30% CPU headroom at `48000/64` on the Pi 4B.

**Stop If:**

- No model tier fits at block 64 → decide block 128 fallback or model constraints **now**, update the baseline constants in every plan, then continue.
- The IR convolver measures above 200 µs/block (15% of budget) on the Pi → execute `docs/superpowers/plans/2026-07-09-ir-convolver-performance.md` Tasks 2–3 (and 4 only if its gate fails) before declaring the spike done. If the convolver is under 200 µs, that plan's optimization tasks do **not** run — record the numbers and move on.

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
- **The audible stop→start switch gap is measured and recorded** (record it in `docs/preset-runtime-testing.md`). This number defines whether the deferred lock-free swap is needed: if the gap exceeds ~150 ms, schedule the lock-free swap before Phase 7.

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
- The safety limiter ceiling is stored per preset but is **not** user-editable in the UI (it is a protective device, not a tone control).
- Daisy modulation, delay, and reverb are available only on the
  `codex/daisy-effects-integration` branch until Pi headroom is measured.

**Stop If:**

- Editing a parameter auto-saves without pressing Save.
- Unsupported effect params start changing audio behavior.

---

### Phase 4: Realtime Robustness

**Plan:** `docs/superpowers/plans/2026-07-08-realtime-robustness.md`

**Purpose:** Add reload stress coverage, memory locking, and make realtime overrun/bypass status visible in UI.

**Run These Tasks:**

- [ ] Task 1: Add Shared Runtime Telemetry Snapshot
- [ ] Task 2: Show Telemetry In LVGL UI State
- [ ] Task 3: Add Preset Reload Stress Test
- [ ] Task 4: Lock Memory In Realtime Mode
- [ ] Task 5: Document Robustness Baseline

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
- UI shows `LIVE` or `BYPASS`. (In the desktop simulator these are test-fed values; they become live device values in Phase 6 — that is expected, not a gap.)
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

- `KEY_F1` through `KEY_F4` select preset slots 0 through 3 (matching the device-tree sketch in `docs/hardware-assembly.md`, which uses the same codes).
- Encoder relative events change master volume from `0%` to `100%`, starting from the safe boot default defined in the plan.
- macOS build still works without compiling Linux input implementation.

**Stop If:**

- Control polling enters the audio callback.
- The simulator grows hardware-control emulation before real Pi input works.

---

### Phase 6: UI + Audio Single-Process Integration

**Plan:** `docs/superpowers/plans/2026-07-09-ui-audio-integration.md`

**Purpose:** Merge LVGL into the realtime app so the display, footswitches, encoder, and engine are one process with one switch boundary and real telemetry.

**Run These Tasks:**

- [ ] Task 1: Deferred UI Rebuild (fix the rebuild-during-event pattern)
- [ ] Task 2: Link UI Into pedal-poc Behind `--ui`
- [ ] Task 3: UI Actions Drive The Engine
- [ ] Task 4: Document The Integrated Mode

**Phase Gate:**

Run:

```bash
cmake --build build --target pedal-poc pedal-ui-sim pedal-ui-model-smoke
ctest --test-dir build -R pedal-ui-model-smoke --output-on-failure
./build/pedal-poc --realtime --ui --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 --block-size 64 --ir-samples 8192
git diff --check
```

Expected:

- Touching a preset slot in the UI audibly switches the engine.
- Saving an edited preset reloads the active slot (audible after Save).
- UI telemetry shows live values; encoder volume and UI master % agree.
- The audio callback gained no new work.

**Stop If:**

- The UI blocks the control loop long enough to delay slot-switch servicing past one second.
- Any LVGL call happens on the audio thread.

---

### Phase 7: Pi Buildroot Target

**Plan:** `docs/superpowers/plans/2026-07-08-pi-buildroot-target.md`

**Purpose:** Produce a bootable Raspberry Pi 4 SD-card image that starts the integrated app, survives power loss, and passes Codec Zero validation.

**Run These Tasks:**

- [ ] Task 1: Bootable Defconfig And Board Files
- [ ] Task 2: Package The Integrated Binary, Asset Layout, And Default Presets
- [ ] Task 3: Boot Service With Supervision, Governor, And Mixer Restore
- [ ] Task 4: Power-Loss-Safe Preset Storage
- [ ] Task 5: First Boot And Codec Zero Verification Checklist

**Phase Gate:**

Run from repo:

```bash
sh -n buildroot/external/package/ardor-pedal/S99ardor-pedal
git diff --check
```

Run from a Buildroot checkout:

```bash
make BR2_EXTERNAL=/Users/bbalazs/Documents/Ardor/buildroot/external raspberrypi4_ardor_pedal_defconfig
make
```

Pi first-boot check: follow `docs/hardware-validation.md` § Raspberry Pi Buildroot First Boot.

Expected:

- `make` produces a flashable `sdcard.img` (boot partition + rootfs), not just a rootfs.
- Codec Zero appears in ALSA and passes audio after the shipped mixer state is restored.
- The integrated UI starts fullscreen on the DSI display via LVGL fbdev.
- Guitar input produces stereo output; slot touch on screen is audible.
- Killing the app process results in automatic respawn within ~2 s.
- Yanking power during a preset save never produces an unloadable preset (torn file recovery verified).

**Stop If:**

- The image requires user assets to be committed into git.
- The service hardcodes one model/IR path instead of using preset slot mode.
- The rootfs is left writable at runtime without a decision recorded in the plan.

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
./build/pedal-poc --realtime --ui --data-root . --bank 0 --slot 0 \
  --capture-device 1 --playback-device 1 \
  --input-channel left --output-channel both \
  --block-size 64 --ir-samples 8192
```

Final expected state:

- UI edits and saves real preset files.
- Runtime loads and switches preset slots from UI, footswitches, and stdin through one boundary.
- Globals and cab params persist and affect audio.
- Telemetry is visible in CLI and UI with live values.
- Buildroot produces a bootable, power-loss-tolerant image for Pi validation.

---

## V1 Decisions Recorded (so nobody re-litigates them)

- **Update story:** none for v1. Firmware updates are "reflash the SD card". Factory reset is the same operation. Documented in `docs/hardware-validation.md` by Phase 7.
- **Empty first boot:** the image ships four valid default presets (empty block chains — clean pass-through), so the app never crash-loops on missing presets. User assets replace them.
- **Startup order:** audio service starts ALSA-muted, restores the Codec Zero mixer state, loads the engine, then unmutes — no boot pop.
- **Preset banks:** the format supports 100 banks × 4 slots; v1 hardware and UI expose exactly bank 0. Validation keeps the full range so files stay forward-compatible.
- **Legacy CLI flags:** `--model/--ir` stay until slot mode is the Pi boot path, then get deleted (tracked below).

## Deferred Until After Pi Validation

- **Daisy effects integration** (`docs/superpowers/plans/2026-07-08-daisy-effects-integration.md`) — developed early on `codex/daisy-effects-integration`, but do not merge into the Pi validation line until Task 8's headroom rule (>30%) is measured on the Pi, not macOS.
- Bank up/down footswitch combinations.
- Multiple NAM/cab stereo chains.
- Modulation, delay, and reverb DSP (arrives with the Daisy plan).
- Lock-free live engine swap — unless the Phase 2 measured switch gap exceeds ~150 ms, in which case it moves before Phase 7.
- Continuous live parameter preview while editing (v1 is audible-after-Save; see integration plan).
- Removing legacy `--model/--ir` CLI flags and their smoke coverage.
- Buildroot CI image publishing.
- Hardware watchdog integration (BCM2711 has one; wire it up once field data shows hangs, not before).
