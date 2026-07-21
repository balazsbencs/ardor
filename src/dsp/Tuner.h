#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ardor {

struct TunerReading {
  bool signalDetected = false;
  float frequencyHz = 0.0f;
  float cents = 0.0f;
  float confidence = 0.0f;
  std::string note = "--";
  int octave = 0;
  uint64_t revision = 0;
};

// Monophonic guitar tuner using a downsampled YIN-style difference function.
// It runs on the control thread; the audio callback only copies raw capture
// samples into a lock-free monitor ring.
class TunerAnalyzer {
public:
  explicit TunerAnalyzer(float sampleRate = 48000.0f);

  void reset();
  void process(const float* samples, std::size_t count);
  const TunerReading& reading() const noexcept { return reading_; }

private:
  void pushDownsampled(float sample);
  void analyze();
  void publishNoSignal();

  static constexpr std::size_t kDownsampleFactor = 4;
  static constexpr std::size_t kWindowSize = 2048;
  static constexpr std::size_t kHopSize = 1024;

  float inputSampleRate_ = 48000.0f;
  float analysisSampleRate_ = 12000.0f;
  std::vector<float> ring_;
  std::vector<float> window_;
  std::vector<float> difference_;
  std::size_t writePosition_ = 0;
  std::size_t available_ = 0;
  std::size_t sinceAnalysis_ = 0;
  float downsampleSum_ = 0.0f;
  std::size_t downsampleCount_ = 0;
  TunerReading reading_;
};

} // namespace ardor
