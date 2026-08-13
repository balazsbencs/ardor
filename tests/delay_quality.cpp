// Objective hosted-delay probe and optional listening-render generator.
// This executable reports measurements; it does not impose subjective pass/fail
// thresholds and therefore is intentionally not registered as a ctest.

#include "daisyfx/DaisyFxCatalog.h"
#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/hosted/dsp/delay_line_sdram.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr float kTwoPi = 6.28318530717958647692f;

struct StereoBuffer {
  std::vector<float> left;
  std::vector<float> right;
};

struct Metrics {
  double peakDb = -240.0;
  double rmsDb = -240.0;
  double energyDb = -240.0;
  double correlation = 0.0;
  double dcDb = -240.0;
  double maxStep = 0.0;
  double p999Step = 0.0;
  std::size_t onsetLeft = 0;
  std::size_t onsetRight = 0;
};

double db(double value, double floor = 1e-12)
{
  return 20.0 * std::log10(std::max(value, floor));
}

uint32_t nextNoise(uint32_t& state)
{
  state = state * 1664525u + 1013904223u;
  return state;
}

float noiseSample(uint32_t& state)
{
  return (static_cast<float>(nextNoise(state) >> 8) / static_cast<float>(1u << 24) - 0.5f) * 2.0f;
}

std::size_t onset(const std::vector<float>& samples, double threshold)
{
  const auto iterator = std::find_if(samples.begin(), samples.end(),
    [threshold](float sample) { return std::fabs(sample) >= threshold; });
  return iterator == samples.end() ? std::numeric_limits<std::size_t>::max()
                                   : static_cast<std::size_t>(iterator - samples.begin());
}

double percentile(std::vector<double> values, double fraction)
{
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(
    std::ceil(fraction * static_cast<double>(values.size() - 1)));
  return values[index];
}

Metrics measure(const StereoBuffer& buffer)
{
  if (buffer.left.size() != buffer.right.size() || buffer.left.empty()) {
    throw std::runtime_error("invalid stereo measurement buffer");
  }
  double peak = 0.0;
  double sumLeft = 0.0;
  double sumRight = 0.0;
  double sumCross = 0.0;
  double sum = 0.0;
  double energy = 0.0;
  std::vector<double> steps;
  steps.reserve(buffer.left.size() * 2);
  for (std::size_t frame = 0; frame < buffer.left.size(); ++frame) {
    const double left = buffer.left[frame];
    const double right = buffer.right[frame];
    if (!std::isfinite(left) || !std::isfinite(right)) {
      throw std::runtime_error("non-finite delay response");
    }
    peak = std::max({peak, std::fabs(left), std::fabs(right)});
    sumLeft += left * left;
    sumRight += right * right;
    sumCross += left * right;
    sum += left + right;
    energy += left * left + right * right;
    if (frame != 0) {
      steps.push_back(std::fabs(left - buffer.left[frame - 1]));
      steps.push_back(std::fabs(right - buffer.right[frame - 1]));
    }
  }
  const double count = static_cast<double>(buffer.left.size() * 2);
  Metrics result;
  result.peakDb = db(peak);
  result.rmsDb = db(std::sqrt(energy / count));
  result.energyDb = 10.0 * std::log10(std::max(energy, 1e-24));
  result.correlation = sumCross / std::sqrt(std::max(sumLeft * sumRight, 1e-24));
  result.dcDb = db(std::fabs(sum / count));
  result.maxStep = steps.empty() ? 0.0 : *std::max_element(steps.begin(), steps.end());
  result.p999Step = percentile(std::move(steps), 0.999);
  const double onsetThreshold = std::max(peak * 1e-4, 1e-7);
  result.onsetLeft = onset(buffer.left, onsetThreshold);
  result.onsetRight = onset(buffer.right, onsetThreshold);
  return result;
}

void writeU16(std::ofstream& stream, uint16_t value)
{
  const std::array<char, 2> bytes{
    static_cast<char>(value & 0xff), static_cast<char>((value >> 8) & 0xff)};
  stream.write(bytes.data(), bytes.size());
}

void writeU32(std::ofstream& stream, uint32_t value)
{
  const std::array<char, 4> bytes{
    static_cast<char>(value & 0xff), static_cast<char>((value >> 8) & 0xff),
    static_cast<char>((value >> 16) & 0xff), static_cast<char>((value >> 24) & 0xff)};
  stream.write(bytes.data(), bytes.size());
}

void writeFloatWav(const std::filesystem::path& path, const StereoBuffer& buffer)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary);
  if (!stream) throw std::runtime_error("failed to create " + path.string());
  const uint32_t dataBytes = static_cast<uint32_t>(buffer.left.size() * 2 * sizeof(float));
  stream.write("RIFF", 4);
  writeU32(stream, 36U + dataBytes);
  stream.write("WAVEfmt ", 8);
  writeU32(stream, 16);
  writeU16(stream, 3); // IEEE float
  writeU16(stream, 2);
  writeU32(stream, static_cast<uint32_t>(kSampleRate));
  writeU32(stream, static_cast<uint32_t>(kSampleRate) * 2U * sizeof(float));
  writeU16(stream, 2U * sizeof(float));
  writeU16(stream, 32);
  stream.write("data", 4);
  writeU32(stream, dataBytes);
  for (std::size_t frame = 0; frame < buffer.left.size(); ++frame) {
    stream.write(reinterpret_cast<const char*>(&buffer.left[frame]), sizeof(float));
    stream.write(reinterpret_cast<const char*>(&buffer.right[frame]), sizeof(float));
  }
  if (!stream) throw std::runtime_error("failed to write " + path.string());
}

nlohmann::json neutralParams(const ardor::DaisyFxDescriptor& descriptor)
{
  auto params = ardor::defaultDaisyFxParams(descriptor);
  params["time"] = 0.2f;
  params["repeats"] = 0.0f;
  params["mix"] = 1.0f;
  params["filter"] = 0.5f;
  params["grit"] = 0.0f;
  params["mod_spd"] = 0.0f;
  params["mod_dep"] = 0.0f;
  return params;
}

nlohmann::json characterParams(const ardor::DaisyFxDescriptor& descriptor)
{
  auto params = ardor::defaultDaisyFxParams(descriptor);
  params["time"] = 0.42f;
  params["repeats"] = 0.58f;
  params["mix"] = 1.0f;
  params["filter"] = 0.42f;
  params["grit"] = 0.5f;
  params["mod_spd"] = 0.38f;
  params["mod_dep"] = 0.28f;
  if (descriptor.mode == "digital") params["grit"] = 0.2f;
  if (descriptor.mode == "dual") params["grit"] = 1.0f;
  if (descriptor.mode == "filter") {
    params["grit"] = 0.5f;
    params["filter"] = 0.35f;
    params["mod_dep"] = 0.55f;
  }
  if (descriptor.mode == "lofi") params["grit"] = 0.62f;
  if (descriptor.mode == "dbucket" || descriptor.mode == "tape") params["grit"] = 0.55f;
  if (descriptor.mode == "duck") params["grit"] = 0.8f;
  if (descriptor.mode == "pattern") params["grit"] = 0.5f;
  if (descriptor.mode == "swell") {
    params["grit"] = 0.12f;
    params["mod_spd"] = 0.72f;
    params["mod_dep"] = 0.45f;
  }
  if (descriptor.mode == "trem") {
    params["grit"] = 0.55f;
    params["mod_spd"] = 0.45f;
    params["mod_dep"] = 0.75f;
  }
  return params;
}

StereoBuffer renderImpulse(const ardor::DaisyFxDescriptor& descriptor)
{
  constexpr std::size_t frames = 3U * static_cast<std::size_t>(kSampleRate);
  ardor::DaisyFxProcessor processor;
  std::string error;
  if (!processor.configure("delay", neutralParams(descriptor), kSampleRate, error)) {
    throw std::runtime_error(descriptor.mode + ": " + error);
  }
  StereoBuffer output{{}, {}};
  output.left.resize(frames);
  output.right.resize(frames);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const float impulse = frame == 0 ? 1.0f : 0.0f;
    const auto sample = processor.process({impulse, impulse});
    output.left[frame] = sample.left;
    output.right[frame] = sample.right;
  }
  return output;
}

ardor::StereoSample phraseInput(std::size_t frame)
{
  if (frame >= 2U * static_cast<std::size_t>(kSampleRate)) return {};
  const float time = static_cast<float>(frame) / kSampleRate;
  const std::size_t noteFrame = frame % 12000U;
  const std::size_t note = frame / 12000U;
  if (noteFrame >= 7200U) return {};
  static constexpr std::array<float, 8> frequencies{110.0f, 146.832f, 164.814f, 220.0f,
                                                    196.0f, 164.814f, 146.832f, 110.0f};
  const float localTime = static_cast<float>(noteFrame) / kSampleRate;
  const float envelope = std::exp(-3.8f * localTime) * std::min(1.0f, localTime * 300.0f);
  const float frequency = frequencies[note % frequencies.size()];
  const float body = std::sin(kTwoPi * frequency * time)
                   + 0.38f * std::sin(kTwoPi * frequency * 2.01f * time)
                   + 0.17f * std::sin(kTwoPi * frequency * 3.98f * time);
  uint32_t pickState = 0x1234567u ^ static_cast<uint32_t>(noteFrame + note * 65537U);
  const float pick = noteFrame < 180U ? noiseSample(pickState) * (1.0f - noteFrame / 180.0f) : 0.0f;
  const float left = (body * 0.22f + pick * 0.08f) * envelope;
  const float right = (body * 0.19f - pick * 0.05f) * envelope;
  return {left, right};
}

StereoBuffer renderCharacter(const ardor::DaisyFxDescriptor& descriptor)
{
  constexpr std::size_t frames = 5U * static_cast<std::size_t>(kSampleRate);
  ardor::DaisyFxProcessor processor;
  std::string error;
  if (!processor.configure("delay", characterParams(descriptor), kSampleRate, error)) {
    throw std::runtime_error(descriptor.mode + ": " + error);
  }
  StereoBuffer output{{}, {}};
  output.left.resize(frames);
  output.right.resize(frames);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    const auto sample = processor.process(phraseInput(frame));
    output.left[frame] = sample.left;
    output.right[frame] = sample.right;
  }
  return output;
}

StereoBuffer renderAutomation(const ardor::DaisyFxDescriptor& descriptor)
{
  constexpr std::size_t frames = 4U * static_cast<std::size_t>(kSampleRate);
  auto params = characterParams(descriptor);
  params["time"] = 0.12f;
  params["repeats"] = 0.7f;
  ardor::DaisyFxProcessor processor;
  std::string error;
  if (!processor.configure("delay", params, kSampleRate, error)) {
    throw std::runtime_error(descriptor.mode + ": " + error);
  }
  StereoBuffer output{{}, {}};
  output.left.resize(frames);
  output.right.resize(frames);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    if (frame == 48000U) processor.setParameterTarget("time", 0.88f);
    if (frame == 96000U) processor.setParameterTarget("time", 0.28f);
    if (frame == 144000U) processor.setParameterTarget("time", 0.66f);
    const float time = static_cast<float>(frame) / kSampleRate;
    const ardor::StereoSample input{
      0.25f * std::sin(kTwoPi * 173.0f * time),
      0.22f * std::sin(kTwoPi * 277.0f * time)};
    const auto sample = processor.process(input);
    output.left[frame] = sample.left;
    output.right[frame] = sample.right;
  }
  return output;
}

void printMetrics(std::string_view mode, std::string_view scenario, const Metrics& metrics)
{
  const auto printOnset = [](std::size_t value) -> long long {
    return value == std::numeric_limits<std::size_t>::max() ? -1LL : static_cast<long long>(value);
  };
  std::cout << mode << ',' << scenario << ',' << printOnset(metrics.onsetLeft) << ','
            << printOnset(metrics.onsetRight) << ',' << metrics.peakDb << ',' << metrics.rmsDb << ','
            << metrics.energyDb << ',' << metrics.correlation << ',' << metrics.dcDb << ','
            << metrics.maxStep << ',' << metrics.p999Step << '\n';
}

void reportInterpolation()
{
  constexpr std::size_t bufferSize = 65536;
  constexpr std::size_t settle = 8192;
  constexpr std::size_t measureFrames = 32768;
  constexpr std::array<float, 5> fractions{0.1f, 0.25f, 0.5f, 0.75f, 0.9f};
  constexpr std::array<float, 5> frequencies{1000.0f, 5000.0f, 10000.0f, 15000.0f, 20000.0f};
  std::vector<float> storage(bufferSize);
  for (const float fraction : fractions) {
    for (const float frequency : frequencies) {
      for (const bool highQuality : {false, true}) {
        pedal::DelayLineSdram line;
        line.Init(storage.data(), storage.size());
        line.SetDelay(100.0f + fraction);
        double outputEnergy = 0.0;
        double inputEnergy = 0.0;
        for (std::size_t frame = 0; frame < settle + measureFrames; ++frame) {
          const float input = std::sin(kTwoPi * frequency * static_cast<float>(frame) / kSampleRate);
          const float output = highQuality ? line.ReadHighQuality() : line.Read();
          line.Write(input);
          if (frame >= settle) {
            outputEnergy += static_cast<double>(output) * output;
            inputEnergy += static_cast<double>(input) * input;
          }
        }
        const double gain = std::sqrt(outputEnergy / std::max(inputEnergy, 1e-24));
        std::cout << "# interpolation," << (highQuality ? "sinc16" : "cubic") << ','
                  << fraction << ',' << frequency << ',' << db(gain) << "\n";
      }
    }
  }
}

} // namespace

int main(int argc, char** argv)
{
  std::filesystem::path renderDirectory;
  if (argc == 3 && std::string_view{argv[1]} == "--render-dir") {
    renderDirectory = argv[2];
  } else if (argc != 1) {
    std::cerr << "Usage: " << argv[0] << " [--render-dir DIRECTORY]\n";
    return 2;
  }

  std::cout << "# quality=ardor-delay-quality-v1\n"
            << "# sample_rate=" << static_cast<unsigned>(kSampleRate) << "\n"
            << "mode,scenario,onset_left_frames,onset_right_frames,peak_dbfs,rms_dbfs,"
               "energy_db,lr_correlation,dc_dbfs,max_step,p999_step\n"
            << std::fixed << std::setprecision(6);
  try {
    for (const auto& descriptor : ardor::daisyFxCatalog()) {
      if (descriptor.blockType != "delay") continue;
      const auto impulse = renderImpulse(descriptor);
      printMetrics(descriptor.mode, "impulse", measure(impulse));

      const auto character = renderCharacter(descriptor);
      printMetrics(descriptor.mode, "character", measure(character));
      if (!renderDirectory.empty()) {
        writeFloatWav(renderDirectory / (descriptor.mode + "-character.wav"), character);
      }

      const auto automation = renderAutomation(descriptor);
      printMetrics(descriptor.mode, "automation", measure(automation));
      if (!renderDirectory.empty()) {
        writeFloatWav(renderDirectory / (descriptor.mode + "-automation.wav"), automation);
      }
    }
    reportInterpolation();
  } catch (const std::exception& exception) {
    std::cerr << "delay-quality: " << exception.what() << '\n';
    return 1;
  }
  return 0;
}
