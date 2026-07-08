# UI + Audio Single-Process Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the on-device UI and the realtime audio engine one process, so touching a preset on the display is audible, saving a preset reloads the running engine, and the telemetry the UI renders is real.

**Architecture Decision (resolves the roadmap's open question):** One process owns both LVGL and the audio thread. The miniaudio callback stays untouched; the main thread becomes the control thread: it runs `lv_timer_handler()` on a short tick, polls control devices, samples telemetry once per second, and services preset-switch requests at the existing stop→build→swap→start boundary. There is no IPC, no second daemon, and no shared-memory protocol. `pedal-ui-sim` remains a desktop-only UI simulator and is not shipped on the Pi.

**Display/input backend decision:** On the Pi, use LVGL's built-in Linux drivers — `LV_USE_LINUX_FBDEV` for the DSI display's `/dev/fb0` and `LV_USE_EVDEV` for touch. This removes SDL2, Mesa, and udev from the target image entirely. (`SDL_VIDEODRIVER=fbcon` does not exist in SDL2; that approach is dead.) On desktop, the existing SDL window path stays for `pedal-ui-sim` and for `pedal-poc --ui` development.

**Tech Stack:** C++20, LVGL v9.5 (fbdev/evdev drivers are in-tree), miniaudio, existing `UiModel`, `LvglUi`, `EngineLoader`, `RuntimeState` telemetry helpers.

## Global Constraints

- The audio callback gains no new work. All UI, file, and control handling happens on the main/control thread.
- The UI redraw pattern must not delete the LVGL object currently dispatching an event; event handlers set a rebuild flag consumed by the `lv_timer_handler` loop.
- Preset switches requested from the UI go through the same `requestedSlot` boundary as keyboard and footswitch switches — one switch path, three sources.
- Keep `48000 Hz`, `--block-size 64`, `--ir-samples 8192` as the realtime baseline.
- Do not add dependencies beyond LVGL config flags already vendored via FetchContent.

---

## Dependency

Implement this after:

1. `docs/superpowers/plans/2026-07-08-ui-save-load-wiring.md`
2. `docs/superpowers/plans/2026-07-08-runtime-preset-switching.md`
3. `docs/superpowers/plans/2026-07-08-realtime-robustness.md` (Tasks 1–2: telemetry helpers and `UiState::telemetry`)
4. `docs/superpowers/plans/2026-07-08-hardware-controls-integration.md`

Implement this **before** `docs/superpowers/plans/2026-07-08-pi-buildroot-target.md`, which packages the integrated binary.

---

## File Structure

- `apps/pedal-poc/main.cpp`: add `--ui`, restructure the realtime loop into a control-loop tick (LVGL + controls + telemetry + slot switching).
- `src/ui/LvglUi.h` / `src/ui/LvglUi.cpp`: replace rebuild-inside-event-callback with a deferred rebuild flag.
- `lv_conf.h`: enable `LV_USE_LINUX_FBDEV` and `LV_USE_EVDEV` for Linux target builds.
- `CMakeLists.txt`: link `ardor_ui` into `pedal-poc`; make SDL optional (desktop only).
- `tests/ui_model_smoke.cpp`: cover UI-originated slot request mapping.
- `README.md`, `docs/preset-runtime-testing.md`: document the integrated run mode.

---

### Task 1: Deferred UI Rebuild (fix the rebuild-during-event pattern)

**Why first:** every later task adds event handlers; they must land on a safe redraw pattern. `LvglUi::build()` currently calls `lv_obj_clean(root)` and `contexts_.clear()` from inside LVGL event callbacks, deleting the object that is dispatching the event and destroying the `UiEventContext` the running callback holds. LVGL forbids deleting the currently-handled object from its own event handler.

- [ ] **Step 1:** Add `bool rebuildRequested_` to `LvglUi` with `requestRebuild()`; event handlers call `requestRebuild()` instead of `build()`.
- [ ] **Step 2:** The owner loop (`pedal-ui-sim` main loop, later the `pedal-poc` control loop) checks `consumeRebuildRequest()` after `lv_timer_handler()` and calls `build()` from outside event dispatch.
- [ ] **Step 3:** Build, run `pedal-ui-sim`, verify preset select / drawer open / drag-drop still work.
- [ ] **Step 4:** Commit: `fix: defer lvgl rebuild out of event handlers`

---

### Task 2: Link UI Into pedal-poc Behind `--ui`

- [ ] **Step 1:** In `CMakeLists.txt`, link `ardor_ui` into `pedal-poc` and add `ARDOR_UI_BACKEND` (values: `sdl` default, `fbdev`). `sdl` keeps `find_package(SDL2 REQUIRED)`; `fbdev` must not reference SDL at all and enables the LVGL Linux drivers instead. The Buildroot plan passes `-DARDOR_UI_BACKEND=fbdev` — this exact option name is a cross-plan contract. While here, delete the dead `ARDOR_ENABLE_REALTIME` and `ARDOR_ENABLE_NAM` options (declared, never referenced).
- [ ] **Step 2:** In `lv_conf.h`, enable `LV_USE_LINUX_FBDEV=1` and `LV_USE_EVDEV=1` (harmless on desktop; drivers compile only when used).
- [ ] **Step 3:** Add `--ui` to `Args`. In realtime slot mode with `--ui`, initialize LVGL and create the display: SDL window on desktop, `lv_linux_fbdev_create()` + `lv_evdev_create(LV_INDEV_TYPE_POINTER, ...)` on Linux.
- [ ] **Step 4:** Replace the fixed `sleep_for(1s)` telemetry loop with a tick loop: `lv_timer_handler()` + control polling every ~5 ms, telemetry sampling and printing once per second (accumulate ticks). The slot-switch service point stays in this loop.
- [ ] **Step 5:** Build both platforms' desktop path; run `./build/pedal-poc --realtime --ui --data-root . --bank 0 --slot 0 ...` and confirm audio runs with the UI visible.
- [ ] **Step 6:** Commit: `feat: run lvgl ui inside pedal-poc`

---

### Task 3: UI Actions Drive The Engine

- [ ] **Step 1 (failing test):** In `tests/ui_model_smoke.cpp`, assert that selecting a preset in `UiState` surfaces a consumable pending-slot request (`int consumePendingSlotRequest(UiState&)` returning `-1` when none).
- [ ] **Step 2:** Implement the pending-slot field + helper in `UiModel`; `selectPreset` sets it.
- [ ] **Step 3:** In the control loop, feed `consumePendingSlotRequest` into the existing `requestedSlot` boundary. Footswitch, stdin, and UI now share one switch path.
- [ ] **Step 4:** After a successful UI Save (from the ui-save-load plan), request a reload of the active slot through the same boundary so edits become audible without a manual switch. This is the v1 form of "live preview": audible after Save, not per-keystroke. The spec's continuous live-preview-while-editing is explicitly deferred (see `docs/superpowers/specs/2026-07-07-preset-ui-architecture-design.md` § V1 Scope Adjustments).
- [ ] **Step 5:** Reflect engine-side state back into the UI each second: `updateRealtimeTelemetry(state, telemetry)` (real values now), and `state.masterVolume = controls.masterVolume` so the encoder and the display agree.
- [ ] **Step 6:** Build, run tests, manual check: touch a preset slot → audio switches; turn encoder → UI master % moves; force overload → UI shows `BYPASS`.
- [ ] **Step 7:** Commit: `feat: wire ui actions into live engine`

---

### Task 4: Document The Integrated Mode

- [ ] **Step 1:** README: document `--ui` and note that `pedal-ui-sim` is desktop-only.
- [ ] **Step 2:** `docs/preset-runtime-testing.md`: add an integrated-mode checklist (slot touch is audible, Save reloads, telemetry live, encoder/UI volume agree).
- [ ] **Step 3:** Commit: `docs: document integrated ui audio mode`

---

## Skipped For This Phase

- IPC/multi-process designs — rejected, not deferred; one process is the architecture.
- Continuous live parameter preview while editing (per-keystroke engine updates); v1 is audible-after-Save. Revisit only if Save-then-hear proves unusable.
- Touchscreen calibration UI; evdev raw coordinates are correct for the official DSI display.
