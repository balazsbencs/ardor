# Wah Effect — Design

Date: 2026-08-06
Status: Approved, ready for implementation planning

## Goal

Add a wah block driven by the expression pedal, so the pedal sweeps the wah the
way a rocker-treadle wah does. The block models the Dunlop GCB-95 Cry Baby
circuit rather than approximating it with a swept bandpass.

Ardor already has an *auto*-wah: `FilterMode`
(`src/daisyfx/hosted/modes/filter_mode.cpp`) sweeps a state-variable filter from
an LFO or an envelope follower, and its Tone control has a "Wah(BP)" position.
That block stays as it is. This design adds a separate, pedal-driven effect; the
two do not share code.

## Decisions

Four decisions were settled during brainstorming. Each names the rejected
alternatives so a later reader can see what the choice cost.

1. **A first-class `wah` block type**, alongside `dynamics` and `eq` — not a new
   mode inside the daisyfx `mod` block, and not an extension of `FilterMode`.
   Mod modes are locked to a fixed seven-slot parameter vocabulary
   (Speed/Depth/Mix/Tone/P1/P2/Level), so `position` would have had to squat on
   `Speed` in the UI and in preset JSON permanently.
2. **Full circuit simulation**, not a filter approximation.
3. **DK-method with precomputed nonlinear tables**, not a live Newton-Raphson
   solve. Live iteration has a signal-dependent worst case, which is unsafe in a
   callback that already runs NAM.
4. **Bypass plus optional auto-engage**, not bypass alone and not auto-engage
   alone.

## Constraints

From the existing platform:

- Raspberry Pi 4B, AArch64. 48 kHz, preferred block size 64 (fallback 128).
- Audio callback runs at `SCHED_FIFO/70` pinned to CPU 2, already hosting NAM.
- Round-trip latency goal under 10 ms.
- `audio-probe-pi` reports whole-core utilization and headroom.

The binding constraint is that per-sample cost must be **bounded and
signal-independent**. That constraint is what selects the table-based solver.

## DSP core

### Circuit

Dunlop GCB-95 Cry Baby, per the [ElectroSmash circuit
analysis](https://electrosmash.mas-effects.com/crybaby-gcb-95.html) (retrieved
2026-08-06):

- Q0: MPSA13 Darlington emitter-follower input buffer.
- Q1: MPSA18 common-emitter gain stage driving the tank.
- Q2: MPSA18 output stage; the 100 kΩ audio-taper pot (VR1) sits in the
  feedback path from here.
- L1 500 mH plus C1/C2 (0.01 µF each) forming the resonant tank.

**Correction against the original design.** This spec first assumed two
transistors. The circuit has three. Modelling all three nonlinearly would make
the solved system 3-D and the runtime table 4-D — roughly 800 MB at the shipped
grid, which is not viable.

**Q0 is therefore modelled as linear.** A Darlington emitter follower running at
near-unity gain, whose job is impedance isolation, contributes essentially
nothing to the wah voicing — that comes from the Q1 feedback stage. This is the
same tradeoff already made in omitting the base-collector junctions, and it
keeps the 2-D nonlinear system and 3-D table that the hardware probe validated.

The pot wiper sets how much of Q2's output is fed back into the tank, and that
feedback is what sweeps the resonant peak. It is also why Q and peak gain rise
together toward the toe — the behaviour a fixed-Q bandpass cannot reproduce and
the reason this design models the circuit at all.

### Solver

DK-method (discretized nodal state space):

- Roughly 7 reactive states (6 capacitors plus the inductor), trapezoidal
  discretization.
- 2 nonlinear ports: the base-emitter junctions of **Q1 and Q2**, Ebers-Moll,
  forward-active. Q0 is linear (see above). The base-collector junctions are
  omitted deliberately — that is what keeps the nonlinear system 2-D instead of
  4-D, and it holds everywhere these stages actually operate.
- Per sample the system is `v = p + K·i(v)`, where the 2-D vector `p` comes
  from the current state and input, and the coupling matrix `K` depends on pot
  position.

### Removing the iteration

`i(v)` is solved offline by Newton-Raphson over a 3-D grid — `p₁ × p₂ ×
potPosition`, approximately 128 × 128 × 33 — and interpolated trilinearly at
runtime.

- Cost per sample is fixed, with no branch on signal level. Safe under
  `SCHED_FIFO/70`.
- Table size is roughly 4.3 MB.
- Grid locality is good in practice because the signal moves smoothly through
  adjacent cells.

### Pot movement

The DK matrices themselves also depend on pot position. They are precomputed at
the same 33 positions and interpolated per control block. They are **not**
re-derived live: matrix inversion inside the callback is precisely the unbounded
cost this design exists to avoid.

Position is smoothed with a one-pole filter, time constant about 15 ms. This
serves two purposes: it de-zippers the 8 ms expression poll cadence (with its
0.002 deadband), and it imitates the mechanical inertia of a real treadle.

### Oversampling

4×, built from two stages of the existing
`src/daisyfx/hosted/dsp/halfband_resampler.h`, so the BJT nonlinearities do not
alias. The processor reports its added latency the way
`DaisyFxProcessor::latencyFrames()` does.

### Table generation

An offline host tool, `apps/wah-table-gen`, runs the Newton solve and the matrix
precompute and emits the data file.

**The generated file is checked in**, and CI verifies that regenerating it is
byte-identical. Running a host tool during the Buildroot cross-compile would
complicate that build for no benefit.

### Files

| Path | Responsibility |
| --- | --- |
| `src/wah/WahNetlist.{h,cpp}` | Component values per voicing |
| `src/wah/WahCircuit.{h,cpp}` | Netlist-agnostic DK core |
| `src/wah/WahProcessor.{h,cpp}` | Block-facing: oversampling, smoothing, level, reset |
| `apps/wah-table-gen/` | Offline table and matrix generation |

### Scope limits

- **One voicing in v1: GCB-95.** The DK derivation is driven entirely by the
  netlist, so a Vox V847 or a bass wah is just another component set plus
  another table — but each voicing costs several more MB and another generation
  and validation pass. Build the pipeline so voicings are data, then ship one.
- **Input-impedance interaction is unrecoverable.** Part of a real wah's tone
  comes from loading the guitar pickup directly. Ardor's input is already
  post-ADC, so no amount of fidelity inside the block recovers it. Recorded here
  so it is not later filed as a modelling defect.

## Integration

### Chain wiring

Mirrors the existing compressor path:

- `src/preset/ChainPlan.cpp` — `"wah"` joins the supported block types, status
  `Ready`, no asset required.
- `src/dsp/RuntimeChain.{h,cpp}` — `Block::Kind::Wah`, `addWah()`,
  `setWahParameter()` including the `block.dualRig->setWahParameter(...)` lane
  recursion that the compressor already performs, plus the `process`,
  `processBlock`, `reset`, `tailFrames`, and `SignalStageKind` cases.
- `src/dsp/PedalEngine.h` — a `setWahParameter()` passthrough beside
  `setCompressorParameter`.
- `apps/pedal-poc/main.cpp` — a `wah` case in `applyPresetParameterValue`, with
  `position` returning `true` so the expression path treats it as
  live-controllable.

### Preset schema

Additive. No version bump; existing presets stay valid.

```json
{
  "id": "wah-1",
  "type": "wah",
  "enabled": true,
  "params": {
    "mode": "gcb95",
    "position": 0.0,
    "level": 0.0,
    "autoEngage": true,
    "autoEngageTimeoutMs": 2000,
    "autoEngageThreshold": 0.05
  }
}
```

Voicing is carried as `params.mode`, following `dynamics` (`"compressor"`,
`"noise_gate"`) and `eq` (`"parametric_eq_5"`). This makes future voicings
catalog entries at `src/ui/UiModel.cpp:243` rather than special cases.

`level` is in dB and trims the output. The modelled circuit has substantial gain
around the resonant peak, so this exists to manage headroom into whatever
follows.

### Expression control

**No new code.** The existing per-preset assignment already covers it:

```json
{
  "expression": {
    "blockId": "wah-1",
    "parameter": "position",
    "minimum": 0.0,
    "maximum": 1.0,
    "inverted": false
  }
}
```

`minimum`/`maximum` give sweep-range limiting and `inverted` handles a
reversed-polarity pedal, both for free. The hypothetical `wah-1` / `position`
example already written at `docs/midi-expression-control.md:68` becomes real.

The control path is `apps/pedal-poc/main.cpp:1312`: an 8 ms poll, through
`ExpressionFilter` (smoothing 0.25, deadband 0.002), into
`applyExpressionPosition`.

### Auto-engage

A control-plane state machine, `src/control/WahAutoEngage.{h,cpp}`, fed
`(position, timestamp)` and driving the existing `setBlockEnabled`. It contains
no audio code, so it is testable without an engine, and bypass state stays
visible in the UI like any other block.

Rules:

1. Bypassed, and position rises above `autoEngageThreshold` → engage.
2. Position falls back below the threshold and stays there for
   `autoEngageTimeoutMs` → bypass.
3. A manual footswitch or UI toggle sets an override that suppresses automatic
   behaviour **until the pedal next moves above the threshold**. Without this
   rule, manually switching the wah on while parked at the heel would be
   silently switched back off by rule 2.
4. Preset activation resets both the enabled state and the override.

The hardware has no toe switch — the expression jack is a pot-only ADC input —
so this stands in for a real wah's under-toe click. It is opt-out via
`autoEngage: false`.

### UI

- `src/ui/LvglChainLayout.cpp:51` — block label.
- `src/ui/UiModel.cpp:243` — catalog entry as `("wah", "gcb95")`.
- Parameter drawer: live position readout, level, auto-engage toggle and
  timeout.
- `services/managerd/internal/presets/presets.go` and the managerd web UI.

## Testing

Follows the existing `tests/*_smoke.cpp` and ctest harness. Written test-first.

| Test | What it proves |
| --- | --- |
| `tests/wah_circuit_smoke.cpp` | No NaN or denormal over long sweeps; clean reset; bounded output at every pot position; **continuity across table grid cells**, the failure mode specific to this solver |
| `tests/wah_response.cpp` | Acceptance test, modelled on `daisy_fx_response.cpp`: peak frequency, peak gain, and Q at N fixed positions. Peak moves monotonically from roughly 400 Hz to 2 kHz; Q *and* gain rise toward the toe |
| `tests/wah_automation.cpp` | Sweeping position at the real 8 ms cadence produces no zipper discontinuities; no allocation after prepare |
| `tests/wah_auto_engage_smoke.cpp` | State machine only: threshold, timeout, override latch, activation reset |
| `tests/preset_smoke.cpp` (extend) | Round-trip serialization and validation of the new block |
| `tests/dsp_bench.cpp` (extend) | Per-sample cost |

### What the response test does not prove

`wah_response.cpp` compares against a reference generated from the same netlist,
checked in as `tests/data/wah_reference_gcb95.json`. It therefore validates the
discretization and table pipeline — not whether the netlist itself is right.

Netlist correctness requires a **manual, one-time check against published GCB-95
measured response curves**, plus listening. This is an explicit step in the
implementation plan. A green suite must not be read as confirming the model
sounds like a Cry Baby.

### Hardware gate

The 4.3 MB table was identified as the main CPU risk, on the theory that cache
behaviour on a Pi 4 sharing CPU 2 with NAM could not be predicted from a
desktop bench.

**Measured 2026-08-06 on the target** (Pi 4B Rev 1.5, governor `performance` at
1.5 GHz, 52 °C, `chrt -f 70`, via `tests/wah_table_probe.cpp`):

| Grid | Table size | Core fraction |
| --- | --- | --- |
| 128 × 128 × 33 (planned) | 4.3 MB | 6.68% |
| 64 × 64 × 17 | 557 KB | 6.43% |
| 32 × 32 × 17 | 139 KB | 6.40% |

Repeat runs at the planned grid: 6.77%, 6.67%, 6.69%.

**The cache concern was unfounded.** A 31× larger table costs 0.3 percentage
points, so the access pattern stays in cache as hoped and the arithmetic
dominates. The full-resolution grid is kept.

Note that the deployed unit runs `--block-size 512`, not the block size 64 that
`PRODUCT.md` lists as preferred — larger blocks give more headroom, so this
measurement is if anything conservative.

The final check remains: `audio-probe-pi` headroom with a real NAM + wah preset
active, once the block exists.

## Build order

1. Offline table tool.
2. Circuit core plus its tests.
3. Processor (oversampling, smoothing, level).
4. Chain wiring: ChainPlan, RuntimeChain, PedalEngine.
5. Preset schema.
6. Expression path — already works; verify only.
7. Auto-engage.
8. UI and managerd catalog.
9. Manual netlist validation against published curves.
10. Pi bench with `audio-probe-pi`.
