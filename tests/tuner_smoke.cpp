#include "dsp/Tuner.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
  if (!condition) throw std::runtime_error(message);
}

void feedTone(ardor::TunerAnalyzer& tuner, float frequency, float amplitude, std::size_t frames)
{
  constexpr float sampleRate = 48000.0f;
  constexpr float twoPi = 6.2831853071795864769f;
  std::vector<float> block(257);
  std::size_t position = 0;
  while (position < frames) {
    const std::size_t count = std::min(block.size(), frames - position);
    for (std::size_t i = 0; i < count; ++i) {
      block[i] = amplitude * std::sin(twoPi * frequency
                                      * static_cast<float>(position + i) / sampleRate);
    }
    tuner.process(block.data(), count);
    position += count;
  }
}

} // namespace

int main()
{
  try {
    ardor::TunerAnalyzer tuner;
    feedTone(tuner, 82.4069f, 0.2f, 8192);
    auto reading = tuner.reading();
    require(reading.signalDetected, "low E should be detected");
    require(reading.note == "E" && reading.octave == 2, "low E note label");
    require(std::fabs(reading.frequencyHz - 82.4069f) < 0.8f, "low E frequency accuracy");
    require(std::fabs(reading.cents) < 8.0f, "low E cents accuracy");

    tuner.reset();
    feedTone(tuner, 112.0f, 0.2f, 8192);
    reading = tuner.reading();
    require(reading.signalDetected && reading.note == "A" && reading.octave == 2,
            "sharp A should identify its nearest note");
    require(reading.cents > 20.0f, "sharp A should report positive cents");

    tuner.reset();
    feedTone(tuner, 110.0f, 0.0001f, 48000);
    require(!tuner.reading().signalDetected, "sub-threshold noise should not produce a note");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "tuner_smoke failed: " << error.what() << '\n';
    return 1;
  }
}
