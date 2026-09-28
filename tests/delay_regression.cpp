#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr double kRate = 48000.0;
constexpr double kTau = 6.2831853071795864769;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void configure(ardor::DaisyFxProcessor& processor, std::string_view mode,
               std::initializer_list<std::pair<const char*, float>> overrides) {
    const auto* descriptor = ardor::findDaisyFxDescriptor("delay", mode);
    require(descriptor != nullptr, "delay mode missing from catalog");
    auto params = ardor::defaultDaisyFxParams(*descriptor);
    params["time"] = 0.0f;
    params["repeats"] = 0.0f;
    params["mix"] = 1.0f;
    params["filter"] = 0.5f;
    params["grit"] = 0.0f;
    params["mod_dep"] = 0.0f;
    for (const auto& [key, value] : overrides) params[key] = value;
    std::string error;
    require(processor.configure("delay", params, 48000.0f, error), "delay configuration failed");
}

double tone(int frame, double frequency, double amplitude) {
    return amplitude * std::sin(kTau * frequency * frame / kRate);
}

double binAmplitude(const std::vector<float>& audio, int frequency) {
    double re = 0.0, im = 0.0;
    for (std::size_t n = 0; n < audio.size(); ++n) {
        const double phase = kTau * frequency * n / kRate;
        re += audio[n] * std::cos(phase);
        im += audio[n] * std::sin(phase);
    }
    return 2.0 * std::hypot(re, im) / audio.size();
}

void verifyFeedbackDecays() {
    for (const char* mode : {"digital", "tape"}) {
        ardor::DaisyFxProcessor processor;
        configure(processor, mode, {{"repeats", 0.5f}, {"grit", 0.5f}});
        double earlyPeak = 0.0, latePeak = 0.0;
        for (int n = 0; n < 8 * 48000; ++n) {
            const float input = n < 2400 ? static_cast<float>(tone(n, 200.0, 0.01)) : 0.0f;
            const auto output = processor.process({input, input});
            if (n < 48000) earlyPeak = std::max(earlyPeak, std::abs(static_cast<double>(output.left)));
            if (n >= 7 * 48000) latePeak = std::max(latePeak, std::abs(static_cast<double>(output.left)));
        }
        require(earlyPeak > 0.001, "feedback test note was not audible");
        require(latePeak < 0.0001, "delay feedback must decay after the note stops");
    }
}

void verifyQuietBucketBrigade() {
    ardor::DaisyFxProcessor processor;
    configure(processor, "dbucket", {});
    std::vector<float> output(48000);
    for (int n = 0; n < 96000; ++n) {
        const float input = static_cast<float>(tone(n, 1000.0, 0.001));
        const auto frame = processor.process({input, input});
        if (n >= 48000) output[n - 48000] = frame.left;
    }
    double sine = 0.0, cosine = 0.0;
    for (int n = 0; n < 48000; ++n) {
        const double phase = kTau * 1000.0 * n / kRate;
        sine += output[n] * std::sin(phase);
        cosine += output[n] * std::cos(phase);
    }
    sine *= 2.0 / 48000.0;
    cosine *= 2.0 / 48000.0;
    double residual = 0.0;
    for (int n = 0; n < 48000; ++n) {
        const double phase = kTau * 1000.0 * n / kRate;
        const double error = output[n] - sine * std::sin(phase) - cosine * std::cos(phase);
        residual += error * error;
    }
    require(std::hypot(sine, cosine) > 0.0008, "quiet bucket-brigade note was lost");
    require(std::sqrt(residual / 48000.0) < 0.000001,
            "bucket-brigade antialiasing must preserve quiet notes");
}

void verifySwellLongTime() {
    ardor::DaisyFxProcessor processor;
    configure(processor, "swell", {{"time", 1.0f}, {"mod_spd", 1.0f},
                                    {"mod_dep", 1.0f}});
    double peak = 0.0;
    for (int n = 0; n < 4 * 48000; ++n) {
        const float input = n < 960 ? static_cast<float>(tone(n, 400.0, 0.4)) : 0.0f;
        const auto output = processor.process({input, input});
        if (n >= 120000) peak = std::max(peak, std::abs(static_cast<double>(output.left)));
    }
    require(peak > 0.1, "a note must survive a Swell envelope shorter than Time");

    ardor::DaisyFxProcessor heldNote;
    configure(heldNote, "swell", {{"mod_spd", 1.0f}, {"mod_dep", 1.0f}});
    double heldPeak = 0.0;
    for (int n = 0; n < 96000; ++n) {
        const float input = static_cast<float>(tone(n, 400.0, 0.4));
        const auto output = heldNote.process({input, input});
        if (n >= 48000) heldPeak = std::max(heldPeak, std::abs(static_cast<double>(output.left)));
    }
    require(heldPeak > 0.1, "a held Swell note must sustain until release");
}

void verifyFirstEchoControls() {
    for (const auto& [mode, key] : {std::pair{"digital", "grit"},
                                     std::pair{"tape", "grit"},
                                     std::pair{"tape", "filter"}}) {
        ardor::DaisyFxProcessor low, high;
        configure(low, mode, {{key, 0.0f}});
        configure(high, mode, {{key, 1.0f}});
        double difference = 0.0;
        for (int n = 0; n < 48000; ++n) {
            const float input = static_cast<float>(tone(n, 1200.0, 0.2));
            const auto a = low.process({input, input});
            const auto b = high.process({input, input});
            if (n > 10000) difference = std::max(difference,
                std::abs(static_cast<double>(a.left - b.left)));
        }
        require(difference > 0.001, "Tape/Digital character control must affect the first echo");
    }
}

void verifyPatternHeadroom() {
    ardor::DaisyFxProcessor processor;
    configure(processor, "pattern", {{"repeats", 1.0f}});
    double peak = 0.0;
    for (int n = 0; n < 12 * 48000; ++n) {
        const float input = static_cast<float>(tone(n, 1000.0, 0.2));
        const auto output = processor.process({input, input});
        peak = std::max({peak, std::abs(static_cast<double>(output.left)),
                              std::abs(static_cast<double>(output.right))});
    }
    require(peak < 1.0, "Pattern coherent taps must fit full-scale headroom");
}

void verifyTimeChangeAntialiasing() {
    ardor::DaisyFxProcessor processor;
    configure(processor, "tape", {{"time", 1.0f}});
    std::vector<float> captured(48000);
    for (int n = 0; n < 6 * 48000; ++n) {
        if (n == 3 * 48000) require(processor.setParameterTarget("time", 0.0f), "time update failed");
        const float input = static_cast<float>(tone(n, 20000.0, 0.1));
        const auto output = processor.process({input, input});
        if (n >= 4 * 48000 && n < 5 * 48000) captured[n - 4 * 48000] = output.left;
    }
    require(binAmplitude(captured, 18000) < 0.003,
            "Tape time glide must suppress a folded 20 kHz tone");
}

void verifyNonlinearAntialiasing() {
    ardor::DaisyFxProcessor processor;
    configure(processor, "digital", {{"grit", 1.0f}, {"repeats", 0.5f}});
    std::vector<float> captured(48000);
    for (int n = 0; n < 5 * 48000; ++n) {
        const float input = static_cast<float>(tone(n, 5000.0, 0.1));
        const auto output = processor.process({input, input});
        if (n >= 4 * 48000) captured[n - 4 * 48000] = output.left;
    }
    require(binAmplitude(captured, 13000) < 0.01 * binAmplitude(captured, 5000),
            "Digital Saturation must suppress its folded 5 kHz harmonic");
}

void verifyTopologyAutomation() {
    for (const char* mode : {"dual", "filter"}) {
        ardor::DaisyFxProcessor processor;
        configure(processor, mode, {{"grit", 0.0f}});
        float previous = 0.0f;
        float maxStep = 0.0f;
        for (int n = 0; n < 58000; ++n) {
            if (n == 48000) require(processor.setParameterTarget("grit", 1.0f), "character update failed");
            const float input = static_cast<float>(tone(n, 100.0, 0.25));
            const auto output = processor.process({input, input});
            if (n > 48000) maxStep = std::max(maxStep, std::abs(output.left - previous));
            previous = output.left;
        }
        require(maxStep < 0.03f, "topology automation created a sample discontinuity");
    }
}

void verifyDigitalMonoFold() {
    ardor::DaisyFxProcessor processor;
    configure(processor, "digital", {{"width", 0.0f}});
    double difference = 0.0, peak = 0.0;
    for (int n = 0; n < 48000; ++n) {
        const float input = static_cast<float>(tone(n, 160.0, 0.1));
        const auto output = processor.process({input, input});
        if (n >= 24000) {
            difference = std::max(difference, std::abs(static_cast<double>(output.left - output.right)));
            peak = std::max(peak, std::abs(static_cast<double>(output.left)));
        }
    }
    require(difference < 0.000001 && peak > 0.09,
            "unmodulated Digital Delay must preserve mono fold-down");
}

void verifyDualStereoSide() {
    ardor::DaisyFxProcessor processor;
    configure(processor, "dual", {{"grit", 1.0f}, {"repeats", 0.5f}});
    double leftPeak = 0.0, rightPeak = 0.0;
    for (int n = 0; n < 24000; ++n) {
        const float input = n < 1200 ? static_cast<float>(tone(n, 200.0, 0.2)) : 0.0f;
        const auto output = processor.process({input, -input});
        leftPeak = std::max(leftPeak, std::abs(static_cast<double>(output.left)));
        rightPeak = std::max(rightPeak, std::abs(static_cast<double>(output.right)));
    }
    require(leftPeak > 0.05 && rightPeak > 0.05,
            "full Ping-Pong must retain anti-phase stereo material");
}

void verifyLofiEndpoint() {
    ardor::DaisyFxProcessor processor;
    configure(processor, "lofi", {{"grit", 0.0f}});
    require(processor.setParameterTarget("grit", 1.0f), "Crush update failed");
    for (int n = 0; n < 3 * 48000; ++n) (void)processor.process({});
    constexpr double r = 0.9993000; // approximate 5.35 Hz DC blocker pole
    uint32_t noise = 12345;
    float previous = 0.0f;
    int oddSteps = 0;
    for (int n = 0; n < 48000; ++n) {
        noise = noise * 1664525u + 1013904223u;
        const float input = 0.5f * (static_cast<float>(noise) / 4294967296.0f - 0.5f);
        const auto output = processor.process({input, input});
        if (n > 0) {
            const int step = static_cast<int>(std::round((output.left - r * previous) * 16.0));
            oddSteps += std::abs(step) % 2;
        }
        previous = output.left;
    }
    require(oddSteps == 0, "automated maximum Crush must reach 4-bit quantization");
}

} // namespace

int main() {
    verifyFeedbackDecays();
    verifyQuietBucketBrigade();
    verifySwellLongTime();
    verifyFirstEchoControls();
    verifyPatternHeadroom();
    verifyTimeChangeAntialiasing();
    verifyNonlinearAntialiasing();
    verifyTopologyAutomation();
    verifyDigitalMonoFold();
    verifyDualStereoSide();
    verifyLofiEndpoint();
}
