#include "filter_mode.h"
#include "../config/constants.h"
#include "../dsp/fast_math.h"
#include "../dsp/freq_table.h"
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

void FilterMode::Init() {
    Reset();
}

void FilterMode::Reset() {
    static constexpr float kHalfPi = 1.57079633f;
    lfo_.Init(1.0f, LfoWave::Sine);
    lfo_r_.Init(1.0f, LfoWave::Sine);
    lfo_r_.SetPhaseOffset(kHalfPi);
    lfo_r_.Reset();
    svf_.Reset();
    svf_r_.Reset();
    env_.Init(5.0f, 80.0f);
    dc_.Init();
    dc_r_.Init();
    base_pos_ = freq_table::position_for_hz(1000.0f);
    depth_    = 0.5f;
    use_env_  = false;
    env_inv_  = false;
    ftype_    = 0;
}

void FilterMode::Prepare(const ParamSet& params) {
    // Waveshape from P2: 0=Sine, 1=Tri, 2=Sq, 3=RampUp, 4=RampDown, 5=S&H, 6=Env+, 7=Env-
    int shape = static_cast<int>(params.p2 * 7.999f);
    if (shape < 0) shape = 0;
    if (shape > 7) shape = 7;

    use_env_ = (shape >= 6);
    env_inv_ = (shape == 7);

    if (!use_env_) {
        static const LfoWave kWaves[6] = {
            LfoWave::Sine, LfoWave::Triangle, LfoWave::Square,
            LfoWave::RampUp, LfoWave::RampDown, LfoWave::SampleAndHold
        };
        lfo_.SetWave(kWaves[shape]);
        lfo_r_.SetWave(kWaves[shape]);
        lfo_.SetRate(params.speed);
        lfo_r_.SetRate(params.speed);
    }

    // Filter type from tone, with dead bands. Crossing a bare threshold swapped
    // the output tap instantaneously, which clicks; FilterDelay already guards
    // its equivalent choice this way.
    switch (ftype_) {
        case 0:  // LP
            if (params.tone > 0.43f) ftype_ = 1;
            break;
        case 1:  // BP / wah
            if (params.tone < 0.37f)      ftype_ = 0;
            else if (params.tone > 0.63f) ftype_ = 2;
            break;
        default: // HP
            if (params.tone < 0.57f) ftype_ = 1;
            break;
    }

    // Sweep centre. Env modes sit lower so an auto-wah has room to open.
    const float centre_hz = use_env_ ? 80.0f + params.tone * 2000.0f
                                     : 80.0f + params.tone * 11920.0f;
    base_pos_ = freq_table::position_for_hz(centre_hz);
    depth_    = params.depth;

    // Resonance Q from P1 (0..1 → 0.5..20)
    const float q = 0.5f + params.p1 * 19.5f;
    svf_.SetQ(q);
    svf_r_.SetQ(q);
}

float FilterMode::bandOutput(const Svf& svf) const {
    switch (ftype_) {
        case 1:  return svf.bp();
        case 2:  return svf.hp();
        default: return svf.lp();
    }
}

StereoFrame FilterMode::Process(StereoFrame input, const ParamSet& params) {
    const float mono = input.mono();

    // Cutoff is set per sample in every mode. The envelope path used to compute
    // its target here but only apply it once per control block, which stepped a
    // Q-20 filter every millisecond — zipper noise on the mode most exposed to
    // it. The shared table makes a per-sample update cheap.
    float pos_l = base_pos_;
    float pos_r = base_pos_;
    if (use_env_) {
        float env_val = env_.Process(mono);   // 0..1
        if (env_inv_) env_val = 1.0f - env_val;
        // Up to ~3.2 octaves of sweep above the centre.
        const float octaves = env_val * depth_ * 3.2f;
        pos_l += octaves * (1.0f / freq_table::kOctaves);
        pos_r = pos_l;   // envelope is a mono control; both channels track it
    } else {
        const float lfo_l = 0.5f + 0.5f * lfo_.Process();
        const float lfo_r = 0.5f + 0.5f * lfo_r_.Process();
        const float span  = depth_ * 2.6f * (1.0f / freq_table::kOctaves);
        pos_l += span * lfo_l;
        pos_r += span * lfo_r;
    }

    svf_.SetG(freq_table::g_at(pos_l));
    svf_r_.SetG(freq_table::g_at(pos_r));

    svf_.Process(mono);
    svf_r_.Process(mono);

    // A resonant SVF can exceed unity even with bounded input.
    const float wet_l = dc_.Process(soft_clip_tanh(bandOutput(svf_)));
    const float wet_r = dc_r_.Process(soft_clip_tanh(bandOutput(svf_r_)));
    return {wet_l, wet_r};
}

} // namespace pedal
