#include "vibe_mode.h"
#include "../dsp/fast_math.h"
#include "../dsp/freq_table.h"

using namespace pedal::mod_fx;

namespace pedal {

static constexpr float TWO_PI = 6.28318530717958647692f;

// LFO phase offsets per stage (radians): models the angular position of each
// LDR around the lamp in the original Univox circuit. Stages sweep in a
// rolling cascade — notches chase each other through the spectrum.
static constexpr float kStagePhase[4]  = {0.0f, 0.5236f, 1.2217f, 1.9199f}; // 0°, 30°, 70°, 110°

// Per-stage sweep, expressed in log frequency rather than in allpass
// coefficient. The original coefficient-domain constants gave a badly uneven
// sweep, because coefficient is a compressed function of frequency: the notch
// raced through the top of its range and crawled through the bottom.
// Centre frequency of each LDR stage at mid lamp brightness (Hz).
static constexpr float kStageCentreHz[4] = {260.0f, 620.0f, 1500.0f, 3600.0f};

// Sweep width per stage in octaves at full depth. Higher stages sweep wider,
// matching the original's non-uniform LDR spacing.
static constexpr float kStageOctaves[4] = {1.2f, 1.6f, 2.0f, 2.6f};

// AM optical coupler sits ~90° ahead of stage 0 in the original circuit.
static constexpr float kAmPhaseOffset = 1.5707963f; // π/2

void VibeMode::Init() {
    Reset();
}

void VibeMode::Reset() {
    // Cache the normalised log-frequency position of each stage centre once.
    for (int i = 0; i < kStages; ++i) {
        stage_centre_[i] = freq_table::position_for_hz(kStageCentreHz[i]);
    }
    lfo_.Init(1.0f, LfoWave::Sine);
    lfo_.SetJitter(0.15f);
    for (auto& channel : channels_) {
        for (auto& stage : channel.stages) stage.Reset();
        channel.dc.Init();
        // Loudness-neutral: the tone stage sits after the regen tap, outside
        // the feedback loop, so its treble lift cannot raise the loop gain.
        channel.tone.Init(SAMPLE_RATE, ToneGain::Loudness); // flat (knob = 0.5)
        channel.feedback = 0.0f;
    }
    sweep_shape_ = 0.0f;
}

void VibeMode::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.speed);
    for (auto& channel : channels_) channel.tone.SetKnob(params.tone); // recomputes only on change
    sweep_shape_ = params.p2;
}

float VibeMode::ProcessChannel(Channel& channel, float input, const float* coeffs,
                               float regen, float am_gain) {
    float x = input + channel.feedback * regen;

    // Mild pre-saturation: germanium transistor 3rd-harmonic coloring (~4%).
    // Clamp before applying: x - 0.04x³ is non-monotonic above |x|=2.89, which
    // can be reached through the regen feedback path.
    if (x >  2.87f) x =  2.87f;
    if (x < -2.87f) x = -2.87f;
    x = x - 0.04f * x * x * x;

    for (int i = 0; i < kStages; ++i) {
        channel.stages[i].SetCoeff(coeffs[i]);
        x = channel.stages[i].Process(x);
    }
    x *= am_gain;

    x = channel.dc.Process(x);
    // Take regen before the tone stage. Regen is capped at 0.7 and the stages
    // are allpass, so the loop gain stays below unity for any Tone setting.
    channel.feedback = x;

    // Transistor preamp coloring via tone knob.
    return channel.tone.Process(x);
}

StereoFrame VibeMode::Process(StereoFrame input, const ParamSet& params) {
    // Capture phase before advancing so per-stage offsets are anchored to this sample.
    const float base_phase = lfo_.GetPhase();
    lfo_.Process(); // advance only; value discarded (computed per-stage below)

    // Per-stage allpass sweep with independent LDR phase offsets. Computed
    // once and shared by both channels.
    float coeffs[kStages];
    for (int i = 0; i < kStages; ++i) {
        float ph = base_phase + kStagePhase[i];
        if (ph >= TWO_PI) ph -= TWO_PI;

        // Unipolar lamp brightness [0..1].
        const float lamp = 0.5f + 0.5f * fast_sin(ph);

        // Smoothstep LDR response: approximates power-law photoresistor curve.
        const float smooth_ldr = lamp * lamp * (3.0f - 2.0f * lamp);
        // Shape moves from the original smooth optical response to a more
        // asymmetric, pulse-like photocell sweep without discontinuous modes.
        const float shaped_ldr = lamp * lamp;
        const float ldr = smooth_ldr + sweep_shape_ * (shaped_ldr - smooth_ldr);

        // Map ldr [0..1] → a symmetric swing in octaves around the stage centre,
        // then convert to a coefficient through the shared log-frequency table.
        const float octaves = params.depth * kStageOctaves[i] * (ldr - 0.5f);
        const float position = stage_centre_[i] + octaves * (1.0f / freq_table::kOctaves);
        coeffs[i] = freq_table::allpass_coeff_at(position);
    }

    // AM throb: volume dips slightly ahead of the sweep peak, matching the
    // original's separate optical coupler position at ~90° from stage 0.
    float am_ph = base_phase + kAmPhaseOffset;
    if (am_ph >= TWO_PI) am_ph -= TWO_PI;
    const float am_lamp = 0.5f + 0.5f * fast_sin(am_ph);
    float am_gain = 1.0f - params.depth * 0.12f * am_lamp;
    if (am_gain < 0.1f) am_gain = 0.1f;

    const float regen = params.p1 * 0.7f;
    return {ProcessChannel(channels_[0], input.left, coeffs, regen, am_gain),
            ProcessChannel(channels_[1], input.right, coeffs, regen, am_gain)};
}

} // namespace pedal
