#pragma once
#include <cstddef>

namespace pedal {

// Granular pitch shifter: two overlapping Hann-windowed grains.
// buf must be caller-owned float storage of size buf_size.
// Minimum recommended buf_size = GRAIN_SIZE * max(1, ratio_max).
class PitchShifter {
public:
    static constexpr size_t GRAIN_SIZE = 2048;

    void  Init(float* buf, size_t buf_size, float sample_rate = 48000.0f);
    void  Reset();
    void  SetShift(float semitones);  // range: -24..+24
    float Process(float input);

private:
    float  ReadInterp(float pos) const;

    float*  buf_         = nullptr;
    size_t  buf_size_    = 0;
    size_t  write_pos_   = 0;
    float   read_pos_[2] = {};
    float   grain_phase_[2] = {};
    float   ratio_       = 1.0f;
};

} // namespace pedal
