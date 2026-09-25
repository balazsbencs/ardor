#include "rotary_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"
#include "../dsp/fast_math.h"
#include <cmath>

using namespace pedal::mod_fx;

namespace pedal {

// Doppler swing at full Depth, in samples. A Leslie horn mouth turns on a
// radius near 0.17 m, so its path length changes by about +/-0.5 ms: at the
// fast 400 rpm that is a +/-2 % pitch swing. The drum's baffle deflects a
// fixed woofer and moves the pitch less. The earlier +/-90 and +/-180 sample
// swings gave close to a semitone of vibrato on fast, far from the real thing.
static constexpr float kHornSwing = 24.0f;
static constexpr float kDrumSwing = 12.0f;

// Both rotors read around the same centre, just clear of the 7-sample reach
// of the sinc read. Equal centres keep the two bands time-aligned at rest,
// and the short centre keeps latency near 0.7 ms.
static constexpr float kRotorCenter = 8.0f + kHornSwing;

// Motor inertia: ramp coefficient per Prepare() call (once per BLOCK_SIZE samples).
// Horn τ ≈ 1.2 s → coef = 1 − exp(−BLOCK_SIZE / (SAMPLE_RATE × 1.2))
static constexpr float kHornRampCoef = 8.33e-4f;
// Drum τ ≈ 2.5 s → coef = 1 − exp(−BLOCK_SIZE / (SAMPLE_RATE × 2.5))
static constexpr float kDrumRampCoef = 4.00e-4f;

// Leslie 122 chorale (slow) speeds: horn near 50 rpm, drum near 40 rpm.
static constexpr float kHornChorale = 0.83f;
static constexpr float kDrumChorale = 0.67f;
// On fast the drum turns near 340 rpm while the horn turns near 400 rpm.
static constexpr float kDrumFastRatio = 0.85f;

// Horn cabinet BP resonance mix level (0 = off, 1 = full parallel add). The
// band-pass peaks at Q = 1.5, so 0.17 gives a +2 dB honk. The earlier 0.3 gave
// +3.2 dB, where guitar pick attacks carry much of their energy, and raised
// transient peaks by 2.6 dB once the band sum was restored to unity.
static constexpr float kHornColorAmt = 0.17f;

// Ceiling for the amplitude-modulation make-up. Full RMS make-up at full
// Depth lifts the horn's loudest point by +2.5 dB on top of the honk; +1.5 dB
// keeps the level within about half a decibel of bypass and the peaks near
// the input's, as Vintage Trem's capped make-up does.
static constexpr float kMaxAmMakeup = 1.19f;

static constexpr float kTwoPi = 6.28318530717958647692f;

// RMS make-up for a raised-sine amplitude modulator 1 - d*(0.5 + 0.5*sin).
// Its mean is 1 - d/2 and its mean square adds d^2/8.
static float am_makeup(float d) {
    const float mean = 1.0f - 0.5f * d;
    const float mean_square = mean * mean + 0.125f * d * d;
    const float makeup = mean_square > 0.0001f ? 1.0f / std::sqrt(mean_square) : 1.0f;
    return makeup > kMaxAmMakeup ? kMaxAmMakeup : makeup;
}

void RotaryMode::Init() {
    for (auto& feed : feeds_) {
        feed.horn_line.Init(feed.horn_buf, kHornBufSize);
        feed.drum_line.Init(feed.drum_buf, kDrumBufSize);
    }

    actual_horn_rate_ = kHornChorale;
    actual_drum_rate_ = kDrumChorale;

    horn_lfo_.Init(kHornChorale, LfoWave::Sine);
    horn_lfo_.SetJitter(0.1f);
    horn_lfo_q_.Init(kHornChorale, LfoWave::Sine);
    horn_lfo_q_.SetPhaseOffset(kTwoPi * 0.25f);
    horn_lfo_q_.Reset();

    drum_lfo_.Init(kDrumChorale, LfoWave::Sine);
    drum_lfo_.SetJitter(0.1f);
    drum_lfo_q_.Init(kDrumChorale, LfoWave::Sine);
    drum_lfo_q_.SetPhaseOffset(kTwoPi * 0.25f);
    drum_lfo_q_.Reset();

    // Butterworth sections for the LR4 crossover; frequency set in Prepare()
    for (auto& feed : feeds_) {
        for (auto& section : feed.xover) {
            section.SetQ(0.70710678f);
            section.SetFreq(800.0f);
        }
    }

    // Horn cabinet resonance: Q fixed here, frequency updated each Prepare() via tone
    horn_color_l_.SetQ(1.5f);
    horn_color_l_.SetFreq(2500.0f);
    horn_color_r_.SetQ(1.5f);
    horn_color_r_.SetFreq(2500.0f);

    drive_.Init(WaveCurve::Tube);
    dc_l_.Init();
    dc_r_.Init();
}

void RotaryMode::Reset() {
    for (auto& feed : feeds_) {
        feed.horn_line.Reset();
        feed.drum_line.Reset();
        for (auto& section : feed.xover) section.Reset();
    }
    horn_lfo_.Reset();
    horn_lfo_q_.Reset();
    drum_lfo_.Reset();
    drum_lfo_q_.Reset();
    actual_horn_rate_ = kHornChorale;
    actual_drum_rate_ = kDrumChorale;
    horn_color_l_.Reset();
    horn_color_r_.Reset();
    dc_l_.Init();
    dc_r_.Init();
}

void RotaryMode::Prepare(const ParamSet& params) {
    // P2 ≥ 0.5 → tremolo (fast); P2 < 0.5 → chorale (slow).
    // Speed param sets the fast horn target; drum is always at the Leslie ratio.
    const float target_horn = (params.p2 >= 0.5f) ? params.speed : kHornChorale;
    const float target_drum = (params.p2 >= 0.5f) ? params.speed * kDrumFastRatio : kDrumChorale;

    // Exponential smoothing toward target — motor inertia
    actual_horn_rate_ += (target_horn - actual_horn_rate_) * kHornRampCoef;
    actual_drum_rate_ += (target_drum - actual_drum_rate_) * kDrumRampCoef;

    horn_lfo_.SetRate(actual_horn_rate_);
    horn_lfo_q_.SetRate(actual_horn_rate_);
    drum_lfo_.SetRate(actual_drum_rate_);
    drum_lfo_q_.SetRate(actual_drum_rate_);

    // Tone maps to crossover frequency: 0 → 500 Hz, 1 → 2000 Hz
    for (auto& feed : feeds_) {
        for (auto& section : feed.xover) section.SetFreq(500.0f + params.tone * 1500.0f);
    }

    // Horn cabinet resonance tracks tone: 0 → 1.8 kHz (warm), 1 → 3.5 kHz (bright)
    const float horn_fc = 1800.0f + params.tone * 1700.0f;
    horn_color_l_.SetFreq(horn_fc);
    horn_color_r_.SetFreq(horn_fc);

    // Drive models the Leslie 122 preamp pushed hard. Up to 6.4x into the
    // tube curve; the earlier 1.6x ceiling could not be heard at all.
    drive_.SetDrive(params.p1 * 0.6f);
    // Blend into the shaper over the first 10 % of the control, so Drive at
    // zero stays exactly clean and there is no step when it comes on.
    drive_blend_ = params.p1 * 10.0f > 1.0f ? 1.0f : params.p1 * 10.0f;
    const float drive_gain = 1.0f + 0.36f * params.p1 * params.p1 * 15.0f;
    // Hold the level of a typical guitar peak (0.3) steady across the range.
    drive_makeup_ = 0.3f / soft_clip_tanh(0.3f * drive_gain);
    if (drive_makeup_ > 1.0f / 0.3f) drive_makeup_ = 1.0f / 0.3f;

    // Cache Depth-derived modulation depths for per-sample use
    am_depth_ = params.depth * 0.65f;   // Leslie 122: horn sweeps ~65% amplitude
    horn_mod_ = params.depth * kHornSwing;
    drum_mod_ = params.depth * kDrumSwing;
    // Amplitude modulation takes level away on average. Restore the RMS of
    // each band's modulator, as Vintage Trem does, so engaging the rotary or
    // raising Depth does not drop the level.
    horn_am_makeup_ = am_makeup(am_depth_);
    drum_am_makeup_ = am_makeup(am_depth_ * 0.6f);

    // Balance (p3): turns one rotor down at a time. 0.5 plays both at full
    // level; 0 leaves only the drum and 1 only the horn.
    horn_level_ = params.p3 * 2.0f > 1.0f ? 1.0f : params.p3 * 2.0f;
    drum_level_ = (1.0f - params.p3) * 2.0f > 1.0f ? 1.0f : (1.0f - params.p3) * 2.0f;

    // Mic Spread (p4): the angle between the two mics, 90 to 180 degrees.
    // 90 is the classic close pair; 180 puts them on opposite sides, where
    // the Doppler of the two channels runs in opposition for the widest image.
    const float spread = kTwoPi * (0.25f + 0.25f * params.p4);
    horn_lfo_q_.SetPhaseOffset(spread);
    drum_lfo_q_.SetPhaseOffset(spread);
}

void RotaryMode::WriteFeed(Feed& feed, float input) {
    float x = input;
    if (drive_blend_ > 0.0f) {
        const float driven = drive_.Process(x) * drive_makeup_;
        x += drive_blend_ * (driven - x);
    }

    // Linkwitz-Riley crossover: LP -> drum band, HP -> horn band.
    //
    // The two LR4 bands are in phase at every frequency and their magnitudes
    // sum to exactly one, so no relative delay between the rotors can add them
    // above the input. The earlier split took the horn as input minus a
    // 2-pole low pass. That reconstructs the input only while both bands are
    // time-aligned; its horn band had a +1 dB bump and a 60 degree lead at
    // the crossover, so once the rotors moved the bands apart they could sum
    // to +4.2 dB over the input and pushed guitar peaks close to full scale.
    // (A single SVF's lp() + hp() is worse still: it nulls at the crossover.)
    feed.xover[0].Process(x);
    feed.drum_line.Write(feed.xover[1].Process(feed.xover[0].lp()));
    feed.xover[2].Process(x);
    feed.xover[3].Process(feed.xover[2].hp());
    feed.horn_line.Write(feed.xover[3].hp());
}

StereoFrame RotaryMode::Process(StereoFrame input, const ParamSet& params) {
    (void)params;
    WriteFeed(feeds_[0], input.left);
    WriteFeed(feeds_[1], input.right);

    // Per-sample LFO advance (avoids block-boundary zipper noise on Doppler)
    const float hl  = horn_lfo_.Process();
    horn_lfo_q_.FollowPhaseOf(horn_lfo_);   // keep the mics exactly 90 deg apart
    const float hlq = horn_lfo_q_.Process();
    const float dl  = drum_lfo_.Process();
    drum_lfo_q_.FollowPhaseOf(drum_lfo_);
    const float dlq = drum_lfo_q_.Process();

    // True stereo Doppler: L mic = in-phase LFO, R mic = 90° quadrature —
    // physically correct for two microphones placed 90° apart around a
    // rotating speaker. Each mic reads its own channel's feed.
    const float horn_l = feeds_[0].horn_line.ReadAtHighQuality(kRotorCenter + hl  * horn_mod_);
    const float horn_r = feeds_[1].horn_line.ReadAtHighQuality(kRotorCenter + hlq * horn_mod_);
    const float drum_l = feeds_[0].drum_line.ReadAtHighQuality(kRotorCenter + dl  * drum_mod_);
    const float drum_r = feeds_[1].drum_line.ReadAtHighQuality(kRotorCenter + dlq * drum_mod_);

    // Horn cabinet coloring: add resonant BP peak (~2.5 kHz "honk").
    // L and R use independent SVF state so they don't cross-contaminate.
    horn_color_l_.Process(horn_l);
    horn_color_r_.Process(horn_r);
    const float colored_horn_l = horn_l + kHornColorAmt * horn_color_l_.bp();
    const float colored_horn_r = horn_r + kHornColorAmt * horn_color_r_.bp();

    // AM: horn is directional (strong AM), drum is diffuse (softer AM)
    const float ha = am_depth_;
    const float da = am_depth_ * 0.6f;
    const float horn_am_l = (1.0f - ha * (0.5f + 0.5f * hl))  * horn_am_makeup_;
    const float horn_am_r = (1.0f - ha * (0.5f + 0.5f * hlq)) * horn_am_makeup_;
    const float drum_am_l = (1.0f - da * (0.5f + 0.5f * dl))  * drum_am_makeup_;
    const float drum_am_r = (1.0f - da * (0.5f + 0.5f * dlq)) * drum_am_makeup_;

    // The LR4 bands sum to an allpass copy of the input, so they add back to
    // unity level at full weight. A 0.5 here once cost 6 dB.
    float out_l = colored_horn_l * horn_am_l * horn_level_ + drum_l * drum_am_l * drum_level_;
    float out_r = colored_horn_r * horn_am_r * horn_level_ + drum_r * drum_am_r * drum_level_;

    out_l = dc_l_.Process(out_l);
    out_r = dc_r_.Process(out_r);
    return {out_l, out_r};
}

} // namespace pedal
