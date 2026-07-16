# Ardor Pedal — Comprehensive Code Review (2026-07-15)

Reviewed on branch `feat/lvgl-flat-knob-redesign` (working tree, uncommitted changes included).
Scope: audio engine core, effects DSP, LVGL UI, cross-cutting performance for the Pi target.

**TL;DR:** The DSP architecture is genuinely solid — lock-free callback, correct engine-swap
protocol, partitioned convolution, smoothed gains everywhere. The highest-leverage findings:
**(1) the audio thread runs at normal priority, not SCHED_FIFO**, and **(2) NAM input-level
calibration is ignored**, which means amp captures don't play at their trained drive point.
Fix those two and you get more improvement in sound and reliability than everything else
combined.

> **Corrections applied 2026-07-15 after verification:** the Pi build is `-O2`
> (`BR2_OPTIMIZE_2=y` confirmed in generated Buildroot output), not `-Os`; non-EQ knob edits
> currently don't touch live audio at all until save/reload; miniaudio disables denormals in
> x86 callbacks only — verified in source to be a **no-op on AArch64**, and a field report on
> Pi 4/Cortex-A72 shows 10–100× denormal slowdowns causing underruns (see finding 8: this is
> a real risk on the Pi, not just hygiene); the compressor stereo-layout note was not a bug
> and has been removed. All performance numbers and projected speedups are estimates pending
> measurement on the Pi.

---

## Critical / High — Performance & Reliability

### 1. Pi build optimization — confirmed `-O2`; `-O3` worth measuring
**Corrected:** generated Buildroot output confirms `BR2_OPTIMIZE_2=y`, so the Pi binary is
built at `-O2` (not `-Os` as originally suspected). This downgrades the finding from critical
to a tuning opportunity: for Eigen-heavy NAM inference, `-O3` (more aggressive inlining and
vectorization) may still yield a measurable gain, but it must be benchmarked on the Pi rather
than assumed.

**Fix:** try `BR2_OPTIMIZE_3=y` (or `ARDOR_PEDAL_CONF_OPTS += -DCMAKE_CXX_FLAGS_RELEASE="-O3
-DNDEBUG"`) and compare full-chain bench numbers on device. Relatedly, the root
`CMakeLists.txt` has no default build type, so a default-configured desktop tree builds at
`-O0` — including `pedal-dsp-bench`, making bench numbers from such a tree meaningless. Add a
Release default and consider `-fno-math-errno -fno-trapping-math` (or full `-ffast-math`) +
LTO for the DSP targets.

### 2. Audio thread is SCHED_OTHER
`src/audio/MiniaudioBackend.cpp:190` passes a NULL context config, so miniaudio uses
`ma_thread_priority_default` — which does **not** select SCHED_FIFO on Linux. The audio
callback competes at normal priority with LVGL rendering and preset loading (seconds of JSON
parse + NAM construction + FFT prep). This is the dominant xrun risk on a loaded Pi.

**Fix:** `ma_context_config cfg = ma_context_config_init(); cfg.threadPriority =
ma_thread_priority_realtime;` (pin rtprio to ~70, not miniaudio's default 99). Bonus headroom
on a dedicated appliance: `isolcpus=3` in the kernel cmdline + pin the audio thread to the
isolated core, steer the I2S/DMA IRQ.

### 3. `replaceEngine` can deadlock forever; no device-loss handling
`src/audio/MiniaudioBackend.cpp:258-260` spin-waits on `swapCompleted`, which is only set
inside the audio callback. If the device stops (USB unplug, ALSA error) or the callback takes
its early-return path, the management thread hangs and the pedal freezes. There is also no
`ma_device_notification` callback and no state polling; if the interface disappears mid-gig,
audio silently dies and telemetry just stops.

**Fix:** bounded wait with a `ma_device_get_state` check + CAS-based swap abort; register the
stop notification and surface/restart on device loss.

### 4. Encoder/footswitch input is effectively unusable (~1 event/second)
Two compounding bugs:
- `src/control/LinuxInput.cpp:28-51` — `poll()` returns `false` on the `EV_SYN` that follows
  every real event, so the `while (poll(...))` drain loop in `apps/pedal-poc/main.cpp:584`
  exits after one event.
- `apps/pedal-poc/main.cpp:573` — input and preset servicing are gated behind
  `if (++tickCount < 200) continue;`, so they run once per second. A 20-detent encoder spin
  takes ~20 seconds to apply; UI-initiated preset switches wait up to ~1 s.

**Fix:** loop inside `poll()` skipping SYN/release/autorepeat events; move input polling and
`requestedSlot`/`requestedBank` servicing into the fast loop, keeping only telemetry at 1 Hz.

### 5. IR convolver partial-block fallback is a landmine
`src/dsp/IrConvolver.cpp:169-177`: if `frames != blockSize_` after preparation, it silently
falls back to per-sample direct convolution — 8192 taps per sample is a guaranteed sustained
xrun, and its `history_` state is disjoint from the FFT state, so output is also wrong (click
+ vanished tail). `PedalEngine::processBlock`'s chunking (`src/dsp/PedalEngine.cpp:228-236`)
can produce exactly this remainder case. The remediation plan (Phase 3.1) says to quarantine
this path; it's still live. Same for the per-sample `RuntimeChain::process` and
`NamProcessor::process(float)` paths (Phase 2.2).

**Fix:** make the mismatch an assert + silence-fill (with a fault counter), or front the
convolver with a fixed-quantum FIFO; move `processSample` behind a test-only guard.

### 6. Duplicate Daisy blocks alias vendor static buffers
`src/audio/EngineLoader.cpp:107-113` rejects duplicate NAM/cab blocks but not duplicate
mod/delay/reverb blocks — yet `apps/pedal-poc/main.cpp:637-642` documents that Daisy modes
contain vendor-owned **static** buffers (the whole reason for the muted-swap dance). Two
delay blocks in one preset would alias the same static state inside one live chain.

**Fix:** reject duplicate Daisy block types per chain in `prepareChainPlan` until the vendor
buffers become instance-owned.

---

## Sound Quality — ranked recommendations

### 1. NAM input-level calibration (biggest tone lever in the codebase)
`src/dsp/NamProcessor.cpp:51-54` normalizes **output** loudness to −18 dB but never reads
`HasInputLevel()`/`GetInputLevel()`, which NeuralAmpModelerCore exposes precisely so captures
get hit at their trained level. Right now whether a model sits at edge-of-breakup or full
saturation is an accident of the interface preamp gain.

**Fix:** one calibration constant for the Pi codec (dBu at 0 dBFS), then apply pre-gain
`10^((codec_dBu − model.GetInputLevel())/20)` when the model provides it, with a user "model
input trim" on top. Expected impact: **large** — captures finally sound like the capture.

### 2. No live parameter updates for Daisy FX / compressor — only the EQ has a live path
**Corrected:** non-EQ knob edits currently do **not** affect live audio at all — changes only
take effect on save/reload, which goes through `configure()`
(`src/daisyfx/DaisyFxProcessor.cpp:208` — fresh `Impl` with zeroed delay/reverb state) and
the full fade-out/mute/fade-in engine replacement. So the gap is that turning a delay-mix or
compressor knob gives no audible feedback until the preset is applied, and applying it ducks
the pedal and truncates tails. Only the parametric EQ updates live.

**Fix:** the infrastructure already exists — the vendor `Prepare(params)` re-runs every 48
samples (`DaisyFxProcessor.cpp:151-165`), and `ParametricEqProcessor::setBandTarget` is the
exact atomic-target-plus-smoothing pattern to copy. Apply it to Daisy params and compressor
coefficients; rebuild only on mode change. Expected impact: **large** — biggest playability
win.

### 3. Safety "limiter" is a hard digital clamp
`src/dsp/PedalEngine.cpp:268` is `std::clamp` — full-bandwidth aliased hard clipping, the
harshest possible failure sound, and easy to hit with a hot IR + EQ boost.

**Fix:** soft-knee cubic/tanh clip starting ~1 dB under the ceiling, or a tiny zero-lookahead
peak limiter (instant attack on |x|, ~50 ms release). Few ops per sample. Impact: **medium** —
worst case goes from digital hash to tape-ish squash.

### 4. No IR gain normalization
`src/audio/WavIo.cpp:51-81` validates and fades but never normalizes; commercial IRs vary by
>20 dB, so cab switches jump in level and hot IRs slam the clamp.

**Fix:** normalize at load (peak frequency-response magnitude to 0 dB, or early-energy RMS),
keep the `level` param as user trim. Impact: **medium**.

### 5. Compressor ballistics applied twice
`src/dynamics/CompressorProcessor.cpp:85-86` smooths the detector envelope with
attack/release, then lines 122-123 smooth the **gain** again with the same coefficients —
effective times ~2× what the knob says, and the cascaded one-poles smear the knee. The RMS
path additionally filters the squared signal, so peak vs RMS modes with identical settings
differ ~4×.

**Fix:** classic feed-forward topology — instantaneous (or lightly filtered) detector →
static gain computer → single log-domain smoothing on gain reduction in dB. Impact:
**medium** — honest attack times, punchier dynamics.

### 6. Default chorus mix = 1.0 makes it a vibrato
`src/daisyfx/DaisyFxCatalog.cpp:14` — the vendor chorus returns wet-only
(`chorus_mode.cpp:167`), and at mix 1.0 there's no dry signal, hence no comb interaction.
Chorus and flanger fundamentally need dry+wet. **Fix:** default mix ≈ 0.5. Impact: **medium**.

### 7. NaN latch-up
`PedalEngine::applySafety` zeroes non-finite output, but nothing scrubs internal state — a
NaN entering a feedback structure (reverb FDN, delay at high feedback, biquad state)
recirculates forever and the pedal goes permanently silent until a preset switch.

**Fix:** sanitize the callback input; detect a run of non-finite pre-clamp samples and reset
the offending block / chain from the control thread.

### 8. Denormal handling — REAL RISK on the Pi 4 audio thread (verified 2026-07-15)
Verification results:
- **miniaudio source checked** (`ma_disable_denormals`, miniaudio.h:12070-12117): denormals
  are disabled per-callback **only on MSVC and x86/SSE2**; on AArch64 the function is an
  explicit no-op ("Unknown or unsupported architecture"). So the Pi callback runs with
  FPCR.FZ = 0.
- **Measured on Apple M2 Pro** (`tests/denormal_bench.cpp`): 1.00× penalty — Apple cores
  handle subnormals at full speed. This does **not** transfer to the A72.
- **Field report on the exact target hardware**: Mixxx hit this on a Raspberry Pi 4
  (Cortex-A72) — audio thread without FPCR.FZ, stacked feedback effects producing denormal
  tails (~1e-38), per-op cost **10–100× slower**, causing deadline misses and buffer
  underruns ([mixxxdj/mixxx#16126](https://github.com/mixxxdj/mixxx/issues/16126)). Their
  main thread was protected because gcc's `-ffast-math` links `crtfastmath.o` (sets FZ at
  startup), but `pthread_create` children start with FPCR = 0 — and the Ardor callback runs
  on a miniaudio-created thread. Ardor doesn't use `-ffast-math` anywhere, so no thread is
  protected.
- Ardor's worst case is exactly the Mixxx pattern: reverb FDN + delay feedback + one-pole
  smoother tails all decay through the subnormal range whenever input goes silent — i.e. the
  spike hits between songs, when it's most audible.
- **Definitive A72 measurement still pending**: the pedal wasn't reachable on the LAN. A
  static ARM64 Linux benchmark is ready — build with
  `docker run --rm --platform linux/arm64 -v $PWD/tests:/src -v /tmp:/out gcc:13 sh -c
  'g++ -O2 -std=c++20 -static -o /out/denormal_bench_pi /src/denormal_bench.cpp'`, scp it to
  the pedal, run it; exit code 1 + "VERDICT: denormals ARE a problem" confirms.

**Fix (upgrade from "hygiene" to "do it"):** set FPCR.FZ once at the top of the audio
callback via a first-call flag (aarch64: `mrs/msr fpcr`, bit 24), keep miniaudio's built-in
handling for x86, and set FTZ at the entry of offline render and bench paths too.

### Smaller wins (rough priority order)
- Oversample the hard-nonlinear vendor stages (Destroyer bitcrush, delay `grit`) 2–4× with
  halfband filters — less aliasing fizz on high notes.
- Orfanidis/Vicanek-matched peaking for the top EQ band — RBJ cramps near Nyquist
  (`ParametricEqMath.cpp:15-16`).
- Offline-resample 44.1 kHz IRs at load instead of rejecting them
  (`EngineLoader.cpp:94`, fallback warning at `main.cpp:422-426`).
- Real-FFT convolver with flat contiguous partition storage — ~2–2.5× cheaper
  (`IrConvolver.cpp:44-76,193-200`); reinvest in longer IRs (8192 taps ≈ 170 ms is fine for
  cabs, short for room blends).
- Bypass equal-power crossfade gives up to +3 dB mid-fade bump on correlated dry/wet
  (`PedalEngine.cpp:201-207`) — minor, consider correlation-aware law or leave it.
- Delay/reverb wet path collapses input to mono (`DaisyFxProcessor.cpp:293,300`) — vendor
  constraint, document it.

### What's already good
- Partitioned convolver is textbook-correct: uniform partitions, frequency-domain delay line,
  proper overlap-add, zero added latency, double-precision twiddles.
- NAM hygiene: sample-rate guard, prewarm at load with `SetPrewarmOnReset(false)`, −18 dB
  loudness normalization, block-vs-sample equivalence test.
- EQ: RBJ-conformant, double-precision coefficient math, log-domain frequency smoothing,
  extreme-setting stability covered by tests.
- Vendor delay lines: 4-point Hermite interpolation, per-sample LFO advance, DC blockers on
  chorus wet, BBD emphasis modeling — genuinely good modulation quality.
- Preset switches are click-free (10 ms fades, block-aligned FIFO discard); bypass is a
  smoothed equal-power mix; all top-level gains one-pole smoothed at 5 ms.
- 48 kHz contract enforced honestly at every boundary; IR truncation with 5 ms tail fade.

---

## Audio engine — additional findings

- **Detached stdin thread captures `main`'s stack atomics by reference**
  (`apps/pedal-poc/main.cpp:430-438`) — use-after-scope on shutdown. Give
  `requestedSlot`/`requestedBank` process lifetime or join the thread.
- **Failed preset switch can strand the pedal on the muted engine** (`main.cpp:655-667`) with
  the UI still showing the new preset. Surface the failure and resync `uiState`; ideally keep
  the last-known-good engine alive until the replacement is confirmed (blocked today by the
  Daisy static-buffer constraint — same root cause as finding 6).
- **The FIFO always adds one full block (64 frames = 1.33 ms) of latency** even when the
  device period equals the block size (`MiniaudioBackend.cpp:76-135`). Add a fast path that
  processes directly on the callback buffers when aligned; keep the FIFO for misaligned
  callbacks.
- **miniaudio silently resamples** if the device's native rate isn't 48 kHz
  (`cfg.sampleRate = 48000` is a conversion request, not a requirement) — hidden latency +
  CPU. Log `device.sampleRate` vs native after init.
- **`consumeRuntimeCommands` directory iteration can throw**
  (`src/preset/RuntimeCommands.cpp:24-31`) — increment errors throw `filesystem_error`, which
  escapes to `main`'s catch-all and exits the pedal process on a transient FS error. Use the
  `increment(ec)` API or wrap in try/catch.
- **`tailFrames()` only counts the cab IR** (`RuntimeChain.cpp:270-279`) — offline renders
  truncate delay/reverb tails. Add per-block tail estimates or document `--tail-seconds`.
- **`--block-size 0` infinite loop offline** (`main.cpp:839`) — validate `blockSize > 0`.
- LOW: `callbacksInFlight` maintained with seq_cst but never read (dead cost);
  `stats()`/`stop()` single-thread contract undocumented; signal handlers installed after
  `backend.start`; raw `new`/`delete` for `MiniaudioBackendState`.

### Audio engine — done well
- Genuinely allocation/lock/log-free steady-state callback; all preparation on the control
  thread; correct acquire/release swap protocol with fade-to-zero handoff.
- `mlockall(MCL_CURRENT | MCL_FUTURE)` before realtime start (`main.cpp:449, 780`).
- Lock-free atomic parameter targets with smoothing everywhere; state-preserving bypass with
  rationale comment; `ParametricEqProcessor::setBandTarget` is a textbook lock-free param
  path.
- Self-protective overload latch with documented anti-oscillation rationale; per-callback
  timing telemetry with lock-free max via CAS.

---

## LVGL UI

### Critical / High
- **C1 — Input drain bug** (see Performance finding 4 — `LinuxInput.cpp:28-51` +
  `main.cpp:573,584`). For a live pedal this makes hardware controls effectively unusable.
- **H1 — Externally-driven state changes never repaint.** `requestBankChange`
  (`src/ui/LvglUi.cpp:193-200`) mutates state but never requests a rebuild — the new
  Bank +/− buttons appear dead until an unrelated tap. Same root cause hits telemetry
  (`main.cpp:704`), master volume (`main.cpp:615`), footswitch slot changes
  (`main.cpp:599-601`), and runtime `ApplyPreset` (`main.cpp:622-625`): the on-screen
  LIVE/BYPASS line shows boot-time values forever. Call `redraw(context)` /
  `ui->requestRebuild()` after any external mutation of `uiState` (with a cheap change check
  for telemetry). The smoke test only asserts `requestedBankDelta == 1`
  (`tests/lvgl_ui_smoke.cpp:606-607`) — it never verifies a repaint.
- **H2 — EQ knob/node press tears down the tree mid-drag.** `onEqKnobPressed`
  (`LvglUi.cpp:427-440`) and `onEqNodePressed` (:500-512) call
  `selectEqBand`/`focusEqBandField` (latching `rebuildPending_`) **before**
  `beginKnobInteraction()` enables suppression — the next frame rebuilds the UI while the
  finger is down, dropping the gesture start, and can strand `knobInteractionActive_` true
  (silently suppressing all future rebuilds). The regular-knob path (`onKnobPressed`,
  :329-330) does it in the right order; mirror it, and reset the flag at the top of `build()`
  as a safety net.
- **H3 — Full-tree rebuild + `LV_DISPLAY_RENDER_MODE_FULL` per interaction.** Every encoder
  detent triggers `lv_obj_clean` + full recreate + full 1280×720 software render + rotation
  (`LvglUi.cpp:1311-1352`, `lv_conf.h:24`), on the same CPU running NAM inference. Route the
  encoder path through the existing targeted updates (`refreshKnobVisual`,
  `refreshEqGraphCurve`); consider `LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NEON` for the Pi build.

### Medium
- **M1** — Trailing `LV_EVENT_CLICKED` after a chain-tile drag opens the drawer for the wrong
  block (`LvglUi.cpp:1524-1527`, :542-547, :652-674). The asset drawer already solved this
  with `suppressClick` (:696-700, :766) — copy it.
- **M2** — Chain tiles (:1524-1527) and drawer assets (:1629-1632) don't handle
  `LV_EVENT_PRESS_LOST`: ghost/insert-indicator widgets stay stuck on screen. Register
  `PRESS_LOST` → same handler as `RELEASED` (drop suppressed).
- **M3** — EQ node drag mapping uses compile-time offsets (`kEqGraphCanvasX/Y`,
  `LvglUi.cpp:61-62`) that ignore theme padding — grabbed nodes jump at drag start. Compute
  the graph origin at runtime via `lv_obj_get_coords` + `toCanvas()`.
- **M4** — Telemetry label uses `std::to_string(double)` → prints `max 1.230000ms`
  (`LvglUi.cpp:1375-1383`). Use `snprintf("%.2f", ...)` like the EQ label helpers.
- **M5** — `loadAssetsFromDataRoot` (`src/ui/UiModel.cpp:157-170`) uses throwing filesystem
  iterators; runs on every `ReloadAssets` command and can kill the pedal process while audio
  is live. Use the `error_code` overloads.
- **M6** — No user-visible error surface: failed saves/loads go to stderr only, invisible on
  device. Add a toast/status label.
- **M7** — Child identification by magic geometry (`refreshKnobVisual` matches by 56×56 size
  and label string, `refreshEqGraphCurve` by point count / 32×32; a null
  `lv_line_get_points_mutable` desyncs the band↔line mapping, :962-964). Stash child pointers
  or role tags in `lv_obj_set_user_data` at creation.
- **M8** — UI-initiated preset switches wait up to ~1 s (same gate as C1).

### Low
- `contexts_.clear()` runs before `lv_obj_clean(root)` (`LvglUi.cpp:1313-1315`) — reverse for
  free UAF safety.
- `telemetryLine` helper has external linkage (`LvglUi.cpp:1375`) — mark `static`.
- Dead code at `UiModel.cpp:71`; bank bounds duplicated three times (LvglUi.cpp:40-41,
  UiModel.cpp:559, main.cpp:534 / pedal-ui-sim:73) — one constant in `UiModel.h`.
- `pedal-ui-sim` `while (true)` has no exit path.
- Repo hygiene: compiled `ardor-pedal` binary tracked/modified at root; `sdcard.img*`,
  `test.html` untracked in the worktree — shouldn't ride along with a UI PR.

### UI — done well
- No stack `lv_style_t` anywhere (classic UAF class absent); line-point arrays freed via
  `LV_EVENT_DELETE`; grid descriptors static.
- `contexts_` as `std::deque` for stable addresses; deferred-rebuild pattern avoids
  delete-from-callback hazards.
- Threading verified clean: all `lv_*` calls single-threaded; cross-thread traffic is atomics
  only.
- Smoke tests drive real simulated indev gestures (knob needle geometry, mid-drag EQ curve
  redraw) — unusually good UI test coverage.
- Design-grid canvas + `toCanvas()` mapping is a clean resolution-independence approach.

---

## Performance — additional findings

- **Daisy FX and compressor processed per-sample with virtual dispatch**
  (`RuntimeChain.cpp:220-234`, `DaisyFxProcessor.cpp:278-307`) — ~9k indirect calls per
  64-frame block in a worst-case chain, defeating vectorization. Add
  `processBlock(...)` to `DaisyFxProcessor` (hoist branches, devirtualize through a local,
  vectorize the dry/wet mix); the remediation plan Phase 1.1 already mandates this. Expected
  saving: 20–40% of Daisy cost.
- **Compressor burns 2 libm transcendentals per sample** (`CompressorProcessor.cpp:92,108`) —
  compute the gain in dB domain per block or use fast log2/exp2 polynomials.
- **`PedalEngine::processBlock` output loop** does 3 atomic loads + 2 `sqrtf` per sample even
  when the mix is pinned at 0/1 (`PedalEngine.cpp:244-252`) — precompute per block once
  converged.
- **Convolver spectral loop** (`IrConvolver.cpp:193-200`): `complex<float>` MAC without
  fast-math emits the NaN-correct multiply (no FMA/NEON); partitions stored as
  `vector<vector<...>>` (128 scattered heap blocks). Flatten to one contiguous array, use a
  real FFT, and/or hand-write the complex MAC — combined ~2–2.5× on the convolver. Working
  set is ~256 KB streamed per 1.33 ms block, pressuring the shared L2 next to NAM weights;
  consider non-uniform partitioning if IRs grow.
- **Governor set for cpu0 only** (`S99ardor-pedal:42`) — works on bcm2711's shared policy but
  fragile; runbook uses `cpu*` and calls performance governor "characterization only" while
  the shipped init script forces it — docs and image disagree.
- **`replaceEngine` busy-yields** the control thread for the full fade
  (`MiniaudioBackend.cpp:258`) — harmless once audio is SCHED_FIFO; today it burns a core the
  audio thread needs. Use a 1 ms sleep loop or futex.
- **`BR2_ARM_FPU_VFPV4=y` with `BR2_aarch64=y`** (defconfig:3) — ignored on aarch64; config
  drift, remove it. `BR2_cortex_a72=y` correctly yields `-mcpu=cortex-a72`.
- **"xruns" are only over-budget timings** (`MiniaudioBackend.cpp:145-147`) — real ALSA xruns
  invisible; remediation plan Phase 8 (deviceXruns + rename) unimplemented.
- `NAM_SAMPLE_FLOAT` is defined (`CMakeLists.txt:163`) — good, the single most important NAM
  flag. Double-precision creep is well-contained (control-rate only).

### Benchmark gaps (`tests/dsp_bench.cpp`)
- No full-chain benchmark (plan Phase 0.3 requires NAM + 8192 IR + mod + delay + reverb +
  compressor with avg/p99/max).
- No p99 — `BenchResult` has min/avg/max only, but the runbook gate is defined on p99. Store
  per-block samples and report p50/p99/p999.
- Nothing enforces Release build for benches; print `NDEBUG` status + flags in the header.
- No IR-size/block-size sweep; no memory-pressure variant (bench runs with hot cache; the
  real chain shares L2 with LVGL rendering).

### Budget estimates (Pi 4, one A72 core @ 1.5 GHz, 64 frames / 1.333 ms, built at -O2)

> These figures — and the projected speedups above (Daisy block processing 20–40%, convolver
> 2–2.5×, etc.) — are analytical estimates, not measurements. Validate on the Pi with the
> full-chain p99 bench before acting on any of them.

| Stage | Est. per block | % budget | Notes |
|---|---|---|---|
| NAM (slimmable WaveNet, largest tier) | ~250–600 µs | 20–45% | Dominant. A standard NAM (channels=16) is ~10–20× this — not feasible on a Pi 4 core; enforce a tier cap. |
| IR convolution (8192-tap, partitioned) | ~50–150 µs | 4–11% | Halvable via real-FFT + flat layout + fast-math. |
| Daisy reverb (FDN) | ~30–100 µs | 2–8% | Per-sample virtuals inflate ~25–40%. |
| Daisy delay / mod | ~20–60 µs each | 2–5% each | |
| Compressor | ~6–15 µs | ~1% | log10+pow per sample. |
| 5-band EQ stereo | ~10–20 µs | ~1% | |
| PedalEngine gain/mix + adapter | ~5–15 µs | ~1% | 2 sqrt/sample removable. |
| **Worst-case chain total** | **~0.45–1.0 ms** | **35–75%** | Straddles the 65% gate — measure on device. |

---

## Suggested fix order

1. SCHED_FIFO audio thread + FPCR.FZ on the audio thread (verified real risk on A72, and a
   two-line fix) + device-loss notification + `replaceEngine` timeout.
2. Input polling fixes (LinuxInput SYN bug + main-loop gating) — makes the hardware usable.
3. NAM input calibration + live Daisy/compressor parameter smoothing — the two big sound
   wins.
4. UI repaint-on-external-change (H1) + EQ press ordering (H2).
5. Full-chain p99 bench on the Pi — establishes the baseline for everything below.
6. Soft limiter, IR normalization, compressor ballistics, NaN scrubbing.
7. CMake build-type default; try `-O3` on the Pi image and keep it only if the bench shows a
   win.
