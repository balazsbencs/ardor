#include "vintage_trem_mode.h"
#include "../config/constants.h"
#include "../dsp/fast_math.h"
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

void VintageTremMode::Init() {
    tone_l_.Init();
    tone_r_.Init();
    Reset();
}

void VintageTremMode::Reset() {
    lfo_.Init(4.0f, LfoWave::Sine);
    tone_l_.Reset();
    tone_r_.Reset();
    depth_ = 0.5f;
    makeup_ = 1.0f;
    shape_ = 0.0f;
    crossover_l_ = 0.0f;
    crossover_r_ = 0.0f;
    sub_mode_ = 0;
}

void VintageTremMode::Prepare(const ParamSet& params) {
    // sub-mode from p2: 0=Tube (sine), 1=Harmonic (true crossover), 2=Photoresistor (exponential)
    sub_mode_ = static_cast<int>(params.p2 * 2.999f);
    if (sub_mode_ == 0)      lfo_.SetWave(LfoWave::Sine);
    else if (sub_mode_ == 1) lfo_.SetWave(LfoWave::Sine); // use Sine for harmonic, it's smoother!
    else                     lfo_.SetWave(LfoWave::Exponential);

    lfo_.SetRate(params.speed);
    depth_ = params.depth;
    // Preserve RMS, not mean. The modulator (1 - depth*contour) has mean
    // 1 - depth/2 and mean square (1 - depth/2)^2 + depth^2/8. Compensating the
    // mean would put peaks 6 dB over unity at full depth; matching RMS restores
    // the perceived level with far less headroom cost.
    const float mean = 1.0f - depth_ * 0.5f;
    const float mean_square = mean * mean + depth_ * depth_ * 0.125f;
    makeup_ = mean_square > 0.0001f ? 1.0f / std::sqrt(mean_square) : 1.0f;
    // Shape morphs the sine tremolo toward a more pulsed optical contour.
    shape_ = params.p1;
    tone_l_.SetKnob(params.tone);
    tone_r_.SetKnob(params.tone);
}

StereoFrame VintageTremMode::Process(StereoFrame input, const ParamSet& /*params*/) {
    // Per-sample LFO for smooth amplitude modulation.
    const float lfo_val = lfo_.Process(); // -1..+1

    if (sub_mode_ == 1) {
        // True Harmonic Tremolo: split signal into LP and HP bands at ~700 Hz (alpha = 0.0916f)
        constexpr float alpha = 0.0916f;
        crossover_l_ += alpha * (input.left  - crossover_l_);
        crossover_r_ += alpha * (input.right - crossover_r_);

        const float lp_l = crossover_l_;
        const float hp_l = input.left - lp_l;

        const float lp_r = crossover_r_;
        const float hp_r = input.right - lp_r;

        // Modulate LP and HP bands 180 degrees out of phase
        const float sine_lp = 0.5f + 0.5f * lfo_val;
        const float sine_hp = 1.0f - sine_lp;
        const float contour_lp = sine_lp + shape_ * (sine_lp * sine_lp - sine_lp);
        const float contour_hp = sine_hp + shape_ * (sine_hp * sine_hp - sine_hp);
        // Harmonic mode keeps constant total energy across the two bands, so
        // it needs no make-up.
        const float gain_lp = 1.0f - depth_ * contour_lp;
        const float gain_hp = 1.0f - depth_ * contour_hp;

        return {tone_l_.Process(lp_l * gain_lp + hp_l * gain_hp),
                tone_r_.Process(lp_r * gain_lp + hp_r * gain_hp)};
    }

    // Tube and Photoresistor modes: standard amplitude modulation
    const float sine = 0.5f + 0.5f * lfo_val;
    const float contour = sine + shape_ * (sine * sine - sine);
    float gain = 1.0f - depth_ * contour;
    if (gain < 0.0f) gain = 0.0f;
    // Make-up gain. The mean of the modulator is 0.5, so without this the mode
    // is 6 dB quieter than bypass at full depth and engaging it drops the level.
    gain *= makeup_;
    // Make-up lifts peaks above unity, so limit smoothly rather than let a
    // downstream stage hard clip them.
    return {soft_clip_tanh(tone_l_.Process(input.left * gain)),
            soft_clip_tanh(tone_r_.Process(input.right * gain))};
}

} // namespace pedal
