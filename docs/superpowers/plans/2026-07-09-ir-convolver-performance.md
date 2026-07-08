# IR Convolver Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the partitioned IR convolver cheap enough on the Pi 4B that NAM gets the CPU budget, without changing its audible output.

**Architecture:** Keep the existing uniform-partition overlap-add design in `src/dsp/IrConvolver.cpp` — it is correct (verified against the naive convolver in `tests/offline_smoke.cpp`) and the right shape for cab IRs. Optimize inside it, measurement-first: fix what profiling on the Pi shows, stop when the headroom target is met. Every task keeps the existing naive-vs-fast equivalence test as the correctness harness.

**Tech Stack:** C++20, existing test binaries, `std::chrono` benchmarking, optionally pffft (single-file BSD, NEON) behind a decision gate.

## Where The Time Actually Goes (cost model, verify by measuring)

At the baseline `48000 Hz / block 64 / IR 8192`: `fftSize = 128`, `partitions = 128`, budget `1333 µs/block`, 750 blocks/s.

| Component | Cost per block | Notes |
| --- | --- | --- |
| Spectral accumulation (`sum_[i] += x[i] * h[i]` over all partitions) | **16,384 complex MACs, ~256 KB streamed** | The dominant term. 256 KB/block is 8× the A72's 32 KB L1D — this loop is memory-bound, not FLOP-bound. |
| Forward + inverse FFT | 896 butterflies total | An order of magnitude smaller than accumulation. |
| `inputPartitions_[writeIndex_] = scratch_` | 1 KB copy | Avoidable. |
| Twiddle recomputation (`w *= wlen` per butterfly) | included above | Also an error-accumulation problem at larger sizes. |

Consequence: **the accumulation loop is the target, not the FFT.** Replacing the FFT with a fast library while leaving the accumulation loop untouched would optimize the ~5% and skip the ~90%. Conjugate symmetry (real input ⇒ spectrum bins `N/2+1..N-1` are conjugates of `1..N/2-1`) halves both the MACs and the streamed bytes of the dominant term — that is the big win, and it needs no new dependency.

## Global Constraints

- Bit-exact output is not required; the existing equivalence-test tolerance (`0.0005`) is the contract. Tighten it if precomputed twiddles improve precision (they should).
- No behavior change: same API, same latency (one block), same partition scheme.
- All sizing still happens at load time (`preparePartitions`), never in the callback — this plan assumes the Phase 0 preallocation prerequisite from the master roadmap has landed.
- Headroom target: convolver ≤ **200 µs/block (15% of budget)** on the Pi 4B at `64 / 8192` with the `performance` governor. NAM needs the rest.
- Do not add a dependency unless Task 4's gate says the target is missed without it.

---

## Execution Position

- Task 1 (benchmark) belongs in roadmap **Phase 0** — it produces the Pi numbers the spike needs.
- Tasks 2–3 run only if Phase 0 measures the convolver above the 200 µs target on the Pi. On the desktop the current code is already comfortably inside budget; do not optimize on macOS numbers.
- Task 4 is gated on Tasks 2–3 measurements.
- Task 5 is deferred out of v1 entirely.

---

### Task 1: DSP Microbenchmark Binary

You cannot see per-component cost from the existing once-per-second telemetry — it lumps NAM, convolver, and gain staging into one number. Add a tiny benchmark target that isolates them.

**Files:**
- Create: `tests/dsp_bench.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1:** Create `tests/dsp_bench.cpp`: a `main` that (a) loads an 8192-sample synthetic IR into `IrConvolver`, feeds noise through `processBlock` for 10,000 blocks of 64, and reports µs/block min/avg/max; (b) if a `.nam` path is passed as `argv[1]`, does the same through `NamProcessor` alone. Print one summary line per component plus the % of the 1333 µs budget. Use `steady_clock`, warm up 100 blocks before timing.
- [ ] **Step 2:** Register `pedal-dsp-bench` in CMake (a normal executable, **not** a ctest — timings are not pass/fail on shared CI hardware).
- [ ] **Step 3:** Run on desktop, record numbers in `docs/hardware-validation.md` as the desktop reference row of a small table (host, per-component µs/block, % budget).
- [ ] **Step 4:** Cross-compile and run on the Pi 4B (`performance` governor — the Phase 0 spike environment). Record the Pi row. **This row decides whether Tasks 2–4 run at all.**
- [ ] **Step 5:** Commit: `test: add dsp microbenchmark`

---

### Task 2: Precomputed Twiddles And Bit-Reversal (always worth it)

Small, safe, improves precision and speed regardless of the gate — the one exception to "measure first" because it also fixes the error-accumulation problem (`w *= wlen` compounds rounding across every butterfly at every call).

**Files:**
- Modify: `src/dsp/IrConvolver.h`
- Modify: `src/dsp/IrConvolver.cpp`
- Modify: `tests/offline_smoke.cpp`

- [ ] **Step 1:** In `preparePartitions`, precompute for the fixed `fftSize_`: the bit-reversal permutation table and the twiddle factors per stage (`std::vector<std::complex<float>>` sized `fftSize_`). Store as members.
- [ ] **Step 2:** Rewrite `fft()` as a member using the tables (table lookup instead of `w *= wlen`; permutation table instead of the incremental bit-twiddling loop). Inverse FFT uses conjugated twiddles + the existing `1/n` scale.
- [ ] **Step 3:** In the equivalence test in `tests/offline_smoke.cpp`, tighten the naive-vs-fast tolerance from `0.0005` to `0.0001` — precomputed twiddles should make this pass; if it does not, that is a bug in the tables, not a reason to loosen the tolerance back.
- [ ] **Step 4:** Run `pedal-dsp-bench` before/after; record both rows. Run full `ctest`.
- [ ] **Step 5:** Commit: `perf: precompute fft twiddles and bit reversal`

---

### Task 3: Halve The Dominant Loop Via Conjugate Symmetry

The input signal and the IR are real, so every spectrum in `inputPartitions_` and `impulsePartitions_` is conjugate-symmetric: bin `N-k` is `conj(bin k)`. Products of conjugate-symmetric spectra are conjugate-symmetric, so `sum_` is too.

**Files:**
- Modify: `src/dsp/IrConvolver.h`
- Modify: `src/dsp/IrConvolver.cpp`

- [ ] **Step 1:** Store only bins `0..N/2` (that is `N/2+1 = 65` complex values at `fftSize 128`) for impulse and input partitions. This halves the working set from ~256 KB to ~130 KB per block — the memory-bound accumulation loop should speed up close to 2×.
- [ ] **Step 2:** Accumulate `sum_[i] += x[i] * h[i]` over bins `0..N/2` only. Before the inverse FFT, reconstruct bins `N/2+1..N-1` as conjugates. (Do not restructure into a full real-FFT algorithm yet — reconstruction plus full-size inverse FFT is simpler and the inverse FFT is not the hot spot; see cost model.)
- [ ] **Step 3:** Keep the FFT writing `scratch_` full-size; copy out only `0..N/2` into the input partition slot — this also removes the old full-size `inputPartitions_[writeIndex_] = scratch_` copy.
- [ ] **Step 4:** Verify: full `ctest` (the naive-vs-fast equivalence test at the tightened tolerance is the gate), then `pedal-dsp-bench` before/after on desktop **and Pi**; record rows.
- [ ] **Step 5:** Compile the DSP objects with `-O3` if the build type does not already, and check (via `objdump` or a quick benchmark diff) that the accumulation loop auto-vectorizes to NEON on aarch64. Complex MAC over contiguous arrays vectorizes well with `-O3 -ffast-math`; if `-ffast-math` is needed for vectorization, scope it to `IrConvolver.cpp` only (`set_source_files_properties`) — never to NAM/Eigen code, whose numerics were not written for it.
- [ ] **Step 6:** Commit: `perf: exploit real-signal symmetry in ir convolver`

**Gate:** if the Pi benchmark now shows convolver ≤ 200 µs/block, **stop here — Task 4 does not run.** Record the closing numbers in `docs/hardware-validation.md`.

---

### Task 4: pffft Fallback (only if the gate fails)

If, after Task 3, the convolver still misses 200 µs/block on the Pi, the scalar FFT and reconstruction overhead are what is left — replace them with pffft (single `pffft.c/.h`, BSD, NEON-vectorized, has a native real-FFT path and a `pffft_zconvolve_accumulate` designed for exactly this partitioned-convolution accumulation pattern).

- [ ] **Step 1:** Vendor `pffft.c/.h` into `third_party/pffft/` with its license file; add to `ardor_dsp` sources. Justify in commit message with the Task 3 Pi numbers.
- [ ] **Step 2:** Replace the member FFT with `pffft_transform` (real setup, `fftSize_`), store spectra in pffft's internal layout, and use `pffft_zconvolve_accumulate` for the partition loop.
- [ ] **Step 3:** Same verification as Task 3 Step 4. Equivalence test tolerance stays the contract.
- [ ] **Step 4:** Commit: `perf: use pffft for ir convolution`

---

### Task 5: Non-Uniform Partitioning — deferred, recorded here so the ceiling is explicit

Uniform 64-sample partitions cost O(irSamples/64) MACs per block: linear in IR length. Fine at 8192 (cab IRs), untenable at reverb lengths (a 2 s IR = 96,000 samples = 1500 partitions ≈ 12× today's dominant cost). The known upgrade path is non-uniform partitioning (small head partitions for latency, exponentially larger tail partitions FFT'd at larger sizes). **Out of scope for v1**: v1 caps IRs at `--ir-samples 8192`, and the Daisy plan (post-Pi-validation) owns algorithmic reverb, which is far cheaper than convolution reverb anyway. Revisit only if a product decision demands IRs beyond ~16k samples.

---

## Skipped For This Phase

- Replacing the convolver architecture (FFT libraries with plan caching, JUCE-style engines) — the shape is right; only the inner loops need work.
- Hand-written NEON intrinsics — the compiler gets the contiguous complex-MAC loop close enough; intrinsics are a maintenance tax to pay only if Task 4's measured numbers still miss.
- Multi-threading the convolver — at 8192 samples it fits one core's leftover budget; threads on a 4-core 1GB Pi belong to NAM/UI, and cross-core handoff jitter is exactly what a 1.33 ms deadline does not need.
- Fixed-point/half-precision spectra — precision contract is float; not worth it at this working-set size.
