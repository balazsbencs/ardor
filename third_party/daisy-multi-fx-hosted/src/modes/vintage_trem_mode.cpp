#include "vintage_trem_mode.h"
#include "../config/constants.h"

using namespace pedal::mod_fx;

namespace pedal {

void VintageTremMode::Init() {
    Reset();
}

void VintageTremMode::Reset() {
    lfo_.Init(4.0f, LfoWave::Sine);
    depth_ = 0.5f;
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
        const float gain_lp = 1.0f - depth_ * (0.5f + 0.5f * lfo_val);
        const float gain_hp = 1.0f - depth_ * (0.5f - 0.5f * lfo_val);

        return {lp_l * gain_lp + hp_l * gain_hp,
                lp_r * gain_lp + hp_r * gain_hp};
    }

    // Tube and Photoresistor modes: standard amplitude modulation
    float gain = 1.0f - depth_ * (0.5f + 0.5f * lfo_val);
    if (gain < 0.0f) gain = 0.0f;
    return {input.left * gain, input.right * gain};
}

} // namespace pedal
