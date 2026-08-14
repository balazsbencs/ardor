#pragma once
#include <array>
#include <cmath>
#include <cstddef>

namespace pedal {

// Log-spaced tan(pi*f/fs) table for controls that sweep a corner frequency at
// audio rate.
//
// Two problems it solves at once. First, calling tanf() per sample is an
// expensive libm call in the audio path. Second, and more audibly: modulating a
// filter or allpass coefficient linearly gives a wildly uneven sweep, because
// the coefficient is a compressed function of frequency. An LFO that moves
// evenly in coefficient races through the top of its range and crawls through
// the bottom. Analogue OTA and photocell circuits respond roughly exponentially
// in frequency, which is why they sweep evenly to the ear. Sweeping a
// normalised position through this table restores that.
//
// The table is built for the 48 kHz host rate. Every current user (Phaser,
// Vibe, FilterMode) is a modulation-slot effect and runs there.
namespace freq_table {

inline constexpr std::size_t kSize    = 257;
inline constexpr float       kLowHz   = 20.0f;
inline constexpr float       kOctaves = 10.0f;   // 20 Hz .. 20.48 kHz
inline constexpr float       kRate    = 48000.0f;

inline const std::array<float, kSize>& table()
{
    // Built once, off the audio thread, on first use.
    static const std::array<float, kSize> built = [] {
        std::array<float, kSize> result{};
        for (std::size_t i = 0; i < kSize; ++i) {
            const float position  = static_cast<float>(i) / static_cast<float>(kSize - 1);
            const float frequency = kLowHz * std::exp2(kOctaves * position);
            // Keep the corner below Nyquist so tan() stays finite and positive.
            const float safe = frequency > kRate * 0.49f ? kRate * 0.49f : frequency;
            result[i] = std::tan(3.14159265358979f * safe / kRate);
        }
        return result;
    }();
    return built;
}

// position in [0,1] maps logarithmically across kLowHz .. kLowHz * 2^kOctaves.
inline float g_at(float position)
{
    if (!(position > 0.0f)) position = 0.0f;   // also catches NaN
    if (position > 1.0f)    position = 1.0f;
    const float scaled = position * static_cast<float>(kSize - 1);
    const std::size_t lower = static_cast<std::size_t>(scaled);
    if (lower >= kSize - 1) return table()[kSize - 1];
    const float fraction = scaled - static_cast<float>(lower);
    const auto& t = table();
    return t[lower] + fraction * (t[lower + 1] - t[lower]);
}

// Maps a normalised sweep position to a first-order allpass coefficient using
// the sign convention the phaser and vibe stages expect: a in [-1, 0), with
// -0.99 near the bottom of the range and values approaching 0 at the top.
inline float allpass_coeff_at(float position)
{
    const float g = g_at(position);
    float a = -(1.0f - g) / (1.0f + g);
    if (a > -0.01f) a = -0.01f;
    if (a < -0.99f) a = -0.99f;
    return a;
}

// Converts an absolute frequency to the normalised position this table uses.
inline float position_for_hz(float hz)
{
    if (hz < kLowHz) hz = kLowHz;
    return std::log2(hz / kLowHz) / kOctaves;
}

} // namespace freq_table
} // namespace pedal
