#include "delay_line_sdram.h"
#include <cstring>

namespace pedal {

void DelayLineSdram::Init(float* buf, size_t size) {
    buf_   = buf;
    size_  = size;
    Reset();
}

void DelayLineSdram::Reset() {
    if (buf_) std::memset(buf_, 0, size_ * sizeof(float));
    write_ = 0;
    delay_ = 2;
    frac_  = 0.0f;
}

void DelayLineSdram::SetDelay(float delay_samples) {
    if (delay_samples < 2.0f) delay_samples = 2.0f;
    size_t int_part = static_cast<size_t>(delay_samples);
    frac_  = delay_samples - static_cast<float>(int_part);
    if (int_part < 2)          int_part = 2;
    // Integer delays use the Read() fast path which only accesses index d → max is size_-1.
    // Fractional delays use Hermite interpolation reading d-1..d+2 → max is size_-3.
    const size_t max_int = (frac_ == 0.0f) ? (size_ - 1) : (size_ - 3);
    if (int_part > max_int)    int_part = max_int;
    delay_ = int_part;
}

void DelayLineSdram::Write(float sample) {
    buf_[write_] = sample;
    write_ = (write_ == 0) ? size_ - 1 : write_ - 1;
}

// Requires base + offset < 2 * size (guaranteed by SetDelay/ReadAt clamps).
static inline size_t wrap_idx(size_t base, size_t offset, size_t size) {
    size_t i = base + offset;
    if (i >= size) i -= size;
    return i;
}

float DelayLineSdram::Read() const {
    const size_t d = delay_;  // clamped to [2, size_-3] by SetDelay
    const float  t = frac_;
    // Fast path for integer delays (frac_==0): allpass filters always land here.
    if (t == 0.0f) return buf_[wrap_idx(write_, d, size_)];
    const float xm1 = buf_[wrap_idx(write_, d - 1, size_)];
    const float x0  = buf_[wrap_idx(write_, d,     size_)];
    const float x1  = buf_[wrap_idx(write_, d + 1, size_)];
    const float x2  = buf_[wrap_idx(write_, d + 2, size_)];
    const float c1  = 0.5f * (x1 - xm1);
    const float c2  = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3  = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

float DelayLineSdram::ReadAt(float delay_samples) const {
    if (delay_samples < 2.0f) delay_samples = 2.0f;
    size_t int_part = static_cast<size_t>(delay_samples);
    const float t   = delay_samples - static_cast<float>(int_part);
    if (int_part > size_ - 3)  int_part = size_ - 3;
    const float xm1 = buf_[wrap_idx(write_, int_part - 1, size_)];
    const float x0  = buf_[wrap_idx(write_, int_part,     size_)];
    const float x1  = buf_[wrap_idx(write_, int_part + 1, size_)];
    const float x2  = buf_[wrap_idx(write_, int_part + 2, size_)];
    const float c0  = x0;
    const float c1  = 0.5f * (x1 - xm1);
    const float c2  = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3  = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

float DelayLineSdram::ReadNearest(float delay_samples) const {
    if (delay_samples < 1.0f) delay_samples = 1.0f;
    size_t int_part = static_cast<size_t>(delay_samples + 0.5f);
    if (int_part > size_ - 1) int_part = size_ - 1;
    return buf_[wrap_idx(write_, int_part, size_)];
}

float DelayLineSdram::ReadLinear(float delay_samples) const {
    if (delay_samples < 1.0f) delay_samples = 1.0f;
    size_t int_part = static_cast<size_t>(delay_samples);
    const float t   = delay_samples - static_cast<float>(int_part);
    if (int_part > size_ - 2) int_part = size_ - 2;
    const float x0  = buf_[wrap_idx(write_, int_part,     size_)];
    const float x1  = buf_[wrap_idx(write_, int_part + 1, size_)];
    return x0 + t * (x1 - x0);
}

} // namespace pedal
