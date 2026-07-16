#pragma once

#include <array>
#include <cmath>

#include "band_shifter.h"

//=============================================================================
// Ported from terrarium-poly-octave/util/OctaveGenerator.h.
// Owns 80 parallel BandShifter instances, one per log-spaced frequency band.
// Call init() once before use (replaces the constructor parameter).
class OctaveGenerator
{
public:
    // Reduced from 80 to 48 bands (same frequency coverage, ~40% less CPU).
    // Frequency step adjusted from 0.027 to 0.0453 to preserve the 60–1650 Hz range.
    static constexpr int kNumBands = 48;

    OctaveGenerator() = default;

    void init(float sample_rate)
    {
        for (int i = 0; i < kNumBands; ++i)
        {
            const float center = centerFreq(i);
            const float bw     = bandwidth(i);
            _shifters[i] = BandShifter(center, sample_rate, bw);
        }
    }

    void update(float sample)
    {
        _up1   = 0;
        _down1 = 0;
        _down2 = 0;

        for (auto& shifter : _shifters)
        {
            shifter.update(sample);
            _up1   += shifter.up1();
            _down1 += shifter.down1();
            _down2 += shifter.down2();
        }
    }

    float up1()   const { return _up1;   }
    float down1() const { return _down1; }
    float down2() const { return _down2; }

private:
    // Log-spaced center frequencies from ~60 Hz to ~7 kHz.
    static float centerFreq(int n)
    {
        return 480.0f * std::pow(2.0f, 0.0453f * n) - 420.0f;
    }

    static float bandwidth(int n)
    {
        const float f0 = centerFreq(n - 1);
        const float f1 = centerFreq(n);
        const float f2 = centerFreq(n + 1);
        const float a  = f2 - f1;
        const float b  = f1 - f0;
        return 2.0f * (a * b) / (a + b);
    }

    std::array<BandShifter, kNumBands> _shifters;

    float _up1   = 0;
    float _down1 = 0;
    float _down2 = 0;
};
