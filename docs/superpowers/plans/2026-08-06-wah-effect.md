# Wah Effect Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a circuit-modelled GCB-95 Cry Baby wah as a first-class `wah` block, swept by the existing expression-pedal assignment.

**Architecture:** A DK-method (discretized nodal) state-space model of the GCB-95. The two BJT base-emitter junctions form a 2-D nonlinear system whose solution is precomputed offline by Newton-Raphson into a 3-D table (`p₁ × p₂ × potPosition`) and interpolated trilinearly at runtime, so per-sample cost is fixed and signal-independent. Runs 4× oversampled. Pot position is smoothed and the DK matrices are interpolated between 33 precomputed positions rather than re-derived live.

**Tech Stack:** C++20, CMake, nlohmann::json, existing `pedal::HalfbandDecimator2x`/`HalfbandInterpolator2x`, LVGL (device UI), Go (managerd).

**Spec:** `docs/superpowers/specs/2026-08-06-wah-effect-design.md`

## Global Constraints

- C++20 (`target_compile_features(... cxx_std_20)`), matching every existing `ardor_*` library.
- Sample rate 48 kHz; block size 64 preferred, 128 fallback.
- **No allocation, no locks, and no unbounded loops in the audio path.** The callback runs `SCHED_FIFO/70` on CPU 2 alongside NAM. All buffers and tables are sized at `configure()` time.
- Target hardware: Raspberry Pi 4B, AArch64. Round-trip latency goal under 10 ms.
- `src/` code lives in `namespace ardor`. Vendored DSP under `src/daisyfx/hosted/` lives in `namespace pedal`.
- Tests are standalone `int main()` executables using the local `require(bool, std::string)` throw-on-false helper, as in `tests/compressor_smoke.cpp`. Each is registered with `add_executable` + `target_link_libraries` + `add_test` in `CMakeLists.txt`.
- Preset changes must be additive — version 1 and version 2 presets stay valid, no version bump.
- Voicing is carried as `params.mode`, following `dynamics` (`"compressor"`) and `eq` (`"parametric_eq_5"`).

## File Structure

| Path | Responsibility |
| --- | --- |
| `src/wah/WahNetlist.h/.cpp` | GCB-95 component values, pot taper law, netlist validation. Pure data + small pure functions. |
| `src/wah/WahDk.h/.cpp` | Netlist → DK state-space matrices. Host-side/offline-capable, double precision. |
| `src/wah/WahTable.h/.cpp` | Table format, load, and trilinear interpolation. |
| `src/wah/WahCircuit.h/.cpp` | Runtime per-sample DK evaluation at the oversampled rate. |
| `src/wah/WahProcessor.h/.cpp` | Block-facing: configure/process/reset, 4× oversampling, pot smoothing, level trim, latency. |
| `src/control/WahAutoEngage.h/.cpp` | Auto-engage state machine. No audio code. |
| `apps/wah-table-gen/main.cpp` | Offline Newton solve + matrix precompute; emits the data file. |
| `assets/wah/gcb95.wahtable` | Generated, checked in. |
| `tests/wah_*.cpp` | See per-task tests. |

Tasks 1–6 build a testable DSP unit with no chain integration. Tasks 7–10 integrate it. Tasks 11–12 surface it. Tasks 0 and 13 are the hardware/authenticity gates.

---

### Task 0: Cache-behaviour probe on the Pi (de-risk gate)

The 4.3 MB table is the one assumption that could invalidate the whole approach. Measure it before building anything real. This task writes no wah DSP — it writes a synthetic workload with the *same memory access shape*.

**Files:**
- Create: `tests/wah_table_probe.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: nothing consumed by later tasks. Produces a **number** that gates Task 1.

- [ ] **Step 1: Write the probe**

```cpp
// tests/wah_table_probe.cpp
// Synthetic stand-in for the wah's runtime cost. Allocates a table of the
// planned size and performs the planned per-sample access pattern, so the
// Pi's cache behaviour is measured before the real DSP exists.
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

constexpr std::size_t kGridP = 128;
constexpr std::size_t kGridPot = 33;
constexpr std::size_t kOutputs = 2;
constexpr std::size_t kOversample = 4;
constexpr std::size_t kSampleRate = 48000;

std::size_t index(std::size_t a, std::size_t b, std::size_t c)
{
  return ((c * kGridP + b) * kGridP + a) * kOutputs;
}

} // namespace

int main()
{
  std::vector<float> table(kGridP * kGridP * kGridPot * kOutputs);
  for (std::size_t i = 0; i < table.size(); ++i) {
    table[i] = static_cast<float>(i % 1000) * 0.001f;
  }

  // One second of audio at the oversampled rate.
  const std::size_t samples = kSampleRate * kOversample;
  float accumulator = 0.0f;
  float phase = 0.0f;
  std::size_t pot = 0;

  const auto start = std::chrono::steady_clock::now();
  for (std::size_t n = 0; n < samples; ++n) {
    // A signal-like trajectory through the grid: smooth, as real audio is.
    phase += 0.01f;
    const float p1 = 0.5f + 0.45f * std::sin(phase);
    const float p2 = 0.5f + 0.45f * std::sin(phase * 1.7f);
    if (n % (kSampleRate / 4) == 0) pot = (pot + 1) % (kGridPot - 1);

    const float fa = p1 * static_cast<float>(kGridP - 2);
    const float fb = p2 * static_cast<float>(kGridP - 2);
    const auto ia = static_cast<std::size_t>(fa);
    const auto ib = static_cast<std::size_t>(fb);
    const float wa = fa - static_cast<float>(ia);
    const float wb = fb - static_cast<float>(ib);

    // Trilinear: 8 corners, 2 outputs each.
    for (std::size_t out = 0; out < kOutputs; ++out) {
      float corner = 0.0f;
      for (std::size_t dc = 0; dc < 2; ++dc) {
        const float c00 = table[index(ia, ib, pot + dc) + out];
        const float c10 = table[index(ia + 1, ib, pot + dc) + out];
        const float c01 = table[index(ia, ib + 1, pot + dc) + out];
        const float c11 = table[index(ia + 1, ib + 1, pot + dc) + out];
        const float top = c00 + wa * (c10 - c00);
        const float bottom = c01 + wa * (c11 - c01);
        corner += top + wb * (bottom - top);
      }
      accumulator += corner;
    }

    // Stand-in for the ~200 flops of state-space arithmetic per sample.
    for (int k = 0; k < 50; ++k) {
      accumulator = accumulator * 0.9999f + 0.0001f;
    }
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const double seconds = std::chrono::duration<double>(elapsed).count();

  std::printf("table bytes: %zu\n", table.size() * sizeof(float));
  std::printf("one audio second took: %.4f s\n", seconds);
  std::printf("core fraction: %.2f%%\n", seconds * 100.0);
  std::printf("checksum: %f\n", accumulator);
  return 0;
}
```

- [ ] **Step 2: Register it in CMakeLists.txt**

Add after the `pedal-dsp-bench` block (around line 351):

```cmake
add_executable(pedal-wah-table-probe tests/wah_table_probe.cpp)
```

Do **not** add `add_test` — this is a benchmark, not a pass/fail test.

- [ ] **Step 3: Build and run on the desktop first**

Run: `cmake --build build-sdl --target pedal-wah-table-probe && ./build-sdl/pedal-wah-table-probe`
Expected: prints a core fraction. Desktop number is a sanity check only; it does not gate anything.

- [ ] **Step 4: Run it on the Pi 4**

Deploy and run the binary on the target. Then, with a NAM preset active, run `audio-probe-pi` and record the headroom.

- [ ] **Step 5: Record the result and decide**

Append the measured core fraction to the spec under "Hardware gate".

**Gate:** if the probe exceeds roughly 15% of a core on the Pi, stop and report back before starting Task 1 — the design needs a smaller grid (for example 64 × 64 × 17, which is ~0.5 MB) or a different interpolation scheme. Do not silently proceed with a design that will not fit.

- [ ] **Step 6: Commit**

```bash
git add tests/wah_table_probe.cpp CMakeLists.txt docs/superpowers/specs/2026-08-06-wah-effect-design.md
git commit -m "test: probe wah table cache behaviour on target hardware"
```

---

### Task 1: GCB-95 netlist and pot taper

**Files:**
- Create: `src/wah/WahNetlist.h`, `src/wah/WahNetlist.cpp`
- Create: `tests/wah_netlist_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `struct ardor::WahNetlist` with the fields listed below.
  - `const WahNetlist& ardor::gcb95Netlist();`
  - `bool ardor::wahNetlistValid(const WahNetlist&);`
  - `double ardor::wahPotWiperOhms(const WahNetlist&, double position);` — `position` 0 (heel) to 1 (toe).

> **Source-of-truth warning.** The component values below are a starting point, not an authority. Before committing this task, check every value against a published GCB-95 schematic and correct any mismatch, then record which schematic you used in a comment at the top of `WahNetlist.cpp`. The tests in this task deliberately assert *structural invariants* (positive values, monotonic taper) rather than specific component values, because a test that asserts the numbers you just typed proves nothing.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/wah_netlist_smoke.cpp
#include "wah/WahNetlist.h"

#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main()
{
  const auto& netlist = ardor::gcb95Netlist();
  require(ardor::wahNetlistValid(netlist), "the shipped GCB-95 netlist should validate");

  // A netlist with a non-positive component value is not a circuit.
  ardor::WahNetlist broken = netlist;
  broken.inductorHenries = 0.0;
  require(!ardor::wahNetlistValid(broken), "a zero inductor should fail validation");
  broken = netlist;
  broken.potOhms = -1.0;
  require(!ardor::wahNetlistValid(broken), "a negative pot resistance should fail validation");

  // The taper spans the whole pot and never reverses: the treadle must sweep
  // in one direction only, or the wah will sound like it stalls mid-throw.
  const double heel = ardor::wahPotWiperOhms(netlist, 0.0);
  const double toe = ardor::wahPotWiperOhms(netlist, 1.0);
  require(heel >= 0.0 && heel < netlist.potOhms * 0.02,
          "heel position should sit at the bottom of the pot track");
  require(toe > netlist.potOhms * 0.98 && toe <= netlist.potOhms,
          "toe position should reach the top of the pot track");

  double previous = -1.0;
  for (int i = 0; i <= 100; ++i) {
    const double value = ardor::wahPotWiperOhms(netlist, i / 100.0);
    require(value > previous, "wiper resistance should increase monotonically with position");
    previous = value;
  }

  // Audio taper, not linear: the midpoint must sit well below half the track.
  const double middle = ardor::wahPotWiperOhms(netlist, 0.5);
  require(middle < netlist.potOhms * 0.30,
          "an audio-taper pot should be well under 30% of track resistance at midpoint");

  // Out-of-range positions clamp rather than extrapolate.
  require(ardor::wahPotWiperOhms(netlist, -1.0) == heel, "positions below 0 should clamp to heel");
  require(ardor::wahPotWiperOhms(netlist, 2.0) == toe, "positions above 1 should clamp to toe");
  return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-sdl --target pedal-wah-netlist-smoke`
Expected: FAIL — `wah/WahNetlist.h` does not exist.

- [ ] **Step 3: Write the header**

```cpp
// src/wah/WahNetlist.h
#pragma once

namespace ardor {

// Component values for one wah voicing. Resistances in ohms, capacitances in
// farads, inductance in henries. Doubles throughout: this feeds the offline
// matrix derivation, where float rounding in a matrix inverse is not worth the
// risk. Runtime code consumes the derived matrices as floats, not this struct.
struct WahNetlist {
  double inputSeriesOhms = 68000.0;
  double inputCouplingFarads = 10e-9;

  // Q1: emitter follower.
  double q1BiasOhms = 470000.0;
  double q1CollectorOhms = 33000.0;
  double q1EmitterOhms = 1500.0;

  // Feedback path from Q2's emitter into the tank; the pot sits in this path.
  double feedbackCouplingFarads = 4.7e-6;
  double potOhms = 100000.0;

  // Resonant tank.
  double inductorHenries = 0.5;
  double inductorSeriesOhms = 40.0; // Winding DC resistance; damps the peak.
  double resonanceFarads = 10e-9;

  // Q2: common-emitter gain stage.
  double q2BiasOhms = 22000.0;
  double q2CollectorOhms = 33000.0;
  double q2EmitterOhms = 390.0;
  double outputCouplingFarads = 220e-9;

  // Shared BJT model (2N5088-class). Ebers-Moll, forward-active, BE junction
  // only — the base-collector junctions are deliberately omitted, which is
  // what keeps the nonlinear system 2-D instead of 4-D.
  double bjtSaturationCurrent = 1e-14;
  double bjtEmissionCoefficient = 1.0;
  double bjtThermalVolts = 0.02585;
  double bjtBeta = 300.0;

  double supplyVolts = 9.0;

  // Audio-taper approximation exponent: wiper = potOhms * position^exponent.
  // Larger means more of the sweep is concentrated toward the toe.
  double taperExponent = 2.4;
};

const WahNetlist& gcb95Netlist();

// Rejects netlists that are not physically realizable. Every component value
// must be positive and finite; beta and the supply must be positive.
bool wahNetlistValid(const WahNetlist& netlist);

// Maps treadle position (0 = heel, 1 = toe) to wiper resistance. Positions
// outside 0..1 clamp.
double wahPotWiperOhms(const WahNetlist& netlist, double position);

} // namespace ardor
```

- [ ] **Step 4: Write the implementation**

```cpp
// src/wah/WahNetlist.cpp
//
// Component values transcribed from <RECORD THE SCHEMATIC SOURCE HERE during
// Step 6 verification>. Values are the starting point for the DK derivation in
// WahDk; correctness against the real circuit is confirmed by the manual
// response-curve check in Task 13, not by any automated test.
#include "wah/WahNetlist.h"

#include <algorithm>
#include <cmath>

namespace ardor {
namespace {

bool positiveFinite(double value)
{
  return std::isfinite(value) && value > 0.0;
}

} // namespace

const WahNetlist& gcb95Netlist()
{
  static const WahNetlist netlist{};
  return netlist;
}

bool wahNetlistValid(const WahNetlist& netlist)
{
  return positiveFinite(netlist.inputSeriesOhms)
    && positiveFinite(netlist.inputCouplingFarads)
    && positiveFinite(netlist.q1BiasOhms)
    && positiveFinite(netlist.q1CollectorOhms)
    && positiveFinite(netlist.q1EmitterOhms)
    && positiveFinite(netlist.feedbackCouplingFarads)
    && positiveFinite(netlist.potOhms)
    && positiveFinite(netlist.inductorHenries)
    && positiveFinite(netlist.inductorSeriesOhms)
    && positiveFinite(netlist.resonanceFarads)
    && positiveFinite(netlist.q2BiasOhms)
    && positiveFinite(netlist.q2CollectorOhms)
    && positiveFinite(netlist.q2EmitterOhms)
    && positiveFinite(netlist.outputCouplingFarads)
    && positiveFinite(netlist.bjtSaturationCurrent)
    && positiveFinite(netlist.bjtEmissionCoefficient)
    && positiveFinite(netlist.bjtThermalVolts)
    && positiveFinite(netlist.bjtBeta)
    && positiveFinite(netlist.supplyVolts)
    && positiveFinite(netlist.taperExponent);
}

double wahPotWiperOhms(const WahNetlist& netlist, double position)
{
  const double clamped = std::clamp(position, 0.0, 1.0);
  return netlist.potOhms * std::pow(clamped, netlist.taperExponent);
}

} // namespace ardor
```

- [ ] **Step 5: Add the library and test to CMakeLists.txt**

Add a new library after the `ardor_equalizer` block (around line 160):

```cmake
add_library(ardor_wah
  src/wah/WahNetlist.cpp
)
target_include_directories(ardor_wah PUBLIC
  src
  ${neuralampmodelercore_SOURCE_DIR}/Dependencies
)
target_compile_features(ardor_wah PUBLIC cxx_std_20)
```

Add the test alongside the other test executables (near line 334):

```cmake
add_executable(pedal-wah-netlist-smoke tests/wah_netlist_smoke.cpp)
target_link_libraries(pedal-wah-netlist-smoke PRIVATE ardor_wah)
```

And with the other `add_test` lines (near line 392):

```cmake
add_test(NAME pedal-wah-netlist-smoke COMMAND pedal-wah-netlist-smoke)
```

- [ ] **Step 6: Verify the component values against a real schematic**

Open a published GCB-95 schematic. Check each value in `WahNetlist`. Correct any that differ, and replace the `<RECORD THE SCHEMATIC SOURCE HERE>` placeholder in the `.cpp` header comment with the actual source.

If you cannot obtain a schematic, **stop and report that** rather than committing unverified values — every later task inherits this data, and a wrong netlist produces a model that is internally consistent and sounds wrong.

- [ ] **Step 7: Run the test to verify it passes**

Run: `cmake --build build-sdl --target pedal-wah-netlist-smoke && ./build-sdl/pedal-wah-netlist-smoke`
Expected: exit code 0, no output.

If the audio-taper assertion fails after you corrected values, adjust `taperExponent` — do not weaken the test. A real GCB-95 pot is audio taper and the midpoint genuinely sits low.

- [ ] **Step 8: Commit**

```bash
git add src/wah/WahNetlist.h src/wah/WahNetlist.cpp tests/wah_netlist_smoke.cpp CMakeLists.txt
git commit -m "feat(wah): add GCB-95 netlist and audio-taper pot law"
```

---

### Task 2: DK matrix derivation

Builds the state-space matrices from the netlist at a fixed pot position, treating the BJTs as linear small-signal devices for now. This gets the discretization correct and testable before the nonlinear table exists.

**Files:**
- Create: `src/wah/WahDk.h`, `src/wah/WahDk.cpp`
- Create: `tests/wah_dk_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ardor::WahNetlist`, `ardor::gcb95Netlist()`, `ardor::wahPotWiperOhms()` from Task 1.
- Produces:
  - `struct ardor::WahDkMatrices` — `a` (7×7), `b` (7×1), `c` (7×2), `d` (2×7), `e` (2×1), `f` (2×2), `g` (1×7), `h` (1×1), `k` (1×2), all `std::vector<double>` in row-major order, plus `std::size_t states = 7`, `nonlinearPorts = 2`.
  - `ardor::WahDkMatrices ardor::deriveWahDk(const WahNetlist&, double wiperOhms, double sampleRate);`
  - `double ardor::wahLinearPeakHz(const WahDkMatrices&, double sampleRate);` — analysis helper for tests, sweeps the linear response and returns the peak frequency.

The DK formulation, for reference while implementing:

```
x[n] = A·x[n-1] + B·u[n] + C·i[n]
v[n] = D·x[n-1] + E·u[n] + F·i[n]
y[n] = G·x[n-1] + H·u[n] + K·i[n]
```

where `x` is the reactive state (6 capacitor voltages + 1 inductor current), `u` the input sample, `v` the two nonlinear port voltages, `i = f(v)` the nonlinear currents, and `y` the output. Reactive elements are discretized trapezoidally. Matrices come from the MNA system solved for the state, port, and output rows.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/wah_dk_smoke.cpp
#include "wah/WahDk.h"
#include "wah/WahNetlist.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool allFinite(const std::vector<double>& values)
{
  for (const double value : values) {
    if (!std::isfinite(value)) return false;
  }
  return true;
}

} // namespace

int main()
{
  const auto& netlist = ardor::gcb95Netlist();
  constexpr double kSampleRate = 192000.0; // 4x oversampled rate.

  const auto middle = ardor::deriveWahDk(netlist, ardor::wahPotWiperOhms(netlist, 0.5), kSampleRate);
  require(middle.states == 7, "the GCB-95 model should have 7 reactive states");
  require(middle.nonlinearPorts == 2, "the model should expose 2 nonlinear ports");
  require(middle.a.size() == 49 && middle.b.size() == 7 && middle.c.size() == 14,
          "state matrices should be sized states x {states, 1, ports}");
  require(middle.d.size() == 14 && middle.e.size() == 2 && middle.f.size() == 4,
          "port matrices should be sized ports x {states, 1, ports}");
  require(allFinite(middle.a) && allFinite(middle.b) && allFinite(middle.c)
            && allFinite(middle.d) && allFinite(middle.e) && allFinite(middle.f)
            && allFinite(middle.g) && allFinite(middle.h) && allFinite(middle.k),
          "no derived matrix entry should be non-finite");

  // Discrete-time stability: every eigenvalue of A must sit inside the unit
  // circle, or the filter will run away regardless of what drives it. Tested
  // by power iteration on the spectral radius.
  const double radius = ardor::wahSpectralRadius(middle);
  require(radius < 1.0, "the discretized state matrix must be stable");

  // The whole point of the circuit: the resonant peak sweeps upward with the
  // pot, monotonically, across the audible wah range.
  double previousHz = 0.0;
  for (int i = 0; i <= 10; ++i) {
    const double position = i / 10.0;
    const auto matrices =
      ardor::deriveWahDk(netlist, ardor::wahPotWiperOhms(netlist, position), kSampleRate);
    require(ardor::wahSpectralRadius(matrices) < 1.0,
            "the model must be stable at every pot position, not just midpoint");
    const double peakHz = ardor::wahLinearPeakHz(matrices, kSampleRate);
    require(peakHz > previousHz, "the resonant peak should rise monotonically toward the toe");
    previousHz = peakHz;
  }

  const auto heel = ardor::deriveWahDk(netlist, ardor::wahPotWiperOhms(netlist, 0.0), kSampleRate);
  const auto toe = ardor::deriveWahDk(netlist, ardor::wahPotWiperOhms(netlist, 1.0), kSampleRate);
  const double heelHz = ardor::wahLinearPeakHz(heel, kSampleRate);
  const double toeHz = ardor::wahLinearPeakHz(toe, kSampleRate);
  require(heelHz > 300.0 && heelHz < 600.0,
          "heel-down peak should land near 400 Hz for a GCB-95");
  require(toeHz > 1400.0 && toeHz < 2600.0,
          "toe-down peak should land near 2 kHz for a GCB-95");
  return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-sdl --target pedal-wah-dk-smoke`
Expected: FAIL — `wah/WahDk.h` does not exist.

- [ ] **Step 3: Write the header**

```cpp
// src/wah/WahDk.h
#pragma once

#include "wah/WahNetlist.h"

#include <cstddef>
#include <vector>

namespace ardor {

// Discretized nodal (DK-method) state space for one pot position:
//
//   x[n] = A x[n-1] + B u[n] + C i[n]
//   v[n] = D x[n-1] + E u[n] + F i[n]
//   y[n] = G x[n-1] + H u[n] + K i[n]
//
// with i = f(v) the nonlinear device currents. All matrices are row-major
// doubles; the runtime converts to float once, at load.
struct WahDkMatrices {
  std::size_t states = 7;
  std::size_t nonlinearPorts = 2;
  std::vector<double> a; // states x states
  std::vector<double> b; // states x 1
  std::vector<double> c; // states x ports
  std::vector<double> d; // ports x states
  std::vector<double> e; // ports x 1
  std::vector<double> f; // ports x ports
  std::vector<double> g; // 1 x states
  std::vector<double> h; // 1 x 1
  std::vector<double> k; // 1 x ports
};

// Builds the MNA system for `netlist` with the pot wiper at `wiperOhms`,
// discretizes the reactive elements trapezoidally at `sampleRate`, and solves
// for the state, port, and output rows.
WahDkMatrices deriveWahDk(const WahNetlist& netlist, double wiperOhms, double sampleRate);

// Spectral radius of A by power iteration. Below 1.0 means the discretized
// system is stable. Test and validation helper; not used in the audio path.
double wahSpectralRadius(const WahDkMatrices& matrices);

// Frequency of the largest magnitude-response peak, found by evaluating the
// linearized transfer function on a log-spaced grid from 50 Hz to Nyquist.
// Test and validation helper; not used in the audio path.
double wahLinearPeakHz(const WahDkMatrices& matrices, double sampleRate);

} // namespace ardor
```

- [ ] **Step 4: Write the implementation**

`WahDk.cpp` needs, in this order:

1. A small dense linear-algebra helper block (LU decomposition with partial pivoting, `solve`, `multiply`, `invert`) in an anonymous namespace. Do not add a dependency for this — the matrices are 7×7 at most, and a third-party linear algebra library is not worth a new build input for one file.
2. MNA stamping: build the conductance matrix `G`, the input incidence vector, the reactive-element incidence matrix, and the nonlinear-port incidence matrix from the netlist. The pot appears as a resistance of `wiperOhms` in the feedback branch and `potOhms - wiperOhms` in the shunt branch.
3. Trapezoidal discretization: each capacitor contributes conductance `2C·fs` with a state-dependent current source; the inductor contributes resistance `2L·fs` in series with `inductorSeriesOhms`.
4. Solve the stamped system for the A/B/C, D/E/F, and G/H/K rows.
5. The two nonlinear ports are the base-emitter voltages of Q1 and Q2.

Implement `wahSpectralRadius` as 200 power iterations with renormalization each step, returning the Rayleigh quotient magnitude. Implement `wahLinearPeakHz` by linearizing `i = f(v)` about the quiescent operating point (small-signal conductance `g = Is/(N·Vt)·exp(v_q/(N·Vt))`), folding that into the state space, then evaluating `|H(e^{jω})|` over 2048 log-spaced points from 50 Hz to `sampleRate/2` and returning the argmax.

- [ ] **Step 5: Register in CMakeLists.txt**

Add `src/wah/WahDk.cpp` to the `ardor_wah` source list, then:

```cmake
add_executable(pedal-wah-dk-smoke tests/wah_dk_smoke.cpp)
target_link_libraries(pedal-wah-dk-smoke PRIVATE ardor_wah)
```

```cmake
add_test(NAME pedal-wah-dk-smoke COMMAND pedal-wah-dk-smoke)
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build build-sdl --target pedal-wah-dk-smoke && ./build-sdl/pedal-wah-dk-smoke`
Expected: exit code 0.

**If the peak-frequency assertions fail**, the netlist or the stamping is wrong — this is the test doing its job. Debug the stamping before touching the assertion bounds. The 400 Hz / 2 kHz range is the defining characteristic of the effect, not a tuning knob.

- [ ] **Step 7: Commit**

```bash
git add src/wah/WahDk.h src/wah/WahDk.cpp tests/wah_dk_smoke.cpp CMakeLists.txt
git commit -m "feat(wah): derive DK state-space matrices from the netlist"
```

---

### Task 3: Offline table generation

**Files:**
- Create: `apps/wah-table-gen/main.cpp`
- Create: `src/wah/WahTable.h`, `src/wah/WahTable.cpp`
- Create: `tests/wah_table_smoke.cpp`
- Create: `assets/wah/gcb95.wahtable` (generated output, checked in)
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ardor::deriveWahDk`, `ardor::WahDkMatrices`, `ardor::gcb95Netlist`.
- Produces:
  - `struct ardor::WahTableHeader { std::uint32_t magic; std::uint32_t version; std::uint32_t gridP; std::uint32_t gridPot; float pMin; float pMax; float sampleRate; };`
  - `struct ardor::WahTable` holding the header, the interleaved matrix set per pot position, and the nonlinear solution grid.
  - `bool ardor::writeWahTable(const std::filesystem::path&, const WahTable&, std::string& error);`
  - `bool ardor::readWahTable(const std::filesystem::path&, WahTable&, std::string& error);`
  - `bool ardor::solveWahPort(const WahDkMatrices&, const WahNetlist&, double p1, double p2, double& i1, double& i2, int maxIterations);` — the offline Newton solve.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/wah_table_smoke.cpp
#include "wah/WahDk.h"
#include "wah/WahNetlist.h"
#include "wah/WahTable.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main()
{
  const auto& netlist = ardor::gcb95Netlist();
  constexpr double kSampleRate = 192000.0;
  const auto matrices = ardor::deriveWahDk(netlist, ardor::wahPotWiperOhms(netlist, 0.5), kSampleRate);

  // The Newton solve must converge everywhere in the grid's range, including
  // the corners, where the exponential is at its most hostile.
  for (const double p1 : {-3.0, -0.5, 0.0, 0.5, 3.0}) {
    for (const double p2 : {-3.0, -0.5, 0.0, 0.5, 3.0}) {
      double i1 = 0.0;
      double i2 = 0.0;
      require(ardor::solveWahPort(matrices, netlist, p1, p2, i1, i2, 100),
              "Newton should converge at grid corner p1=" + std::to_string(p1)
                + " p2=" + std::to_string(p2));
      require(std::isfinite(i1) && std::isfinite(i2), "solved currents should be finite");
    }
  }

  // Round-trip the on-disk format.
  const auto path = std::filesystem::temp_directory_path() / "wah_table_smoke.wahtable";
  ardor::WahTable written;
  std::string error;
  require(ardor::buildWahTable(netlist, kSampleRate, 16, 5, written, error), error);
  require(ardor::writeWahTable(path, written, error), error);

  ardor::WahTable read;
  require(ardor::readWahTable(path, read, error), error);
  require(read.header.gridP == written.header.gridP, "grid width should survive the round trip");
  require(read.header.gridPot == written.header.gridPot, "pot count should survive the round trip");
  require(read.solutions.size() == written.solutions.size(), "solution count should match");
  for (std::size_t i = 0; i < written.solutions.size(); ++i) {
    require(read.solutions[i] == written.solutions[i],
            "solutions should round-trip bit-exactly; the checked-in table depends on it");
  }

  // A truncated or corrupt file must be rejected, not read as garbage.
  std::filesystem::resize_file(path, 8);
  ardor::WahTable truncated;
  require(!ardor::readWahTable(path, truncated, error),
          "a truncated table file should fail to load");
  require(!error.empty(), "a failed load should explain itself");
  std::filesystem::remove(path);
  return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-sdl --target pedal-wah-table-smoke`
Expected: FAIL — `wah/WahTable.h` does not exist.

- [ ] **Step 3: Implement WahTable.h/.cpp**

The header declares `WahTableHeader`, `WahTable` (header + `std::vector<float> matrices` + `std::vector<float> solutions`), and the five functions listed under Interfaces, plus:

```cpp
// Builds a complete table: derives matrices at `potCount` evenly spaced
// positions and Newton-solves the nonlinear system at every (p1, p2, pot)
// grid point. Slow by design — this is the offline path.
bool buildWahTable(const WahNetlist& netlist, double sampleRate,
                   std::uint32_t gridP, std::uint32_t potCount,
                   WahTable& out, std::string& error);
```

`solveWahPort` implements Newton-Raphson on `g(v) = v - p - F·i(v)`, with the Ebers-Moll BE current `i(v) = Is·(exp(v/(N·Vt)) - 1)` and Jacobian `I - F·J` where `J = diag(Is/(N·Vt)·exp(v/(N·Vt)))`. Two guards matter and both are load-bearing:

- **Clamp the exponent argument** to at most about 40 before calling `exp`, or the first Newton step from a cold start overflows to infinity and never recovers.
- **Damp the step** — limit each iteration's voltage change to 0.1 V. Undamped Newton on a diode exponential oscillates instead of converging.

`writeWahTable` writes the header then the two float arrays, little-endian, no padding. `readWahTable` validates magic, version, and that the file length exactly matches the size implied by the header — return `false` with a populated `error` otherwise.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build-sdl --target pedal-wah-table-smoke && ./build-sdl/pedal-wah-table-smoke`
Expected: exit code 0.

- [ ] **Step 5: Write the generator app**

```cpp
// apps/wah-table-gen/main.cpp
// Offline generator for the wah nonlinear solution table. The output is
// checked into assets/ and CI verifies regeneration is byte-identical, so this
// tool never runs during a cross-compile.
#include "wah/WahNetlist.h"
#include "wah/WahTable.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

int main(int argc, char** argv)
{
  std::filesystem::path output = "assets/wah/gcb95.wahtable";
  double sampleRate = 192000.0;
  std::uint32_t gridP = 128;
  std::uint32_t potCount = 33;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto value = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "%s requires a value\n", arg.c_str());
        std::exit(1);
      }
      return argv[++i];
    };
    if (arg == "--output") output = value();
    else if (arg == "--sample-rate") sampleRate = std::stod(value());
    else if (arg == "--grid") gridP = static_cast<std::uint32_t>(std::stoul(value()));
    else if (arg == "--pot-positions") potCount = static_cast<std::uint32_t>(std::stoul(value()));
    else {
      std::fprintf(stderr, "usage: wah-table-gen [--output PATH] [--sample-rate HZ]"
                           " [--grid N] [--pot-positions N]\n");
      return 1;
    }
  }

  ardor::WahTable table;
  std::string error;
  if (!ardor::buildWahTable(ardor::gcb95Netlist(), sampleRate, gridP, potCount, table, error)) {
    std::fprintf(stderr, "failed to build table: %s\n", error.c_str());
    return 1;
  }
  std::filesystem::create_directories(output.parent_path());
  if (!ardor::writeWahTable(output, table, error)) {
    std::fprintf(stderr, "failed to write %s: %s\n", output.string().c_str(), error.c_str());
    return 1;
  }
  std::printf("wrote %s (%zu solutions, %zu matrix floats)\n",
              output.string().c_str(), table.solutions.size(), table.matrices.size());
  return 0;
}
```

Register it:

```cmake
add_executable(wah-table-gen apps/wah-table-gen/main.cpp)
target_link_libraries(wah-table-gen PRIVATE ardor_wah)
```

- [ ] **Step 6: Generate the table and confirm determinism**

```bash
cmake --build build-sdl --target wah-table-gen
./build-sdl/wah-table-gen --output assets/wah/gcb95.wahtable
shasum -a 256 assets/wah/gcb95.wahtable
./build-sdl/wah-table-gen --output /tmp/regen.wahtable
shasum -a 256 /tmp/regen.wahtable
```

Expected: identical hashes. If they differ, the generator has an ordering or uninitialized-memory bug — fix it before committing, because CI will verify this property.

- [ ] **Step 7: Verify the table size matches the Task 0 probe**

Run: `ls -l assets/wah/gcb95.wahtable`
Expected: roughly 4.3 MB at the default grid. If it is materially larger than what Task 0 measured as acceptable, reduce `--grid` and regenerate.

- [ ] **Step 8: Commit**

```bash
git add src/wah/WahTable.h src/wah/WahTable.cpp apps/wah-table-gen/main.cpp \
        tests/wah_table_smoke.cpp assets/wah/gcb95.wahtable CMakeLists.txt
git commit -m "feat(wah): generate the nonlinear solution table offline"
```

---

### Task 4: Runtime circuit evaluation

**Files:**
- Create: `src/wah/WahCircuit.h`, `src/wah/WahCircuit.cpp`
- Create: `tests/wah_circuit_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ardor::WahTable`, `ardor::readWahTable`.
- Produces:
  - `class ardor::WahCircuit` with `bool load(const std::filesystem::path&, std::string& error)`, `void setPotPosition(float position)`, `float process(float input)`, `void reset()`, `float potPosition() const`.

`WahCircuit::process` runs at the **oversampled** rate — it knows nothing about oversampling. Per sample it computes `p = D·x + E·u`, looks up `i` by trilinear interpolation, then updates `x` and computes `y`. All state is fixed-size `std::array`; nothing allocates after `load`.

`setPotPosition` selects the two bracketing pot indices and their blend weight; the matrices used per sample are the linear blend of the two precomputed sets. This is a control-rate operation but is cheap enough to be safe anywhere.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/wah_circuit_smoke.cpp
#include "wah/WahCircuit.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

const char* tablePath()
{
  const char* fromEnv = std::getenv("ARDOR_WAH_TABLE");
  return fromEnv != nullptr ? fromEnv : "assets/wah/gcb95.wahtable";
}

} // namespace

int main()
{
  ardor::WahCircuit circuit;
  std::string error;
  require(circuit.load(tablePath(), error), error);

  // Stability: a full-scale signal at every pot position must stay finite and
  // bounded. This is the assertion that catches a bad discretization.
  for (int p = 0; p <= 20; ++p) {
    circuit.reset();
    circuit.setPotPosition(p / 20.0f);
    float peak = 0.0f;
    for (int n = 0; n < 192000; ++n) {
      const float input = std::sin(static_cast<float>(n) * 0.05f);
      const float output = circuit.process(input);
      require(std::isfinite(output), "output must stay finite at pot position "
                                       + std::to_string(p / 20.0));
      peak = std::max(peak, std::fabs(output));
    }
    require(peak < 100.0f, "output must stay bounded at pot position " + std::to_string(p / 20.0));
    require(peak > 0.01f, "the circuit should actually pass signal, not collapse to silence");
  }

  // Reset must fully clear state: the same input after a reset must produce
  // the same output, or preset switching will carry tails across.
  circuit.reset();
  circuit.setPotPosition(0.5f);
  std::vector<float> first;
  for (int n = 0; n < 1000; ++n) first.push_back(circuit.process(std::sin(n * 0.05f)));
  circuit.reset();
  for (int n = 0; n < 1000; ++n) {
    const float repeated = circuit.process(std::sin(n * 0.05f));
    require(std::fabs(repeated - first[n]) < 1e-6f, "reset should restore identical behaviour");
  }

  // Table interpolation continuity: nudging the pot by a hair must not step
  // the output. This is the failure mode specific to a table-based solver, and
  // it is audible as a click when a player rocks the treadle slowly.
  circuit.reset();
  circuit.setPotPosition(0.5f);
  for (int n = 0; n < 4096; ++n) circuit.process(std::sin(n * 0.05f));
  const float before = circuit.process(0.5f);
  circuit.setPotPosition(0.5f + 1e-4f);
  const float after = circuit.process(0.5f);
  require(std::fabs(after - before) < 0.01f,
          "a tiny pot change must not step the output across a grid cell boundary");

  // A missing table is an error, not a crash or a silent pass-through.
  ardor::WahCircuit missing;
  require(!missing.load("does/not/exist.wahtable", error), "loading a missing table should fail");
  require(!error.empty(), "a failed load should explain itself");
  return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-sdl --target pedal-wah-circuit-smoke`
Expected: FAIL — `wah/WahCircuit.h` does not exist.

- [ ] **Step 3: Implement WahCircuit**

Key points, all of them load-bearing:

- State is `std::array<float, 7>`; the blended matrices are `std::array<float, N>` members refreshed only in `setPotPosition`.
- Trilinear interpolation over `(p₁, p₂, potBlend)`: clamp grid coordinates before indexing. An out-of-range `p` from a transient must clamp, never index out of bounds.
- Apply the existing `DenormalGuard` (`src/dsp/DenormalGuard.h`) idiom around the state update — a resonant filter parked in silence is exactly where denormals accumulate.
- `process` must contain no branches on sample value beyond the clamps, and no loops whose trip count depends on the signal.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build-sdl --target pedal-wah-circuit-smoke && ./build-sdl/pedal-wah-circuit-smoke`
Expected: exit code 0.

The continuity assertion is the one most likely to fail first. If it does, the cause is almost always interpolating the *solutions* while snapping the *matrices* to the nearest pot index — both have to blend.

- [ ] **Step 5: Register in CMakeLists.txt**

Add `src/wah/WahCircuit.cpp` to `ardor_wah`, then the executable and `add_test` entries following the pattern from Task 1 Step 5. The test needs the asset path, so run it from the repo root:

```cmake
add_test(NAME pedal-wah-circuit-smoke COMMAND pedal-wah-circuit-smoke)
set_tests_properties(pedal-wah-circuit-smoke PROPERTIES
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

- [ ] **Step 6: Commit**

```bash
git add src/wah/WahCircuit.h src/wah/WahCircuit.cpp tests/wah_circuit_smoke.cpp CMakeLists.txt
git commit -m "feat(wah): evaluate the DK model from the precomputed table"
```

---

### Task 5: WahProcessor — oversampling, smoothing, level

**Files:**
- Create: `src/wah/WahProcessor.h`, `src/wah/WahProcessor.cpp`
- Create: `tests/wah_automation.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ardor::WahCircuit`, `pedal::HalfbandDecimator2x`, `pedal::HalfbandInterpolator2x` (`src/daisyfx/hosted/dsp/halfband_resampler.h`).
- Produces:
  - `class ardor::WahProcessor` mirroring `CompressorProcessor`'s shape: move-only, `bool configure(const nlohmann::json& params, float sampleRate, std::string& error)`, `bool setParameterTarget(const std::string& key, float value)`, `void reset()`, `StereoSample process(StereoSample input)`, `std::size_t latencyFrames() const noexcept`.

Parameter keys accepted by `setParameterTarget`: `"position"` (0..1), `"level"` (dB, -24..+24). `configure` also reads `"mode"` (voicing name, currently only `"gcb95"`).

The wah is a mono circuit. `process` sums to mono, processes once, and returns the result on both channels — matching how a real wah sits in a mono guitar path, and half the CPU of running two circuits.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/wah_automation.cpp
#include "wah/WahProcessor.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

nlohmann::json defaultParams()
{
  return nlohmann::json{{"mode", "gcb95"}, {"position", 0.0f}, {"level", 0.0f}};
}

} // namespace

int main()
{
  ardor::WahProcessor wah;
  std::string error;
  require(wah.configure(defaultParams(), 48000.0f, error), error);

  // Sweeping the pedal at the real 8 ms control cadence must not produce
  // zipper noise. Discontinuity is measured as a jump in the output's first
  // difference far outside what the signal itself can explain.
  wah.reset();
  float previous = 0.0f;
  float previousDelta = 0.0f;
  float worstJump = 0.0f;
  const int framesPerPoll = 384; // 8 ms at 48 kHz.
  for (int poll = 0; poll < 125; ++poll) {
    require(wah.setParameterTarget("position", poll / 125.0f),
            "position should be a live-settable parameter");
    for (int n = 0; n < framesPerPoll; ++n) {
      const float input = 0.5f * std::sin(static_cast<float>(poll * framesPerPoll + n) * 0.02f);
      const float output = wah.process({input, input}).left;
      require(std::isfinite(output), "swept output must stay finite");
      const float delta = output - previous;
      worstJump = std::max(worstJump, std::fabs(delta - previousDelta));
      previousDelta = delta;
      previous = output;
    }
  }
  require(worstJump < 0.05f, "a full pedal sweep should not step the output");

  // Both channels carry the same mono result.
  wah.reset();
  const auto stereo = wah.process({0.3f, -0.7f});
  require(stereo.left == stereo.right, "the wah is a mono circuit fed to both channels");

  // Level trims the output in dB.
  ardor::WahProcessor loud;
  auto louder = defaultParams();
  louder["level"] = 6.0f;
  require(loud.configure(louder, 48000.0f, error), error);
  ardor::WahProcessor unity;
  require(unity.configure(defaultParams(), 48000.0f, error), error);
  float loudPeak = 0.0f;
  float unityPeak = 0.0f;
  for (int n = 0; n < 4800; ++n) {
    const float input = 0.5f * std::sin(static_cast<float>(n) * 0.05f);
    loudPeak = std::max(loudPeak, std::fabs(loud.process({input, input}).left));
    unityPeak = std::max(unityPeak, std::fabs(unity.process({input, input}).left));
  }
  const float ratio = loudPeak / unityPeak;
  require(ratio > 1.8f && ratio < 2.2f, "+6 dB of level should roughly double the output");

  // Unknown parameters are rejected rather than silently ignored, so a typo in
  // a preset surfaces instead of producing a dead control.
  require(!wah.setParameterTarget("postion", 0.5f), "a misspelled key should be rejected");

  // Latency is reported so the chain can align.
  require(wah.latencyFrames() > 0, "4x halfband oversampling has non-zero latency");
  require(wah.latencyFrames() < 64, "oversampling latency should stay well inside one block");

  // An unknown voicing fails configure with an explanation.
  ardor::WahProcessor bad;
  auto badParams = defaultParams();
  badParams["mode"] = "not_a_voicing";
  require(!bad.configure(badParams, 48000.0f, error), "an unknown voicing should fail configure");
  require(!error.empty(), "a failed configure should explain itself");
  return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-sdl --target pedal-wah-automation`
Expected: FAIL — `wah/WahProcessor.h` does not exist.

- [ ] **Step 3: Implement WahProcessor**

- Oversampling: two `HalfbandInterpolator2x` stages up, four `WahCircuit::process` calls, two `HalfbandDecimator2x` stages down. `latencyFrames()` returns the combined group delay of the halfband chain in host-rate frames.
- Pot smoothing: one-pole with a 15 ms time constant at the host rate, applied to the target set by `setParameterTarget("position", …)`. Call `circuit_.setPotPosition()` once per host-rate sample with the smoothed value.
- Level: `std::pow(10.0f, levelDb / 20.0f)`, applied after the decimator.
- Follow `CompressorProcessor`'s cross-thread pattern: `setParameterTarget` is called from the control thread and must publish without locking. Reuse the `std::shared_ptr<LiveParameters>` + revision-counter idiom from `CompressorProcessor::refreshLiveParameters`.
- The table path: resolve `assets/wah/<mode>.wahtable` relative to the data root. Match how existing blocks resolve assets in `ChainPlan.cpp`.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build-sdl --target pedal-wah-automation && ./build-sdl/pedal-wah-automation`
Expected: exit code 0.

If `worstJump` fails, lengthen the smoothing time constant before touching the threshold — but if it needs to go past about 40 ms the pedal will feel laggy, which means the real problem is elsewhere (most likely matrices snapping rather than blending, from Task 4).

- [ ] **Step 5: Register in CMakeLists.txt and commit**

```cmake
add_executable(pedal-wah-automation tests/wah_automation.cpp)
target_link_libraries(pedal-wah-automation PRIVATE ardor_wah ardor_daisyfx)
add_test(NAME pedal-wah-automation COMMAND pedal-wah-automation)
set_tests_properties(pedal-wah-automation PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

Add `src/wah/WahProcessor.cpp` to `ardor_wah` and link `ardor_wah` against `ardor_daisyfx` (for the halfband resampler):

```cmake
target_link_libraries(ardor_wah PUBLIC ardor_daisyfx)
```

```bash
git add src/wah/WahProcessor.h src/wah/WahProcessor.cpp tests/wah_automation.cpp CMakeLists.txt
git commit -m "feat(wah): add the oversampled block-facing wah processor"
```

---

### Task 6: Response acceptance test

This is the test that decides whether the thing is a wah or merely a filter.

**Files:**
- Create: `tests/wah_response.cpp`
- Create: `tests/data/wah_reference_gcb95.json`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ardor::WahProcessor`.
- Produces: nothing consumed by later tasks.

Model it on `tests/daisy_fx_response.cpp` — read that file first and follow its measurement approach.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/wah_response.cpp
//
// Measures the swept resonant peak against a checked-in reference. The
// reference is generated from the same netlist, so this proves the
// discretization and table pipeline are faithful to the model — it does NOT
// prove the model matches a real GCB-95. That check is manual; see the plan.
#include "wah/WahProcessor.h"

#include <cmath>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

// Magnitude at one frequency by single-bin Goertzel-style correlation, after
// letting the filter settle.
float magnitudeAt(ardor::WahProcessor& wah, float frequency, float sampleRate)
{
  const float omega = 2.0f * std::numbers::pi_v<float> * frequency / sampleRate;
  for (int n = 0; n < 8192; ++n) wah.process({std::sin(omega * n), std::sin(omega * n)});
  float real = 0.0f;
  float imaginary = 0.0f;
  const int measured = 16384;
  for (int n = 0; n < measured; ++n) {
    const float output = wah.process({std::sin(omega * (n + 8192)), std::sin(omega * (n + 8192))}).left;
    real += output * std::cos(omega * n);
    imaginary += output * std::sin(omega * n);
  }
  return 2.0f * std::sqrt(real * real + imaginary * imaginary) / measured;
}

struct PeakMeasurement {
  float frequencyHz = 0.0f;
  float gainDb = 0.0f;
  float q = 0.0f;
};

PeakMeasurement measurePeak(ardor::WahProcessor& wah, float position, float sampleRate)
{
  std::vector<float> frequencies;
  for (int i = 0; i < 200; ++i) {
    frequencies.push_back(100.0f * std::pow(10.0f, 1.5f * i / 199.0f)); // 100 Hz .. ~3.2 kHz
  }
  PeakMeasurement peak;
  float peakMagnitude = 0.0f;
  std::vector<float> magnitudes;
  for (const float frequency : frequencies) {
    wah.reset();
    wah.setParameterTarget("position", position);
    // Settle the smoother before measuring.
    for (int n = 0; n < 4800; ++n) wah.process({0.0f, 0.0f});
    const float magnitude = magnitudeAt(wah, frequency, sampleRate);
    magnitudes.push_back(magnitude);
    if (magnitude > peakMagnitude) {
      peakMagnitude = magnitude;
      peak.frequencyHz = frequency;
    }
  }
  peak.gainDb = 20.0f * std::log10(peakMagnitude);

  // -3 dB points either side of the peak give Q = f0 / bandwidth.
  const float target = peakMagnitude / std::sqrt(2.0f);
  float lower = frequencies.front();
  float upper = frequencies.back();
  for (std::size_t i = 0; i < magnitudes.size(); ++i) {
    if (frequencies[i] < peak.frequencyHz && magnitudes[i] < target) lower = frequencies[i];
    if (frequencies[i] > peak.frequencyHz && magnitudes[i] < target) { upper = frequencies[i]; break; }
  }
  peak.q = peak.frequencyHz / std::max(1.0f, upper - lower);
  return peak;
}

} // namespace

int main()
{
  constexpr float kSampleRate = 48000.0f;
  ardor::WahProcessor wah;
  std::string error;
  require(wah.configure({{"mode", "gcb95"}, {"position", 0.0f}, {"level", 0.0f}}, kSampleRate, error),
          error);

  std::ifstream referenceFile("tests/data/wah_reference_gcb95.json");
  require(referenceFile.good(), "the reference file should be readable from the repo root");
  nlohmann::json reference;
  referenceFile >> reference;

  float previousHz = 0.0f;
  float previousQ = 0.0f;
  float previousGainDb = -1000.0f;

  for (const auto& entry : reference.at("positions")) {
    const auto position = entry.at("position").get<float>();
    const auto measured = measurePeak(wah, position, kSampleRate);

    const auto expectedHz = entry.at("peakHz").get<float>();
    const auto expectedGainDb = entry.at("peakGainDb").get<float>();
    const auto expectedQ = entry.at("q").get<float>();

    require(std::fabs(measured.frequencyHz - expectedHz) < expectedHz * 0.08f,
            "peak frequency at position " + std::to_string(position) + " should stay within 8% of reference");
    require(std::fabs(measured.gainDb - expectedGainDb) < 2.0f,
            "peak gain at position " + std::to_string(position) + " should stay within 2 dB of reference");
    require(std::fabs(measured.q - expectedQ) < expectedQ * 0.20f,
            "Q at position " + std::to_string(position) + " should stay within 20% of reference");

    // The behavioural claims that make it a wah rather than a filter sweep.
    require(measured.frequencyHz > previousHz, "the peak must rise toward the toe");
    require(measured.q >= previousQ * 0.95f, "Q must not fall as the pedal moves toward the toe");
    require(measured.gainDb >= previousGainDb - 0.5f, "peak gain must not fall toward the toe");
    previousHz = measured.frequencyHz;
    previousQ = measured.q;
    previousGainDb = measured.gainDb;
  }

  require(previousHz > 1400.0f, "the sweep should reach at least 1.4 kHz at the toe");
  return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-sdl --target pedal-wah-response && ./build-sdl/pedal-wah-response`
Expected: FAIL — the reference file does not exist.

- [ ] **Step 3: Generate the reference file**

Temporarily add a `--emit-reference` path to the test (or a throwaway script) that runs `measurePeak` at positions `0.0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1.0` and writes:

```json
{
  "netlist": "gcb95",
  "sampleRate": 48000,
  "note": "Generated from the same netlist the test exercises. Validates the discretization and table pipeline, not fidelity to a physical GCB-95.",
  "positions": [
    { "position": 0.0, "peakHz": 0.0, "peakGainDb": 0.0, "q": 0.0 }
  ]
}
```

with the measured values filled in. Inspect the numbers before accepting them: the peak should run from roughly 400 Hz to roughly 2 kHz, with Q and gain both climbing. **If they do not, the model is wrong — do not enshrine bad numbers as the reference.** Fix the model, then regenerate.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build-sdl --target pedal-wah-response && ./build-sdl/pedal-wah-response`
Expected: exit code 0.

- [ ] **Step 5: Register and commit**

```cmake
add_executable(pedal-wah-response tests/wah_response.cpp)
target_link_libraries(pedal-wah-response PRIVATE ardor_wah)
add_test(NAME pedal-wah-response COMMAND pedal-wah-response)
set_tests_properties(pedal-wah-response PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
```

```bash
git add tests/wah_response.cpp tests/data/wah_reference_gcb95.json CMakeLists.txt
git commit -m "test(wah): pin the swept resonant response to a reference"
```

---

### Task 7: RuntimeChain integration

**Files:**
- Modify: `src/dsp/RuntimeChain.h`, `src/dsp/RuntimeChain.cpp`
- Modify: `tests/runtime_chain_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `ardor::WahProcessor`.
- Produces:
  - `void RuntimeChain::addWah(std::string id, WahProcessor processor);`
  - `bool RuntimeChain::setWahParameter(const std::string& id, const std::string& key, float value);`

- [ ] **Step 1: Write the failing test**

Append to `tests/runtime_chain_smoke.cpp`, following the file's existing style:

```cpp
  // Wah blocks join the chain, take live parameter changes, and are reachable
  // inside Dual Rig lanes like every other parameter-bearing block.
  {
    ardor::RuntimeChain chain;
    chain.prepareBlockSize(64);
    ardor::WahProcessor wah;
    std::string wahError;
    require(wah.configure({{"mode", "gcb95"}, {"position", 0.0f}, {"level", 0.0f}},
                          48000.0f, wahError), wahError);
    chain.addWah("wah-1", std::move(wah));

    require(chain.setWahParameter("wah-1", "position", 0.7f),
            "position should be live-settable on a chain wah block");
    require(!chain.setWahParameter("wah-2", "position", 0.7f),
            "an unknown block id should be rejected");
    require(!chain.setWahParameter("wah-1", "postion", 0.7f),
            "an unknown parameter key should be rejected");

    const auto processed = chain.process({0.5f, 0.5f});
    require(std::isfinite(processed.left) && std::isfinite(processed.right),
            "a chain containing a wah should produce finite output");

    require(chain.setBlockEnabled("wah-1", false), "the wah block should be bypassable");
    const auto bypassed = chain.process({0.5f, 0.5f});
    require(std::fabs(bypassed.left - 0.5f) < 1e-6f,
            "a bypassed wah should pass its input through untouched");
  }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-sdl --target pedal-runtime-chain-smoke`
Expected: FAIL to compile — `addWah` is not a member.

- [ ] **Step 3: Implement the chain changes**

In `RuntimeChain.h`: add `#include "wah/WahProcessor.h"`, and declare `addWah` and `setWahParameter` next to `addCompressor`/`setCompressorParameter`.

In `RuntimeChain.cpp`:
- Add `Wah` to `Block::Kind` (`src/dsp/RuntimeChain.cpp:50`).
- Add `std::unique_ptr<WahProcessor> wah;` to `Block`.
- `addWah` mirrors `addCompressor` (`src/dsp/RuntimeChain.cpp:209`).
- `setWahParameter` mirrors `setCompressorParameter` (`src/dsp/RuntimeChain.cpp:218`) **including the `block.dualRig->setWahParameter(id, key, value)` recursion** — omitting it makes wahs inside Dual Rig lanes silently uncontrollable.
- Add `case Block::Kind::Wah:` to the per-sample `process` switch (near line 331), the `processBlock` switch (near line 425), `reset`, `tailFrames`, and the `SignalStageKind` mapping (near line 535). Add a `SignalStageKind::Wah` enumerator in `src/dsp/SignalRouting.h`.
- `DualRigProcessor` needs the matching `setWahParameter` passthrough — check `src/dsp/DualRigProcessor.{h,cpp}` for how it forwards `setCompressorParameter` and mirror it.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build-sdl --target pedal-runtime-chain-smoke && ./build-sdl/pedal-runtime-chain-smoke`
Expected: exit code 0.

- [ ] **Step 5: Link ardor_wah into ardor_dsp**

```cmake
target_link_libraries(ardor_dsp PUBLIC ardor_daisyfx ardor_dynamics ardor_equalizer ardor_wah)
```

- [ ] **Step 6: Commit**

```bash
git add src/dsp/RuntimeChain.h src/dsp/RuntimeChain.cpp src/dsp/SignalRouting.h \
        src/dsp/DualRigProcessor.h src/dsp/DualRigProcessor.cpp \
        tests/runtime_chain_smoke.cpp CMakeLists.txt
git commit -m "feat(wah): host wah blocks in the runtime chain"
```

---

### Task 8: ChainPlan, PedalEngine, and EngineLoader

**Files:**
- Modify: `src/preset/ChainPlan.cpp`
- Modify: `src/dsp/PedalEngine.h`, `src/dsp/PedalEngine.cpp`
- Modify: `src/audio/EngineLoader.cpp`
- Modify: `tests/preset_smoke.cpp`

**Interfaces:**
- Consumes: `RuntimeChain::addWah`, `RuntimeChain::setWahParameter`.
- Produces:
  - `bool PedalEngine::addWah(std::string id, const nlohmann::json& params, float sampleRate, std::string& error);`
  - `bool PedalEngine::setWahParameter(const std::string& id, const std::string& key, float value);`

- [ ] **Step 1: Write the failing test**

Append to `tests/preset_smoke.cpp`:

```cpp
  // A wah block plans as Ready without needing an asset file, and an unknown
  // voicing plans as Unsupported rather than silently loading something else.
  {
    ardor::Preset preset;
    preset.name = "wah plan";
    preset.blocks.push_back({"wah-1", "wah", true, "",
                             {{"mode", "gcb95"}, {"position", 0.0f}, {"level", 0.0f}}, {}});
    const auto plan = ardor::buildChainPlan(preset, ".");
    require(plan.blocks.size() == 1, "the wah block should appear in the plan");
    require(plan.blocks[0].status == ardor::ChainBlockStatus::Ready,
            "a wah block needs no asset and should plan as Ready");
    require(plan.runnableBlockCount == 1, "the wah should count as runnable");

    ardor::Preset unknown;
    unknown.name = "bad voicing";
    unknown.blocks.push_back({"wah-1", "wah", true, "", {{"mode", "nope"}}, {}});
    const auto unknownPlan = ardor::buildChainPlan(unknown, ".");
    require(unknownPlan.blocks[0].status == ardor::ChainBlockStatus::Unsupported,
            "an unknown wah voicing should plan as Unsupported");
  }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-sdl --target pedal-preset-smoke && ./build-sdl/pedal-preset-smoke`
Expected: FAIL — the wah block plans as `Unsupported` because `ChainPlan` does not know the type.

- [ ] **Step 3: Implement**

In `src/preset/ChainPlan.cpp`, add near the other predicates (around line 38):

```cpp
bool isSupportedWahBlock(std::string_view type, const nlohmann::json& params)
{
  if (type != "wah") return false;
  const auto mode = params.value("mode", std::string{});
  return mode == "gcb95";
}
```

and a branch in the block loop (after the `"eq"` branch, around line 135):

```cpp
  } else if (block.type == "wah") {
    if (isSupportedWahBlock(block.type, blockPlan.params)) {
      blockPlan.status = ChainBlockStatus::Ready;
      ++runnableBlockCount;
    } else {
      blockPlan.status = ChainBlockStatus::Unsupported;
    }
```

In `PedalEngine`, add `addWah` and `setWahParameter` mirroring `addCompressor` (`src/dsp/PedalEngine.cpp:177`) and the compressor's parameter passthrough.

In `src/audio/EngineLoader.cpp`, add wah handling at **both** call sites — the `chain.addCompressor` site near line 303 and the `engine.addCompressor` site near line 592. Missing the second one means the wah works offline but not live.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build-sdl --target pedal-preset-smoke && ./build-sdl/pedal-preset-smoke`
Expected: exit code 0.

- [ ] **Step 5: Run the full suite**

Run: `ctest --test-dir build-sdl --output-on-failure`
Expected: all tests pass. This is the first task that touches shared plumbing, so a regression elsewhere is plausible.

- [ ] **Step 6: Commit**

```bash
git add src/preset/ChainPlan.cpp src/dsp/PedalEngine.h src/dsp/PedalEngine.cpp \
        src/audio/EngineLoader.cpp tests/preset_smoke.cpp
git commit -m "feat(wah): plan and load wah blocks into the engine"
```

---

### Task 9: Preset schema and expression control

**Files:**
- Modify: `src/preset/Preset.cpp`
- Modify: `apps/pedal-poc/main.cpp`
- Modify: `tests/preset_smoke.cpp`
- Modify: `docs/midi-expression-control.md`

**Interfaces:**
- Consumes: `PedalEngine::setWahParameter`.
- Produces: a `wah` branch in `applyPresetParameterValue` that returns `true` for `position`, `level`, making the existing expression path live.

- [ ] **Step 1: Write the failing test**

Append to `tests/preset_smoke.cpp`:

```cpp
  // A wah block plus an expression assignment targeting it round-trips.
  {
    ardor::Preset preset;
    preset.name = "wah preset";
    preset.blocks.push_back({"wah-1", "wah", true, "",
                             {{"mode", "gcb95"}, {"position", 0.0f}, {"level", 0.0f},
                              {"autoEngage", true}, {"autoEngageTimeoutMs", 2000},
                              {"autoEngageThreshold", 0.05f}}, {}});
    preset.expression = ardor::PresetExpression{"wah-1", "position", 0.0f, 1.0f, false};

    const auto json = ardor::toJson(preset);
    const auto restored = ardor::presetFromJson(json);
    require(restored.blocks.size() == 1 && restored.blocks[0].type == "wah",
            "the wah block should survive a JSON round trip");
    require(restored.blocks[0].params.value("mode", "") == "gcb95",
            "the voicing should survive a JSON round trip");
    require(restored.expression.has_value(), "the expression assignment should survive");
    require(restored.expression->blockId == "wah-1" && restored.expression->parameter == "position",
            "the expression assignment should still target the wah's position");

    // The existing assignment maths already gives range limiting and inversion.
    require(std::fabs(ardor::expressionValueAt(*restored.expression, 0.0f) - 0.0f) < 1e-6f,
            "heel should map to the assignment minimum");
    require(std::fabs(ardor::expressionValueAt(*restored.expression, 1.0f) - 1.0f) < 1e-6f,
            "toe should map to the assignment maximum");

    ardor::PresetExpression limited{"wah-1", "position", 0.2f, 0.8f, true};
    require(std::fabs(ardor::expressionValueAt(limited, 0.0f) - 0.8f) < 1e-6f,
            "an inverted assignment should map heel to the maximum");
  }

  // An expression assignment pointing at a block that does not exist is invalid.
  {
    ardor::Preset preset;
    preset.name = "dangling";
    preset.blocks.push_back({"wah-1", "wah", true, "", {{"mode", "gcb95"}}, {}});
    preset.expression = ardor::PresetExpression{"wah-9", "position", 0.0f, 1.0f, false};
    bool threw = false;
    try {
      (void)ardor::toJson(preset);
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    require(threw, "an expression assignment naming a missing block should be rejected");
  }
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-sdl --target pedal-preset-smoke && ./build-sdl/pedal-preset-smoke`
Expected: FAIL on the round-trip assertions if the params are not preserved.

Note: `validateExpression` in `src/preset/Preset.cpp:32` already handles the dangling-block case, so that half may pass immediately. That is fine — it documents behaviour the wah now depends on.

- [ ] **Step 3: Add the wah branch to applyPresetParameterValue**

In `apps/pedal-poc/main.cpp`, in `applyPresetParameterValue` (near line 1020, alongside the `cab` branch):

```cpp
  if (block->type == "wah") {
    if (parameter == "position" || parameter == "level") {
      return engine.setWahParameter(block->id, parameter, value);
    }
    return false;
  }
```

`position` returning `true` is what makes `applyExpressionPosition` (`apps/pedal-poc/main.cpp:604`) treat the wah as live-controllable and stop printing the "not live-controllable" warning.

- [ ] **Step 4: Verify the expression path end to end**

Run:

```bash
./build-sdl/pedal-poc --preset presets/bank-000/<a preset with a wah block>.json --offline ...
```

Follow the offline-render invocation documented in `README.md`. Confirm the render completes and the output is audibly filtered.

There is no automated test for the ADC-to-engine path — it needs Linux hardware. The `--expression-*` CLI flags exist for manual verification on the Pi; that happens in Task 13.

- [ ] **Step 5: Update the documentation**

In `docs/midi-expression-control.md`, the example at line 68 already uses `wah-1` / `position`. Add a note under "Expression assignment in a preset" stating that the `wah` block type now exists and that `position` is its expression target, with the JSON from the spec.

- [ ] **Step 6: Run the suite and commit**

Run: `ctest --test-dir build-sdl --output-on-failure`

```bash
git add src/preset/Preset.cpp apps/pedal-poc/main.cpp tests/preset_smoke.cpp \
        docs/midi-expression-control.md
git commit -m "feat(wah): sweep the wah from the expression pedal"
```

---

### Task 10: Auto-engage state machine

**Files:**
- Create: `src/control/WahAutoEngage.h`, `src/control/WahAutoEngage.cpp`
- Create: `tests/wah_auto_engage_smoke.cpp`
- Modify: `apps/pedal-poc/main.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: nothing from earlier tasks — this is pure control logic with no audio dependency.
- Produces:
  - `struct ardor::WahAutoEngageConfig { bool enabled = true; float threshold = 0.05f; int timeoutMs = 2000; };`
  - `enum class ardor::WahAutoEngageAction { None, Engage, Bypass };`
  - `class ardor::WahAutoEngage` with `explicit WahAutoEngage(WahAutoEngageConfig)`, `WahAutoEngageAction update(float position, std::int64_t nowMs)`, `void notifyManualToggle(bool enabled)`, `void reset(bool enabled)`.

- [ ] **Step 1: Write the failing test**

```cpp
// tests/wah_auto_engage_smoke.cpp
#include "control/WahAutoEngage.h"

#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

int main()
{
  const ardor::WahAutoEngageConfig config{true, 0.05f, 2000};

  // Rocking off the heel engages.
  {
    ardor::WahAutoEngage engage{config};
    engage.reset(false);
    require(engage.update(0.0f, 0) == ardor::WahAutoEngageAction::None,
            "sitting at heel should not engage");
    require(engage.update(0.2f, 100) == ardor::WahAutoEngageAction::Engage,
            "moving off heel should engage");
    require(engage.update(0.5f, 200) == ardor::WahAutoEngageAction::None,
            "staying engaged should not re-emit an action");
  }

  // Returning to heel bypasses only after the timeout elapses.
  {
    ardor::WahAutoEngage engage{config};
    engage.reset(false);
    require(engage.update(0.5f, 0) == ardor::WahAutoEngageAction::Engage, "engage first");
    require(engage.update(0.0f, 100) == ardor::WahAutoEngageAction::None,
            "returning to heel should not bypass immediately");
    require(engage.update(0.0f, 2099) == ardor::WahAutoEngageAction::None,
            "the timeout should not fire early");
    require(engage.update(0.0f, 2101) == ardor::WahAutoEngageAction::Bypass,
            "the timeout should fire once elapsed");
  }

  // Moving again during the timeout cancels it.
  {
    ardor::WahAutoEngage engage{config};
    engage.reset(false);
    engage.update(0.5f, 0);
    engage.update(0.0f, 100);
    require(engage.update(0.6f, 1000) == ardor::WahAutoEngageAction::None,
            "moving again while engaged should stay engaged");
    require(engage.update(0.0f, 1100) == ardor::WahAutoEngageAction::None, "back to heel");
    require(engage.update(0.0f, 2099) == ardor::WahAutoEngageAction::None,
            "the timeout should restart from the most recent return to heel");
    require(engage.update(0.0f, 3200) == ardor::WahAutoEngageAction::Bypass,
            "the restarted timeout should eventually fire");
  }

  // A manual toggle overrides automatic behaviour until the pedal next moves.
  // Without this the timeout would silently undo a deliberate switch-on.
  {
    ardor::WahAutoEngage engage{config};
    engage.reset(false);
    engage.notifyManualToggle(true);
    require(engage.update(0.0f, 0) == ardor::WahAutoEngageAction::None, "manual engage at heel holds");
    require(engage.update(0.0f, 10000) == ardor::WahAutoEngageAction::None,
            "the timeout must not undo a manual engage");
    require(engage.update(0.5f, 11000) == ardor::WahAutoEngageAction::None,
            "moving off heel while already engaged changes nothing");
    require(engage.update(0.0f, 11100) == ardor::WahAutoEngageAction::None, "back to heel");
    require(engage.update(0.0f, 13200) == ardor::WahAutoEngageAction::Bypass,
            "automatic behaviour resumes once the pedal has moved");
  }

  // A manual bypass while mid-sweep sticks until the pedal moves again.
  {
    ardor::WahAutoEngage engage{config};
    engage.reset(false);
    engage.update(0.5f, 0);
    engage.notifyManualToggle(false);
    require(engage.update(0.5f, 100) == ardor::WahAutoEngageAction::None,
            "a manual bypass should not immediately re-engage at the same position");
    require(engage.update(0.0f, 200) == ardor::WahAutoEngageAction::None, "return to heel");
    require(engage.update(0.7f, 300) == ardor::WahAutoEngageAction::Engage,
            "a fresh move off heel should re-engage");
  }

  // Disabled config never acts.
  {
    ardor::WahAutoEngage engage{{false, 0.05f, 2000}};
    engage.reset(false);
    require(engage.update(0.9f, 0) == ardor::WahAutoEngageAction::None,
            "auto-engage disabled means no automatic actions at all");
    require(engage.update(0.0f, 9000) == ardor::WahAutoEngageAction::None,
            "auto-engage disabled means no automatic bypass either");
  }

  // reset() restores preset state and clears any override.
  {
    ardor::WahAutoEngage engage{config};
    engage.reset(false);
    engage.notifyManualToggle(true);
    engage.reset(false);
    require(engage.update(0.3f, 0) == ardor::WahAutoEngageAction::Engage,
            "after reset the machine should behave as freshly activated");
  }
  return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-sdl --target pedal-wah-auto-engage-smoke`
Expected: FAIL — `control/WahAutoEngage.h` does not exist.

- [ ] **Step 3: Implement the state machine**

Internal state: `bool engaged_`, `bool overridden_`, `bool belowThreshold_`, `std::int64_t belowSinceMs_`. The override clears the first time `position` crosses above `threshold` **while `overridden_` is set**, then normal rules resume from the next update.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build-sdl --target pedal-wah-auto-engage-smoke && ./build-sdl/pedal-wah-auto-engage-smoke`
Expected: exit code 0.

- [ ] **Step 5: Wire it into the expression poll**

In `apps/pedal-poc/main.cpp`, inside the expression poll block (near line 1324, where `applyExpressionPosition` is called):

- Construct a `WahAutoEngage` per active preset, configured from the wah block's `autoEngage`, `autoEngageThreshold`, and `autoEngageTimeoutMs` params. If the preset has no wah block, skip entirely.
- After applying the position, call `update(position, nowMs)` and act on the result via the same `engine.setBlockEnabled(...)` path the footswitch uses.
- Call `reset(...)` on preset activation, alongside the existing MIDI toggle-binding reset.
- Call `notifyManualToggle(...)` wherever a footswitch or UI action toggles the wah block's enabled state.

- [ ] **Step 6: Register and commit**

```cmake
add_executable(pedal-wah-auto-engage-smoke tests/wah_auto_engage_smoke.cpp)
target_link_libraries(pedal-wah-auto-engage-smoke PRIVATE ardor_control)
add_test(NAME pedal-wah-auto-engage-smoke COMMAND pedal-wah-auto-engage-smoke)
```

Add `src/control/WahAutoEngage.cpp` to the `ardor_control` library (line 265).

```bash
git add src/control/WahAutoEngage.h src/control/WahAutoEngage.cpp \
        tests/wah_auto_engage_smoke.cpp apps/pedal-poc/main.cpp CMakeLists.txt
git commit -m "feat(wah): auto-engage the wah from pedal movement"
```

---

### Task 11: Device UI

**Files:**
- Modify: `src/ui/LvglChainLayout.cpp`
- Modify: `src/ui/UiModel.cpp`
- Modify: `src/ui/LvglUiParameterRenderer.cpp`
- Modify: `tests/ui_model_smoke.cpp`

**Interfaces:**
- Consumes: the `wah` block type and its params.
- Produces: no new public API.

- [ ] **Step 1: Write the failing test**

Append to `tests/ui_model_smoke.cpp`, following the file's existing style:

```cpp
  // The wah appears in the effect catalog and gets a display name.
  {
    require(ardor::blockDisplayName("wah", {{"mode", "gcb95"}}) == std::string("Wah"),
            "a wah block should be labelled Wah");
    bool found = false;
    for (const auto& asset : ardor::effectCatalog()) {
      if (asset.blockType == "wah" && asset.mode == "gcb95") found = true;
    }
    require(found, "the wah should be offered in the effect catalog");
  }
```

Check the actual function names in `src/ui/UiModel.h` before writing this — the names above follow the pattern at `src/ui/UiModel.cpp:187` and `:243`, but match them exactly to what the header declares.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build build-sdl --target pedal-ui-model-smoke && ./build-sdl/pedal-ui-model-smoke`
Expected: FAIL — the catalog has no wah entry.

- [ ] **Step 3: Implement**

- `src/ui/LvglChainLayout.cpp:51` — add `if (block.type == "wah") return "Wah";` alongside the `dynamics` and `eq` cases.
- `src/ui/UiModel.cpp:62`, `:114`, `:187`, `:243` — add the `wah` / `gcb95` cases to each, mirroring how `eq` / `parametric_eq_5` is handled at each site. All four sites matter; missing one leaves the block un-nameable or un-addable.
- `src/ui/LvglUiParameterRenderer.cpp` — render `position` as a read-only live meter (it is pedal-driven, so a draggable slider would fight the pedal), `level` as a normal dB slider, `autoEngage` as a toggle, and `autoEngageTimeoutMs` as a stepped choice (500, 1000, 2000, 4000 ms).

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build-sdl --target pedal-ui-model-smoke && ./build-sdl/pedal-ui-model-smoke`
Expected: exit code 0.

- [ ] **Step 5: Check it visually in the simulator**

Run: `./build-sdl/pedal-ui-sim`
Add a wah block, open its parameter drawer, confirm the controls render and the position meter is not draggable.

- [ ] **Step 6: Commit**

```bash
git add src/ui/LvglChainLayout.cpp src/ui/UiModel.cpp src/ui/LvglUiParameterRenderer.cpp \
        tests/ui_model_smoke.cpp
git commit -m "feat(wah): surface the wah block in the device UI"
```

---

### Task 12: Manager catalog

**Files:**
- Modify: `services/managerd/internal/presets/presets.go`
- Modify: `services/managerd/internal/presets/presets_test.go`
- Modify: `tests/manager_effect_catalog_smoke.cpp`

**Interfaces:**
- Consumes: the `wah` block type.
- Produces: no new API.

- [ ] **Step 1: Write the failing Go test**

Append to `services/managerd/internal/presets/presets_test.go`, following the file's existing table-test style, a case asserting that a preset containing a `wah` block with `mode: "gcb95"` validates, and that one with an unknown mode is rejected.

- [ ] **Step 2: Run it to verify it fails**

Run: `cd services/managerd && go test ./internal/presets/...`
Expected: FAIL — the wah type is unknown.

- [ ] **Step 3: Implement**

Add the `wah` block type and its `gcb95` mode to the catalog and validation in `presets.go`, mirroring how `dynamics` / `compressor` is handled.

- [ ] **Step 4: Run both test suites to verify they pass**

Run: `cd services/managerd && go test ./internal/presets/...`
Run: `cmake --build build-sdl --target pedal-manager-effect-catalog-smoke && ./build-sdl/pedal-manager-effect-catalog-smoke`
Expected: both pass. The C++ catalog smoke test exists to keep the two catalogs in step — if it fails, the Go and C++ catalogs have diverged.

- [ ] **Step 5: Commit**

```bash
git add services/managerd/internal/presets/presets.go \
        services/managerd/internal/presets/presets_test.go \
        tests/manager_effect_catalog_smoke.cpp
git commit -m "feat(wah): add the wah block to the manager catalog"
```

---

### Task 13: Hardware and authenticity gates

Neither gate is automatable. Both must pass before this is called done.

**Files:**
- Modify: `docs/superpowers/specs/2026-08-06-wah-effect-design.md`
- Modify: `README.md`
- Modify: `CHANGELOG.md`

**Interfaces:** none.

- [ ] **Step 1: Deploy to the Pi and confirm the expression pedal sweeps it**

Build the AArch64 image per `BUILD.md`, deploy, load a preset with a wah block and an expression assignment targeting `wah-1` / `position`, and rock the pedal. Confirm the sweep is smooth with no zipper noise or clicks.

- [ ] **Step 2: Measure CPU headroom**

Run `audio-probe-pi` with a NAM + wah preset active at block size 64. Record whole-core utilization and headroom.

**Gate:** headroom must remain positive with margin comparable to the pre-wah baseline. If it does not, report the numbers and stop — the fallback is a smaller table grid, which means regenerating in Task 3 and re-running Tasks 4 through 6.

- [ ] **Step 3: Confirm auto-engage feels right**

Rock the pedal from heel and confirm it engages; park at heel and confirm it bypasses after the timeout; manually switch it on at heel and confirm the timeout does not undo it.

The 2000 ms default and 0.05 threshold are estimates, not measurements. If they feel wrong in use, change the defaults in the spec, `ChainPlan`, and the manager catalog together.

- [ ] **Step 4: Validate the netlist against published response curves**

Compare the measured peak frequency, gain, and Q from `tests/data/wah_reference_gcb95.json` against published GCB-95 measured response curves.

**This is the only check that proves the model sounds like a Cry Baby rather than merely being self-consistent.** A fully green test suite says nothing about it.

If no curve source is available, say so explicitly in the spec and fall back on a listening comparison against a known recording — and record that a listening test is what was done, rather than implying a measurement.

- [ ] **Step 5: Record the results**

Update the spec's "Hardware gate" section with the measured numbers and the netlist validation outcome. Add the wah to `README.md`'s effect list and to `CHANGELOG.md`.

- [ ] **Step 6: Commit**

```bash
git add docs/superpowers/specs/2026-08-06-wah-effect-design.md README.md CHANGELOG.md
git commit -m "docs(wah): record hardware measurements and netlist validation"
```

---

## Self-Review

**Spec coverage:** every spec section maps to a task — circuit and solver (2–4), table generation (3), pot movement and oversampling (5), scope limits (1, single voicing enforced by `isSupportedWahBlock`), chain wiring (7–8), preset schema (9), expression (9), auto-engage (10), UI (11–12), testing (1–6, 10), hardware gate (0, 13). The build order in the spec is followed, with the Pi bench pulled forward to Task 0 as agreed.

**Placeholder scan:** one intentional placeholder remains — the schematic source citation in Task 1 Step 4, which Step 6 requires filling in before commit. It is a verification step, not an unspecified requirement.

**Type consistency:** `WahDkMatrices` field names (`a`–`k`) are consistent across Tasks 2–4. `setParameterTarget(key, value)` matches `CompressorProcessor`'s signature throughout. `WahAutoEngageAction` enumerators (`None`, `Engage`, `Bypass`) are used consistently in Task 10. `wahSpectralRadius` is declared in the Task 2 header and used in the Task 2 test.

**Known soft spot:** Task 2's implementation is described structurally rather than given as complete code — the MNA stamping is netlist-specific and depends on values verified in Task 1 Step 6. It is the one task where the implementer needs real circuit-simulation knowledge; the tests pin the required behaviour tightly enough to make it verifiable.
