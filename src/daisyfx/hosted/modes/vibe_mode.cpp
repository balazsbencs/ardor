#include "vibe_mode.h"
#include "../dsp/freq_table.h"
#include "../config/constants.h"
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

// Per-stage sweep, expressed in log frequency rather than in allpass
// coefficient. The original coefficient-domain constants gave a badly uneven
// sweep, because coefficient is a compressed function of frequency: the notch
// raced through the top of its range and crawled through the bottom.
// Centre frequency of each LDR stage at mid lamp brightness (Hz).
static constexpr float kStageCentreHz[4] = {260.0f, 620.0f, 1500.0f, 3600.0f};

// Sweep width per stage in octaves at full depth. Higher stages sweep wider,
// matching the original's non-uniform LDR spacing.
static constexpr float kStageOctaves[4] = {1.2f, 1.6f, 2.0f, 2.6f};

// Lamp and photocell. The incandescent filament heats and cools with a time
// constant of tens of milliseconds, so it cannot follow a fast oscillator:
// the sweep narrows as Speed rises. The CdS cells respond to light in about
// 10 ms and recover in the dark over 40 ms (Lag 0) to 160 ms (Lag 1).
static constexpr float kFilamentSeconds  = 0.030f;
static constexpr float kCellAttackSeconds = 0.010f;
static constexpr float kCellReleaseMin   = 0.040f;
static constexpr float kCellReleaseMax   = 0.160f;
// The lamp never goes fully dark in the circuit; this floor also bounds the
// power law below.
static constexpr float kCellFloor = 0.03f;
// Stage corner ~ 1/R and R ~ light^-gamma for a CdS cell, so the corner moves
// as log2(light) in octaves. Normalised so the floor..full-light range spans
// each stage's full sweep width.
static const float kInvLogFloor = 1.0f / std::log2(kCellFloor);

static float one_pole(float seconds) {
    return 1.0f - std::exp(-1.0f / (seconds * SAMPLE_RATE));
}

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
    lamp_ = 0.0f;
    cell_ = 0.0f;
    cell_release_ = one_pole(kCellReleaseMin);
}

void VibeMode::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.speed);
    for (auto& channel : channels_) channel.tone.SetKnob(params.tone); // recomputes only on change
    // Lag (p2): how slowly the cells recover in the dark.
    cell_release_ = one_pole(kCellReleaseMin + params.p2 * (kCellReleaseMax - kCellReleaseMin));
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
    // The oscillator drives the lamp through a transistor; the filament heats
    // with the power it receives and lags behind it.
    static const float kFilament = one_pole(kFilamentSeconds);
    static const float kCellAttack = one_pole(kCellAttackSeconds);
    const float drive = 0.5f + 0.5f * lfo_.Process();
    lamp_ += kFilament * (drive * drive - lamp_);

    // One light, four cells: they share the lamp and respond alike, so one
    // cell state serves all four. Fast to brighten, slow to darken.
    cell_ += (lamp_ > cell_ ? kCellAttack : cell_release_) * (lamp_ - cell_);

    // Brightness in log terms: 0 at the floor, 1 at full light.
    const float light = cell_ > kCellFloor ? cell_ : kCellFloor;
    const float bright = 1.0f - std::log2(light) * kInvLogFloor;

    // Each stage's capacitor sets its own centre; the shared light moves all
    // four corners together in log frequency, computed once for both channels.
    float coeffs[kStages];
    for (int i = 0; i < kStages; ++i) {
        const float octaves = params.depth * kStageOctaves[i] * (bright - 0.5f);
        const float position = stage_centre_[i] + octaves * (1.0f / freq_table::kOctaves);
        coeffs[i] = freq_table::allpass_coeff_at(position);
    }

    // AM throb from the same lamp: the level dips as the cells light up.
    float am_gain = 1.0f - params.depth * 0.12f * bright;
    if (am_gain < 0.1f) am_gain = 0.1f;

    const float regen = params.p1 * 0.7f;
    return {ProcessChannel(channels_[0], input.left, coeffs, regen, am_gain),
            ProcessChannel(channels_[1], input.right, coeffs, regen, am_gain)};
}

} // namespace pedal
