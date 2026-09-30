#pragma once
#include "mod_mode.h"
#include "../dsp/halfband_resampler.h"
#include <array>

namespace pedal {
namespace ph2 {

// Original October 1984 circuit, not the serial-497100 noise-reduction revision.
inline constexpr float kRate = 96000.0f;
inline constexpr float kMaxResonance = 10000.0f / 14700.0f;
inline constexpr float kFixedHz = 1.0f / (6.28318530718f * 5600.0f * 1e-8f);
inline constexpr float kOtaScale = 0.02585f * (68000.0f + 2.0f * 560.0f) / 560.0f;

struct Value { float signal; float slope; };
Value rail(float input);

// Trapezoidal companion state. Evaluation is side-effect free; only the
// converged feedback-loop solution advances a capacitor's state.
struct Pole {
    float state = 0.0f;
    Value lowpass(float input, float g) const;
    void commit(float output) { state = 2.0f * output - state; }
};

struct Allpass {
    Pole cap;
    Value evaluate(float input, float g, bool ota, float& next) const;
};

struct Loop {
    std::array<Allpass, 10> stages{};
    Pole return_filter, resonance_filter, coupling, fixed_coupling, chip2_coupling;
    float previous_input = 0.0f;
    float max_residual = 0.0f; // worst accepted root residual since reset
    float process(float input, float swept_g, float fixed_g, float beta,
                  int count, bool second);
};

// NE571 feedback compressor / feedforward expander. Both rectify the signal
// at their own gain cell, not a shared input envelope. C16/C17 = 4.7uF.
struct Compander {
    float compression_envelope = 0.0f;
    float expansion_envelope = 0.0f;
    float compress(float input);
    float expand(float input);
};

struct Routing {
    Loop first, second;
    float process(float input, float swept_g, float fixed_g, float resonance,
                  bool mode2);
};

} // namespace ph2

class Ph2Mode : public ModMode {
public:
    static constexpr size_t kLatencyFrames = 15;
    void Init() override { Reset(); }
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "PH-2 Phaser"; }
    bool OwnsDryMix() const override { return true; }
    float SolverResidual() const;

private:
    struct Channel {
        ph2::Pole input_hp, buffer_hp, input_lp, output_hp, output_lp;
        ph2::Compander compander;
        std::array<ph2::Routing, 2> routing;
        HalfbandInterpolator2x up;
        HalfbandDecimator2x down;
        std::array<float, kLatencyFrames> dry{};
        size_t dry_index = 0;
        float cv = 0.0f;
        float process(float input, float triangle, float centre, float depth,
                      float resonance, float fixed_g, float mode_blend, bool run_both, int active);
    };
    std::array<Channel, 2> channels_{};
    float phase_ = 0.0f;
    float rate_ = 1.0f;
    float fixed_g_ = 0.0f;
    float centre_ = 0.0f, centre_target_ = 0.0f;
    float depth_ = 0.0f, depth_target_ = 0.0f;
    float resonance_ = 0.0f, resonance_target_ = 0.0f;
    float stereo_ = 0.0f, stereo_target_ = 0.0f;
    float mode_blend_ = 0.0f;
    int mode_target_ = 0;
    bool prepared_ = false;
    bool channels_identical_ = true;
};
} // namespace pedal
