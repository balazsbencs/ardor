#pragma once

#include <array>
#include <cstddef>

namespace pedal {

// 31-tap, linear-phase half-band FIR for 48 kHz <-> 24 kHz conversion.
// The 9 kHz passband is essentially flat and aliases above the 24 kHz-stage
// Nyquist are attenuated by roughly 60 dB. It is deliberately stateful and
// fixed-size so the audio path performs no allocations.
// Shared half-band kernel. Every odd-indexed tap is exactly zero except the
// centre — that is what makes it a half-band filter — so both classes below
// skip them and evaluate only the 8 non-zero symmetric pairs plus the centre.
// The history buffer is rounded up to a power of two so the circular index is
// a mask rather than an integer modulo.
namespace halfband {

inline constexpr std::size_t kTaps = 31;
inline constexpr std::size_t kHistory = 32;          // next power of two >= kTaps
inline constexpr std::size_t kHistoryMask = kHistory - 1U;
inline constexpr std::size_t kCentre = 15;
inline constexpr float kCentreCoeff = 0.5008082269f;

// The 8 non-zero coefficients at even offsets 0,2,4,...,14, each shared by the
// tap at that offset and its mirror at 30-offset.
inline constexpr std::array<float, 8> kEvenCoeffs = {
    -0.0017003969f, 0.0029373316f, -0.0067300914f, 0.0140938879f,
    -0.0267850358f, 0.0490989606f, -0.0969383328f,  0.3156195633f,
};

// Convolves the kernel against `history` whose most recent sample sits at
// index (write - 1) & mask. 9 multiplies instead of 31.
inline float convolve(const std::array<float, kHistory>& history, std::size_t write)
{
    const std::size_t newest = (write + kHistory - 1U) & kHistoryMask;
    float sum = kCentreCoeff * history[(newest + kHistory - kCentre) & kHistoryMask];
    for (std::size_t pair = 0; pair < kEvenCoeffs.size(); ++pair) {
        const std::size_t lo = pair * 2U;               // 0,2,...,14
        const std::size_t hi = kTaps - 1U - lo;         // 30,28,...,16
        const float a = history[(newest + kHistory - lo) & kHistoryMask];
        const float b = history[(newest + kHistory - hi) & kHistoryMask];
        sum += kEvenCoeffs[pair] * (a + b);
    }
    return sum;
}

} // namespace halfband

class HalfbandDecimator2x {
public:
    static constexpr std::size_t kTaps = halfband::kTaps;

    void Reset() { history_.fill(0.0f); write_ = 0; phase_ = 0; }

    // Returns true every second host-rate sample and stores the 24 kHz output.
    bool Push(float input, float& output) {
        push(input);
        phase_ ^= 1U;
        if (phase_ != 0U) return false;
        output = halfband::convolve(history_, write_);
        return true;
    }

private:
    void push(float input) {
        history_[write_] = input;
        write_ = (write_ + 1U) & halfband::kHistoryMask;
    }

    std::array<float, halfband::kHistory> history_{};
    std::size_t write_ = 0;
    unsigned phase_ = 0;
};

class HalfbandInterpolator2x {
public:
    static constexpr std::size_t kTaps = halfband::kTaps;

    void Reset() { history_.fill(0.0f); write_ = 0; }

    std::array<float, 2> Process(float input) {
        push(input);
        const float even = 2.0f * halfband::convolve(history_, write_);
        push(0.0f);
        const float odd = 2.0f * halfband::convolve(history_, write_);
        return {even, odd};
    }

private:
    void push(float input) {
        history_[write_] = input;
        write_ = (write_ + 1U) & halfband::kHistoryMask;
    }

    std::array<float, halfband::kHistory> history_{};
    std::size_t write_ = 0;
};

} // namespace pedal
