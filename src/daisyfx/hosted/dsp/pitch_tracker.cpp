#include "pitch_tracker.h"

#include <cmath>

namespace pedal {

void PitchTracker::Init(float sample_rate)
{
    sample_rate_ = (sample_rate > 0.0f && std::isfinite(sample_rate)) ? sample_rate : 48000.0f;
    analysis_rate_ = sample_rate_ / static_cast<float>(kDecimation);
    // Two poles at about 1 kHz: high enough to leave every guitar fundamental
    // intact, low enough to keep harmonics from dominating the difference.
    lp_coeff_ = 1.0f - std::exp(-6.2831853f * 1000.0f / sample_rate_);
    Reset();
}

void PitchTracker::Reset()
{
    lp1_ = 0.0f;
    lp2_ = 0.0f;
    decimate_count_ = 0;
    write_ = 0;
    lag_ = 0;
    running_ = false;
    since_pass_ = 0;
    frequency_ = 0.0f;
    confidence_ = 0.0f;
    voiced_ = false;
    for (float& value : history_) value = 0.0f;
    for (float& value : snapshot_) value = 0.0f;
    for (float& value : difference_) value = 0.0f;
}

void PitchTracker::startPass()
{
    // Freeze the window so the search sees a consistent signal while it is
    // spread over the next few milliseconds.
    // Two straight copies rather than a modulo per element: the ring is exactly
    // the window length, so the oldest sample is the one at write_.
    const int head = kHistory - write_;
    for (int i = 0; i < head; ++i) snapshot_[i] = history_[write_ + i];
    for (int i = 0; i < write_; ++i) snapshot_[head + i] = history_[i];
    lag_ = kMinLag;
    running_ = true;
}

void PitchTracker::stepPass()
{
    // One lag per decimated sample.
    float sum = 0.0f;
    for (int j = 0; j < kWindow; ++j) {
        const float delta = snapshot_[j] - snapshot_[j + lag_];
        sum += delta * delta;
    }
    difference_[lag_] = sum;
    ++lag_;
    if (lag_ > kMaxLag) finishPass();
}

void PitchTracker::finishPass()
{
    running_ = false;

    // Cumulative mean normalization. Dividing each lag by the running mean of
    // the lags below it is what stops the octave-below from winning, because a
    // true period's dip is deep relative to everything shorter than it.
    float runningSum = 0.0f;
    float best = 1.0e30f;
    int bestLag = kMinLag;
    for (int lag = kMinLag; lag <= kMaxLag; ++lag) {
        runningSum += difference_[lag];
        const float mean = runningSum / static_cast<float>(lag - kMinLag + 1);
        difference_[lag] = mean > 1.0e-12f ? difference_[lag] / mean : 1.0f;
        if (difference_[lag] < best) {
            best = difference_[lag];
            bestLag = lag;
        }
    }

    // Absolute threshold: take the first lag that dips below it, then walk to
    // the bottom of that dip. Taking the global minimum instead would happily
    // return a multiple of the period — the octave-down error this method
    // exists to avoid.
    int chosen = 0;
    for (int lag = kMinLag; lag <= kMaxLag; ++lag) {
        if (difference_[lag] >= kThreshold) continue;
        while (lag + 1 <= kMaxLag && difference_[lag + 1] < difference_[lag]) ++lag;
        chosen = lag;
        break;
    }
    if (chosen == 0) chosen = bestLag;

    if (chosen < kMinLag || best > 0.6f) {
        // Nothing convincing. Hold the previous note instead of reporting
        // silence, so a decaying string does not make the harmony jump.
        voiced_ = false;
        confidence_ = 0.0f;
        return;
    }

    // Parabolic interpolation around the minimum for sub-sample resolution;
    // without it the quantised lag is worth tens of cents at the top of the range.
    float period = static_cast<float>(chosen);
    if (chosen > kMinLag && chosen < kMaxLag) {
        const float a = difference_[chosen - 1];
        const float b = difference_[chosen];
        const float c = difference_[chosen + 1];
        // Vertex of the parabola through (-1,a), (0,b), (1,c). A higher left
        // neighbour means the true minimum lies to the right, so the sign of
        // the denominator matters: inverted, this pushes the estimate the wrong
        // way and roughly doubles the error instead of removing it.
        const float denominator = 2.0f * (a - 2.0f * b + c);
        if (std::fabs(denominator) > 1.0e-9f) {
            const float offset = (a - c) / denominator;
            if (offset > -1.0f && offset < 1.0f) period += offset;
        }
    }

    if (period > 0.5f) {
        frequency_ = analysis_rate_ / period;
        confidence_ = 1.0f - best;
        voiced_ = true;
    }
}

void PitchTracker::Push(float sample)
{
    if (!std::isfinite(sample)) sample = 0.0f;

    lp1_ += lp_coeff_ * (sample - lp1_);
    lp2_ += lp_coeff_ * (lp1_ - lp2_);

    if (++decimate_count_ < kDecimation) return;
    decimate_count_ = 0;

    history_[write_] = lp2_;
    write_ = (write_ + 1) % kHistory;

    if (running_) {
        stepPass();
        return;
    }

    // Start a new pass once enough fresh audio has arrived to be worth it.
    if (++since_pass_ >= kMaxLag) {
        since_pass_ = 0;
        startPass();
    }
}

} // namespace pedal
