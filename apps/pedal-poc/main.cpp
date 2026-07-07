#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "dsp/PedalEngine.h"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Args {
  bool offline = false;
  bool bypassNam = false;
  std::filesystem::path model;
  std::filesystem::path ir;
  std::filesystem::path input;
  std::filesystem::path output;
};

bool parse(int argc, char** argv, Args& args)
{
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
    } else if (a == "--bypass-nam") {
      args.bypassNam = true;
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

  return args.offline && !args.ir.empty() && !args.input.empty() && !args.output.empty()
         && (args.bypassNam || !args.model.empty());
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

} // namespace

int main(int argc, char** argv)
{
  try {
    Args args;
    if (!parse(argc, argv, args)) {
      std::cerr << "Usage: pedal-poc --offline --ir cab.wav --input dry.wav --output wet.wav (--model amp.nam | --bypass-nam)\n";
      return 2;
    }

    ma_uint32 inputRate = 0;
    ma_uint32 irRate = 0;
    auto input = readMono(args.input, inputRate);
    auto impulse = readMono(args.ir, irRate);
    if (inputRate != 48000 || irRate != 48000) {
      std::cerr << "Expected 48000 Hz input and IR\n";
      return 1;
    }

    ardor::PedalEngine engine;
    engine.loadIr(std::move(impulse));
    if (!args.bypassNam && !engine.loadNam(args.model, 48000.0, 128)) {
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
