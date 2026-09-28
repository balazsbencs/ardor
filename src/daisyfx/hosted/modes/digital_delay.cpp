#include "digital_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../config/constants.h"

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 150.0f;
void DigitalDelay::Init() {
    digital_line_l_.Init(digital_buf_l_, MAX_DELAY_SAMPLES);
    digital_line_r_.Init(digital_buf_r_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    filter_l_.Init();
    filter_r_.Init();
    filter_l_.SetKnob(0.5f);
    filter_r_.SetKnob(0.5f);
    dc_l_.Init();
    dc_r_.Init();
    sat_l_.Reset();
    sat_r_.Reset();
    dc_fb_l_.Init(SAMPLE_RATE, DcBlocker::FEEDBACK_LOOP_CUTOFF_HZ);
    dc_fb_r_.Init(SAMPLE_RATE, DcBlocker::FEEDBACK_LOOP_CUTOFF_HZ);
}

void DigitalDelay::Reset() {
    spread_.Reset();
    digital_line_l_.Reset();
    digital_line_r_.Reset();
    lfo_.Reset();
    filter_l_.Reset();
    filter_r_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    sat_l_.Reset();
    sat_r_.Reset();
    time_transition_.Reset();
    aa_state_l_ = 0.0f;
    aa_state_r_ = 0.0f;
    aa_coef_    = 1.0f;
    aa_mix_     = 0.0f;
    previous_lfo_ = 0.0f;
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
    dc_fb_l_.Init(SAMPLE_RATE, DcBlocker::FEEDBACK_LOOP_CUTOFF_HZ);
    dc_fb_r_.Init(SAMPLE_RATE, DcBlocker::FEEDBACK_LOOP_CUTOFF_HZ);
}

void DigitalDelay::Prepare(const ParamSet& params) {
    time_transition_.SetTarget(params.time * SAMPLE_RATE);
    lfo_.SetRate(params.mod_spd);
    filter_l_.SetKnob(params.filter);
    filter_r_.SetKnob(params.filter);
    if (params.grit <= 0.00001f) { sat_l_.Reset(); sat_r_.Reset(); }
    // Anti-alias LP: cutoff tracks mod depth × rate.
    // At zero mod this is transparent (coef=1). At max mod it rolls off ~8 kHz.
    if (params.mod_dep <= 0.00001f || params.mod_spd <= 0.00001f) {
        aa_coef_ = 1.0f;
        aa_mix_ = 0.0f;
    } else {
        const float mod_rate_hz = params.mod_spd * params.mod_dep * 30.0f;
        const float norm = mod_rate_hz / (10.0f * 30.0f);
        aa_mix_ = fminf(norm, 1.0f);
        const float aa_fc = fmaxf(20000.0f - norm * 12000.0f, 8000.0f);
        aa_coef_ = 1.0f - expf(-2.0f * 3.14159265f * aa_fc * INV_SAMPLE_RATE);
    }
}

StereoFrame DigitalDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame DigitalDelay::Process(StereoFrame input, const ParamSet& params) {
    const float lfo_val   = lfo_.Process();
    const float lfo_step = lfo_val - previous_lfo_;
    previous_lfo_ = lfo_val;
    const float mod_samps = params.mod_dep * 30.0f;

    const float spread = spread_.Update(kStereoOffsetSamples, params.width);
    const auto read = [mod_samps](const DelayLineSdram& line, float delay, float rate, bool fractional) {
        return mod_samps <= 0.00001f && !fractional ? line.ReadNearest(delay)
                                     : line.ReadAtResampled(delay, rate);
    };
    const auto readHead = [&](float base, bool right) {
        const float offset = right ? spread - lfo_val * mod_samps
                                   : lfo_val * mod_samps;
        const float rate = 1.0f + (right ? lfo_step : -lfo_step) * mod_samps;
        return read(right ? digital_line_r_ : digital_line_l_, base + offset, rate,
                    right && spread_.Fractional());
    };

    float wet_l = readHead(time_transition_.to(), false);
    float wet_r = readHead(time_transition_.to(), true);
    if (time_transition_.active()) {
        const float fade = time_transition_.mix();
        const float old_l = readHead(time_transition_.from(), false);
        const float old_r = readHead(time_transition_.from(), true);
        wet_l = old_l + fade * (wet_l - old_l);
        wet_r = old_r + fade * (wet_r - old_r);
        time_transition_.Advance();
    }

    wet_l = filter_l_.Process(wet_l);
    wet_r = filter_r_.Process(wet_r);

    // Keep the clean digital repeat path exactly transparent at grit=0, then
    // progressively blend in tape-like loop saturation at higher values.
    const float drive = 1.0f + 3.0f * params.grit * params.grit;
    const float colored_l = params.grit <= 0.00001f ? wet_l
        : wet_l + params.grit * (sat_l_.Process(wet_l, drive) - wet_l);
    const float colored_r = params.grit <= 0.00001f ? wet_r
        : wet_r + params.grit * (sat_r_.Process(wet_r, drive) - wet_r);
    const float feedback_l = dc_fb_l_.Process(fb_lim_l_.Process(colored_l * params.repeats));
    const float feedback_r = dc_fb_r_.Process(fb_lim_r_.Process(colored_r * params.repeats));

    // Anti-alias LP on write input
    const float write_l = input.left + feedback_l;
    const float write_r = input.right + feedback_r;
    aa_state_l_ += aa_coef_ * (write_l - aa_state_l_);
    aa_state_r_ += aa_coef_ * (write_r - aa_state_r_);
    digital_line_l_.Write(write_l + aa_mix_ * (aa_state_l_ - write_l));
    digital_line_r_.Write(write_r + aa_mix_ * (aa_state_r_ - write_r));

    wet_l = dc_l_.Process(colored_l) * filter_l_.LoudnessCorrection();
    wet_r = dc_r_.Process(colored_r) * filter_r_.LoudnessCorrection();

    return StereoFrame{wet_l, wet_r};
}

} // namespace pedal
