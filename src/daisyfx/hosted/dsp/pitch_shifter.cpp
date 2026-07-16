#include "pitch_shifter.h"
#include "fast_math.h"
#include <cmath>
#include <cstring>

namespace pedal {

void PitchShifter::Init(float* buf, size_t buf_size, float /*sample_rate*/) {
    buf_      = buf;
    buf_size_ = buf_size > 0 ? buf_size : GRAIN_SIZE * 2;
    if (buf_) std::memset(buf_, 0, buf_size_ * sizeof(float));
    write_pos_     = 0;
    read_pos_[0]   = 0.0f;
    read_pos_[1]   = static_cast<float>(GRAIN_SIZE) * 0.5f;  // 50% offset
    grain_phase_[0] = 0.0f;
    grain_phase_[1] = 0.5f;
    ratio_         = 1.0f;
}

void PitchShifter::Reset() {
    if (buf_) std::memset(buf_, 0, buf_size_ * sizeof(float));
    write_pos_      = 0;
    read_pos_[0]    = 0.0f;
    read_pos_[1]    = static_cast<float>(GRAIN_SIZE) * 0.5f;
    grain_phase_[0] = 0.0f;
    grain_phase_[1] = 0.5f;
}

void PitchShifter::SetShift(float semitones) {
    ratio_ = std::pow(2.0f, semitones / 12.0f);
    const float max_ratio = static_cast<float>(buf_size_) / static_cast<float>(GRAIN_SIZE);
    if (ratio_ > max_ratio) ratio_ = max_ratio;
}

float PitchShifter::ReadInterp(float pos) const {
    const float sz = static_cast<float>(buf_size_);
    while (pos >= sz) pos -= sz;
    while (pos < 0.0f) pos += sz;

    // 4-point Catmull-Rom / Hermite cubic — same kernel as DelayLineSdram.
    // Stencil: [i-1, i, i+1, i+2] — all wrapped circularly.
    const size_t i    = static_cast<size_t>(pos);
    const float  frac = pos - static_cast<float>(i);

    const float xm1 = buf_[(i == 0)             ? buf_size_ - 1 : i - 1];
    const float x0  = buf_[i];
    const float x1  = buf_[(i + 1 < buf_size_)  ? i + 1 : 0];
    const float x2  = buf_[(i + 2 < buf_size_)  ? i + 2 : i + 2 - buf_size_];

    // Horner's method evaluation of the cubic.
    const float c1 =  0.5f * (x1 - xm1);
    const float c2 =  xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = -0.5f * xm1 + 1.5f * x0 - 1.5f * x1 + 0.5f * x2;
    return x0 + frac * (c1 + frac * (c2 + frac * c3));
}

float PitchShifter::Process(float input) {
    if (!buf_) return input;

    buf_[write_pos_] = input;
    write_pos_ = (write_pos_ + 1 < buf_size_) ? write_pos_ + 1 : 0;

    float out = 0.0f;
    const float phase_inc = 1.0f / static_cast<float>(GRAIN_SIZE);

    for (int g = 0; g < 2; ++g) {
        // Hann window: peaks at phase=0.5, zero at phase=0 and 1.
        // Trig identity: 0.5 * (1 - cos(2*pi*x)) = sin(pi*x)^2.
        // We use fast_sin which is highly efficient.
        const float sin_val = fast_sin(grain_phase_[g] * 3.14159265359f);
        const float w = sin_val * sin_val;
        out += w * ReadInterp(read_pos_[g]);

        read_pos_[g]   += ratio_;
        grain_phase_[g] += phase_inc;

        if (grain_phase_[g] >= 1.0f) {
            grain_phase_[g] -= 1.0f;
            float restart = static_cast<float>(write_pos_);
            if (ratio_ > 1.0f) {
                restart -= (ratio_ - 1.0f) * static_cast<float>(GRAIN_SIZE) + 4.0f;
            }
            read_pos_[g] = restart;
        }
    }
    return out;
}

} // namespace pedal
