#include "poly_octave_mode.h"
#include "../config/constants.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace pedal::mod_fx;

namespace pedal {

// ── ShelfBiquad factory methods ──────────────────────────────────────────────
// Coefficients match cycfi::q::config_highshelf / config_lowshelf exactly:
// beta = sqrt(A + A), not the bandwidth-parameterised alpha.

ShelfBiquad ShelfBiquad::make_highshelf(double db_gain, double freq_hz, double sps) noexcept
{
    const double A     = std::pow(10.0, db_gain / 40.0);
    const double beta  = std::sqrt(A + A);
    const double omega = 2.0 * std::numbers::pi_v<double> * freq_hz / sps;
    const double sinw  = std::sin(omega);
    const double cosw  = std::cos(omega);

    const double b0 =  A * ((A + 1) + (A - 1) * cosw + beta * sinw);
    const double b1 = -2.0 * A * ((A - 1) + (A + 1) * cosw);
    const double b2 =  A * ((A + 1) + (A - 1) * cosw - beta * sinw);
    const double a0 =      (A + 1) - (A - 1) * cosw + beta * sinw;
    const double a1 =  2.0 * ((A - 1) - (A + 1) * cosw);
    const double a2 =      (A + 1) - (A - 1) * cosw - beta * sinw;

    ShelfBiquad bq;
    bq.a0 = static_cast<float>(b0 / a0);
    bq.a1 = static_cast<float>(b1 / a0);
    bq.a2 = static_cast<float>(b2 / a0);
    bq.a3 = static_cast<float>(a1 / a0);
    bq.a4 = static_cast<float>(a2 / a0);
    return bq;
}

ShelfBiquad ShelfBiquad::make_lowshelf(double db_gain, double freq_hz, double sps) noexcept
{
    const double A     = std::pow(10.0, db_gain / 40.0);
    const double beta  = std::sqrt(A + A);
    const double omega = 2.0 * std::numbers::pi_v<double> * freq_hz / sps;
    const double sinw  = std::sin(omega);
    const double cosw  = std::cos(omega);

    const double b0 =  A * ((A + 1) - (A - 1) * cosw + beta * sinw);
    const double b1 =  2.0 * A * ((A - 1) - (A + 1) * cosw);
    const double b2 =  A * ((A + 1) - (A - 1) * cosw - beta * sinw);
    const double a0 =      (A + 1) + (A - 1) * cosw + beta * sinw;
    const double a1 = -2.0 * ((A - 1) + (A + 1) * cosw);
    const double a2 =      (A + 1) + (A - 1) * cosw - beta * sinw;

    ShelfBiquad bq;
    bq.a0 = static_cast<float>(b0 / a0);
    bq.a1 = static_cast<float>(b1 / a0);
    bq.a2 = static_cast<float>(b2 / a0);
    bq.a3 = static_cast<float>(a1 / a0);
    bq.a4 = static_cast<float>(a2 / a0);
    return bq;
}

// ── PolyOctaveMode ────────────────────────────────────────────────────────────

void PolyOctaveMode::Init()
{
    octave_gen_.init(SAMPLE_RATE / static_cast<float>(resample_factor));
    eq_high_ = ShelfBiquad::make_highshelf(-11.0, 140.0, SAMPLE_RATE);
    eq_low_  = ShelfBiquad::make_lowshelf(   5.0, 160.0, SAMPLE_RATE);
    tone_.Init();
    Reset();
}

void PolyOctaveMode::Reset()
{
    for (auto& s : in_buf_)  s = 0.0f;
    for (auto& s : out_buf_) s = 0.0f;
    decimator_.Reset();
    interpolator_.Reset();
    buf_idx_ = 0;
    out_idx_ = 0;
    eq_high_.reset();
    eq_low_.reset();
    tone_.Reset();
    up1_target_   = 0.0f;
    down1_target_ = 0.0f;
    down2_target_ = 0.0f;
    up1_level_    = 0.0f;
    down1_level_  = 0.0f;
    down2_level_  = 0.0f;
    tracking_coefficient_ = 1.0f;
}

void PolyOctaveMode::Prepare(const ParamSet& params)
{
    // p1 → octave up 1, p2 → octave down 1, depth → octave down 2.
    up1_target_   = params.p1;
    down1_target_ = params.p2;
    down2_target_ = params.depth;

    // Tone's bright half is a high-pass that reaches 3 kHz at full travel. This
    // mode generates sub-harmonic content — a 196 Hz note produces 49, 98 and
    // 392 Hz — so at the top of the knob almost nothing survives. Measured
    // output against the dry input: +8.1 dB at tone 0.5, -0.2 dB at 0.8,
    // -6.1 dB at 0.9, then -39.7 dB at 1.0. That last tenth of travel is a
    // 33 dB cliff and makes the effect sound broken rather than bright.
    //
    // Keep the dark half exactly as it was and compress only the bright half,
    // so the top of the knob thins the voices instead of deleting them.
    const float tone = params.tone <= 0.5f
        ? params.tone
        : 0.5f + (params.tone - 0.5f) * 0.76f;   // 1.0 maps to 0.88
    tone_.SetKnob(tone);
    // Speed is presented as Tracking: low settings deliberately soften note
    // attacks, while the top of the range stays effectively immediate.
    // This coefficient smooths the VOICE LEVELS, not the audio. Applying it to
    // the output sample (as this once did) is a one-pole low-pass on the signal
    // and rolled the mode off from 2.2 kHz even at the fastest setting.
    const float tracking = (params.speed - 0.05f) / 9.95f;
    tracking_coefficient_ = 0.0005f + std::clamp(tracking, 0.0f, 1.0f) * 0.25f;
}

StereoFrame PolyOctaveMode::Process(StereoFrame input, const ParamSet& /*params*/)
{
    in_buf_[buf_idx_++] = input.mono();

    // Slew the voice levels toward their targets. Tracking shapes how quickly a
    // voice swells in behind a note; it must never touch the audio itself.
    up1_level_   += tracking_coefficient_ * (up1_target_   - up1_level_);
    down1_level_ += tracking_coefficient_ * (down1_target_ - down1_level_);
    down2_level_ += tracking_coefficient_ * (down2_target_ - down2_level_);

    if (buf_idx_ == static_cast<int>(resample_factor))
    {
        const float decimated = decimator_(
            std::span<const float, resample_factor>{in_buf_, resample_factor});

        octave_gen_.update(decimated);
        const float wet = up1_level_   * octave_gen_.up1()
                        + down1_level_ * octave_gen_.down1()
                        + down2_level_ * octave_gen_.down2();

        const auto interp = interpolator_(wet);
        for (int j = 0; j < static_cast<int>(resample_factor); ++j)
            out_buf_[j] = eq_low_(eq_high_(interp[j]));

        buf_idx_ = 0;
        out_idx_ = 0;
    }

    const float shaped = tone_.Process(out_buf_[out_idx_++]);
    return {shaped, shaped};
}

} // namespace pedal
