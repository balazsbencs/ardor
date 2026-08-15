#pragma once

#include <cstddef>

namespace pedal {

// Monophonic pitch tracker for the audio thread.
//
// ardor::TunerAnalyzer already does this job well, but it is built for the
// control thread: it allocates, and it works in 1024-sample hops. A harmoniser
// needs the estimate inside the audio path, so this is a fixed-size version
// with the work spread across calls.
//
// The method is YIN's cumulative mean normalized difference, which is chosen
// over plain autocorrelation because it does not favour the octave below — a
// harmoniser that drops an octave on a strummed note is worse than one that
// briefly holds its previous answer.
//
// Cost is one lag of the difference function per decimated sample, so the whole
// search is spread over about 15 ms rather than landing in a single callback.
class PitchTracker {
public:
    void Init(float sample_rate);
    void Reset();

    // Call once per input sample at the host rate.
    void Push(float sample);

    // 0 when nothing convincing is being tracked. The previous estimate is held
    // rather than dropped, so a harmoniser does not lurch between notes when a
    // string decays.
    float FrequencyHz() const { return frequency_; }
    bool Voiced() const { return voiced_; }
    float Confidence() const { return confidence_; }

private:
    // 48 kHz / 4 = 12 kHz. Decimating keeps the difference function short, but
    // not too far: the lag grid sets how finely the period can be resolved, and
    // at 8 kHz a note near the top of the neck lands on a grid step worth 90
    // cents. Interpolation recovers most of that, but measured error still
    // reached 43 cents, which is uncomfortably close to rounding a note to the
    // wrong semitone. 12 kHz brings it inside 20.
    static constexpr int kDecimation = 4;
    // 576 samples at 12 kHz is 48 ms, about four periods of a low E.
    static constexpr int kWindow = 576;
    // 63 Hz to 1333 Hz, which covers a dropped-D low string up to the dusty end.
    static constexpr int kMinLag = 9;
    static constexpr int kMaxLag = 192;
    static constexpr int kHistory = kWindow + kMaxLag;
    // Below this, YIN considers the period confidently found.
    static constexpr float kThreshold = 0.15f;

    void startPass();
    void stepPass();
    void finishPass();

    float sample_rate_ = 48000.0f;
    float analysis_rate_ = 8000.0f;

    // Anti-alias before decimating. Two poles near 1 kHz also suppress the upper
    // harmonics that make a difference function pick the wrong period.
    float lp1_ = 0.0f;
    float lp2_ = 0.0f;
    float lp_coeff_ = 0.2f;
    int   decimate_count_ = 0;

    float history_[kHistory] = {};
    int   write_ = 0;

    float snapshot_[kHistory] = {};
    float difference_[kMaxLag + 1] = {};
    int   lag_ = 0;
    bool  running_ = false;
    int   since_pass_ = 0;

    float frequency_ = 0.0f;
    float confidence_ = 0.0f;
    bool  voiced_ = false;
};

} // namespace pedal
