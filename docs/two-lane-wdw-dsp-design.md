# Two-lane wet/dry/wet DSP design

Status: DSP design proposal after the Pi feasibility probe.  This document
does not define the editor UI and does not authorize production-route
integration by itself.

## 1. Decision

The first production topology should be a fixed, pair-aware two-lane program:

```text
                         +--> dry lane: pre/distortion -> NAM -> cab -> align --+
mono input -> copy/sanitize                                                       +--> stereo mix -> output
                         +--> wet lane: NAM -> cab -> stereo delay -> reverb --+
```

The dry lane is a complete amp path and is normally centre-panned.  The wet
lane is a complete amp/cab path followed by stereo time-based effects.  There
is no raw-input path in the mix.  A lane that is disabled contributes silence;
an unavailable processed generation contributes either the last complete pair
or silence according to the bounded underflow policy below.

This is deliberately a constrained product topology, not a general-purpose
DAG scheduler.  It covers the intended wet/dry/wet rig, preserves stereo time
effects, and gives the scheduler a fixed two-worker admission problem.

## 2. Why this is a new boundary

The existing experimental `FlexibleRoutingGraph` is a useful generic probe,
but it is not the production boundary for this topology:

* `ParallelLaneExecutor` exposes mono lane outputs.
* `ParallelLaneMixer` mixes those mono outputs before any post stage.
* `FlexibleRoutingProgram::processPostJoin()` downmixes a stereo result before
  invoking a post-join `RuntimeChain`.
* A shared post cabinet therefore loses lane stereo/pan semantics and also
  makes the dry path pass through the same cabinet.

The new boundary should be a dedicated `WdwRoutingProgram` backed by a
`WdwPairExecutor`.  The generic graph can remain for experiments and for a
future generalization after the pair contract has proved stable.

## 3. Immutable control-thread plan

The control thread builds an immutable activation plan.  No structural change
is made to an active plan from the audio callback.

Conceptual data model:

```cpp
struct WdwLanePlan {
  std::string id;
  std::unique_ptr<RuntimeChain> chain;
  WdwLaneKind kind;                 // Dry or Wet
  int workerCpu;
  WdwMix mix;                       // gain, pan/width, enabled
  std::size_t intrinsicLatency;
  std::size_t alignmentDelay;
};

struct WdwRoutingPlan {
  double sampleRate;
  std::size_t blockSize;
  WdwLanePlan dry;
  WdwLanePlan wet;
  WdwPipelineOptions pipeline;
  WdwUnderflowPolicy underflow;
};
```

`RuntimeChain` remains the sequential node container inside each lane.  The
first admitted chain shapes are:

* Dry: zero or more mono pre/drive stages, one NAM, one mono cab.
* Wet: one NAM, one mono cab, then zero or more stereo time stages.

The chain builder may support additional serial NAM blocks inside a lane, but
the admission check must charge every block.  The Pi probe measured one NAM
per lane plus two cabinets, Rat, delay, and reverb; a chain with more NAMs is a
new cost case, not an automatic extension of that result.

Each lane gets its own convolution state.  The immutable IR/model asset may be
deduplicated later, but an `IrConvolver`'s history, overlap, and partition
state cannot be shared between lanes.

The planner rejects:

* a missing lane, missing NAM/cab, invalid block size, or unsupported effect
  ordering;
* a worker CPU equal to the audio CPU or to the other lane worker;
* worker setup failure when realtime scheduling/affinity is required;
* a lane whose measured or declared cost exceeds its quantum budget;
* an arbitrary stereo stage before the wet lane's cab unless its semantics are
  explicitly supported;
* a structural fallback to callback execution after a worker admission fail.

## 4. Pair executor

`WdwPairExecutor` owns one bounded submission ring and two worker
threads.  It is not two independent `ParallelStereoStageExecutor` instances.
Independent executors can publish adjacent generations at different callback
boundaries; the feasibility harness observed this as 19 mismatches in 20,500
calls during the long exact run.  A shared pair ring makes the publication
unit explicit.

### Pair slot

Every slot is preallocated for the fixed quantum:

```text
PairSlot {
  generation
  input[frames]                 // one mono copy, read-only to workers
  dry.left[frames], dry.right[frames]
  wet.left[frames], wet.right[frames]
  completionMask                // dry bit | wet bit
}
```

Use a fixed, preallocated ring (three slots are the current Pi default; two
were also measured).  The exact count remains a configuration constant; it
must never grow from the audio callback.  Increase it only after measuring the
additional memory and latency headroom on the target hardware; it is not a
substitute for admission of a slow lane.

### Submission and completion protocol

1. The callback sanitizes/copies the input into a free slot, clears its
   completion mask, assigns the next generation, and publishes the generation
   with release semantics.
2. It posts one bounded job to each worker.  Each worker reads the same input
   block and writes only its own output arrays and completion bit.
3. A worker publishes its completion bit with release semantics after the
   complete `RuntimeChain::processBlock()` call.
4. The callback scans the bounded live window for the newest slot whose mask is
   `dry|wet`.  An acquire operation establishes visibility of both outputs.
5. The callback copies/mixes that one pair, then releases the slot for reuse.

The callback never waits for a worker, and workers never write into a slot that
the callback has not acknowledged.  The worker callbacks contain no dynamic
allocation, locks, filesystem work, or control-message processing.

`WdwRoutingProgram` is the production-shaped owner around this executor.  It
owns the two `RuntimeChain` instances, turns the dry chain's stereo result into
one centred/pannable contribution, preserves the wet chain's stereo result,
applies the declared fixed-path alignment delays, and performs the final mix.
Its mix targets are atomic and block-smoothed; changing a mix, pan, width, or
lane-enable target does not rebuild the worker topology.

### Output policy

* Before the first complete pair, emit zeroes.  This is intentional; the raw
  input must not leak into a WDW rig during worker warmup.
* If no newer complete pair is available, hold the last complete pair for one
  block and increment a pair-underflow counter.
* If the pair is still unavailable after the bounded hold, emit zero (the
  prototype uses this deterministic hard transition; production may add a
  preallocated short safety ramp).  Do not repeat stale audio indefinitely and
  do not bypass to the raw input.
* A pair is always committed atomically.  The mixer never combines a dry
  generation with a different wet generation.

The exact hold/ramp constants are product policy, but they must be fixed in the
plan and covered by tests.  They are not allowed to be silently changed by a
worker failure.

### Reset and teardown

`reset()` and `clear()` are control-thread operations:

1. stop new submissions;
2. mark the workers stopping and drain/join them;
3. reset both `RuntimeChain` instances and the alignment delay lines;
4. clear pair generations and zero all publication buffers;
5. start workers again only after setup and affinity succeed.

The program owns the pair executor after its lane contexts, so worker callbacks
are stopped before their `RuntimeChain` targets are destroyed.  A block-size or
sample-rate change requires a new prepared plan.

## 5. Stereo mix contract

The wet lane must stay stereo from the first stereo-capable node through the
output.  The dry lane's mono signal is converted to a stereo contribution only
at the final mixer.

For an equal-power dry pan and a wet-width control:

```text
dryL, dryR = equalPowerPan(dryMono, dryGain, dryPan)
wetL, wetR = stereoWidth(wetL, wetR, wetWidth) * wetGain
outL = dryL + wetL
outR = dryR + wetR
```

The initial product preset is `dryPan = 0` and full wet stereo width.  The
mixing gains and width are atomic control targets smoothed at block/sample
rate; changing them never rebuilds the graph.  Lane enable/disable is also a
control target and contributes zero when disabled.

The dry and wet signals must be clipped/observed at their own chain boundaries
and again at the final output boundary, using the existing diagnostics
machinery.  A non-finite lane block is zeroed and counted; it is never replaced
with the raw input.

## 6. Latency and alignment

There are three different latency classes and they must not be conflated:

1. **Pipeline latency:** the pair executor normally adds one or more complete
   audio quanta while a worker finishes.
2. **Intrinsic node latency:** Rat/other oversampled processors and the hosted
   reverb adapter report fixed algorithmic latency.  The current known values
   include Rat's 26 frames and the non-native hosted reverb's 31 frames.
3. **Path/semantic latency:** a wet effect can contain an immediate dry part
   and a delayed wet part.  A single node's reported algorithmic latency is not
   necessarily the first-arrival latency of the complete mixed signal.

The planner should expose a `RuntimeChain::latencyFrames()` metadata path for
known serial nodes, then verify the complete lane with an offline impulse during
plan preparation.  The calibration finds the first stable arrival of each
lane's output and records the difference.  A preallocated fixed delay line is
inserted on the earlier lane before the final mix.

The reported program latency is:

```text
pairPipelineBlocks * blockSize
  + max(aligned dry/wet first-arrival latency)
```

The dry path must be aligned to the wet lane's direct amp component, not blindly
to the reverb tail.  If the wet lane is configured as an effect-only return,
the planner uses that explicit mode and aligns to the effect return instead.
The chosen mode is part of the preset contract and must be visible to offline
rendering/tests.

Changing a delay time or reverb decay does not change fixed alignment.  Changing
topology or the effect's declared latency requires a rebuilt plan.

## 7. CPU and admission policy

The measured exact topology used:

* audio callback: CPU2 in the production-placement check;
* dry worker: CPU0;
* wet worker: CPU3;
* worker priority: `SCHED_FIFO` 69;
* audio priority: `SCHED_FIFO` 60;
* pipeline: three-slot preferred configuration at 48 kHz (two-slot comparison
  also measured);

The Pi results were:

```text
quantum     callback mean     callback deadline misses     dry worker mean/max
32          8.2 us            0/1000                      566/711 us
64          10.5 us           0/1000                      979/1381 us
128         15.3 us           0/1000                      1762/2266 us
```

The direct-thread reference missed every deadline in the exact Rat-enabled
matrix.  Therefore the production path must never wait for both lanes in the
callback and must never silently fall back to a serial two-lane evaluation.

Admission rules for the first implementation:

* require two distinct pinned workers and a distinct pinned audio CPU;
* reserve one worker per complete lane, not one worker per node;
* reject a plan if the worker setup cannot acquire its requested FIFO policy
  and affinity;
* use 128 frames as the first Rat-enabled default on this Pi; 64 frames is
  opt-in and must surface its bounded underflow counter;
* prefer three pair-ring slots on this Pi after the calibrated stress run;
  two slots remain a bounded low-memory option;
* keep 32-frame operation opt-in until a longer worst-case run shows adequate
  worker headroom;
* collect worker p99/max timing, pair age, submission misses, and non-finite
  counts in telemetry;
* do not admit more serial NAM/cab work merely because the callback itself is
  cheap—the worker completion budget is the limiting resource.

The first admission test should require zero submission misses and zero
non-finite blocks over at least one million paced blocks on the target Pi.  A
small startup underflow is expected; recurring underflow or a growing pair age
is a failed admission.

## 8. Program lifecycle

The initial integration should retain the repository's explicit stopped-audio
activation contract:

1. control thread parses/validates the lane plan;
2. it loads NAMs and IRs, constructs chains, prepares every block size, warms
   all model/convolver/effect state, calibrates alignment, and configures the
   pair workers;
3. it verifies worker CPUs, latency, memory bounds, and admission telemetry;
4. audio is stopped;
5. the prepared program is installed with
   `PedalEngine::installPreparedWdwRouting()`;
6. audio is restarted only after all workers report ready.

Structural edits (adding/removing/reordering NAM, cab, or time stages) rebuild a
new plan.  Continuous parameters use the existing atomic target/smoothing
paths.  A later seamless swap can use an epoch/RCU handoff, but it is not a
requirement for the first implementation.

If activation fails, keep the previously active program and report the exact
reason.  Do not install a partially prepared graph and do not fall back to the
raw input.

## 9. Control-thread construction seam

The first production-shaped construction API is now `buildWdwRoutingProgram()`
in `src/audio/WdwRoutingBuilder.{h,cpp}`.  It intentionally accepts two
already-built `ChainPlan` objects rather than changing the JSON/preset schema.
That keeps DSP admission testable while the editor and storage format are still
being designed.

The builder enforces the fixed product contract before starting any worker:

* each lane has exactly one NAM and one cabinet, with NAM before cabinet;
* dry-lane blocks are NAM/cab, dynamics, EQ, distortion, or wah;
* wet-lane blocks are NAM/cab, modulation, delay, reverb, IR reverb, or stereo
  widening; time/stereo stages must follow the cabinet;
* nested split blocks, missing/unsupported blocks, duplicate IDs, and unknown
  lane effects are rejected;
* pipelined admission requires distinct worker CPUs and rejects a worker that
  collides with the audio CPU when affinity is enabled.

Preparation loads both chains, applies disabled-block state, probes each chain
with a control-thread impulse, and places the measured first-arrival frames in
the final mixer.  A probe that produces no finite signal fails preparation; it
does not invent a latency or silently enable a raw path.  The builder returns a
report containing both measured arrivals and the resulting fixed program
latency.

`applyWdwRouting()` mirrors `applyChainPlan()`: it prepares a temporary
`PedalEngine`, installs the complete WDW owner, copies the shared global gain
and limiter settings, and publishes it with `replacePreparedProgram()`.  The
`prepareAndActivateWdwDraft()` activation helper then uses the existing
stopped-audio/backend-rejection contract.  A failed model/IR load, calibration,
worker setup, or backend replacement leaves the currently audible engine and
selection unchanged.  No UI or persistent preset format depends on this seam
yet.

## 10. Verification plan

### Unit and deterministic tests

* Pair generation: dry and wet outputs are never mixed from different
  generations.
* Warmup: first output is zero, not input.
* Underflow: one-block hold and bounded transition-to-zero behave
  deterministically.
* Ring ownership: no worker writes a slot before callback acknowledgement.
* Reset: all model, delay, reverb, alignment, and generation state returns to
  the same output sequence.
* Pan/width: dry pan affects only the final dry contribution; wet stereo is not
  downmixed before delay/reverb.
* Latency: calibrated dry/wet impulse arrivals differ by at most one sample.
* Structural rejection: invalid ordering, duplicate IDs, worker collision, and
  budget overflow fail before activation.
* Program boundary: dry mono conversion, wet stereo preservation, declared
  latency alignment, mix smoothing, reset, and no-raw-bypass startup behavior.

### Offline audio comparison

Render the new pair program and a serial reference using identical model/IR/
effect state.  After compensating for the declared pipeline and alignment
latency, compare each lane and the final stereo mix.  Include impulse, stepped
input, sustained tone, silence, and rapid parameter movement.

### Target hardware

Repeat the existing probe with actual cab assets in addition to the dense
synthetic 8192-frame proxy:

* 32/64/128 frames, Rat on/off;
* all available NAM model pairs, including the highest-cost pair;
* 20,000-block and one-million-block paced runs;
* audio CPU2 with workers CPU0/CPU3;
* service telemetry before, during, and after each run;
* stop/start and reset/reload stress while confirming no use-after-free or
  block-size mismatch.

The current Pi image contains NAM models but no WAV/IR/AIFF/FLAC cabinet
assets under `/opt/ardor-pedal`.  For the final hardware gate a deterministic
48 kHz, 8192-frame dense cabinet-shaped proxy was deployed and loaded through
the production `readMonoWav()`/`prepareMonoIr()` path; its provenance is
recorded in `benchmark-results/representative-cab-ir-48k.txt`.  It is a
representative stress fixture, not a licensed microphone capture.  A captured
cabinet asset still needs its own admission run before shipping it as a preset.

The original independent-worker probe is summarized in
`benchmark-results/wdw-feasibility-pi4-20260908-summary.txt`.  The follow-up
pair-aware run is summarized in
`benchmark-results/wdw-pair-feasibility-pi4-20260908-summary.txt` with its
machine-readable result in the adjacent CSV.  The latest program-boundary
run (the feasibility harness now drives `WdwRoutingProgram`, not only the
lower-level executor) is in
`benchmark-results/wdw-program-feasibility-pi4-20260908-128-20k.csv`: at
128 frames it produced 20,495 matched pairs, five bounded one-block holds,
zero submission misses, zero callback deadline misses, and zero non-finite
blocks.  Host validation currently passes the full 47-test suite, including
the pair and program smoke tests; target-hardware admission at the one-million
block scale, latency calibration, and production lifecycle tests are still
required before live integration.  A current-source rerun of that exact case
produced 20,499 matched pairs with only the expected startup underflow; its
CSV is `benchmark-results/wdw-program-feasibility-pi4-20260908-128-20k-current.csv`.
The harness now performs a control-thread impulse probe before activation; the
same run with measured alignment reported dry latency 0 frames and wet latency
31 frames, with 20,497 matched pairs, three bounded underflows, and zero
submission/deadline/non-finite faults.  See
`benchmark-results/wdw-program-feasibility-pi4-20260908-128-20k-calibrated.csv`.
With three pair-ring slots, the same calibrated run had only the expected
startup underflow and zero later pair age; see
`benchmark-results/wdw-program-feasibility-pi4-20260908-128-20k-calibrated-slots3.csv`.
A longer 60,000-block paced run at the same 128-frame placement produced
60,199 matched pairs out of 60,200 calls, one startup underflow, zero
submission/deadline/non-finite faults, and zero post-startup pair age; see
`benchmark-results/wdw-program-feasibility-pi4-20260908-128-60k-calibrated-slots3.csv`.

### Final hardware admission gate

The final calibrated 128-frame run used three pair-ring slots, the Rat-enabled
dry lane, two NAM evaluations, two cabinet convolvers, stereo delay/reverb on
the wet lane, and the representative WAV fixture. With 200 warmup blocks and
1,000,000 paced timed blocks it produced 1,000,196 matched pairs out of
1,000,200 calls, four bounded underflows (one initial silence), one maximum
pair-age block, zero submission misses, zero callback deadline misses, and zero
non-finite or lane-fault blocks. Measured latency was dry 0 frames and wet 31
frames. Callback mean/p99/p999/max were 18.938/26.426/45.481/114.296 us;
worker maxima were 2,354.722 us dry and 2,050.777 us wet against a 2,666.667
us quantum. The exact row is in
`benchmark-results/wdw-feasibility-pi4-20260908-real-ir-1m.csv`.

The five NAM models present on the image were screened as all 25 dry/wet pairs
at 128 frames with the same fixture, 100 warmup blocks and 1,000 paced timed
blocks per pair. Every pair reported workers on CPUs 0/3, one startup
underflow, zero submission/deadline/non-finite/fault blocks, and wet latency of
31 frames. The worst observed worker maxima were 1,788.156 us dry and
1,466.686 us wet. Results are in
`benchmark-results/wdw-model-pair-screen-pi4-20260909.csv`.

After both tests the `S99ardor-pedal` supervisor was started again. Its live
telemetry returned to `over=0`, `gaps=0`, `worker_over=0`, `nonfinite=0`, and
`block_mismatch=0` with a 1.33 ms callback budget.

### Pair-aware prototype result

The pair executor and harness now use one shared generation ring.  On the Pi,
the exact Rat + two-NAM + two-cab + stereo-delay/reverb topology at 64 frames
produced 5,199 matched pairs out of 5,200 calls in the initial 5,000-block
run, with one expected startup underflow, zero submission misses, zero
callback deadline misses, and no non-finite blocks.  A longer 20,000-block
 64-frame run remained deadline-safe and submission-safe but recorded 14
  bounded one-block underflows; the lower-level pair executor at 128 frames
  ran 20,000 blocks with only its one startup underflow.  The program-boundary
  run above adds the final mixer and declared alignment and recorded five
  bounded holds under the same paced workload.  Unlike the independent-worker
run, a partially completed generation cannot be mixed with another
generation: the only startup output is silence, and every non-startup
publication is a complete pair.  The host pair smoke also passes under
ThreadSanitizer.

## 11. Implementation order

1. Add latency metadata/calibration and the pair-aware executor as DSP-only
   classes with unit tests. **Pair executor and deterministic tests are now
   present.**
2. Build `WdwRoutingProgram` around two prepared `RuntimeChain` instances and
   the final stereo mixer; keep it behind a test-only or disabled feature gate.
   **The DSP-only program boundary and smoke test are now present.**
3. Extend the feasibility harness to use the real pair executor and perform
   serial-reference/latency/audio comparisons. **The Pi harness now uses the
   `WdwRoutingProgram` boundary, performs impulse-based first-arrival
   calibration, and the program smoke compares direct/pipelined generations
   with an independent serial reference.**
4. Integrate the prepared program into the stopped-audio `PedalEngine` lifecycle
   and target-hardware telemetry. **The explicit WDW install/clear boundary and
   block/scalar contract smoke coverage are now present; preset construction and
   the control-thread builder/activation seam are now present; persistent preset
   construction and live admission policy remain.**
5. Only after these DSP contracts and tests are accepted, design the editor
   layout and preset schema for the two-lane topology. **Static device and
   manager layout mockups are now recorded in `mockups/lvgl-redesign/wdw.html`;
   production schema and interaction wiring remain deliberately out of scope.**

Out of scope for this first implementation are arbitrary graphs/cycles, more
than two independently scheduled lanes, live worker migration, and a general
stereo DAG editor.
