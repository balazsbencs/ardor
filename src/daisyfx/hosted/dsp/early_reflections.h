#pragma once
#include <cstddef>
#include <cstdint>
#include "delay_line_sdram.h"
#include "fast_math.h"
#include "../audio/stereo_frame.h"

namespace pedal {

struct ErTap {
    uint16_t delay_samples;
    float    gain;
    float    pan;  // -1..+1 (left=-1, center=0, right=+1)
};

class EarlyReflections {
public:
    static constexpr int MAX_TAPS = 48;

    void Init(float* buf, size_t size) { line_.Init(buf, size); }

    // Equal-power pan: angle sweeps [0, π/2] as pan goes from -1 to +1.
    // Gains are precomputed here so Process() is free of trig. They become
    // targets rather than live values: modes rebuild this table every control
    // block from Size, Decay or position controls, and assigning straight to
    // the live gains steps every tap at once, which zippers on a sweep.
    void SetTaps(const ErTap* taps, int count) {
        tap_count_ = count < MAX_TAPS ? count : MAX_TAPS;
        for (int i = 0; i < tap_count_; ++i) {
            taps_[i] = taps[i];
            const float angle = (taps[i].pan + 1.0f) * 0.7853982f;
            target_l_[i] = taps[i].gain * fast_cos(angle);
            target_r_[i] = taps[i].gain * fast_sin(angle);

            // Absorption coefficient falls with arrival time: the first tap
            // stays open, later ones progressively darker. `kFarthestTap` is the
            // delay at which the roll-off reaches its darkest setting.
            constexpr float kFarthestTap = 2600.0f;   // samples at the 24 kHz stage
            // At the 24 kHz stage these are roughly 11 kHz for the first
            // arrival and 3 kHz for the farthest. Kept gentle on purpose: this
            // is meant to take the metallic edge off a tap field, not to act as
            // a tone control, and steeper settings cost broadband level.
            constexpr float kNearestK    = 0.95f;     // open
            constexpr float kFarthestK   = 0.55f;     // dark
            float distance = static_cast<float>(taps[i].delay_samples) / kFarthestTap;
            if (distance > 1.0f) distance = 1.0f;
            absorb_k_[i] = kNearestK + distance * (kFarthestK - kNearestK);
        }
    }

    void Reset() {
        line_.Reset();
        seeded_ = false;
        for (auto& state : absorb_) state = 0.0f;
    }

    StereoFrame Process(float input) {
        // Seed on the first sample, not inside SetTaps. Modes call SetTaps once
        // from Init() with a provisional table and again from Prepare() with the
        // real one; seeding at SetTaps captured the provisional gains and let
        // them slew away audibly over the first 20 ms.
        if (!seeded_) {
            seeded_ = true;
            for (int i = 0; i < tap_count_; ++i) {
                gain_l_[i] = target_l_[i];
                gain_r_[i] = target_r_[i];
            }
        }

        StereoFrame out{};
        for (int i = 0; i < tap_count_; ++i) {
            gain_l_[i] += kGainSlew * (target_l_[i] - gain_l_[i]);
            gain_r_[i] += kGainSlew * (target_r_[i] - gain_r_[i]);
            // Delays are integer samples — ReadNearest is lossless vs Hermite here.
            float s = line_.ReadNearest(static_cast<float>(taps_[i].delay_samples));
            // Per-tap high-frequency absorption. A real reflection loses treble
            // to both air and the surface it bounced off, and the later it
            // arrives the more it has lost. Flat taps are the main reason a
            // tap-based early field reads as metallic rather than as a room.
            absorb_[i] += absorb_k_[i] * (s - absorb_[i]);
            s = absorb_[i];
            out.left  += s * gain_l_[i];
            out.right += s * gain_r_[i];
        }
        line_.Write(input);
        return out;
    }

private:
    // ~20 ms at the 24 kHz reverb rate: fast enough to track a knob, slow
    // enough that a per-control-block rebuild is inaudible.
    static constexpr float kGainSlew = 0.0025f;

    DelayLineSdram line_;
    ErTap          taps_[MAX_TAPS]{};
    float          gain_l_[MAX_TAPS]{};
    float          gain_r_[MAX_TAPS]{};
    float          target_l_[MAX_TAPS]{};
    float          target_r_[MAX_TAPS]{};
    float          absorb_[MAX_TAPS]{};
    float          absorb_k_[MAX_TAPS]{};
    int            tap_count_ = 0;
    bool           seeded_ = false;
};

} // namespace pedal
