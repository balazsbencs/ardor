#include "ph2_mode.h"
#include "../dsp/freq_table.h"
#include <algorithm>
#include <cmath>

namespace pedal {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kEnvelopeStep = 1.0f / (ph2::kRate * 10000.0f * 4.7e-6f + 1.0f);
constexpr float kCompressorReference = 10000.0f * 20000.0f * 140e-6f / (2.0f * 47000.0f);
// R16 adds 22k to the gain-cell's internal 20k. IC1B adds R91/R79.
constexpr float kExpanderScale = (2.0f * 47000.0f / (10000.0f * 42000.0f * 140e-6f)) * (47000.0f / 10000.0f);
constexpr float rcG(float resistance, float capacitance) {
    return 1.0f / (2.0f * ph2::kRate * resistance * capacitance);
}
float triangle(float phase) {
    phase -= std::floor(phase);
    return phase < 0.5f ? -1.0f + 4.0f * phase : 3.0f - 4.0f * phase;
}
ph2::Value tanhCurve(float x) {
    if (std::abs(x) >= 3.0f) return {std::copysign(1.0f, x), 0.0f};
    const float xx = x * x;
    // Share one reciprocal between the value and its analytic derivative.
    // This preserves the Padé curve while avoiding a second scalar division
    // in every OTA/feedback trial on the target ARM CPU.
    const float inverse = 1.0f / (27.0f + 9.0f * xx);
    const float derivative_term = (xx - 9.0f) * inverse;
    return {x * (27.0f + xx) * inverse,
            9.0f * derivative_term * derivative_term};
}
float cleanState(float x) { return std::abs(x) < 1e-20f ? 0.0f : x; }
}

namespace ph2 {
Value rail(float input) {
    // Nominal 9 V single-supply op-amp headroom. Preserve ordinary signals
    // exactly, then approach +/-3 V continuously with a continuous slope.
    constexpr float knee = 2.0f, span = 1.0f;
    const float over = std::abs(input) - knee;
    if (over <= 0.0f) return {input, 1.0f};
    const auto curve = tanhCurve(over / span);
    return {std::copysign(knee + span * curve.signal, input), curve.slope};
}

Value Pole::lowpass(float input, float g) const {
    const float inverse = 1.0f / (1.0f + g);
    return {(state + g * input) * inverse, g * inverse};
}

Value Allpass::evaluate(float input, float g, float inverse, bool ota, float& next) const {
    Value lp{(cap.state + g * input) * inverse, g * inverse};
    if (ota) {
        // OTA differential pair: I = Iabc*tanh(Vdiff/(2*Vt)). The 68k /
        // 560-ohm summing network turns this into a nonlinear RC integrator.
        // Solve lp = state + g*scale*tanh((input-lp)/scale) by Newton.
        auto curve = tanhCurve((input - lp.signal) / kOtaScale);
        for (int i = 0; i < 3; ++i) {
            const float residual = lp.signal - cap.state - g * kOtaScale * curve.signal;
            if (std::abs(residual) < 1e-7f) break;
            lp.signal -= residual / (1.0f + g * curve.slope);
            curve = tanhCurve((input - lp.signal) / kOtaScale);
        }
        // Reuse the accepted curve for the analytic slope; evaluating it
        // again would duplicate the last trial without advancing any state.
        lp.slope = g * curve.slope / (1.0f + g * curve.slope);
    }
    next = cleanState(2.0f * lp.signal - cap.state);
    auto output = rail(input - 2.0f * lp.signal);
    output.slope *= 1.0f - 2.0f * lp.slope;
    return output;
}

float Loop::process(float input, float swept_g, float fixed_g, float beta,
                    int count, bool second) {
    // IC5B / IC10A are differential amplifiers. Both have 4.7k feedback
    // shunted by 1n. Their non-inverting return dividers differ in capacitor value.
    constexpr float divider = 4700.0f / (6800.0f + 4700.0f);
    const float return_g = rcG(6800.0f * 4700.0f / 11500.0f, second ? 2.7e-9f : 4.7e-9f);
    const float resonance_g = rcG(4700.0f, 1e-9f);
    const float coupling_g = rcG(68000.0f, 1e-6f);
    const float fixed_coupling_g = rcG(10000.0f, 1e-6f);
    // Both coefficients are shared by every stage and every trial evaluation
    // of this current-sample root. Compute their reciprocals once per loop.
    const float swept_inverse = 1.0f / (1.0f + swept_g);
    const float fixed_inverse = 1.0f / (1.0f + fixed_g);
    std::array<float, 10> next{};
    float coupling_next = 0.0f, fixed_next = 0.0f, chip2_next = 0.0f;
    Value phased{}, feedback{}, returned{};
    auto evaluate = [&](float u) {
        const auto coupled = coupling.lowpass(u, coupling_g);
        coupling_next = cleanState(2.0f * coupled.signal - coupling.state);
        phased = {u - coupled.signal, 1.0f - coupled.slope};
        for (int i = 0; i < count; ++i) {
            const int fixed_index = count - 2;
            if (i == fixed_index || (count == 10 && i == 4)) {
                const bool fixed_entry = i == fixed_index;
                const Pole& pole = fixed_entry ? fixed_coupling : chip2_coupling;
                const auto hp = pole.lowpass(phased.signal, fixed_entry ? fixed_coupling_g : coupling_g);
                (fixed_entry ? fixed_next : chip2_next) = cleanState(2.0f * hp.signal - pole.state);
                phased.signal -= hp.signal;
                phased.slope *= 1.0f - hp.slope;
            }
            const bool fixed = i >= fixed_index;
            const auto stage = stages[i].evaluate(phased.signal, fixed ? fixed_g : swept_g,
                                                  fixed ? fixed_inverse : swept_inverse, !fixed, next[i]);
            phased.signal = stage.signal;
            phased.slope *= stage.slope;
        }
        // Dry goes to the INVERTING input. The divider on the NON-INVERTING
        // input receives the phase return (through VR1 in the first loop).
        // Thus u = -LP(input) + (1+LP)*divider*LP_return(beta*phase), not
        // positive dry plus a unity inverted phase-feedback tap.
        returned = return_filter.lowpass(beta * divider * phased.signal, return_g);
        feedback = resonance_filter.lowpass(returned.signal - input, resonance_g);
        const auto mixer = rail(returned.signal + feedback.signal);
        return Value{u - mixer.signal, 1.0f - mixer.slope * (1.0f + feedback.slope) *
                         returned.slope * beta * divider * phased.slope};
    };
    // Solve the current-sample loop instead of inserting an artificial sample
    // delay. All trial evaluations leave capacitor histories untouched.
    float u = previous_input;
    auto accepted = evaluate(u);
    for (int i = 0; i < 6; ++i) {
        if (std::abs(accepted.signal) < 1e-6f) break;
        u = std::clamp(u - accepted.signal / accepted.slope, -3.0f, 3.0f);
        accepted = evaluate(u);
    }
    max_residual = std::max(max_residual, std::abs(accepted.signal));
    previous_input = cleanState(u);
    for (int i = 0; i < count; ++i) stages[i].cap.state = next[i];
    coupling.state = coupling_next;
    fixed_coupling.state = fixed_next;
    if (count == 10) chip2_coupling.state = chip2_next;
    return_filter.commit(returned.signal);
    resonance_filter.commit(feedback.signal);
    return_filter.state = cleanState(return_filter.state);
    resonance_filter.state = cleanState(resonance_filter.state);
    return phased.signal;
}

float Compander::compress(float input) {
    // Feedback rectification follows compressor OUTPUT. Solving the positive
    // quadratic avoids both division by zero and a guessed attack envelope.
    constexpr float floor = 1e-6f; // numerical rectifier floor, not added noise
    const float b = (1.0f - kEnvelopeStep) * compression_envelope + floor;
    const float product = kCompressorReference * std::abs(input);
    const float magnitude = 2.0f * product / (b + std::sqrt(b * b + 4.0f * kEnvelopeStep * product));
    const float output = rail(std::copysign(magnitude, input)).signal;
    compression_envelope += kEnvelopeStep * (std::abs(output) - compression_envelope);
    compression_envelope = cleanState(compression_envelope);
    return output;
}

float Compander::expand(float input) {
    expansion_envelope += kEnvelopeStep * (std::abs(input) - expansion_envelope);
    expansion_envelope = cleanState(expansion_envelope);
    return rail(input * expansion_envelope * kExpanderScale).signal;
}

float Routing::process(float input, float swept_g, float fixed_g, float resonance, bool mode2) {
    const float phased = first.process(input, swept_g, fixed_g, resonance, mode2 ? 6 : 10, false);
    constexpr float wet1 = 4700.0f / 18000.0f;
    constexpr float dry1 = (4700.0f / 26700.0f) * (1.0f + wet1);
    const float mixed = rail(dry1 * input - wet1 * phased).signal;
    if (!mode2) return mixed;
    // Original schematic: R68=10k, not the later modified value. IC10A has
    // no user pot. Its return divider sets the low-frequency loop gain to
    // 2*4.7/(6.8+4.7) ~= 0.817, and its 1n compensation further limits Q.
    const float phased2 = second.process(mixed, swept_g, fixed_g, 1.0f, 6, true);
    constexpr float wet2 = 4700.0f / 10000.0f;
    constexpr float dry2 = (4700.0f / 16700.0f) * (1.0f + wet2);
    return rail(dry2 * mixed - wet2 * phased2).signal;
}
} // namespace ph2

void Ph2Mode::Reset() {
    // Initialize the shared lookup table and fixed-stage coefficient during
    // setup, before entering the audio callback.
    fixed_g_ = freq_table::g_at(freq_table::position_for_hz(ph2::kFixedHz * 0.5f));
    channels_ = {};
    for (auto& channel : channels_) {
        // Decimate on the even 96k phase, so the FIR pair has exactly
        // 15 host samples of latency rather than 14.5.
        float ignored;
        channel.down.Push(0.0f, ignored);
    }
    phase_ = 0.0f;
    mode_blend_ = 0.0f;
    mode_target_ = 0;
    prepared_ = false;
    channels_identical_ = true;
}

void Ph2Mode::Prepare(const mod_fx::ParamSet& p) {
    rate_ = std::clamp(p.speed, 1.0f / 14.0f, 10.0f);
    // Tone is a trim extension. 0.5 is the nominal 1 kHz adjustment; all
    // fixed 5.6k / 10n stages stay at their schematic frequency.
    centre_target_ = freq_table::position_for_hz(1000.0f * std::exp2((p.tone - 0.5f) * 4.0f));
    depth_target_ = std::clamp(p.depth, 0.0f, 1.0f) * 0.6f;
    resonance_target_ = std::clamp(p.p1, 0.0f, 1.0f) * ph2::kMaxResonance * (p.p4 >= 0.5f ? -1.0f : 1.0f);
    stereo_target_ = std::clamp(p.p3, 0.0f, 1.0f) * 0.5f;
    const int new_mode = p.p2 >= 0.5f ? 1 : 0;
    if (prepared_ && new_mode != mode_target_ && mode_blend_ == static_cast<float>(mode_target_)) {
        // An inactive path has not advanced. Start it clean instead of
        // reintroducing stale capacitor voltages when switching back.
        for (auto& channel : channels_) channel.routing[new_mode] = {};
    }
    mode_target_ = new_mode;
    if (!prepared_) {
        centre_ = centre_target_; depth_ = depth_target_;
        resonance_ = resonance_target_; stereo_ = stereo_target_;
        mode_blend_ = static_cast<float>(mode_target_);
        prepared_ = true;
    }
}

float Ph2Mode::Channel::process(float input, float lfo, float centre, float depth,
                               float resonance, float fixed_g, float mode_blend, bool run_both, int active) {
    float wet = 0.0f;
    const auto oversampled = up.Process(input);
    // Lookup prewarped g at 96k by halving the desired frequency in the
    // shared 48k table. Never clamp a 48k coefficient and reuse it at 96k.
    for (const float x : oversampled) {
        // The OTA exponential converter sees the filtered control voltage.
        constexpr float cv_step = 1.0f / (1.2f * ph2::kRate + 1.0f);
        cv += cv_step * (lfo - cv);
        const float swept_g = freq_table::g_at(centre - 0.1f + depth * 0.5f * cv);
        // Input coupling C1=47n/R6=1M and buffer coupling C4=1u/R5=47k.
        const auto hp = input_hp.lowpass(x, rcG(1000000.0f, 47e-9f));
        input_hp.commit(hp.signal);
        const auto buffer = buffer_hp.lowpass(x - hp.signal, rcG(47000.0f, 1e-6f));
        buffer_hp.commit(buffer.signal);
        // Reduced gain-cell compensation bandwidth, C8=100p / R12=22k.
        const auto lp = input_lp.lowpass(x - hp.signal - buffer.signal, rcG(22000.0f, 100e-12f));
        input_lp.commit(lp.signal);
        const float compressed = compander.compress(lp.signal);
        float mixed;
        if (run_both) {
            const float a = routing[0].process(compressed, swept_g, fixed_g, resonance, false);
            const float b = routing[1].process(compressed, swept_g, fixed_g, resonance, true);
            mixed = a + mode_blend * (b - a);
        } else {
            mixed = routing[active].process(compressed, swept_g, fixed_g, resonance, active == 1);
        }
        const float expanded = compander.expand(mixed);
        const auto out_lp = output_lp.lowpass(expanded, rcG(47000.0f, 100e-12f));
        output_lp.commit(out_lp.signal);
        // C6=1u/R7=100k output coupling; host sees an AC signal, not Vref.
        const auto out_hp = output_hp.lowpass(out_lp.signal, rcG(100000.0f, 1e-6f));
        output_hp.commit(out_hp.signal);
        float decimated;
        if (down.Push(out_lp.signal - out_hp.signal, decimated)) wet = decimated;
        input_hp.state = cleanState(input_hp.state);
        buffer_hp.state = cleanState(buffer_hp.state);
        input_lp.state = cleanState(input_lp.state);
        output_hp.state = cleanState(output_hp.state);
        output_lp.state = cleanState(output_lp.state);
    }
    return wet;
}

StereoFrame Ph2Mode::Process(StereoFrame input, const mod_fx::ParamSet& p) {
    if (!prepared_) Prepare(p);
    constexpr float slew = 1.0f / (0.01f * 48000.0f + 1.0f);
    centre_ += slew * (centre_target_ - centre_);
    depth_ += slew * (depth_target_ - depth_);
    resonance_ += slew * (resonance_target_ - resonance_);
    stereo_ += slew * (stereo_target_ - stereo_);
    const bool run_both = mode_blend_ != static_cast<float>(mode_target_);
    if (run_both) {
        constexpr float step = 1.0f / (0.02f * 48000.0f);
        mode_blend_ += mode_target_ ? step : -step;
        mode_blend_ = std::clamp(mode_blend_, 0.0f, 1.0f);
    }
    const float lfo_l = triangle(phase_);
    const float lfo_r = triangle(phase_ + stereo_);
    phase_ += rate_ / 48000.0f;
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    const auto channel = [&](Channel& c, float x, float lfo) {
        const float wet = c.process(x, lfo, centre_, depth_, resonance_, fixed_g_, mode_blend_, run_both, mode_target_);
        const float dry = c.dry[c.dry_index];
        c.dry[c.dry_index] = x;
        c.dry_index = (c.dry_index + 1) % kLatencyFrames;
        return dry + p.mix * (wet - dry);
    };
    const float left = channel(channels_[0], input.left, lfo_l);
    if (channels_identical_ && stereo_ == 0.0f && input.left == input.right) {
        // A mono source with the pedal's original mono sweep has identical
        // histories. Copying fixed-size state avoids solving the same circuit
        // twice; any stereo input or offset permanently disables this shortcut
        // until reset, so previous stereo histories are never discarded.
        channels_[1] = channels_[0];
        return {left, left};
    }
    channels_identical_ = false;
    return {left, channel(channels_[1], input.right, lfo_r)};
}

float Ph2Mode::SolverResidual() const {
    float result = 0.0f;
    for (const auto& channel : channels_)
        for (const auto& routing : channel.routing)
            result = std::max({result, routing.first.max_residual, routing.second.max_residual});
    return result;
}
} // namespace pedal
