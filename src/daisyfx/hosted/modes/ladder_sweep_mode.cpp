#include "ladder_sweep_mode.h"

#include "../dsp/fast_math.h"
#include "../dsp/freq_table.h"

#include <algorithm>
#include <cmath>

namespace pedal {

namespace {
// Signal level whose loudness the drive make-up holds constant.
constexpr float kDriveReference = 0.2f;
} // namespace

float LadderSweepMode::Channel::Process(float input, float g, float resonance)
{
    // Four zero-delay one-pole stages form a 24 dB/octave cascade, and the
    // feedback around them is solved in the same sample. Each TPT stage is
    // y = G*v + (1 - G)*s with G = g/(1+g), so the fourth stage's output is
    // G^4*u + S, where S collects the stage states. Solving u = x - k*y4 gives
    // u = (x - k*S) / (1 + k*G^4).
    //
    // The earlier version fed back the previous sample's output instead. That
    // unit delay turned the loop unstable at high cutoff: at 12 kHz it rang on
    // at -16 dBFS with Resonance at half. Solved in the same sample the loop
    // is stable for every cutoff below k = 4 and self-oscillates only above.
    const float G = g / (1.0f + g);
    const float one_minus_G = 1.0f - G;
    const float S = G * G * G * one_minus_G * state[0] + G * G * one_minus_G * state[1]
                  + G * one_minus_G * state[2] + one_minus_G * state[3];
    const float G4 = G * G * G * G;
    // Saturation at the input of the ladder, as in the transistor circuit:
    // it rounds overload and bounds self-oscillation.
    float value = soft_clip_tanh((input - resonance * S) / (1.0f + resonance * G4));
    for (float& memory : state) {
        const float delta = (value - memory) * G;
        value = memory + delta;
        memory = value + delta;
    }
    return value;
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
    // A ladder loses bass as resonance rises: its passband gain is 1/(1+k).
    // Restore about half of that in dB, so turning up Resonance adds a peak
    // without taking the body out of the note.
    resonance_makeup_ = std::sqrt(1.0f + resonance_);

    // The shared modulation parameter arrives in its physical 0..2 range.
    // Re-normalize it to the block's -6..+18 dB drive control.
    const float driveControl = std::clamp(params.level * 0.5f, 0.0f, 1.0f);
    drive_ = std::pow(10.0f, (-6.0f + 24.0f * driveControl) / 20.0f);
    // Drive sets how hard the input stage saturates, not how loud the block
    // is. Hold the level of a typical guitar peak steady across the range;
    // without this Drive swept the output across 20 dB.
    drive_makeup_ = kDriveReference / soft_clip_tanh(kDriveReference * drive_);
}

StereoFrame LadderSweepMode::Process(StereoFrame input, const mod_fx::ParamSet&)
{
    const float sweep = 0.5f + 0.5f * lfo_.Process();
    const float position = basePosition_ + sweepSpan_ * sweep;
    const float g = freq_table::g_at(position);
    // The resonance make-up goes in before the ladder, so the input stage
    // saturates the restored level as the circuit would; the drive make-up
    // comes after it.
    const float gain_in = drive_ * resonance_makeup_;
    return {
        left_.Process(input.left * gain_in, g, resonance_) * drive_makeup_,
        right_.Process(input.right * gain_in, g, resonance_) * drive_makeup_,
    };
}

} // namespace pedal
