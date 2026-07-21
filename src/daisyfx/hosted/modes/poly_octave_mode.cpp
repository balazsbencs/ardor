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
    buf_idx_ = 0;
    out_idx_ = 0;
    eq_high_.reset();
    eq_low_.reset();
    tone_.Reset();
    up1_level_   = 0.0f;
    down1_level_ = 0.0f;
    down2_level_ = 0.0f;
    tracking_ = 0.0f;
    tracking_coefficient_ = 1.0f;
}

void PolyOctaveMode::Prepare(const ParamSet& params)
{
    // p1 → octave up 1, p2 → octave down 1, depth → octave down 2.
    up1_level_   = params.p1;
    down1_level_ = params.p2;
    down2_level_ = params.depth;
    tone_.SetKnob(params.tone);
    // Speed is presented as Tracking: low settings deliberately soften note
    // attacks, while the existing default stays effectively immediate.
    const float tracking = (params.speed - 0.05f) / 9.95f;
    tracking_coefficient_ = 0.0005f + std::clamp(tracking, 0.0f, 1.0f) * 0.25f;
}

StereoFrame PolyOctaveMode::Process(StereoFrame input, const ParamSet& /*params*/)
{
    in_buf_[buf_idx_++] = input.mono();

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

    const float out = out_buf_[out_idx_++];
    tracking_ += tracking_coefficient_ * (out - tracking_);
    const float shaped = tone_.Process(tracking_);
    return {shaped, shaped};
}

} // namespace pedal
