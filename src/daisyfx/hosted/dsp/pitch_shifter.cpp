#include "pitch_shifter.h"
#include "fast_math.h"
#include <cmath>
#include <cstring>

namespace pedal {

void PitchShifter::Init(float* buf, size_t buf_size, float sample_rate,
                        size_t grain_size)
{
    sample_rate_ = (sample_rate > 0.0f && std::isfinite(sample_rate)) ? sample_rate : 48000.0f;
    grain_size_  = grain_size > 16 ? grain_size : 16;
    buf_      = buf;
    buf_size_ = buf_size > 0 ? buf_size : grain_size_ * 2;
    if (grain_size_ > buf_size_ / 2) grain_size_ = buf_size_ / 2;
    if (buf_) std::memset(buf_, 0, buf_size_ * sizeof(float));
    write_pos_ = 0;
    for (int g = 0; g < GRAINS; ++g) {
        const float offset = static_cast<float>(g) / static_cast<float>(GRAINS);
        read_pos_[g]    = static_cast<float>(grain_size_) * offset;
        grain_phase_[g] = offset;
    }
    ratio_     = 1.0f;
    aa_state1_ = 0.0f;
    aa_state2_ = 0.0f;
    UpdateAntiAlias();
}

void PitchShifter::Reset() {
    if (buf_) std::memset(buf_, 0, buf_size_ * sizeof(float));
    write_pos_ = 0;
    for (int g = 0; g < GRAINS; ++g) {
        const float offset = static_cast<float>(g) / static_cast<float>(GRAINS);
        read_pos_[g]    = static_cast<float>(grain_size_) * offset;
        grain_phase_[g] = offset;
    }
    aa_state1_ = 0.0f;
    aa_state2_ = 0.0f;
}

float PitchShifter::clampRatio(float ratio) const {
    if (!std::isfinite(ratio) || ratio <= 0.0f) return 1.0f;
    // The reader must not lap the writer within one grain.
    const float maximum = static_cast<float>(buf_size_) / static_cast<float>(grain_size_);
    if (ratio > maximum) return maximum;
    if (ratio < 0.05f) return 0.05f;
    return ratio;
}

void PitchShifter::SetShift(float semitones) {
    ratio_ = clampRatio(std::pow(2.0f, semitones / 12.0f));
    UpdateAntiAlias();
}

// Corner at fs/(2*ratio) - the highest frequency that survives the decimation
// the upward read performs. A downward shift interpolates instead, so nothing
// folds and the filter is bypassed.
void PitchShifter::UpdateAntiAlias() {
    if (ratio_ <= 1.0f) { aa_coeff_ = 1.0f; return; }
    const float cutoff = 0.5f * sample_rate_ / ratio_;
    const float k = 1.0f - std::exp(-6.2831853f * cutoff / sample_rate_);
    aa_coeff_ = k < 0.02f ? 0.02f : (k > 1.0f ? 1.0f : k);
}

float PitchShifter::At(long index) const {
    const long size = static_cast<long>(buf_size_);
    long i = index % size;
    if (i < 0) i += size;
    return buf_[static_cast<size_t>(i)];
}

// Waveform-similarity overlap-add. Compares the kMatchWindow samples leading
// into each candidate restart position against those leading into the point the
// outgoing grain reached, and returns the best match.
//
// The search only ever moves the restart EARLIER. Moving it later would shrink
// the gap to the write head, and for a small upward ratio the reader would
// overtake the writer partway through the grain and read samples that have not
// been written yet.
float PitchShifter::FindRestart(float nominal, float trailing_from) const {
    const long target = static_cast<long>(trailing_from);
    const long start  = static_cast<long>(nominal);

    // Reference energy of the outgoing context.
    float reference[kMatchWindow / kMatchStride];
    int taps = 0;
    for (int i = kMatchWindow; i > 0; i -= kMatchStride) {
        reference[taps++] = At(target - i);
    }

    // Normalise by the candidate's energy so a loud region cannot win on level
    // alone; this is a similarity measure, not a raw correlation peak.
    const auto score_at = [&](long candidate) {
        float dot = 0.0f;
        float energy = 1.0e-9f;
        int t = 0;
        for (int i = kMatchWindow; i > 0; i -= kMatchStride) {
            const float b = At(candidate - i);
            dot += reference[t++] * b;
            energy += b * b;
        }
        return dot / std::sqrt(energy);
    };

    // Coarse pass then a local refine. Scanning every candidate put ~45k
    // multiply-accumulates into the single sample where a grain restarts, which
    // showed up as a p99 seven times the median. The correlation peak is at
    // least a quarter-period wide, so a stride of 8 cannot step over it for any
    // pitch this is used at, and the refine recovers the exact position.
    float best_score = -1.0e30f;
    long  best_offset = 0;
    for (long back = 0; back <= kSearchSpan; back += kCoarseStride) {
        const float score = score_at(start - back);
        if (score > best_score) {
            best_score = score;
            best_offset = back;
        }
    }
    const long lo = best_offset - kCoarseStride + 1;
    const long hi = best_offset + kCoarseStride - 1;
    for (long back = lo; back <= hi; ++back) {
        if (back < 0 || back > kSearchSpan) continue;
        const float score = score_at(start - back);
        if (score > best_score) {
            best_score = score;
            best_offset = back;
        }
    }
    return nominal - static_cast<float>(best_offset);
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

    if (aa_coeff_ < 1.0f) {
        aa_state1_ += aa_coeff_ * (input - aa_state1_);
        aa_state2_ += aa_coeff_ * (aa_state1_ - aa_state2_);
        input = aa_state2_;
    }

    buf_[write_pos_] = input;
    write_pos_ = (write_pos_ + 1 < buf_size_) ? write_pos_ + 1 : 0;

    float out = 0.0f;
    const float grain = static_cast<float>(grain_size_);
    const float phase_inc = 1.0f / grain;

    for (int g = 0; g < GRAINS; ++g) {
        // Hann window: peaks at phase=0.5, zero at phase=0 and 1.
        // Trig identity: 0.5 * (1 - cos(2*pi*x)) = sin(pi*x)^2.
        const float sin_val = fast_sin(grain_phase_[g] * 3.14159265359f);
        const float w = sin_val * sin_val;
        out += w * ReadInterp(read_pos_[g]);

        read_pos_[g]    += ratio_;
        grain_phase_[g] += phase_inc;

        if (grain_phase_[g] >= 1.0f) {
            grain_phase_[g] -= 1.0f;
            // Align the restarting grain with the partner it is about to overlap,
            // not with its own history. A grain restarts as the other one passes
            // its window peak, and the two then advance at the same rate, so
            // matching the partner's current read position keeps them coherent
            // for the whole overlap. Matching a grain against itself leaves the
            // pair free to land in anti-phase, which cancels the carrier and
            // leaves the output as a pair of sidebands at +/- the grain rate.
            const float partner = read_pos_[g ^ 1];
            float restart = static_cast<float>(write_pos_);
            if (ratio_ > 1.0f) {
                restart -= (ratio_ - 1.0f) * grain + 4.0f;
            }
            const float jump = std::fabs(restart - partner);
            read_pos_[g] = (jump >= kMinJumpForSearch)
                ? FindRestart(restart, partner)
                : restart;
        }
    }
    return out;
}

} // namespace pedal
