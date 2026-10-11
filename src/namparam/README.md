ConcatWaveNet and ConcatLSTM runtime support adapted from
https://github.com/phillipmself/NeuralAmpModelerCoreParametric
commit fddcbc91e74ba79066a43c3b73cc690073529232 (MIT; LICENSE).

Changes: upstream core includes, explicit parser invocation instead of static
registration, weight-count validation before WaveNet construction, and reuse of
the prepared conditioning ramp during same-rate resets. Ardor keeps its pinned
stock NAM runtime. HyperWaveNet is not
supported: its weight updates need a separate realtime-safe integration.

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
