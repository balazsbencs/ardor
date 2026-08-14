#pragma once
#include <cmath>
#include <cstddef>
#include "allpass.h"
#include "fast_math.h"

namespace pedal {

// 4-stage allpass cascade for smearing transients.
//
// The delay set is supplied by the owning mode rather than fixed here. Sharing
// one table across every reverb gave them all the same allpass resonances, and
// using the same table for left and right meant a mono source left the
// diffusion stage still perfectly correlated. Each mode now passes its own
// mutually co-prime set, with a different set per channel.
namespace diffuser_delays {

// All values are prime, so every set is mutually co-prime both within itself
// and against the other sets. Tuned for the 24 kHz reverb stage.
inline constexpr size_t ROOM_L[4]      = { 71, 109, 191, 311};
inline constexpr size_t ROOM_R[4]      = { 79, 127, 197, 317};
inline constexpr size_t HALL_L[4]      = { 83, 131, 199, 331};
inline constexpr size_t HALL_R[4]      = { 89, 137, 211, 337};
inline constexpr size_t CLOUD_A_L[4]   = { 73, 113, 193, 313};
inline constexpr size_t CLOUD_A_R[4]   = { 97, 139, 223, 347};
inline constexpr size_t CLOUD_B_L[4]   = {101, 149, 227, 353};
inline constexpr size_t CLOUD_B_R[4]   = {103, 151, 229, 359};
inline constexpr size_t SHIMMER_L[4]   = {107, 157, 233, 367};
inline constexpr size_t SHIMMER_R[4]   = {109, 163, 239, 373};
inline constexpr size_t BLOOM_L[4]     = {113, 167, 241, 379};
inline constexpr size_t BLOOM_R[4]     = {127, 173, 251, 383};
inline constexpr size_t NONLINEAR_L[4] = {131, 179, 257, 389};
inline constexpr size_t NONLINEAR_R[4] = {137, 181, 263, 397};
inline constexpr size_t MAGNETO_L[4]   = {139, 191, 269, 401};
inline constexpr size_t MAGNETO_R[4]   = {149, 193, 271, 409};

} // namespace diffuser_delays

class Diffuser {
public:
    static constexpr int STAGES = 4;
    static constexpr float  MAX_MOD_DEPTH_SAMPLES = 4.0f;
    // Headroom each stage buffer needs above its nominal delay so the last two
    // stages can be modulated without running off the end. It must cover the
    // drift itself plus the band-limited reader's right-hand margin (8 taps)
    // plus one, or ReadAtHighQuality clamps the tap and the drift never happens.
    static constexpr size_t MOD_HEADROOM = 16;

    // bufs[i] must hold at least delays[i] + MOD_HEADROOM entries.
    void Init(float* bufs[STAGES], const size_t sizes[STAGES], const size_t delays[STAGES]) {
        for (int i = 0; i < STAGES; ++i) {
            delays_[i] = delays[i];
            stages_[i].Init(bufs[i], sizes[i]);
            stages_[i].SetDelay(delays_[i]);
        }
        phase_ = 0.0f;
    }

    void Reset() {
        for (auto& s : stages_) s.Reset();
        phase_ = 0.0f;
    }

    // d: 0..1
    // |g| must remain < 1 for allpass stability.
    // First pair (0,1) use lower g to reduce spectral coloration.
    // Second pair (2,3) use slightly higher g for better diffusion depth.
    void SetDiffusion(float d) {
        g_[0] = 0.50f + d * 0.10f;  // 0.50 – 0.60
        g_[1] = 0.50f + d * 0.10f;  // 0.50 – 0.60
        g_[2] = 0.55f + d * 0.15f;  // 0.55 – 0.70
        g_[3] = 0.55f + d * 0.15f;  // 0.55 – 0.70
    }

    // Slow, shallow movement on the last two stages. A static allpass cascade
    // rings on fixed frequencies; drifting the longer stages breaks that up
    // without becoming an audible chorus. 0 disables it entirely.
    void SetModulation(float depth_samples) {
        if (!std::isfinite(depth_samples) || depth_samples < 0.0f) depth_samples = 0.0f;
        mod_depth_ = depth_samples > MAX_MOD_DEPTH_SAMPLES ? MAX_MOD_DEPTH_SAMPLES
                                                           : depth_samples;
    }

    // rate_hz applies to the internal drift oscillator; sample_rate is the rate
    // Process() is called at.
    void SetModulationRate(float rate_hz, float sample_rate) {
        if (!(rate_hz > 0.0f) || !(sample_rate > 0.0f)) { phase_inc_ = 0.0f; return; }
        phase_inc_ = rate_hz / sample_rate;
    }

    float Process(float input) {
        if (!std::isfinite(input)) { Reset(); return 0.0f; }
        float s = input;

        if (mod_depth_ <= 0.0f) {
            for (int i = 0; i < STAGES; ++i) {
                s = stages_[i].Process(s, g_[i]);
                if (!std::isfinite(s)) { Reset(); return 0.0f; }
            }
            return s;
        }

        phase_ += phase_inc_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        const float drift = mod_depth_ * fast_sin(phase_ * 6.28318530718f);

        // Stages 0 and 1 stay fixed; they carry the early density. Stages 2 and
        // 3 drift in opposite directions so the pair's total delay is steady.
        s = stages_[0].Process(s, g_[0]);
        s = stages_[1].Process(s, g_[1]);
        s = stages_[2].ProcessMod(s, g_[2], static_cast<float>(delays_[2]) + drift);
        s = stages_[3].ProcessMod(s, g_[3], static_cast<float>(delays_[3]) - drift);
        if (!std::isfinite(s)) { Reset(); return 0.0f; }
        return s;
    }

private:
    DelayAllpassFilter stages_[STAGES];
    size_t             delays_[STAGES] = {73, 113, 193, 313};
    // Defaults correspond to SetDiffusion(0.0f) — minimum operating point.
    float              g_[STAGES] = {0.50f, 0.50f, 0.55f, 0.55f};
    float              mod_depth_ = 0.0f;
    float              phase_     = 0.0f;
    float              phase_inc_ = 0.0f;
};

} // namespace pedal
