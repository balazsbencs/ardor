#include "miniaudio.h"

#include "audio/MiniaudioBackend.h"
#include "dsp/PedalEngine.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct Args {
  bool offline = false;
  bool realtime = false;
  bool bypassNam = false;
  bool devices = false;
  uint32_t sampleRate = 48000;
  uint32_t blockSize = 64;
  size_t irSamples = 0;
  int captureDeviceIndex = -1;
  int playbackDeviceIndex = -1;
  uint32_t inputChannel = 0;
  ardor::OutputChannel outputChannel = ardor::OutputChannel::Both;
  std::filesystem::path model;
  std::filesystem::path ir;
  std::filesystem::path input;
  std::filesystem::path output;
};

bool parse(int argc, char** argv, Args& args)
{
  auto parseChannel = [](const std::string& value, uint32_t& channel) {
    if (value == "left" || value == "0") {
      channel = 0;
      return true;
    }
    if (value == "right" || value == "1") {
      channel = 1;
      return true;
    }
    return false;
  };

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto value = [&]() -> const char* {
      if (i + 1 >= argc) return nullptr;
      return argv[++i];
    };

    if (a == "--version") {
      std::cout << "ardor pedal poc\n";
      std::exit(0);
    } else if (a == "--offline") {
      args.offline = true;
    } else if (a == "--realtime") {
      args.realtime = true;
    } else if (a == "--devices") {
      args.devices = true;
    } else if (a == "--bypass-nam") {
      args.bypassNam = true;
    } else if (a == "--sample-rate") {
      const char* v = value();
      if (!v) return false;
      args.sampleRate = static_cast<uint32_t>(std::stoul(v));
    } else if (a == "--block-size") {
      const char* v = value();
      if (!v) return false;
      args.blockSize = static_cast<uint32_t>(std::stoul(v));
    } else if (a == "--ir-samples") {
      const char* v = value();
      if (!v) return false;
      args.irSamples = static_cast<size_t>(std::stoull(v));
    } else if (a == "--capture-device") {
      const char* v = value();
      if (!v) return false;
      args.captureDeviceIndex = std::stoi(v);
    } else if (a == "--playback-device") {
      const char* v = value();
      if (!v) return false;
      args.playbackDeviceIndex = std::stoi(v);
    } else if (a == "--input-channel") {
      const char* v = value();
      if (!v || !parseChannel(v, args.inputChannel)) return false;
    } else if (a == "--output-channel") {
      const char* v = value();
      if (!v) return false;
      const std::string channel = v;
      if (channel == "both") args.outputChannel = ardor::OutputChannel::Both;
      else if (channel == "left") args.outputChannel = ardor::OutputChannel::Left;
      else if (channel == "right") args.outputChannel = ardor::OutputChannel::Right;
      else return false;
    } else if (a == "--model") {
      const char* v = value();
      if (!v) return false;
      args.model = v;
    } else if (a == "--ir") {
      const char* v = value();
      if (!v) return false;
      args.ir = v;
    } else if (a == "--input") {
      const char* v = value();
      if (!v) return false;
      args.input = v;
    } else if (a == "--output") {
      const char* v = value();
      if (!v) return false;
      args.output = v;
    } else {
      return false;
    }
  }

  if (args.devices) return true;
  if (args.offline) {
    return !args.ir.empty() && !args.input.empty() && !args.output.empty() && (args.bypassNam || !args.model.empty());
  }
  if (args.realtime) {
    return !args.ir.empty() && !args.model.empty();
  }
  return false;
}

std::vector<float> readMono(const std::filesystem::path& path, ma_uint32& sampleRate)
{
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 48000);
  ma_decoder decoder;
  if (ma_decoder_init_file(path.string().c_str(), &cfg, &decoder) != MA_SUCCESS) {
    throw std::runtime_error("failed to open wav: " + path.string());
  }

  sampleRate = decoder.outputSampleRate;
  std::vector<float> samples;
  float chunk[4096];
  for (;;) {
    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &framesRead);
    if (framesRead == 0) break;
    samples.insert(samples.end(), chunk, chunk + framesRead);
  }
  ma_decoder_uninit(&decoder);
  return samples;
}

void writeStereo(const std::filesystem::path& path, const std::vector<float>& interleaved, ma_uint32 sampleRate)
{
  ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, sampleRate);
  ma_encoder encoder;
  if (ma_encoder_init_file(path.string().c_str(), &cfg, &encoder) != MA_SUCCESS) {
    throw std::runtime_error("failed to create wav: " + path.string());
  }
  ma_encoder_write_pcm_frames(&encoder, interleaved.data(), interleaved.size() / 2, nullptr);
  ma_encoder_uninit(&encoder);
}

int printDevices()
{
  ma_context context;
  if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
    std::cerr << "Failed to initialize audio context\n";
    return 1;
  }

  ma_device_info* playback = nullptr;
  ma_uint32 playbackCount = 0;
  ma_device_info* capture = nullptr;
  ma_uint32 captureCount = 0;
  if (ma_context_get_devices(&context, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS) {
    ma_context_uninit(&context);
    std::cerr << "Failed to enumerate audio devices\n";
    return 1;
  }

  std::cout << "Playback devices:\n";
  for (ma_uint32 i = 0; i < playbackCount; ++i) {
    std::cout << "  [" << i << "] " << playback[i].name << "\n";
  }
  std::cout << "Capture devices:\n";
  for (ma_uint32 i = 0; i < captureCount; ++i) {
    std::cout << "  [" << i << "] " << capture[i].name << "\n";
  }

  ma_context_uninit(&context);
  return 0;
}

} // namespace

int main(int argc, char** argv)
{
  try {
    Args args;
    if (!parse(argc, argv, args)) {
      std::cerr << "Usage:\n"
                << "  pedal-poc --devices\n"
                << "  pedal-poc --offline --ir cab.wav --input dry.wav --output wet.wav (--model amp.nam | --bypass-nam)\n"
                << "  pedal-poc --realtime --model amp.nam --ir cab.wav [--sample-rate 48000] [--block-size 64]\n"
                << "            [--capture-device N] [--playback-device N] [--input-channel left|right]\n"
                << "            [--output-channel both|left|right] [--ir-samples N]\n";
      return 2;
    }

    if (args.devices) {
      return printDevices();
    }

    ma_uint32 irRate = 0;
    auto impulse = readMono(args.ir, irRate);
    if (irRate != args.sampleRate) {
      std::cerr << "Expected " << args.sampleRate << " Hz IR\n";
      return 1;
    }

    ardor::PedalEngine engine;
    if (args.realtime) {
      const size_t maxRealtimeIr = args.irSamples == 0 ? 4096 : args.irSamples;
      if (impulse.size() > maxRealtimeIr) {
        // ponytail: naive FIR; use partitioned convolution when long realtime IRs matter.
        impulse.resize(maxRealtimeIr);
        std::cerr << "Trimmed realtime IR to " << maxRealtimeIr << " samples\n";
      }
    } else if (args.irSamples > 0 && impulse.size() > args.irSamples) {
      impulse.resize(args.irSamples);
      std::cerr << "Trimmed IR to " << args.irSamples << " samples\n";
    }
    engine.loadIr(std::move(impulse));

    if (args.realtime) {
      if (!engine.loadNam(args.model, args.sampleRate, static_cast<int>(args.blockSize))) {
        std::cerr << "Failed to load NAM model\n";
        return 1;
      }

      ardor::RealtimeOptions options;
      options.sampleRate = args.sampleRate;
      options.blockSize = args.blockSize;
      options.captureDeviceIndex = args.captureDeviceIndex;
      options.playbackDeviceIndex = args.playbackDeviceIndex;
      options.inputChannel = args.inputChannel;
      options.outputChannel = args.outputChannel;

      ardor::MiniaudioBackend backend;
      if (!backend.start(engine, options)) {
        std::cerr << "Failed to start realtime audio\n";
        return 1;
      }

      for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto stats = backend.stats();
        const double overPercent = stats.callbacks == 0
                                     ? 0.0
                                     : static_cast<double>(stats.overBudget) * 100.0 / static_cast<double>(stats.callbacks);
        std::cerr << std::fixed << std::setprecision(2)
                  << "callbacks=" << stats.callbacks
                  << " over=" << stats.overBudget
                  << " over%=" << overPercent
                  << " max=" << stats.maxMs << "ms"
                  << " avg=" << stats.averageMs << "ms"
                  << " budget=" << stats.budgetMs << "ms"
                  << "\n";
      }
    }

    ma_uint32 inputRate = 0;
    auto input = readMono(args.input, inputRate);
    if (inputRate != args.sampleRate) {
      std::cerr << "Expected " << args.sampleRate << " Hz input\n";
      return 1;
    }

    if (!args.bypassNam && !engine.loadNam(args.model, args.sampleRate, 128)) {
      std::cerr << "Failed to load NAM model\n";
      return 1;
    }

    std::vector<float> out;
    out.reserve(input.size() * 2);
    for (float x : input) {
      const auto [left, right] = engine.process(x);
      out.push_back(left);
      out.push_back(right);
    }

    writeStereo(args.output, out, inputRate);
    std::cerr << "Wrote " << args.output << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
