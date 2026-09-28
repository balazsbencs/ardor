#include "delay_line_sdram.h"
#include <array>
#include <cmath>
#include <cstring>

namespace pedal {

namespace {

constexpr size_t kSincTaps = 16;
constexpr size_t kSincPhases = 256;
constexpr int kSincLeft = 7;
constexpr int kSincRight = 8;
constexpr float kPi = 3.14159265358979323846f;
constexpr size_t kResampleTaps = 32;
constexpr size_t kResampleBands = 9;

using SincTable = std::array<std::array<float, kSincTaps>, kSincPhases>;
using ResampleTable = std::array<std::array<std::array<float, kResampleTaps>, kSincPhases>, kResampleBands>;

const ResampleTable& resampleTable()
{
    static const ResampleTable table = [] {
        ResampleTable result{};
        for (size_t band = 0; band < kResampleBands; ++band) {
            const double rate = 1.0 + static_cast<double>(band) / 8.0;
            const double cutoff = 0.96 / rate;
            for (size_t phase = 0; phase < kSincPhases; ++phase) {
                const double fraction = static_cast<double>(phase) / kSincPhases;
                double sum = 0.0;
                for (size_t tap = 0; tap < kResampleTaps; ++tap) {
                    const double distance = static_cast<double>(tap) - 15.0 - fraction;
                    const double arg = cutoff * distance;
                    const double sinc = arg == 0.0 ? 1.0 : std::sin(kPi * arg) / (kPi * arg);
                    const double position = static_cast<double>(tap) / (kResampleTaps - 1);
                    const double window = 0.42 - 0.5 * std::cos(2.0 * kPi * position)
                                        + 0.08 * std::cos(4.0 * kPi * position);
                    const float coefficient = static_cast<float>(cutoff * sinc * window);
                    result[band][phase][tap] = coefficient;
                    sum += coefficient;
                }
                for (float& coefficient : result[band][phase]) {
                    coefficient = static_cast<float>(coefficient / sum);
                }
            }
        }
        return result;
    }();
    return table;
}

const SincTable& highQualityTable()
{
    // Constructed when the first delay line is initialized, never on the audio
    // thread. Quantizing the fractional phase to 1/256 sample limits timing
    // error to 0.041 us at 48 kHz while avoiding coefficient interpolation in
    // every delay tap.
    static const SincTable table = [] {
        SincTable result{};
        for (size_t phase = 0; phase < kSincPhases; ++phase) {
            const float fraction = static_cast<float>(phase) / static_cast<float>(kSincPhases);
            float sum = 0.0f;
            for (size_t tap = 0; tap < kSincTaps; ++tap) {
                const int node = static_cast<int>(tap) - kSincLeft;
                const float distance = static_cast<float>(node) - fraction;
                const float sinc = distance == 0.0f
                    ? 1.0f : std::sin(kPi * distance) / (kPi * distance);
                const float position = static_cast<float>(tap) /
                                       static_cast<float>(kSincTaps - 1);
                const float window = 0.42f - 0.5f * std::cos(2.0f * kPi * position)
                                   + 0.08f * std::cos(4.0f * kPi * position);
                result[phase][tap] = sinc * window;
                sum += result[phase][tap];
            }
            const float inverse = 1.0f / sum;
            for (float& coefficient : result[phase]) coefficient *= inverse;
        }
        return result;
    }();
    return table;
}

} // namespace

void DelayLineSdram::Init(float* buf, size_t size) {
    (void)highQualityTable();
    (void)resampleTable();
    buf_   = buf;
    size_  = size;
    write_ = 0;
    delay_ = 2;
    frac_  = 0.0f;
    Reset();
}

void DelayLineSdram::Reset() {
    if (buf_) std::memset(buf_, 0, size_ * sizeof(float));
    write_ = 0;
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

float DelayLineSdram::ReadHighQuality() const {
    return ReadAtHighQuality(static_cast<float>(delay_) + frac_);
}

float DelayLineSdram::ReadAtHighQuality(float delay_samples) const {
    if (size_ <= kSincTaps) return ReadAt(delay_samples);
    if (delay_samples < static_cast<float>(kSincLeft)) {
        delay_samples = static_cast<float>(kSincLeft);
    }
    const float maximum = static_cast<float>(size_ - 1U - kSincRight);
    if (delay_samples > maximum) delay_samples = maximum;

    size_t int_part = static_cast<size_t>(delay_samples);
    const float fraction = delay_samples - static_cast<float>(int_part);
    size_t phase = static_cast<size_t>(fraction * static_cast<float>(kSincPhases) + 0.5f);
    if (phase == 0U) return buf_[wrap_idx(write_, int_part, size_)];
    if (phase >= kSincPhases) {
        return buf_[wrap_idx(write_, int_part + 1U, size_)];
    }

    const auto& coefficients = highQualityTable()[phase];
    const size_t first = int_part - static_cast<size_t>(kSincLeft);
    float output = 0.0f;
    for (size_t tap = 0; tap < kSincTaps; ++tap) {
        output += coefficients[tap] * buf_[wrap_idx(write_, first + tap, size_)];
    }
    return output;
}

float DelayLineSdram::ReadAtResampled(float delay_samples, float playback_rate) const {
    if (playback_rate <= 1.0f || size_ <= kResampleTaps) {
        return ReadAtHighQuality(delay_samples);
    }
    if (delay_samples < 15.0f) delay_samples = 15.0f;
    const float maximum = static_cast<float>(size_ - 18U);
    if (delay_samples > maximum) delay_samples = maximum;
    const size_t integer = static_cast<size_t>(delay_samples);
    size_t phase = static_cast<size_t>((delay_samples - integer) * kSincPhases + 0.5f);
    size_t sample = integer;
    if (phase == kSincPhases) { phase = 0; ++sample; }
    const float band_position = fminf(playback_rate - 1.0f, 1.0f) * 8.0f;
    const size_t lower = static_cast<size_t>(band_position);
    const size_t upper = lower < 8 ? lower + 1 : lower;
    const float blend = band_position - lower;
    const auto& table = resampleTable();
    float output = 0.0f;
    const size_t first = sample - 15U;
    for (size_t tap = 0; tap < kResampleTaps; ++tap) {
        const float coefficient = table[lower][phase][tap] + blend *
            (table[upper][phase][tap] - table[lower][phase][tap]);
        output += coefficient * buf_[wrap_idx(write_, first + tap, size_)];
    }
    // Fade in the wider kernel to avoid a spectral step as the tap starts
    // moving faster than the write head.
    if (playback_rate < 1.05f) {
        const float fade = (playback_rate - 1.0f) / 0.05f;
        const float original = ReadAtHighQuality(delay_samples);
        output = original + fade * (output - original);
    }
    return output;
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
