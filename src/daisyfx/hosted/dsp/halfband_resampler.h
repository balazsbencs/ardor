#pragma once

#include <array>
#include <cstddef>

namespace pedal {

// 31-tap, linear-phase half-band FIR for 48 kHz <-> 24 kHz conversion.
// The 9 kHz passband is essentially flat and aliases above the 24 kHz-stage
// Nyquist are attenuated by roughly 60 dB. It is deliberately stateful and
// fixed-size so the audio path performs no allocations.
class HalfbandDecimator2x {
public:
    static constexpr std::size_t kTaps = 31;

    void Reset() { history_.fill(0.0f); write_ = 0; phase_ = 0; }

    // Returns true every second host-rate sample and stores the 24 kHz output.
    bool Push(float input, float& output) {
        push(input);
        phase_ ^= 1U;
        if (phase_ != 0U) return false;
        output = convolve();
        return true;
    }

private:
    static constexpr std::array<float, kTaps> kCoeffs = {
        -0.0017003969f, 0.0f, 0.0029373316f, 0.0f, -0.0067300914f, 0.0f,
         0.0140938879f, 0.0f, -0.0267850358f, 0.0f, 0.0490989606f, 0.0f,
        -0.0969383328f, 0.0f, 0.3156195633f, 0.5008082269f, 0.3156195633f,
         0.0f, -0.0969383328f, 0.0f, 0.0490989606f, 0.0f, -0.0267850358f,
         0.0f, 0.0140938879f, 0.0f, -0.0067300914f, 0.0f, 0.0029373316f,
         0.0f, -0.0017003969f,
    };

    void push(float input) {
        history_[write_] = input;
        write_ = (write_ + 1U) % kTaps;
    }

    float convolve() const {
        float sum = 0.0f;
        for (std::size_t i = 0; i < kTaps; ++i) {
            const std::size_t index = (write_ + kTaps - 1U - i) % kTaps;
            sum += kCoeffs[i] * history_[index];
        }
        return sum;
    }

    std::array<float, kTaps> history_{};
    std::size_t write_ = 0;
    unsigned phase_ = 0;

    friend class HalfbandInterpolator2x;
};

class HalfbandInterpolator2x {
public:
    static constexpr std::size_t kTaps = HalfbandDecimator2x::kTaps;

    void Reset() { history_.fill(0.0f); write_ = 0; }

    std::array<float, 2> Process(float input) {
        push(input);
        const float even = 2.0f * convolve();
        push(0.0f);
        const float odd = 2.0f * convolve();
        return {even, odd};
    }

private:
    void push(float input) {
        history_[write_] = input;
        write_ = (write_ + 1U) % kTaps;
    }

    float convolve() const {
        float sum = 0.0f;
        for (std::size_t i = 0; i < kTaps; ++i) {
            const std::size_t index = (write_ + kTaps - 1U - i) % kTaps;
            sum += HalfbandDecimator2x::kCoeffs[i] * history_[index];
        }
        return sum;
    }

    std::array<float, kTaps> history_{};
    std::size_t write_ = 0;
};

} // namespace pedal
