#include "filter_delay.h"
#include "../dsp/delay_line_sdram.h"
#include "../dsp/fast_math.h"
#include "../config/constants.h"
#include <array>

using namespace pedal::delay_fx;

namespace pedal {

static constexpr float kStereoOffsetSamples = 150.0f;
static constexpr size_t kFilterTableSize = 257;

static const std::array<float, kFilterTableSize>& filterGTable() {
    static const std::array<float, kFilterTableSize> table = [] {
        std::array<float, kFilterTableSize> result{};
        for (size_t i = 0; i < result.size(); ++i) {
            // Six octaves from 100 Hz to 6.4 kHz, with 800 Hz at the centre.
            const float frequency = 100.0f * exp2f(6.0f * static_cast<float>(i) /
                                                   static_cast<float>(result.size() - 1));
            result[i] = tanf(3.14159265f * frequency * INV_SAMPLE_RATE);
        }
        return result;
    }();
    return table;
}

void FilterDelay::Init() {
    filter_line_l_.Init(filter_buf_l_, MAX_DELAY_SAMPLES);
    filter_line_r_.Init(filter_buf_r_, MAX_DELAY_SAMPLES);
    lfo_.Init(1.0f, LfoWave::Sine);
    dc_l_.Init();
    dc_r_.Init();
    svf_l_.Reset();
    svf_r_.Reset();
    (void)filterGTable();
}

void FilterDelay::Reset() {
    filter_line_l_.Reset();
    filter_line_r_.Reset();
    lfo_.Reset();
    dc_l_.Init();
    dc_r_.Init();
    svf_l_.Reset();
    svf_r_.Reset();
    time_transition_.Reset();
    filter_type_ = FilterType::Lowpass;
    fb_lim_l_.Reset();
    fb_lim_r_.Reset();
    dc_fb_l_.Init();
    dc_fb_r_.Init();
}

void FilterDelay::Prepare(const ParamSet& params) {
    lfo_.SetRate(params.mod_spd);

    float target_delay = params.time * SAMPLE_RATE;
    if (target_delay > static_cast<float>(MAX_DELAY_SAMPLES - 1))
        target_delay = static_cast<float>(MAX_DELAY_SAMPLES - 1);
    time_transition_.SetTarget(target_delay);

    float q = 0.5f + params.filter * 14.5f;
    svf_l_.SetQ(q);
    svf_r_.SetQ(q);

    // Do not chatter between filter topologies when an automated control
    // hovers around a boundary. The 0.03 dead bands are inaudible in normal
    // use but eliminate rapid state changes and their associated clicks.
    switch (filter_type_) {
        case FilterType::Lowpass:
            if (params.grit > 0.36f) filter_type_ = FilterType::Bandpass;
            break;
        case FilterType::Bandpass:
            if (params.grit < 0.30f) filter_type_ = FilterType::Lowpass;
            else if (params.grit > 0.69f) filter_type_ = FilterType::Highpass;
            break;
        case FilterType::Highpass:
            if (params.grit < 0.63f) filter_type_ = FilterType::Bandpass;
            break;
    }

    sweep_depth_indices_ = params.mod_dep * 128.0f;
}

StereoFrame FilterDelay::Process(float input, const ParamSet& params) {
    return Process(StereoFrame{input, input}, params);
}

StereoFrame FilterDelay::Process(StereoFrame input, const ParamSet& params) {
    // Advance LFO per-sample
    float lfo_val = lfo_.Process();

    // Out-of-phase cutoff modulation per-sample to eliminate zipper noise (Bug 9).
    // g bounds are precomputed in Prepare() so no tanf() call is needed here.
    const auto tableG = [](float index) {
        if (index < 0.0f) index = 0.0f;
        if (index > 256.0f) index = 256.0f;
        const size_t lower = static_cast<size_t>(index);
        if (lower >= 256U) return filterGTable()[256];
        const float fraction = index - static_cast<float>(lower);
        return filterGTable()[lower] + fraction *
               (filterGTable()[lower + 1U] - filterGTable()[lower]);
    };
    svf_l_.SetG(tableG(128.0f + lfo_val * sweep_depth_indices_));
    svf_r_.SetG(tableG(128.0f - lfo_val * sweep_depth_indices_));

    const auto readHeads = [&](float base) {
        return StereoFrame{filter_line_l_.ReadNearest(base),
                           filter_line_r_.ReadNearest(base + kStereoOffsetSamples)};
    };
    StereoFrame raw = readHeads(time_transition_.to());
    if (time_transition_.active()) {
        const StereoFrame old = readHeads(time_transition_.from());
        const float fade = time_transition_.mix();
        raw.left = old.left + fade * (raw.left - old.left);
        raw.right = old.right + fade * (raw.right - old.right);
        time_transition_.Advance();
    }
    float wet_l = raw.left;
    float wet_r = raw.right;

    // Process through the TPT Svf
    svf_l_.Process(wet_l);
    svf_r_.Process(wet_r);

    switch (filter_type_) {
        case FilterType::Lowpass:
            wet_l = svf_l_.lp();
            wet_r = svf_r_.lp();
            break;
        case FilterType::Bandpass:
            wet_l = svf_l_.bp();
            wet_r = svf_r_.bp();
            break;
        case FilterType::Highpass:
            wet_l = svf_l_.hp();
            wet_r = svf_r_.hp();
            break;
    }

    // Keep repeat decay independent of resonance. The filter is an animated
    // output voice; the feedback loop receives the level-stable raw taps.
    const float feedback_l = dc_fb_l_.Process(fb_lim_l_.Process(raw.left * params.repeats));
    const float feedback_r = dc_fb_r_.Process(fb_lim_r_.Process(raw.right * params.repeats));

    filter_line_l_.Write(input.left + feedback_l);
    filter_line_r_.Write(input.right + feedback_r);

    // A resonant SVF can exceed unity even with bounded input. Soft limiting
    // reserves explicit output headroom without changing the feedback decay.
    wet_l = dc_l_.Process(soft_clip_tanh(wet_l));
    wet_r = dc_r_.Process(soft_clip_tanh(wet_r));

    return StereoFrame{wet_l, wet_r};
}

} // namespace pedal
