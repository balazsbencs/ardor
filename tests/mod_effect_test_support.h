// Shared helpers for the modulation-effect test programs. Header-only so each
// test stays a single self-contained executable.
#pragma once

#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace mod_test {

inline constexpr float kSampleRate = 48000.0f;
inline constexpr double kTwoPi = 6.283185307179586;

inline void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

inline double db(double value) { return 20.0 * std::log10(std::max(value, 1e-12)); }

inline std::string fmt(double value)
{
  char buffer[32];
  std::snprintf(buffer, sizeof buffer, "%.3f", value);
  return buffer;
}

// A plucked-string stand-in for a guitar: a phrase of decaying notes with a
// 1/n harmonic series, peaking near -6 dBFS. Its energy sits below 2 kHz like
// a real guitar, which is what makes tone and level checks meaningful.
inline const std::vector<float>& guitarPhrase()
{
  static const std::vector<float> phrase = [] {
    constexpr double kNotes[] = {82.4, 110.0, 146.8, 196.0, 246.9, 329.6, 196.0, 110.0};
    constexpr int kNoteFrames = 24000;
    std::vector<float> out;
    out.reserve(std::size(kNotes) * kNoteFrames);
    for (const double f0 : kNotes) {
      for (int i = 0; i < kNoteFrames; ++i) {
        const double t = i / static_cast<double>(kSampleRate);
        double sample = 0.0;
        for (int h = 1; h <= 20 && f0 * h < 12000.0; ++h) {
          sample += std::sin(kTwoPi * f0 * h * t) * std::exp(-t * (2.0 + 0.6 * h)) / h;
        }
        out.push_back(static_cast<float>(0.3 * sample));
      }
    }
    return out;
  }();
  return phrase;
}

inline nlohmann::json defaults(const std::string& mode)
{
  const auto* descriptor = ardor::findDaisyFxDescriptor("mod", mode);
  require(descriptor != nullptr, mode + " descriptor exists");
  return ardor::defaultDaisyFxParams(*descriptor);
}

inline ardor::DaisyFxProcessor configured(const nlohmann::json& params)
{
  ardor::DaisyFxProcessor processor;
  std::string error;
  require(processor.configure("mod", params, kSampleRate, error), error);
  return processor;
}

struct Render {
  std::vector<float> left;
  std::vector<float> right;
};

inline Render render(const nlohmann::json& params, const std::vector<float>& input)
{
  auto processor = configured(params);
  Render out;
  out.left.reserve(input.size());
  out.right.reserve(input.size());
  for (const float x : input) {
    const auto y = processor.process({x, x});
    out.left.push_back(y.left);
    out.right.push_back(y.right);
  }
  return out;
}

// Renders an explicit stereo input.
inline Render renderStereo(const nlohmann::json& params, const std::vector<float>& left,
                           const std::vector<float>& right)
{
  auto processor = configured(params);
  Render out;
  out.left.reserve(left.size());
  out.right.reserve(left.size());
  for (size_t i = 0; i < left.size(); ++i) {
    const auto y = processor.process({left[i], right[i]});
    out.left.push_back(y.left);
    out.right.push_back(y.right);
  }
  return out;
}

inline double rmsDb(const Render& render)
{
  double sum = 0.0;
  for (size_t i = 0; i < render.left.size(); ++i) {
    sum += 0.5 * (render.left[i] * render.left[i] + render.right[i] * render.right[i]);
  }
  return 10.0 * std::log10(std::max(sum / render.left.size(), 1e-24));
}

// Largest sample difference between the two output channels.
inline double channelDifference(const Render& render)
{
  double diff = 0.0;
  for (size_t i = 0; i < render.left.size(); ++i) {
    diff = std::max(diff, static_cast<double>(std::fabs(render.left[i] - render.right[i])));
  }
  return diff;
}

// Engaged RMS relative to the input, in dB, across both channels.
inline double levelDb(const nlohmann::json& params)
{
  const auto& input = guitarPhrase();
  const auto out = render(params, input);
  double inSq = 0.0, outSq = 0.0;
  for (size_t i = 0; i < input.size(); ++i) {
    inSq += static_cast<double>(input[i]) * input[i];
    outSq += 0.5 * (out.left[i] * out.left[i] + out.right[i] * out.right[i]);
  }
  return db(std::sqrt(outSq / inSq));
}

inline double maxDifference(const nlohmann::json& a, const nlohmann::json& b)
{
  const std::vector<float> input(guitarPhrase().begin(), guitarPhrase().begin() + 48000);
  const auto ra = render(a, input), rb = render(b, input);
  double diff = 0.0;
  for (size_t i = 0; i < input.size(); ++i) {
    diff = std::max(diff, static_cast<double>(std::fabs(ra.left[i] - rb.left[i])));
    diff = std::max(diff, static_cast<double>(std::fabs(ra.right[i] - rb.right[i])));
  }
  return diff;
}

inline std::vector<float> sine(double hz, float amplitude, int frames)
{
  std::vector<float> out(frames);
  for (int i = 0; i < frames; ++i) {
    out[i] = amplitude * static_cast<float>(std::sin(kTwoPi * hz * i / kSampleRate));
  }
  return out;
}

// Level of harmonic `h` relative to the fundamental, on the left channel,
// after the first second has settled.
inline double harmonicDbc(const std::vector<float>& signal, double f0, int h)
{
  const size_t start = 48000;
  double re1 = 0, im1 = 0, reh = 0, imh = 0;
  for (size_t i = start; i < signal.size(); ++i) {
    const double w = kTwoPi * f0 * static_cast<double>(i) / kSampleRate;
    re1 += signal[i] * std::cos(w);
    im1 += signal[i] * std::sin(w);
    reh += signal[i] * std::cos(w * h);
    imh += signal[i] * std::sin(w * h);
  }
  return db(std::hypot(reh, imh) / std::hypot(re1, im1));
}

// Instantaneous frequency from interpolated rising zero crossings. Amplitude
// modulation does not move zero crossings, so this isolates Doppler pitch.
inline std::vector<double> frequencyTrack(const std::vector<float>& signal, size_t start)
{
  std::vector<double> crossings;
  for (size_t i = start + 1; i < signal.size(); ++i) {
    if (signal[i - 1] < 0.0f && signal[i] >= 0.0f) {
      crossings.push_back(static_cast<double>(i - 1) + signal[i - 1] / (signal[i - 1] - signal[i]));
    }
  }
  std::vector<double> hz;
  for (size_t i = 1; i < crossings.size(); ++i) {
    hz.push_back(kSampleRate / (crossings[i] - crossings[i - 1]));
  }
  return hz;
}

} // namespace mod_test
