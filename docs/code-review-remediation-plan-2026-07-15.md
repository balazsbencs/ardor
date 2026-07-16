# Code Review Verification and Remediation Plan (2026-07-15)

## Scope and method

This document verifies the findings in `docs/code-review-2026-07-15.md` against the current
`feat/lvgl-flat-knob-redesign` working tree, including its uncommitted changes. It records the
verification and plan; the subsequent implementation progress is tracked below.

Verification used the current source, generated CMake state, the pinned Buildroot 2025.02.15
output in `buildroot_2025_02_15`, miniaudio 0.11.25 and NeuralAmpModelerCore sources fetched by
the build, and the existing test suite.

## Implementation progress (2026-07-15)

The initial, low-risk remediation pass is implemented and covered by the existing smoke suite:

- Phase 1: block-size validation, evdev draining, independent control/command/telemetry cadences,
  non-throwing runtime, asset, and touchscreen-device enumeration, and process-lifetime stdin request state.
- Phase 2 (safety/state subset): external UI invalidation, safe EQ interaction ordering, drag
  thresholds and `PRESS_LOST` cleanup, runtime graph coordinates, two-decimal telemetry, and safe
  LVGL context teardown ordering. Knob and EQ graph updates now retain stable handles rather than
  discovering children by text, geometry, type, or creation order; focused encoder edits update
  the existing widget tree in place. A persistent status line now displays save, asset reload,
  EQ-update, and preset activation/rollback outcomes on-device.
- Phase 3 (safety/observability subset): the Linux callback now requests and records Ardor-owned
  `SCHED_FIFO/70`; the production CLI rejects a normal-priority fallback unless the explicit
  development-only `--allow-non-realtime` override is supplied. Production Linux startup also
  rejects a native capture/playback rate other than 48 kHz unless the explicit development-only
  `--allow-device-resampling` override is supplied. Device-stop publication, native stream-rate
  and period logging, RAII backend state, removal of dead callback bookkeeping, and a
  bounded, typed engine-swap result that quiesces the backend before returning on device loss or
  timeout are also complete. A bounded three-attempt backoff controller now reopens a stopped
  device from the control loop while preserving the prepared engine; device loss during a preset
  swap is requeued after recovery. Aligned device callbacks now process their complete prepared
  quantum directly, avoiding the adapter's former extra 64-frame latency; irregular callbacks
  retain the FIFO path. The Buildroot service now applies the governor to every cpufreq policy,
  and its stale AArch64 VFP setting is removed.
- Phase 7 (input boundary subset): non-finite capture samples are replaced with silence before
  they can enter a stateful DSP block, and the engine exposes a monotonic fault counter. Prepared
  IR/engine quantum mismatches now fail boundedly with silence and a counter instead of entering
  state-incoherent direct convolution. The first AArch64 audio callback now enables FPCR.FZ once;
  block and sample processing retain a scoped FTZ/DAZ guard for offline work, and x86 offline
  processing receives the same protection that miniaudio callbacks already provide. The chain contract now documents the
  intentional mono wet feed for hosted delay/reverb while preserving stereo dry and wet output.
  Runtime-chain block boundaries and the retained offline/sample API now sanitize non-finite output, count faults, retain the first
  offending prepared block ID (including configured NAM and cabinet IDs), and surface new faults in both realtime modes (and the UI where
  present). Slot-based realtime now queues a clean prepared reload of the active preset after a
  reported fault; non-slot realtime still reports the fault only because it has no preset source
  from which to prepare a replacement.
- Phase 6 (default-correction subset): chorus alone defaults to a 50/50 dry/wet mix; other
  modulation modes retain their existing defaults pending listening validation. The compressor no
  longer applies attack/release twice: detector smoothing defines its timing and the static gain
  computer applies that result directly. Peak step-response attack and release regressions now
  protect that timing behavior; RMS-specific timing coverage remains.
- Phase 5 (Daisy subset): hosted Daisy blocks retain atomically published normalized parameter
  targets and consume them at their existing 48-sample control cadence. The UI now sends its
  continuous modulation, delay, and reverb edits to that live path by stable block ID, without a
  preset reload or callback allocation. Compressor continuous controls now use a stable-ID atomic
  revision/snapshot path that refreshes coefficients without resetting detector, sidechain, or gain
  state; detector and auto-makeup remain prepared-engine changes. Global input/output gain and the
  single supported cabinet's level/mix now use the engine's existing smoothed atomic targets.
  The UI now distinguishes these live-only saves from topology, asset, enablement, and discrete-mode
  edits: only the latter request a prepared-engine activation after save. Command-burst queue
  coverage remains outstanding.
- Phase 8 (measurement subset): single-config Release default plus benchmark build metadata and
  p50/p99/p999 latency reporting.
- Phase 9 (offline-tail subset): Daisy delay and reverb now contribute parameter-based offline
  tail estimates; serial cab/Daisy tails are accumulated rather than selecting only the cab IR.
  Automatic Daisy estimates use a documented 60-second cap for extreme feedback, while
  `--tail-seconds` and `--no-tail` remain explicit render controls.
- Phase 4: realtime preset changes now preflight stored-preset parsing,
  chain constraints, Daisy parameter finiteness, and cabinet decoding/rate validation before the
  live program is activated. A rejected preflight or failed preparation leaves the active audio
  program and UI selection unchanged. The copied Daisy implementation is built as Ardor-owned
  source (`ardor_daisyfx_runtime`), not an upstream module; all mutable delay, reverb, and
  modulation state is now per-mode-instance. The unused global registry/compatibility path was
  removed. Preset activation therefore always prepares the next engine while the old engine stays
  live, and the former muted-engine detour has been removed. Duplicate Daisy categories are now
  supported by both the loader and UI.

The remaining phases require their stated backend seams, Pi/device validation, or an explicitly
designed realtime parameter-command path. They are deliberately not
represented as completed by this initial pass.

## Follow-up verification pass (2026-07-15, second session)

Re-verified the above pass against the working tree: duplicate Daisy-category rejection, the
compressor single-smoothing fix, the chorus default mix, and the IR block-size-mismatch containment
(with a passing regression test) are all present and behave as described. Two additional items were
found and fixed:

- `apps/pedal-poc/main.cpp` referenced `uiState` outside its `#if defined(ARDOR_HAS_UI)` declaration
  at two preset-switch call sites, which failed to compile in the headless (no-UI) build. Moved both
  assignments inside the existing UI guard.
- `MiniaudioBackend`'s telemetry only distinguished a callback overrunning its own budget
  (`overBudgetCallbacks`). It had no signal for the device simply failing to wake the callback thread
  on schedule (a scheduling stall, not an overrun). Added `callbackGapCount`, tracked from the
  interval between successive callback starts against 1.5x the expected quantum period, and threaded
  it through `RealtimeStats` and `RuntimeTelemetry` end to end (backend, main loop, UI comparison,
  tests). `deviceXruns` (real ALSA xrun counting) still requires Pi hardware and remains open.

All 18 tests pass in both the headless and UI-enabled build trees after these fixes.

Baseline:

- `ctest --test-dir build --output-on-failure`: 18/18 tests pass.
- The current desktop `build/` tree is Release and reports `-O3 -DNDEBUG`.
- The generated Pi Buildroot configuration is `BR2_OPTIMIZE_2=y`.
- The generated Pi `ardor_dsp` compile flags are `-O2 -g0 ... -DNDEBUG`, not `-Os`.
- The current microbenchmark has component min/average/max results, but cannot validate the
  review's Pi estimates because it has no full-chain percentile benchmark and was run on a
  desktop host.

Verdicts used below:

- **Confirmed**: the current code contains the stated bug or gap.
- **Partial**: the underlying gap is real, but the review overstates, mislocates, or proposes an
  incomplete fix.
- **Not confirmed**: current evidence contradicts the claim.
- **Measure/decision**: a plausible optimization or product choice, not a verified defect.

## Verification results

### Build and realtime backend

| Finding | Verdict | Verification and required correction |
|---|---|---|
| Pi production build is `-Os` | **Not confirmed** | Buildroot actually selects `BR2_OPTIMIZE_2=y`; generated `ardor_dsp` flags contain `-O2`, not `-Os`. Do not treat this as the top production emergency. An explicit `-O3` decision can still be benchmarked. |
| Root CMake defaults to an unoptimized build | **Confirmed** | `CMakeLists.txt` does not set a build type when a single-config generator is configured without one. Existing blank-type build trees confirm this is possible. |
| Audio thread runs as `SCHED_OTHER` | **Confirmed** | `MiniaudioBackend.cpp` passes a null context config. miniaudio's default maps to its `highest` value, but on Linux that retains the creating thread's normal scheduler; only `ma_thread_priority_realtime` requests `SCHED_FIFO`. |
| Set realtime priority to about 70 | **Partial** | `ma_thread_priority_realtime` alone requests Linux priority 99 in this miniaudio version and silently falls back after `EPERM`. Ardor must set an explicit priority, disable or detect fallback, and report the policy actually obtained. |
| `replaceEngine()` can wait forever | **Confirmed** | Completion is produced only by the callback and the wait has no timeout. A stopped device or the callback's `!engine || !in` return can prevent completion forever. |
| No device-loss handling | **Confirmed** | No device notification callback, loss state, restart path, or state polling exists. |
| Fixed-quantum FIFO adds 64 frames | **Confirmed** | An aligned 64-frame input block is processed only after output for that callback has already been emitted, so it is returned in the next callback. |
| miniaudio may resample silently | **Confirmed** | Requested rate and native capture/playback rates may differ. The proposed check must use `capture.internalSampleRate` and `playback.internalSampleRate`; `device.sampleRate` is the requested client rate. |
| Current `xruns` are timing overruns | **Confirmed** | The counter increments when callback execution exceeds its time budget; it is not an ALSA xrun counter. `RealtimeStats::overBudget` already uses the better name, but the backend API and storage still say `xrun`. |
| Busy-yield wastes a control core | **Confirmed** | `replaceEngine()` repeatedly calls `std::this_thread::yield()` for the entire fade. A condition/event or bounded low-rate poll is appropriate off the audio thread. |
| `callbacksInFlight` is dead cost | **Confirmed** | It is incremented/decremented with sequential consistency and never read. |
| Backend ownership/contracts are weak | **Confirmed (low)** | Raw `new`/`delete` is unnecessary, and `stats()`/`stop()` have an unstated single-control-thread contract. Signal handlers are installed after audio starts. |

### Controls, main loop, loading, and lifecycle

| Finding | Verdict | Verification and required correction |
|---|---|---|
| evdev drain stops at `EV_SYN` | **Confirmed** | `LinuxInputDevice::poll()` consumes one event and returns false for ignored events. The caller's drain loop therefore stops on the synchronization event after each useful event. |
| Controls and preset requests run about once per second | **Confirmed** | With UI enabled, `continue` skips all work below it for 199 iterations of a 5 ms loop. Without UI, the loop deliberately sleeps one second. |
| Runtime command filesystem iteration can throw | **Confirmed** | A range-for over `directory_iterator(directory, ec)` still uses throwing iterator increment. The same pattern exists in asset discovery. |
| Detached stdin thread has unsafe lifetime | **Confirmed (low)** | It captures stack atomics and is never joined. Process exit usually masks the problem, but early returns and normal scope teardown leave a formal lifetime race. |
| Failed preset switch can leave the muted engine active | **Confirmed** | Recovery depends on reloading the previous preset and another successful swap. If either fails, the active engine remains the zero-volume engine; the UI has already moved to the requested preset. |
| `--block-size 0` can loop forever offline | **Confirmed** | Parsing accepts zero and the offline loop increments by `args.blockSize`. Realtime loader/backend paths reject it later, but offline mode does not. |
| Offline tails only include the cab IR | **Confirmed** | `RuntimeChain::tailFrames()` ignores delay and reverb. Explicit `--tail-seconds` is the current workaround. |
| Compressor does not assign `currentIsStereo` | **Not a bug** | A mono input remains duplicated and a previously stereo chain remains stereo, so leaving the flag unchanged is correct. Add a comment or encode the layout contract more clearly. |
| Duplicate Daisy blocks alias global state | **Fixed (2026-07-16)** | Every compiled mode now owns its mutable storage. The obsolete global registry source was removed; two same-mode delay and reverb processors are regression-tested for impulse isolation, and duplicate Daisy blocks are accepted in both loader and UI. |
| Transactional preset construction is complete | **Fixed (2026-07-16)** | A next engine is fully prepared while the active engine remains live, then activated atomically by the backend. The Daisy-specific muted-engine detour is gone. |

### DSP correctness and sound quality

| Finding | Verdict | Verification and required correction |
|---|---|---|
| NAM input-level metadata is ignored | **Confirmed** | Only `HasLoudness()`/`GetLoudness()` are used. `HasInputLevel()`/`GetInputLevel()` are available but unused. The correction must wait for a measured Codec Zero ADC full-scale dBu value; it must not guess one. |
| Every effect knob immediately rebuilds the engine | **Partial** | Non-EQ UI edits currently change only `UiState`; they do not reach the live DSP at all. Saving queues a full preset reload, which does reset Daisy/compressor state and truncate tails. The real defect is the missing live-parameter path plus rebuild-on-save. |
| IR partial-block fallback is unsafe | **Partial** | The fallback is both state-incoherent and catastrophically expensive for long IRs. Current realtime and offline entry points normally supply the prepared quantum, so this is a latent public-API/recursive-remainder hazard rather than a demonstrated steady-state Pi path. Remove it from production regardless. |
| Per-sample NAM/chain paths remain public | **Confirmed** | `NamProcessor::process`, `IrConvolver::processSample`, `RuntimeChain::process`, and `PedalEngine::process` remain available. Current live processing uses the block path, but the unsafe fallback surface remains. |
| Safety limiter is a hard clamp | **Confirmed** | `applySafety()` clamps directly to the ceiling. miniaudio also applies a final `[-1, 1]` clip, but Ardor's lower ceiling is reached first. |
| No automatic IR normalization | **Confirmed gap, product decision** | IR import validates and fades truncation but does not measure or normalize level. The existing remediation plan correctly warns that IR gain can be intentional; automatic normalization should be versioned/optional, not silently imposed on existing presets. |
| Compressor applies ballistics twice | **Confirmed** | Attack/release smooth the detector and then smooth linear gain again. The review's exact “2x/4x” timing claims are not established, but the topology does not match the labelled timing contract. |
| Chorus default mix is 100% wet | **Confirmed** | All modulation descriptors default to `mix=1.0`; the chorus mode returns wet-only. Chorus therefore defaults to vibrato-like output. Fix this per mode rather than changing every modulation effect globally. |
| NaN can poison feedback state indefinitely | **Confirmed** | Final output replaces non-finite values with zero, but callback input and internal block outputs are not sanitized and no fault causes a prepared-engine reset/replacement. |
| No denormal handling | **Partial** | miniaudio 0.11.25 already enables FTZ/DAZ around callbacks on x86/x64. Its implementation is a no-op on AArch64, and offline DSP/bench entry points are outside that callback guard. Add explicit platform handling where measurement shows it matters. |
| Equal-power bypass can add 3 dB | **Confirmed, low priority** | At a 50/50 fade, perfectly correlated equal dry/wet paths sum to approximately 1.414. This is a transition-law choice, not a steady-state error. |
| Delay/reverb input is collapsed to mono | **Confirmed, intentional constraint** | Both processors receive `(L+R)/2`; document it in the chain contract or change the vendor interface. |
| 44.1 kHz IRs are rejected | **Confirmed** | `EngineLoader` rejects any IR rate unequal to 48 kHz. Offline resampling is a feature, not an urgent correctness fix. |
| Top-band RBJ response cramps near Nyquist | **Confirmed design limitation** | The current coefficient implementation is standard RBJ. A matched design is a quality enhancement requiring response and listening tests. |
| Oversampling nonlinear vendor stages | **Measure/decision** | Plausible alias-reduction work, but no current spectral evidence or Pi budget supports a priority yet. |

### UI

| Finding | Verdict | Verification and required correction |
|---|---|---|
| External state mutations do not repaint | **Confirmed** | Bank changes, completed preset switches, master volume, telemetry, footswitch actions, and runtime commands mutate `UiState` without requesting a rebuild or targeted refresh. |
| EQ press can rebuild mid-gesture | **Confirmed** | EQ focus/selection latches `rebuildPending_` before interaction suppression begins. `refresh()` rebuilds without checking the active interaction, and `build()` does not clear `knobInteractionActive_`. |
| Encoder changes rebuild the entire tree | **Confirmed** | Encoder deltas call `requestRebuild()`. `build()` cleans and recreates the tree, and the Pi display is configured for full-frame software rendering and rotation. The precise CPU cost still needs target profiling. |
| Chain drag's trailing click selects the wrong block | **Confirmed** | Chain tiles lack the drawer asset's `suppressClick` handling. After a move, the click callback still uses the old context index. |
| Chain/asset drags ignore `PRESS_LOST` | **Confirmed** | Only normal release is registered, so opacity, ghost, and insertion indicator cleanup can be skipped. |
| EQ drag uses compile-time origin | **Confirmed** | The graph and its parents retain default-theme padding, while the drag mapping assumes only authored positions. Runtime object coordinates are the correct source of truth. |
| Telemetry prints six decimals | **Confirmed** | `std::to_string(double)` produces fixed six-decimal text after a manual truncation. |
| Asset enumeration can terminate the app | **Confirmed** | `exists`, `directory_iterator`, `is_regular_file`, and `relative` use throwing overloads on a runtime reload path. |
| No on-device error surface | **Confirmed** | Save/load/swap errors go only to stderr. |
| Child discovery relies on geometry/type heuristics | **Confirmed** | Knob and EQ refresh code searches by sizes, label text, point counts, and iteration order. A failed mutable-point lookup can desynchronize response-line indexing. |
| `contexts_.clear()` precedes object deletion | **Partial (hygiene)** | Current delete handlers do not dereference those contexts, so no present UAF was found. Reversing the order is safer for future delete handlers. |
| UI low-level cleanup items | **Confirmed (low)** | `telemetryLine` has external linkage, one `UiModel` branch is dead, bank bounds are duplicated, and the simulator has no normal exit path. |
| Repository artifacts are mixed into the worktree | **Confirmed (hygiene)** | `ardor-pedal` is tracked and modified; image/test artifacts are untracked and not ignored. Preserve user files, but exclude generated artifacts from implementation commits. |

### Performance and benchmark claims

| Finding | Verdict | Verification and required correction |
|---|---|---|
| Daisy/compressor use per-sample dispatch | **Confirmed** | `RuntimeChain::processBlock()` calls their sample methods in loops. The review's “~9k indirect calls per block” is not supported by the visible call graph; the optimization should be justified by measurement. |
| Compressor uses two transcendentals per sample | **Confirmed** | The linked gain computation performs `log10` then `pow` for each stereo frame (plus `sqrt` in RMS mode). |
| Output loop repeatedly loads atomics and takes square roots | **Confirmed** | Target loads and two square roots occur per sample even after ramps converge. `applySafety()` adds further atomic loads. |
| Convolver uses nested vectors and complex MAC | **Confirmed structure; speed unverified** | Storage is fragmented and the generic complex multiply is used. The stated 2–2.5x gain requires Pi benchmarks before committing to a rewrite. |
| Governor is set only through `cpu0` | **Confirmed** | The init script writes one sysfs path while the runbook distinguishes characterization from product policy. Enumerate policy directories/CPUs and align documentation with the shipped choice. |
| AArch64 config contains an ignored VFP option | **Confirmed** | `BR2_ARM_FPU_VFPV4=y` is stale for the selected AArch64 target. |
| Benchmark lacks full chain and percentiles | **Confirmed** | It reports only component min/average/max. There is no p50/p99/p999, full-chain scenario, block/IR sweep, cache-pressure mode, or build-mode banner. |
| Review's Pi stage timings and savings | **Measure/decision** | They are estimates, not verified results from this repository and target image. Do not use them as acceptance evidence. |
| `-ffast-math`, LTO, real FFT | **Measure/decision** | Test independently. `-ffast-math` can undermine non-finite handling and numerical assumptions; do not enable it globally as a bundled fix. |

## Implementation plan

The phases below are ordered by safety and dependency. Each phase should be a separate reviewable
change set with its own tests. Preserve the current dirty working tree and do not include unrelated
UI work or generated binaries in these changes.

### Phase 1 — Close small correctness holes and create regression tests

Files: `apps/pedal-poc/main.cpp`, `src/control/LinuxInput.cpp`,
`src/preset/RuntimeCommands.cpp`, `src/ui/UiModel.cpp`, `src/audio/EngineLoader.cpp`, and
their smoke tests.

1. Reject `--block-size 0` in argument validation for every mode.
2. Change evdev polling so it loops internally over `EV_SYN`, releases, autorepeat, and unsupported
   events until it returns a meaningful event or reaches `EAGAIN`. Preserve read errors separately
   from “no event.”
3. Split the main loop into independent cadences: LVGL at about 5 ms; hardware controls and local
   slot/bank requests every iteration; runtime command-directory polling at a modest cadence; and
   telemetry at 1 Hz. Remove the gating `continue`.
4. Replace throwing directory iteration and metadata operations with explicit `error_code` loops in
   both runtime command and UI asset paths. A transient error must be reported but must not terminate
   audio.
6. Replace the detached stack-capturing stdin thread with process-owned state and a joinable/stop-aware
   mechanism, or remove stdin control from the appliance build.

Tests and acceptance:

- A Linux event-stream test contains useful events separated by `EV_SYN` and drains all of them in
  one service pass.
- A loop-scheduler test or extracted controller proves slot/bank requests are serviced within 20 ms
  while telemetry remains at 1 Hz.
- Runtime command and asset tests inject iterator/stat failures and the process remains alive.
- CLI tests prove block size zero exits with usage instead of entering rendering.

### Phase 2 — Repair UI state propagation and gesture safety

Files: `src/ui/LvglUi.{h,cpp}`, `src/ui/UiModel.{h,cpp}`, `apps/pedal-poc/main.cpp`,
`apps/pedal-ui-sim/main.cpp`, and `tests/lvgl_ui_smoke.cpp`.

1. Add one explicit UI invalidation/state-update boundary. Successful external mutations request a
   rebuild or targeted refresh; telemetry uses a cheap change comparison. Show a pending bank/preset
   state until activation completes.
2. Begin EQ knob/node interaction suppression before changing focus or selection. Make `refresh()`
   refuse a destructive rebuild while an interaction is active, and defensively clear the flag at
   the start of `build()`.
3. Store stable handles/roles for knob value labels, pointers, EQ lines, and EQ nodes when creating
   them. Replace geometry/text/order discovery in targeted refreshes.
4. Route encoder updates through the same targeted refresh operations used by touch drags. Reserve a
   full rebuild for structural changes, mode changes, and opening/closing panels.
5. Give chain drags a movement threshold and `suppressClick`; register `PRESS_LOST` cleanup for chain
   and asset drags, including opacity restoration and no drop action.
6. Map EQ pointer positions from `lv_obj_get_coords(graph)` converted to canvas coordinates, not
   compile-time offsets.
7. Format telemetry explicitly to two decimals. Delete the LVGL tree before clearing event contexts,
   make file-local helpers internal, centralize bank bounds, and add a simulator quit path.
8. Add a persistent status/toast model for load, save, device, and swap errors.

Tests and acceptance:

- Bank, footswitch, runtime-command, master-volume, bypass, and telemetry mutations visibly repaint
  without an unrelated tap.
- A bank-button smoke test validates the new bank label after asynchronous completion, not merely the
  callback delta.
- EQ press/drag/release and press-lost tests leave no stuck interaction flag and do not replace the
  dragged object mid-gesture.
- A moved chain tile does not open another block's panel; a click without movement still opens it.
- Encoder tests verify object identity is retained for non-structural value changes.
- A graph-origin test changes theme padding and still maps the grabbed node without a jump.

### Phase 3 — Make the audio backend fail-safe and observably realtime

Files: `src/audio/MiniaudioBackend.{h,cpp}`, `apps/pedal-poc/main.cpp`, init/runbook files, and new
backend contract tests.

1. Initialize `ma_context_config` with realtime priority. Pin Linux FIFO priority to an Ardor-owned
   value (initially 70) rather than miniaudio's default 99. Do not silently accept normal-priority
   fallback in the production image.
2. On the first callback, capture and expose the actual scheduler policy/priority. Startup is not
   production-ready unless it reports the intended FIFO policy; provide an explicit development-mode
   fallback if desktop Linux needs one.
3. Install device notification handling that performs only atomic state publication in the callback.
   The control loop owns logging, UI errors, stop/reopen, and bounded restart/backoff.
4. Replace the two booleans used for engine exchange with a generation/status state machine and a
   typed result (`Activated`, `Rejected`, `DeviceLost`, `TimedOut`). A timeout must first quiesce/join
   the device thread before returning ownership of any engine that the callback might have adopted.
   Never solve the timeout with a bare CAS that can race pointer consumption.
5. Replace busy-yield with an event/condition or bounded millisecond poll on the control thread.
6. Log requested/client rate, native capture and playback rates, actual native periods, and whether
   each converter/resampler is active. For the Pi product, fail startup if either native rate is not
   48 kHz rather than hiding conversion latency.
7. Rename the timing counter/API to `overBudgetCallbacks`; add device-loss/restart and callback-gap
   counters. Keep actual ALSA xruns distinct until the backend can report them reliably.
8. Move signal installation before `start()`, remove the unused callback counter, express state
   ownership with RAII, and document the single control-thread contract.
9. After the state machine is tested, add an aligned-callback fast path that processes a complete
   quantum directly. Retain FIFO behavior for partial/oversized callbacks and ensure transition fades
   have identical semantics in both paths.

Tests and acceptance:

- A backend seam or null-device harness simulates callback stop before, during, and after a swap;
  every operation returns within a defined bound and engine ownership is unambiguous.
- Device-loss notification produces silence, a visible fault, and a bounded restart attempt without
  blocking the control loop.
- Target startup records `SCHED_FIFO/70`; a deliberately insufficient-privilege run fails loudly.
- Target logs prove capture/playback native rate is 48 kHz and no resampler is active.
- Aligned mode removes the extra 64-frame adapter latency; irregular callback tests remain sample
  complete and ordered.

### Phase 4 — Make Daisy state instance-owned and preset activation truly transactional

Files: hosted Daisy modes/registries, `src/daisyfx/DaisyFxProcessor.*`,
`src/audio/EngineLoader.cpp`, `apps/pedal-poc/main.cpp`, and Daisy/preset stress tests.

Completed 2026-07-16: all compiled Daisy mode buffers and mutable DSP objects are instance-owned;
the unreferenced global registries and Daisy compatibility shim were removed. `DaisyFxProcessor`
continues to construct modes directly. `pedal-daisy-fx-smoke` proves same-mode modulation phase
isolation plus delay and reverb impulse isolation, while preset/UI smoke tests prove duplicate
Daisy categories are accepted.
The preset switch path now prepares the replacement while audio remains live and has no Daisy-only
mute/recovery branch.

`pedal-preset-activation-smoke` now drives the same activation seam used by the realtime app with
a fake backend. It proves that an invalid Daisy target never reaches the backend, device loss during
replacement leaves the actual live engine and committed bank/slot untouched for normal recovery and
requeue, and the UI is synchronized only after backend acknowledgement. The old snapshot-reload
recovery test is intentionally superseded: preserving the live engine is stronger and avoids a
filesystem-dependent recovery path.

1. Replace file-static mode objects, delay lines, and sample buffers with memory owned by each mode or
   supplied by a per-processor arena at initialization.
2. Prove two identical delay/reverb/mod instances are isolated by feeding only one and checking the
   other remains silent and unchanged.
3. Once isolation exists, prepare the entire next engine while the current engine remains live.
   Submit only a fully validated/prewarmed engine to the backend. Remove the muted-engine construction
   detour.
4. On any parse/load/prepare/swap failure, leave the last-known-good engine active, resynchronize the
   UI to it, and show the diagnostic. Only update `args.bank`, `args.slot`, controls, and UI selection
   after the backend confirms activation.
Acceptance met in host smoke coverage: failed Daisy preparation and device-loss swap rejection
retain the current engine and committed UI/audio selection; successful replacement commits both
only after backend acknowledgement. Same-mode modulation, delay, and reverb instances are
isolated; duplicate categories load and render finite output. Pi headroom remains a separate
release-gate measurement.

### Phase 5 — Add live, smoothed parameter commands

Files: `PedalEngine`, `RuntimeChain`, Daisy/compressor/cab processors, UI actions, and effect tests.

1. Add a bounded SPSC command queue or equivalent block-boundary command mechanism. The UI/control
   thread publishes block ID, parameter ID, and target; the audio thread applies bounded commands at
   quantum boundaries without JSON, allocation, or locks.
2. Add typed live setters for cabinet level/mix, Daisy parameters, compressor parameters, globals,
   bypass, and EQ. Keep mode/topology/asset changes on the prepared-engine path.
3. Store Daisy parameter targets atomically or in queued snapshots, smooth audible discontinuities,
   and update the vendor parameter set at its existing 48-sample control cadence. Do not reconstruct
   `Impl` for ordinary parameter changes.
4. Refactor compressor coefficients/targets so continuous controls update without reset. Treat
   detector mode as a bounded state transition or prepared change.
5. Connect every UI parameter edit to the live path; saving persists state but does not reload an
   unchanged live engine.

Acceptance: turning mix/time/decay/compressor controls changes sound promptly, produces no global
mute/fade, preserves delay/reverb tails, performs no callback allocation, and survives command bursts
without unbounded work.

### Phase 6 — Calibrate NAM drive and correct sound-quality defects

1. Measure Codec Zero ADC full-scale dBu at every supported hardware PGA/input setting using the
   documented 1 kHz procedure. Store the selected calibration in device configuration.
2. In `NamProcessor`, retain model input-level metadata and apply:

   `calibrationGainDb = codecFullScaleDbU - modelInputLevelDbU`

   Fall back to 0 dB when metadata or device calibration is absent. Keep automatic calibration,
   user model-input trim, and preset drive as separate terms, all finite-checked and smoothed.
3. Replace the compressor's detector-plus-gain double smoothing with an instantaneous/light detector,
   static dB gain computer, and one log-domain attack/release smoother. Define attack/release with
   step-response tests for both peak and RMS modes.
4. Set chorus (and, after listening tests, flanger) defaults per descriptor to a dry/wet mix around
   50%; do not change the shared default for unrelated modulation modes.
5. Add IR level analysis at import (peak, DC, RMS/early energy, and peak frequency response). Offer a
   versioned normalization option and user trim; keep existing presets behavior-compatible unless
   migrated explicitly.
6. Replace the normal safety clamp with a stereo-linked bounded limiter/soft-knee design and retain a
   final emergency clamp. Choose lookahead only after accounting for the latency recovered by the
   aligned callback path. Add limiter-event and gain-reduction telemetry.

Acceptance includes calibration tests using NAM files with and without metadata, compressor timing
tests, default-effect unity/impulse tests, IR migration tests, limiter boundedness/monotonicity tests,
and target listening validation.

### Phase 7 — Harden numeric fault handling and DSP block contracts

1. Sanitize non-finite capture samples before gain or feedback processing and count them.
2. Check each block boundary in debug/test builds. In production, sanitize output and publish the
   first offending block ID plus a fault generation. The control thread prepares a clean replacement;
   it must not reset large vendor buffers concurrently with the callback.
3. Make fixed quantum a hard production invariant. Remove/quarantine sample APIs and replace the IR
   mismatch fallback with a debug assertion plus bounded silence/fault behavior. The external adapter
   is solely responsible for forming complete quanta.
4. Add AArch64 FPCR flush-to-zero setup on entry to audio processing and benchmark/offline processing.
   Keep x86 handling compatible with miniaudio's existing FTZ/DAZ guard.
5. Document mono/stereo transitions, including compressor layout preservation and the current mono
   input to delay/reverb.

Tests inject NaN/Inf before each stateful block, then resume finite input and prove the system reports
the block, never emits non-finite output, and recovers without a process restart. A non-multiple block
request must never enter direct convolution.

### Phase 8 — Establish performance evidence, then optimize

1. Make single-config CMake default to Release only when the caller did not choose a build type. Print
   build type, `NDEBUG`, compiler, architecture, and relevant optimization flags in the benchmark.
2. Extend `pedal-dsp-bench` to retain samples and report p50/p99/p999/max. Add a full production chain,
   IR-size/block-size sweeps, representative NAM tiers, and a UI/cache-pressure scenario.
3. Compare the Pi image at Buildroot `-O2` and explicit `BR2_OPTIMIZE_3=y`; adopt `-O3` only with
   reproducible full-chain benefit and unchanged DSP tests. Test LTO independently. Do not enable
   global fast-math; evaluate narrow DSP flags only with finite-fault and audio-difference tests.
4. Add block APIs for Daisy and compressor, hoist mode branches, and reduce dispatch. Optimize the
   converged gain/mix/safety loop so it does not reload stable targets or compute square roots per
   sample.
5. Profile the convolver on Pi before rewriting it. If still material, flatten partition storage,
   hand-vectorize/NEON the complex MAC, then evaluate a real FFT. Record each change separately.
6. Remove the stale AArch64 FPU option. Set the governor through policy/CPU enumeration and make the
   shipped policy agree with the runbook.

Acceptance: the real Pi full chain stays below 65% average and 80% p99 of the 1.333 ms quantum during
UI activity and a thermal soak, with zero over-budget callbacks and separately verified zero device
xruns. Estimates from the review are not acceptance evidence.

### Phase 9 — Offline behavior and lower-priority product improvements

1. Estimate delay/reverb tails or render until a capped energy/hold threshold; preserve explicit
   `--tail-seconds`/`--no-tail` overrides.
2. Add high-quality offline IR resampling to 48 kHz with clear diagnostics and cache the converted IR.
3. Evaluate correlation-aware or linear transition mixing if listening/tests show the 3 dB bypass
   midpoint is objectionable.
4. Evaluate matched high-frequency EQ, nonlinear-stage oversampling, and non-uniform/real-FFT
   convolution only after Phase 8 establishes cost and spectral benefit.
5. Add a documented stereo policy or stereo vendor path if preserving stereo into delay/reverb is a
   product requirement.
6. Remove the tracked generated `ardor-pedal` binary in a dedicated repository-hygiene change after
   confirming it is not a deliberate release artifact; expand `.gitignore` for local image/test
   outputs without deleting the user's existing files.

## Final release gates

- Pi binary flags and benchmark build mode are recorded and reproducible.
- Audio callback actually runs at the intended realtime policy and priority.
- Native capture/playback are 48 kHz with no hidden resampler.
- Controls respond within 20 ms and UI-driven preset requests are not gated by telemetry.
- Device loss and swap timeout cannot hang the process or create ambiguous engine ownership.
- Failed preset load leaves the last-known-good sound and UI selection active.
- Identical Daisy instances do not share state; normal parameter edits preserve tails.
- NAM drive uses measured hardware calibration when metadata is available and a documented neutral
  fallback otherwise.
- No production route can enter per-sample long-IR convolution.
- NaN/Inf injection is contained, attributed, and recoverable.
- Full-chain Pi average is below 65% and p99 below 80% of the 64-frame budget during UI activity and a
  thermal soak, with zero over-budget callbacks and zero independently observed device xruns.
- Round-trip latency, including any limiter lookahead, remains under the product's 10 ms gate.
- Every externally driven state change repaints; gesture loss cannot strand UI interaction state.
