#include "ladder_sweep_mode.h"

#include "../dsp/fast_math.h"
#include "../dsp/freq_table.h"

#include <algorithm>
#include <cmath>

namespace pedal {

float LadderSweepMode::Channel::Process(float input, float g, float resonance)
{
    // Four zero-delay one-pole stages form a 24 dB/octave cascade. The
    // saturating feedback path supplies the rounded overload and keeps the
    // near-self-oscillating setting bounded without a limiter in the loop.
    float value = soft_clip_tanh(input - resonance * state[3]);
    const float coefficient = g / (1.0f + g);
    for (float& memory : state) {
        const float delta = (value - memory) * coefficient;
        value = memory + delta;
        memory = value + delta;
    }
    return soft_clip_tanh(value);
}

void LadderSweepMode::Init()
{
    Reset();
}

void LadderSweepMode::Reset()
{
    lfo_.Init(2.0f, LfoWave::Triangle);
    left_.Reset();
    right_.Reset();
    basePosition_ = freq_table::position_for_hz(220.0f);
    sweepSpan_ = 2.5f / freq_table::kOctaves;
    resonance_ = 1.0f;
    drive_ = 1.0f;
}

void LadderSweepMode::Prepare(const mod_fx::ParamSet& params)
{
    lfo_.SetRate(params.speed);
    lfo_.SetWave(params.p2 < 0.5f ? LfoWave::Triangle : LfoWave::Square);

    const float cutoffControl = std::clamp(params.tone, 0.0f, 1.0f);
    const float cutoffHz = 20.0f * std::exp2(cutoffControl * std::log2(12000.0f / 20.0f));
    basePosition_ = freq_table::position_for_hz(cutoffHz);
    sweepSpan_ = std::clamp(params.depth, 0.0f, 1.0f) * 5.0f / freq_table::kOctaves;

    const float resonanceControl = std::clamp(params.p1, 0.0f, 1.0f);
    resonance_ = 4.05f * resonanceControl;

    // The shared modulation parameter arrives in its physical 0..2 range.
    // Re-normalize it to the block's -6..+18 dB drive control.
    const float driveControl = std::clamp(params.level * 0.5f, 0.0f, 1.0f);
    drive_ = std::pow(10.0f, (-6.0f + 24.0f * driveControl) / 20.0f);
}

StereoFrame LadderSweepMode::Process(StereoFrame input, const mod_fx::ParamSet&)
{
    const float sweep = 0.5f + 0.5f * lfo_.Process();
    const float position = basePosition_ + sweepSpan_ * sweep;
    const float g = freq_table::g_at(position);
    return {
        left_.Process(input.left * drive_, g, resonance_),
        right_.Process(input.right * drive_, g, resonance_),
    };
}

} // namespace pedal
