# Audio Engine Remediation and Refactor Plan

## Purpose

This plan addresses the audio-engine, effects, sound-quality, and realtime-safety issues identified in the Ardor Raspberry Pi audio project.

The first architectural priority is to make the complete live signal path block-oriented. Until that is complete, Raspberry Pi performance measurements and effect-quality work are misleading because the composed engine does not use the optimized NAM or convolution paths.

## Implementation Status (2026-07-14)

The completed code-side remediation batch is as follows:

- The live engine runs NAM and cabinet processing on preallocated block buffers; the fixed-quantum adapter keeps variable device callbacks outside the DSP path.
- Preset preparation is transactional, rejects duplicate mono stages and invalid mono/stereo orderings, and rejects unsupported 48 kHz configurations before mutating the active engine.
- IR import rejects stereo, empty, and non-finite files. Capped IRs receive a 5 ms tail fade instead of an abrupt truncation. Offline renders retain cabinet tails by default and expose `--tail-seconds` and `--no-tail`.
- Input, output, master, cabinet parameters, bypass, and program exchanges are smoothed. Bypass continues processing effect state and crossfades to dry; overload bypass is latched until explicit recovery or a preset change.
- The compressor has one stereo-linked detector/gain envelope. Daisy hosting is fixed to 48 kHz, validates finite parameters, uses its intended 48-sample control interval, and uses unity as the default modulation level.
- The audio device stays open during engine replacement. Because the present Daisy vendor source owns some delay/reverb buffers globally, slot switching temporarily fades to a muted engine before constructing the next preset; this prevents one preset's initialization from corrupting another's state.

The remaining acceptance gates and later quality upgrades are hardware measurements plus a look-ahead limiter: Pi callback/thermal profiling, real ALSA xrun counts, codec gain calibration, noise-floor checks, and limiter listening validation. They must be run on the target Pi/Codec Zero hardware before a production release.

## Target Architecture

```text
miniaudio callback
    |
    v
Fixed-quantum adapter (64 frames)
    |
    v
PedalEngine::processQuantum()
    |-- smoothed input gain
    |-- RuntimeChain::processBlock()
    |     |-- NAM block inference
    |     |-- partitioned cabinet convolution
    |     |-- Daisy effects
    |     `-- compressor
    |-- smoothed output/master gain
    |-- peak limiter and final emergency clamp
    `-- meters
    |
    v
Output FIFO -> miniaudio
```

The audio callback must contain no allocation, destruction, file I/O, model loading, large resets, mutex acquisition, or JSON access.

## Phase 0: Add Regression Tests Before Refactoring

Add tests that fail on the current implementation so the refactor cannot silently preserve or reintroduce the existing problems.

### 0.1 Block-path contract

Add debug counters to `NamProcessor` and `IrConvolver`, or inject test processors, and verify that processing a 64-frame engine quantum:

- calls each block API exactly once;
- never calls `processSample()`; and
- produces the same output as a trusted offline reference.

### 0.2 CLI ordering

Render an impulse through the legacy `--model --ir` path and compare it with a preset containing `NAM -> cab`. The outputs must match.

### 0.3 Full-chain performance gate

Add a full-chain benchmark using:

- 48 kHz sample rate;
- 64-frame processing quantum;
- a representative full-tier NAM;
- an 8192-sample cabinet IR;
- one modulation, delay, and reverb block; and
- the compressor.

Report average, p99, and maximum processing time. Keep this benchmark out of CTest pass/fail initially, but record desktop and Pi baselines.

### 0.4 Effect lifecycle

Verify over multiple blocks that:

- Formant continues moving;
- Envelope Filter responds to an envelope change;
- Rotary reaches its requested speed over time; and
- reverb modulation changes its delay positions.

### 0.5 Instance isolation

Construct two Digital Delays using the same mode, feed only the first, and assert that the second remains silent.

### 0.6 Transition behavior

Check for bounded sample-to-sample discontinuity during:

- input, output, and master gain changes;
- bypass transitions;
- overload bypass; and
- preset switching.

### 0.7 Stereo policy

Put a stereo effect before a cabinet and test the selected policy explicitly: reject the chain, preserve stereo, or intentionally downmix. Silent right-channel loss must never be accepted.

## Phase 1: Introduce a Fixed-Quantum Block Engine

### 1.1 Define the processing contract

Use one engine processing quantum, initially 64 frames at 48 kHz.

```cpp
enum class ChannelLayout {
  Mono,
  Stereo
};

struct ConstAudioSpan {
  const float* left;
  const float* right;
  std::size_t frames;
  ChannelLayout layout;
};

struct AudioSpan {
  float* left;
  float* right;
  std::size_t frames;
  ChannelLayout layout;
};
```

`RuntimeChain` should expose a block-only production interface:

```cpp
class RuntimeChain {
public:
  bool prepare(double sampleRate, std::size_t quantum, std::string& error);
  void processBlock(ConstAudioSpan input, AudioSpan output) noexcept;

  // Only call while the engine is muted or stopped.
  void resetNonRealtime();
};
```

Every processor receives a contiguous block. Remove `RuntimeChain::process(StereoSample)` from the production path.

### 1.2 Use preallocated ping-pong buffers

`PedalEngine` or `RuntimeChain` should own two reusable buffers:

```cpp
struct BlockBuffer {
  std::vector<float> left;
  std::vector<float> right;
  ChannelLayout layout = ChannelLayout::Mono;
};
```

Allocate them during `prepare()`, never during processing. Each chain block reads one buffer and writes the other, then swaps them. This makes serial ordering explicit and avoids hidden in-place assumptions.

### 1.3 Keep variable device callback sizes outside DSP

Miniaudio's callback frame count is not guaranteed to equal the requested period. Retain a quantum adapter that:

- collects capture samples until exactly 64 are available;
- calls `processQuantum()` with exactly 64 frames;
- queues exactly 64 output frames; and
- handles callbacks smaller or larger than 64 without reconfiguring DSP.

Record both the requested and actual device period sizes.

### 1.4 Make channel conversion explicit

Recommended v1 policy:

- guitar input begins mono;
- NAM is mono;
- cabinet convolution is mono;
- stereo begins after the cab when modulation, delay, or reverb creates width; and
- a NAM or mono cab placed after a stereo-producing block is rejected during chain validation.

This is preferable to silently discarding the right channel. If arbitrary reordering is a product requirement, add explicit stereo cabinet processing later.

## Phase 2: Refactor NAM Processing

### 2.1 Separate preparation from processing

Model loading must happen entirely on the control thread:

```cpp
class NamProcessor {
public:
  bool prepare(
      const std::filesystem::path& model,
      double sampleRate,
      std::size_t quantum,
      std::string& error);

  void processBlock(
      std::span<const float> input,
      std::span<float> output) noexcept;

  void resetNonRealtime();
};
```

`prepare()` should:

- parse and construct the model;
- verify mono input/output compatibility;
- verify the expected sample rate;
- allocate input/output arrays for exactly one processing quantum;
- run `ResetAndPrewarm()`;
- disable prewarming on later resets;
- calculate loudness normalization;
- read and retain NAM input/output-level metadata; and
- validate that every derived gain is finite.

### 2.2 Perform one inference call per quantum

For each 64-frame block:

1. Copy or generate mono input into the preallocated NAM input buffer.
2. Call `model_->process(..., 64)` once.
3. Apply loudness normalization in a vector loop.
4. Validate output in debug builds.
5. In release builds, replace non-finite samples with zero and increment a fault counter.

Remove the per-sample production method or mark it test-only.

### 2.3 Preserve block-size correctness

If a NAM model requires a smaller maximum block:

- load it with the engine quantum when possible;
- otherwise divide the quantum into fixed sub-blocks using block calls; and
- never fall back to individual one-sample calls.

### 2.4 Handle reset safely

Do not invoke NAM reset from a normal audio callback. For bypass, leave model state untouched because it is no longer being processed.

When re-enabling:

1. Fade output to silence.
2. Reset the model on the control thread while processing is suspended, or exchange it for an already-reset prepared engine.
3. Fade back in.

If later testing proves a particular NAM reset is strictly bounded and allocation-free, an audio-thread reset can be considered, but it should not be the default contract.

### 2.5 Implement model input calibration

Store a device calibration value representing the ADC full-scale level in dBu for the selected hardware gain.

When NAM metadata supplies `input_level_dbu`, calculate an optional automatic correction:

```text
calibration gain dB =
    hardware ADC full-scale dBu
    - NAM capture input-level dBu
```

Expose three separate concepts:

- hardware/model calibration;
- user input trim; and
- preset drive control, if desired.

Add a calibration procedure using a known 1 kHz sine and document the Codec Zero PGA setting. Never silently guess the Codec Zero's analog full-scale level.

## Phase 3: Refactor Cabinet Convolution

### 3.1 Make partitioned convolution the only live IR path

Keep the current uniform partitioned design initially:

- partition size: 64 samples;
- FFT size: 128 samples;
- IR divided into 64-sample partitions;
- precomputed FFT for every IR partition;
- ring of prior input spectra; and
- overlap-add output.

Use this interface:

```cpp
class PartitionedConvolver {
public:
  bool prepare(
      std::span<const float> impulse,
      std::size_t quantum,
      std::string& error);

  void processBlock(
      std::span<const float> input,
      std::span<float> output) noexcept;

  void resetNonRealtime();
  std::size_t tailFrames() const noexcept;
};
```

Delete or quarantine the naive sample processor so it cannot accidentally return to the live engine.

### 3.2 Guarantee no allocation during convolution

Replace potentially ambiguous vector assignments such as:

```cpp
inputPartitions_[writeIndex_] = scratch_;
```

with explicit copies into pre-sized storage.

Preallocate:

- FFT scratch;
- frequency-domain accumulator;
- IR partitions;
- input-history partitions;
- overlap buffer;
- bit-reversal table; and
- twiddle table.

### 3.3 Align cabinet dry/wet processing

For cabinet mix:

1. Preserve the current dry mono block.
2. Produce the convolved wet block.
3. Apply cabinet level.
4. Mix corresponding samples from the same block.

The backend's one-quantum buffering applies equally to both paths, so dry and wet remain time-aligned.

Avoid equal-power mixing here unless listening tests demonstrate a benefit. Dry and convolved signals are correlated, so linear mixing is generally more predictable.

### 3.4 Define IR import behavior

On IR load:

- reject empty files;
- require 48 kHz initially;
- explicitly accept mono IRs;
- reject stereo IRs with a clear error or provide a deliberate channel-selection/downmix option;
- check every sample for finiteness;
- measure peak and DC offset;
- warn about unusually hot or quiet IRs; and
- do not normalize automatically by default, because IR gain can be intentional.

If an IR is capped:

- apply a short 5-10 ms fade to the truncated tail;
- report the original and retained lengths; and
- store the effective length in telemetry.

### 3.5 Optional stereo cabinet extension

If arbitrary effect order remains supported, build a stereo convolver that shares IR frequency partitions but owns separate channel histories:

```text
Shared:
  H[partition][bin]

Per channel:
  X history ring
  accumulator
  overlap
  write index
```

This avoids duplicating IR spectra while allowing independent left/right convolution. CPU cost is still roughly doubled, so Pi benchmarking is required.

The preferred guitar-pedal configuration remains mono NAM/cab followed by stereo effects.

## Phase 4: Repair Daisy Effect Hosting

### 4.1 Lock the engine to 48 kHz immediately

Until the hosted library is runtime-rate-aware:

- reject any engine rate other than 48 kHz when a Daisy block exists;
- preferably reject non-48 kHz globally for the Pi product; and
- remove misleading arbitrary-rate support from CLI help.

A later refactor can replace hard-coded sample-rate constants with a runtime DSP context.

### 4.2 Add an internal 48-frame control quantum

The hosted effects were designed around 48-frame preparation intervals. The Ardor adapter should maintain a persistent counter:

```cpp
if (samplesUntilPrepare == 0) {
  mode->Prepare(params);
  samplesUntilPrepare = 48;
}

process one or more samples;
samplesUntilPrepare -= processed;
```

This works even when Ardor's external quantum is 64. Preparation occurs at the intended 1 kHz control rate instead of only once or incorrectly every 64 samples.

Longer term, replace `BLOCK_SIZE` with the actual number of samples advanced.

### 4.3 Make effect memory instance-owned

Refactor hosted modes so delay and reverb storage is supplied during initialization:

```cpp
class DigitalDelay {
public:
  struct Memory {
    std::span<float> left;
    std::span<float> right;
  };

  void Init(Memory memory, float sampleRate);
};
```

`DaisyFxProcessor::Impl` allocates this memory during configuration. Each block and each prepared engine then owns independent state.

Until this is complete:

- reject duplicate instances of any mode using global storage;
- stop audio before constructing a replacement engine containing the same mode; and
- do not claim gapless preset switching.

### 4.4 Fix parameter contracts

- Change modulation `level` default to normalized `0.5` if `0..2` remains the physical range.
- Prefer a user-facing dB range such as `-60..+6 dB`, with 0 dB as the default.
- Add a unity-gain test for every default effect.
- Verify every normalized default maps to the upstream physical default.
- Validate mode-specific parameters and reject non-finite values.

## Phase 5: Make Preset Loading Transactional

Build a complete prepared engine without touching the live engine:

```cpp
struct PreparedEngine {
  PedalEngine engine;
  PresetDiagnostics diagnostics;
};
```

The loader should:

1. Parse and validate the preset.
2. Resolve assets.
3. Validate channel-layout transitions.
4. Reject or explicitly bypass unsupported blocks.
5. Load and prewarm NAM.
6. Prepare IR partitions.
7. Allocate all effect memory.
8. Render several silent warm-up blocks.
9. Confirm finite output.
10. Return a complete prepared object.

Only a fully prepared engine may be submitted to the audio runtime.

Additional rules:

- Invalid blocks should be bypassed individually if that remains the product contract.
- Duplicate NAM/cab blocks must be rejected explicitly or fully supported.
- Never silently ignore blocks.
- Migrate checked-in presets to include valid effect modes.
- Do not mutate the current engine before validation succeeds.

## Phase 6: Safe Realtime Engine Exchange

Use a block-boundary command mechanism, preferably an SPSC queue.

Recommended Pi-safe transition:

1. The control thread prepares the replacement engine.
2. The audio thread ramps the old output to silence over 5-10 ms.
3. At a quantum boundary, it exchanges the current engine pointer.
4. The audio thread ramps the new output up over 5-10 ms.
5. Old engine ownership returns to the control thread for destruction.

Never destroy an engine on the audio thread.

Running both engines during a crossfade provides better continuity but can almost double CPU. Begin with fade-out/swap/fade-in. Add spillover or dual-engine crossfade only after Pi headroom is measured.

## Phase 7: Smooth All Realtime Controls

Implement parameter smoothers whose targets are atomic but whose current values belong exclusively to the audio thread.

Suggested smoothing times:

- master volume: 5-10 ms;
- input/output gain: 10-20 ms;
- cabinet level/mix: 10-20 ms;
- bypass: 5-10 ms equal-power crossfade; and
- preset exchange: 5-10 ms fade around the swap.

Do not reset effects merely because bypass changed. A normal user bypass can either:

- continue processing tails while muted, if CPU permits;
- freeze processing while preserving state; or
- reset only through a deliberate non-realtime operation.

Document the selected behavior.

## Phase 8: Fix Overload Handling

Make overload bypass genuinely latched:

```text
normal
  `-- repeated overload -> fade to dry -> latched bypass

latched bypass
  `-- user action, preset change, or explicit recovery -> reset and re-enable
```

Do not automatically retry every three seconds.

Separate telemetry into:

- `overBudgetCallbacks`: Ardor DSP time exceeded the callback budget;
- `callbackGapCount`: callback arrival gap exceeded tolerance;
- `deviceXruns`: only when the backend can obtain real ALSA xrun data;
- `nonFiniteFaults`;
- `limiterEvents`;
- `inputPeak`;
- `preNAMPeak`;
- `postNAMPeak`; and
- `outputPeak`.

Rename the current `xrunCount()` if it remains only an execution-time counter.

## Phase 9: Improve Final Output Protection

Retain a final hard clamp only as an emergency last line of defense.

Before it, add a real stereo-linked peak limiter:

- 0.5-1 ms lookahead if latency permits;
- fast attack;
- 50-150 ms release;
- stereo-linked gain;
- a ceiling around -1 dBFS; and
- gain-reduction telemetry.

At 48 kHz, 32 samples add 0.67 ms and 48 samples add 1 ms. Include this in the total latency budget.

True-peak oversampling can come later. First ensure that normal presets rarely touch the limiter.

## Phase 10: Correct the Compressor

Recommended detector design:

1. Apply compressor input gain.
2. Apply sidechain HPF independently to left and right.
3. Link channels using `max(abs(L), abs(R))`, or linked RMS.
4. Derive instantaneous target gain from threshold, knee, and ratio.
5. Smooth target gain once using attack/release.
6. Apply the same gain to both channels.
7. Apply makeup gain.
8. Mix dry and wet.

This prevents stereo-image movement and removes double attack/release smoothing.

For parallel compression, linear dry/wet mixing is defensible because the paths are highly correlated. Decide whether the product contract is linear or equal-power, document it, and test unity behavior at 0%, 50%, and 100%.

## Phase 11: Preserve Offline Tails

Add:

```text
--tail-seconds N
--no-tail
```

Provide automatic defaults:

- cabinet: IR length minus one sample;
- delay: calculate decay to approximately -80 dB, with a maximum duration cap;
- reverb: use configured decay, capped at approximately 10 seconds; and
- NAM, compressor, and most modulation: normally no extra tail unless measured otherwise.

After input ends, process zero blocks until:

- the requested tail duration is reached; or
- output remains below an energy threshold for a defined hold time.

Offline rendering must use the same block engine as realtime processing.

## Phase 12: Hardware Gain Staging and Validation

1. Measure Codec Zero input full-scale level at the selected AUX/PGA settings.
2. Decide whether the fixed +6 dB PGA is appropriate.
3. Add input peak/RMS meters before and after software input gain.
4. Add post-NAM and final-output meters.
5. Validate several NAM models with different `input_level_dbu` metadata.
6. Test passive and active guitars through the intended input buffer.
7. Check noise floor, DC offset, hum, and display-related interference.
8. Test headphone and line-level outputs separately.

## Final Acceptance Gates

Before calling the audio engine production-ready:

- the full chain remains below 65% average callback budget on a Pi 4;
- p99 processing time remains below 80% of the callback budget;
- no over-budget callback occurs during a 10-minute thermal soak;
- no real ALSA xruns occur;
- no non-finite samples occur;
- no default preset drives the limiter during normal playing;
- preset switches have no click and only the documented short fade;
- two identical effect modes remain isolated;
- stereo survives every permitted chain order;
- the legacy CLI and equivalent preset render identically;
- offline renders preserve effect tails;
- measured round-trip latency remains under 10 ms; and
- closed-enclosure thermal testing shows no throttling.

## Recommended Implementation Order

1. Add regression and performance tests.
2. Build the fixed-quantum block engine.
3. Move NAM inference to the block path.
4. Move cabinet convolution to the partitioned block path.
5. Repair Daisy preparation timing and effect memory ownership.
6. Make preset preparation transactional.
7. Add safe block-boundary engine exchange.
8. Smooth controls, bypass, and preset transitions.
9. Correct overload handling and telemetry.
10. Improve limiter, compressor, and offline-tail behavior.
11. Calibrate hardware gain staging.
12. Run full Pi latency, thermal, sound-quality, and xrun validation.

This ordering restores the Raspberry Pi performance foundation before investing in sound-quality polish and transition behavior.
