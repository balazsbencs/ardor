#pragma once

#include "mod_mode.h"
#include "../dsp/multirate.h"
#include "../dsp/octave_generator.h"
#include "../dsp/tone_filter.h"

namespace pedal {

// Biquad with default constructor — Q's biquad lacks one, so we use our own.
struct ShelfBiquad
{
    float a0 = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, a4 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

    float operator()(float s) noexcept
    {
        const float r = a0*s + a1*x1 + a2*x2 - a3*y1 - a4*y2;
        x2 = x1; x1 = s;
        y2 = y1; y1 = r;
        return r;
    }

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

    // Exact same formula as cycfi::q::config_highshelf (beta = sqrt(A+A)).
    static ShelfBiquad make_highshelf(double db_gain, double freq_hz, double sps) noexcept;
    static ShelfBiquad make_lowshelf(double db_gain, double freq_hz, double sps) noexcept;
};

//=============================================================================
class PolyOctaveMode : public ModMode
{
public:
    void Init()   override;
    void Reset()  override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "PolyOctave"; }

private:
    Decimator       decimator_;
    Interpolator    interpolator_;
    OctaveGenerator octave_gen_;

    ShelfBiquad eq_high_;   // -11 dB high shelf @ 140 Hz
    ShelfBiquad eq_low_;    //  +5 dB low  shelf @ 160 Hz
    ToneFilter tone_;

    float in_buf_[resample_factor]  = {};
    int   buf_idx_                  = 0;
    float out_buf_[resample_factor] = {};
    int   out_idx_                  = 0;

    float up1_level_   = 0.0f;
    float down1_level_ = 0.0f;
    float down2_level_ = 0.0f;
    float tracking_ = 0.0f;
    float tracking_coefficient_ = 1.0f;
};

} // namespace pedal
