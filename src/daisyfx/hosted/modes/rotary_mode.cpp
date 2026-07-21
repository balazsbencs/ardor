#include "rotary_mode.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::mod_fx;

namespace pedal {

// Centre delay offsets in samples (L/R read from same write buffer)
static constexpr float kHornCenter = 120.0f;
static constexpr float kDrumCenter = 240.0f;

// Motor inertia: ramp coefficient per Prepare() call (once per BLOCK_SIZE samples).
// Horn τ ≈ 1.2 s → coef = 1 − exp(−BLOCK_SIZE / (SAMPLE_RATE × 1.2))
static constexpr float kHornRampCoef = 8.33e-4f;
// Drum τ ≈ 2.5 s → coef = 1 − exp(−BLOCK_SIZE / (SAMPLE_RATE × 2.5))
static constexpr float kDrumRampCoef = 4.00e-4f;

// Leslie 122 canonical chorale (slow) speeds
static constexpr float kHornChorale = 0.67f;
static constexpr float kDrumChorale = 0.40f;

// Horn cabinet BP resonance mix level (0 = off, 1 = full parallel add)
static constexpr float kHornColorAmt = 0.3f;

static constexpr float kTwoPi = 6.28318530717958647692f;

void RotaryMode::Init() {
    horn_line_.Init(horn_buf_, kHornBufSize);
    drum_line_.Init(drum_buf_, kDrumBufSize);

    actual_horn_rate_ = kHornChorale;
    actual_drum_rate_ = kDrumChorale;

    horn_lfo_.Init(kHornChorale, LfoWave::Sine);
    horn_lfo_.SetJitter(0.1f);
    horn_lfo_q_.Init(kHornChorale, LfoWave::Sine);
    horn_lfo_q_.SetJitter(0.1f);
    horn_lfo_q_.SetPhaseOffset(kTwoPi * 0.25f);
    horn_lfo_q_.Reset();

    drum_lfo_.Init(kDrumChorale, LfoWave::Sine);
    drum_lfo_.SetJitter(0.1f);
    drum_lfo_q_.Init(kDrumChorale, LfoWave::Sine);
    drum_lfo_q_.SetJitter(0.1f);
    drum_lfo_q_.SetPhaseOffset(kTwoPi * 0.25f);
    drum_lfo_q_.Reset();

    // Butterworth 2-pole crossover; frequency updated each Prepare()
    xover_.SetQ(0.5f);
    xover_.SetFreq(800.0f);

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
    horn_line_.Reset();
    drum_line_.Reset();
    horn_lfo_.Reset();
    horn_lfo_q_.Reset();
    drum_lfo_.Reset();
    drum_lfo_q_.Reset();
    actual_horn_rate_ = kHornChorale;
    actual_drum_rate_ = kDrumChorale;
    xover_.Reset();
    horn_color_l_.Reset();
    horn_color_r_.Reset();
    dc_l_.Init();
    dc_r_.Init();
}

void RotaryMode::Prepare(const ParamSet& params) {
    // P2 ≥ 0.5 → tremolo (fast); P2 < 0.5 → chorale (slow).
    // Speed param sets the fast horn target; drum is always at the Leslie ratio.
    const float target_horn = (params.p2 >= 0.5f) ? params.speed : kHornChorale;
    const float target_drum = (params.p2 >= 0.5f) ? params.speed * 0.56f : kDrumChorale;

    // Exponential smoothing toward target — motor inertia
    actual_horn_rate_ += (target_horn - actual_horn_rate_) * kHornRampCoef;
    actual_drum_rate_ += (target_drum - actual_drum_rate_) * kDrumRampCoef;

    horn_lfo_.SetRate(actual_horn_rate_);
    horn_lfo_q_.SetRate(actual_horn_rate_);
    drum_lfo_.SetRate(actual_drum_rate_);
    drum_lfo_q_.SetRate(actual_drum_rate_);

    // Tone maps to crossover frequency: 0 → 500 Hz, 1 → 2000 Hz
    xover_.SetFreq(500.0f + params.tone * 1500.0f);

    // Horn cabinet resonance tracks tone: 0 → 1.8 kHz (warm), 1 → 3.5 kHz (bright)
    const float horn_fc = 1800.0f + params.tone * 1700.0f;
    horn_color_l_.SetFreq(horn_fc);
    horn_color_r_.SetFreq(horn_fc);

    // Scale p1 to a gentler drive range (max 4× vs Saturation's default 16×).
    // Prevents a loudness jump at moderate P1 settings.
    drive_.SetDrive(params.p1 * 0.2f);

    // Cache Depth-derived modulation depths for per-sample use
    am_depth_ = params.depth * 0.65f;   // Leslie 122: horn sweeps ~65% amplitude
    horn_mod_ = params.depth * 90.0f;   // ±90 samples ≈ ±1.9 ms
    drum_mod_ = params.depth * 180.0f;  // ±180 samples ≈ ±3.75 ms
}

StereoFrame RotaryMode::Process(StereoFrame input, const ParamSet& params) {
    float mono = input.mono();
    if (params.p1 > 0.02f) mono = drive_.Process(mono);

    // 2-pole SVF crossover: LP → drum band, HP → horn band
    xover_.Process(mono);
    const float drum_in = xover_.lp();
    const float horn_in = xover_.hp();

    // Per-sample LFO advance (avoids block-boundary zipper noise on Doppler)
    const float hl  = horn_lfo_.Process();
    const float hlq = horn_lfo_q_.Process();
    const float dl  = drum_lfo_.Process();
    const float dlq = drum_lfo_q_.Process();

    // True stereo Doppler: single write per band, two quadrature reads.
    // L mic = in-phase LFO, R mic = 90° quadrature — physically correct for
    // two microphones placed 90° apart around a rotating speaker.
    horn_line_.Write(horn_in);
    const float horn_l = horn_line_.ReadAt(kHornCenter + hl  * horn_mod_);
    const float horn_r = horn_line_.ReadAt(kHornCenter + hlq * horn_mod_);

    drum_line_.Write(drum_in);
    const float drum_l = drum_line_.ReadAt(kDrumCenter + dl  * drum_mod_);
    const float drum_r = drum_line_.ReadAt(kDrumCenter + dlq * drum_mod_);

    // Horn cabinet coloring: add resonant BP peak (~2.5 kHz "honk").
    // L and R use independent SVF state so they don't cross-contaminate.
    horn_color_l_.Process(horn_l);
    horn_color_r_.Process(horn_r);
    const float colored_horn_l = horn_l + kHornColorAmt * horn_color_l_.bp();
    const float colored_horn_r = horn_r + kHornColorAmt * horn_color_r_.bp();

    // AM: horn is directional (strong AM), drum is diffuse (softer AM)
    const float ha = am_depth_;
    const float da = am_depth_ * 0.6f;
    const float horn_am_l = 1.0f - ha * (0.5f + 0.5f * hl);
    const float horn_am_r = 1.0f - ha * (0.5f + 0.5f * hlq);
    const float drum_am_l = 1.0f - da * (0.5f + 0.5f * dl);
    const float drum_am_r = 1.0f - da * (0.5f + 0.5f * dlq);

    // 0.5 normalises two complementary bands summed at full weight back to unity.
    float out_l = 0.5f * (colored_horn_l * horn_am_l + drum_l * drum_am_l);
    float out_r = 0.5f * (colored_horn_r * horn_am_r + drum_r * drum_am_r);

    out_l = dc_l_.Process(out_l);
    out_r = dc_r_.Process(out_r);
    return {out_l, out_r};
}

} // namespace pedal
