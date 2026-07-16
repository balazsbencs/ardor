#pragma once
#include <cstddef>
#include <cstring>

namespace pedal {

class DelayLineSdram {
public:
    DelayLineSdram() = default;

    // buf must point to caller-owned storage; size is number of floats.
    void Init(float* buf, size_t size);
    void Reset();

    void  SetDelay(float delay_samples); // fractional delay; Hermite cubic interpolation
    void  Write(float sample);
    float Read() const;
    float ReadAt(float delay_samples) const;      // read arbitrary tap (Hermite, 4 reads)
    float ReadLinear(float delay_samples) const;  // read arbitrary tap (linear, 2 reads)
    float ReadNearest(float delay_samples) const; // read arbitrary tap (nearest, 1 read)

private:
    float*  buf_   = nullptr;
    size_t  size_  = 0;
    size_t  write_ = 0;
    size_t  delay_ = 2;
    float   frac_  = 0.0f;
};

} // namespace pedal
