#include "vintage_trem_mode.h"
#include "../config/constants.h"
#include "../dsp/fast_math.h"
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

namespace {

// CdS photocell response. Attack is the cell lighting up (the level
// falling), release is the cell going dark again (the level recovering).
constexpr float kCellAttackSeconds  = 0.005f;
constexpr float kCellReleaseSeconds = 0.070f;
// The make-up follows the gain's mean square over about a second: slow
// enough to hold still within a cycle, fast enough to settle after a change.
constexpr float kMakeupSeconds = 1.0f;
constexpr float kMaxMakeup = 1.41f;  // +3 dB, as for the other types

float one_pole(float seconds) {
    return 1.0f - std::exp(-1.0f / (seconds * SAMPLE_RATE));
}

const float kCellAttack  = one_pole(kCellAttackSeconds);
const float kCellRelease = one_pole(kCellReleaseSeconds);
const float kMakeupCoef  = one_pole(kMakeupSeconds);
// The peak must hold across many cycles, or the make-up ripples within one
// and reshapes the tremolo.
constexpr float kPeakHoldSeconds = 10.0f;
const float kPeakDecay   = std::exp(-1.0f / (kPeakHoldSeconds * SAMPLE_RATE));

} // namespace

void VintageTremMode::Init() {
    tone_l_.Init(SAMPLE_RATE, ToneGain::Loudness);
    tone_r_.Init(SAMPLE_RATE, ToneGain::Loudness);
    Reset();
}

void VintageTremMode::Reset() {
    lfo_.Init(4.0f, LfoWave::Sine);
    lfo_r_.Init(4.0f, LfoWave::Sine);
    tone_l_.Reset();
    tone_r_.Reset();
    depth_ = 0.5f;
    makeup_ = 1.0f;
    shape_ = 0.0f;
    crossover_l_ = 0.0f;
    crossover_r_ = 0.0f;
    photo_[0] = {};
    photo_[1] = {};
    sub_mode_ = 0;
}

void VintageTremMode::Prepare(const ParamSet& params) {
    // sub-mode from p2: 0=Tube (sine), 1=Harmonic (true crossover), 2=Photoresistor (lamp + CdS cell)
    sub_mode_ = static_cast<int>(params.p2 * 2.999f);
    lfo_.SetWave(LfoWave::Sine);
    lfo_r_.SetWave(LfoWave::Sine);

    lfo_.SetRate(params.speed);
    lfo_r_.SetRate(params.speed);
    // Stereo (p3): 0..180 degrees between the channels. 0 is the mono
    // tremolo this mode always was; 180 degrees is a pan tremolo.
    lfo_r_.SetPhaseOffset(params.p3 * 3.14159265f);
    depth_ = params.depth;
    // Preserve RMS, not mean. The modulator (1 - depth*contour) has mean
    // 1 - depth/2 and mean square (1 - depth/2)^2 + depth^2/8. Compensating the
    // mean would put peaks 6 dB over unity at full depth; matching RMS restores
    // the perceived level with far less headroom cost.
    //
    // Full RMS make-up reaches +4.3 dB at the peaks at full depth. Cap it at
    // +3 dB instead: the peaks stay clear of the next stage without a limiter,
    // and full depth gives up only 1.3 dB of average level.
    const float mean = 1.0f - depth_ * 0.5f;
    const float mean_square = mean * mean + depth_ * depth_ * 0.125f;
    makeup_ = mean_square > 0.0001f ? 1.0f / std::sqrt(mean_square) : 1.0f;
    if (makeup_ > kMaxMakeup) makeup_ = kMaxMakeup;
    // Shape morphs the sine tremolo toward a more pulsed optical contour.
    shape_ = params.p1;
    // In the Photoresistor type Shape sets how hard the lamp switches: a
    // neon bulb strikes and quenches abruptly, so even the softest setting
    // is well on the way to a pulse.
    lamp_knee_ = 3.0f + 17.0f * params.p1;
    tone_l_.SetKnob(params.tone);
    tone_r_.SetKnob(params.tone);
}

float VintageTremMode::AmplitudeGain(float lfo_value) const {
    const float sine = 0.5f + 0.5f * lfo_value;
    const float contour = sine + shape_ * (sine * sine - sine);
    float gain = 1.0f - depth_ * contour;
    if (gain < 0.0f) gain = 0.0f;
    // Make-up gain. The mean of the modulator is 0.5, so without this the mode
    // is 6 dB quieter than bypass at full depth and engaging it drops the level.
    return gain * makeup_;
}

void VintageTremMode::HarmonicGains(float lfo_value, float& gain_lp, float& gain_hp) const {
    // Modulate LP and HP bands 180 degrees out of phase
    const float sine_lp = 0.5f + 0.5f * lfo_value;
    const float sine_hp = 1.0f - sine_lp;
    const float contour_lp = sine_lp + shape_ * (sine_lp * sine_lp - sine_lp);
    const float contour_hp = sine_hp + shape_ * (sine_hp * sine_hp - sine_hp);
    // Harmonic mode keeps constant total energy across the two bands, so
    // it needs no make-up.
    gain_lp = 1.0f - depth_ * contour_lp;
    gain_hp = 1.0f - depth_ * contour_hp;
}

float VintageTremMode::PhotoGain(Photocell& cell, float lfo_value) {
    const float lamp = 0.5f + 0.5f * soft_clip_tanh(lamp_knee_ * lfo_value)
                                   / soft_clip_tanh(lamp_knee_);
    const float coef = lamp > cell.light ? kCellAttack : kCellRelease;
    cell.light += coef * (lamp - cell.light);
    const float gain = 1.0f - depth_ * cell.light;
    // The lag reshapes the modulator, so the sine make-up does not apply;
    // restore the gain's own RMS instead. It depends only on the LFO, never
    // on the signal, so it cannot pump.
    cell.mean_square += kMakeupCoef * (gain * gain - cell.mean_square);
    cell.peak = gain > cell.peak * kPeakDecay ? gain : cell.peak * kPeakDecay;
    float target = cell.mean_square > 0.0001f ? 1.0f / std::sqrt(cell.mean_square) : 1.0f;
    // Limit the loudest output point, not the factor: the slow cell never
    // goes fully dark at depth, so its gain peaks below unity and a fixed
    // factor cap left full depth 3.7 dB quiet with 1 dB of headroom unused.
    const float peak_limit = cell.peak > 0.0001f ? kMaxMakeup / cell.peak : kMaxMakeup;
    if (target > peak_limit) target = peak_limit;
    cell.makeup += kMakeupCoef * (target - cell.makeup);
    return gain * cell.makeup;
}

StereoFrame VintageTremMode::Process(StereoFrame input, const ParamSet& /*params*/) {
    // Per-sample LFOs for smooth amplitude modulation. The follower reads the
    // same instant as the leader, so a 0 degree Stereo setting is exact mono.
    lfo_r_.FollowPhaseOf(lfo_);
    const float lfo_r = lfo_r_.Process(); // -1..+1
    const float lfo_l = lfo_.Process();

    if (sub_mode_ == 1) {
        // True Harmonic Tremolo: split signal into LP and HP bands at ~700 Hz (alpha = 0.0916f)
        constexpr float alpha = 0.0916f;
        crossover_l_ += alpha * (input.left  - crossover_l_);
        crossover_r_ += alpha * (input.right - crossover_r_);

        const float lp_l = crossover_l_;
        const float hp_l = input.left - lp_l;

        const float lp_r = crossover_r_;
        const float hp_r = input.right - lp_r;

        float gain_lp_l, gain_hp_l, gain_lp_r, gain_hp_r;
        HarmonicGains(lfo_l, gain_lp_l, gain_hp_l);
        HarmonicGains(lfo_r, gain_lp_r, gain_hp_r);
        return {tone_l_.Process(lp_l * gain_lp_l + hp_l * gain_hp_l),
                tone_r_.Process(lp_r * gain_lp_r + hp_r * gain_hp_r)};
    }

    // Tube and Photoresistor modes: standard amplitude modulation.
    // No output clipper. The soft clip that used to sit here distorted every
    // signal, even at zero depth: -35 dBc of third harmonic on a -6 dBFS tone.
    // A tremolo must pass a clean signal clean; the capped make-up keeps the
    // peaks within +3 dB instead.
    if (sub_mode_ == 2) {
        return {tone_l_.Process(input.left * PhotoGain(photo_[0], lfo_l)),
                tone_r_.Process(input.right * PhotoGain(photo_[1], lfo_r))};
    }
    return {tone_l_.Process(input.left * AmplitudeGain(lfo_l)),
            tone_r_.Process(input.right * AmplitudeGain(lfo_r))};
}

} // namespace pedal
