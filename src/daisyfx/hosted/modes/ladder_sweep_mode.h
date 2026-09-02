#pragma once

#include "mod_mode.h"
#include "../dsp/lfo.h"

#include <array>

namespace pedal {

/// Tempo-set four-pole resonant low-pass inspired by the classic pairing of a
/// voltage-controlled ladder filter with an external triangle/square LFO.
///
/// Speed: 40..240 BPM (one sweep per beat).
/// Depth: 0..5 octaves above the cutoff control.
/// Tone: base cutoff, logarithmic 20 Hz..12 kHz.
/// P1: resonance, reaching self-oscillation at the top of the range.
/// P2: triangle/square waveform.
/// Level: input drive, -6..+18 dB.
class LadderSweepMode : public ModMode {
public:
    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Ladder Sweep"; }

private:
    struct Channel {
        std::array<float, 4> state{};

        void Reset() { state = {}; }
        float Process(float input, float g, float resonance);
    };

    Lfo lfo_;
    Channel left_;
    Channel right_;
    float basePosition_ = 0.0f;
    float sweepSpan_ = 0.0f;
    float resonance_ = 0.0f;
    float drive_ = 1.0f;
};

} // namespace pedal
