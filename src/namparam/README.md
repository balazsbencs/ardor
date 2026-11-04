ConcatWaveNet and ConcatLSTM runtime support adapted from
https://github.com/phillipmself/NeuralAmpModelerCoreParametric
commit fddcbc91e74ba79066a43c3b73cc690073529232 (MIT; LICENSE).

Changes: upstream core includes, explicit parser invocation instead of static
registration, weight-count validation before WaveNet construction, and reuse of
the prepared conditioning ramp during same-rate resets. Ardor keeps its pinned
stock NAM runtime.

HyperWaveNet support adapts `hypernet.{h,cpp}` and
`wavenet/hyperwavenet.{h,cpp}` from the same pinned fork. It validates the full
base weight count before construction, bounds hypernetwork dimensions, and uses
explicit parser dispatch. Weight generation and installation reuse prepared
buffers and run once at the next audio block after a changed parameter vector.
Controls are not smoothed: the upstream measured smoothing experiment introduced
block-rate artifacts and repeated expensive weight generation. Knob changes may
still step audibly. Packed/slimmable inner networks and nested condition DSPs
are rejected. Existing direct concat/FiLM container families remain unchanged.

On ARM64, exact 16-channel A2 HyperWaveNets automatically use
`wavenet/mutable_a2.cpp`. The four-frame NEON kernel keeps the full network and
its floating-point weights. A compact ring mirrors only three frames, and
validated delta ranges determine whether dilated convolution kernels need
reloading after an edit. Other topologies and production hosts retain the generic
runtime; a scalar implementation of the specialized layout is exercised in tests.
No extra worker thread or buffering latency is introduced. Both generic and
specialized paths retain histories on control changes and clear them on reset.
The synthetic test changes convolution weights too, guarding the conditional
reload optimization. The original Marshall HD export is tested externally
against the generic runtime, including varied block sizes and live controls.

Concat models accept 1–24 named controls, including one-hot switches. The
companion trainer's A2 Standard and Nano topology exports retain the same
ConcatWaveNet 1.0.0 contract and use this generic concat runtime. The regression
fixture compares exported Python inference with Ardor at two buffer sizes.

## Gene Ko FiLM adapter

`film_wavenet.cpp` contains the parametric A2 fast kernels adapted from
[mrgeneko/NeuralAmpModelerCore](https://github.com/mrgeneko/NeuralAmpModelerCore)
commit `419bb5712e651fb7fb548f8aa54431207727aa59`, specifically the parametric
portion of `NAM/wavenet/a2_fast.cpp`. Its separate MIT notice is
`LICENSE-Gene-Ko` (Steven Atkinson and Gene Ko). Stock A2 kernels remain in the
pinned upstream core. `parametric_container.cpp` is Ardor's shared container
adapter and factory for FiLM and concat models.

The adapter uses Ardor's existing `IParametricControl` interface rather than
adding knob methods to the pinned core. Hosts serialize control and audio calls.
Defaults come from the model, restored controls settle before prewarm, smoothing
uses actual block length, and processing rejects oversized blocks rather than
allocating. Schema 1 / skip-head / FiLM A2 widths 3–9 are accepted; newer schemas,
LoRA, partial FiLM layers and discrete FiLM parameters are rejected. Every weight
read is bounds-checked, trailing weights and nonfinite weights are rejected.

Containers require identical controls and prepare every tier before playback.
Only the active tier is prewarmed on load. `SetSlimmableSize` has the same
non-realtime contract as stock NAM; preset changes are prepared off the audio
thread. Golden samples from the independent upstream generic Eigen runtime
verify both bundled Clone Overdrive tiers.

ConcatWaveNet containers use version 1.0.0 and identical controls/input calibration
across direct children. Independently trained Standard and Nano exports keep
their own loudness normalization. Size changes reset and prewarm the selected
tier off the audio thread; presets retain the shared control vector. Actual
two-tier exports from the companion trainer are checked against Python audio,
including selecting both tiers after load. Nested and mixed-family containers
are rejected.
