#include "vibe_mode.h"
#include "../dsp/fast_math.h"

using namespace pedal::mod_fx;

namespace pedal {

static constexpr float TWO_PI = 6.28318530717958647692f;

// LFO phase offsets per stage (radians): models the angular position of each
// LDR around the lamp in the original Univox circuit. Stages sweep in a
// rolling cascade — notches chase each other through the spectrum.
static constexpr float kStagePhase[4]  = {0.0f, 0.5236f, 1.2217f, 1.9199f}; // 0°, 30°, 70°, 110°

// Allpass coefficient at the dark-lamp extreme (high LDR resistance = low notch).
// Staggered to produce four distinct notch regions across the audio band.
static constexpr float kStageCenter[4] = {-0.88f, -0.72f, -0.55f, -0.35f};

// Maximum coefficient swing from center at full depth (bright lamp = high freq).
// Higher stages sweep a wider range, matching the original's non-uniform LDR spacing.
static constexpr float kStageSweep[4]  = {0.10f, 0.20f, 0.30f, 0.42f};

// AM optical coupler sits ~90° ahead of stage 0 in the original circuit.
static constexpr float kAmPhaseOffset = 1.5707963f; // π/2

void VibeMode::Init() {
    Reset();
}

void VibeMode::Reset() {
    lfo_.Init(1.0f, LfoWave::Sine);
    lfo_.SetJitter(0.15f);
    for (auto& s : stages_) s.Reset();
    dc_.Init();
    tone_.Init(); // initialises to flat (knob = 0.5)
    feedback_ = 0.0f;
}

void VibeMode::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.speed);
    tone_.SetKnob(params.tone); // only recomputes coefficients when tone changes
}

StereoFrame VibeMode::Process(StereoFrame input, const ParamSet& params) {
    // Capture phase before advancing so per-stage offsets are anchored to this sample.
    const float base_phase = lfo_.GetPhase();
    lfo_.Process(); // advance only; value discarded (computed per-stage below)

    // Regen feedback mixed into input.
    const float regen = params.p1 * 0.7f;
    float x = input.mono() + feedback_ * regen;

    // Mild pre-saturation: germanium transistor 3rd-harmonic coloring (~4%).
    // Clamp before applying: x - 0.04x³ is non-monotonic above |x|=2.89, which
    // can be reached through the regen feedback path.
    if (x >  2.87f) x =  2.87f;
    if (x < -2.87f) x = -2.87f;
    x = x - 0.04f * x * x * x;

    // Per-stage allpass sweep with independent LDR phase offsets.
    for (int i = 0; i < kStages; ++i) {
        float ph = base_phase + kStagePhase[i];
        if (ph >= TWO_PI) ph -= TWO_PI;

        // Unipolar lamp brightness [0..1].
        const float lamp = 0.5f + 0.5f * fast_sin(ph);

        // Smoothstep LDR response: approximates power-law photoresistor curve.
        // Creates asymmetric attack/decay — notches fall quickly as lamp brightens,
        // return slowly as it dims.
        const float ldr = lamp * lamp * (3.0f - 2.0f * lamp);

        // Map ldr [0..1] → coefficient [center - sweep, center + sweep].
        float c = kStageCenter[i] + params.depth * kStageSweep[i] * (ldr * 2.0f - 1.0f);
        if (c > -0.01f) c = -0.01f;
        if (c < -0.99f) c = -0.99f;
        stages_[i].SetCoeff(c);
        x = stages_[i].Process(x);
    }

    // AM throb: volume dips slightly ahead of the sweep peak, matching the
    // original's separate optical coupler position at ~90° from stage 0.
    float am_ph = base_phase + kAmPhaseOffset;
    if (am_ph >= TWO_PI) am_ph -= TWO_PI;
    const float am_lamp = 0.5f + 0.5f * fast_sin(am_ph);
    float am_gain = 1.0f - params.depth * 0.12f * am_lamp;
    if (am_gain < 0.1f) am_gain = 0.1f;
    x *= am_gain;

    // Transistor preamp coloring via tone knob.
    x = tone_.Process(x);

    x = dc_.Process(x);
    feedback_ = x;
    return {x, x};
}

} // namespace pedal
