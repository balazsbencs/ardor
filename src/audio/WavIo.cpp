#include "audio/WavIo.h"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ardor {

MonoWav readMonoWav(const std::filesystem::path& path)
{
  // Leave the output channel count native while opening the file. Asking
  // miniaudio for one output channel would otherwise silently downmix a
  // stereo cabinet capture before the engine has a chance to reject it.
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
  ma_decoder decoder;
  if (ma_decoder_init_file(path.string().c_str(), &cfg, &decoder) != MA_SUCCESS) {
    throw std::runtime_error("failed to open wav: " + path.string());
  }

  MonoWav wav;
  ma_format format{};
  ma_uint32 channels = 0;
  ma_uint32 sampleRate = 0;
  if (ma_decoder_get_data_format(&decoder, &format, &channels, &sampleRate, nullptr, 0) != MA_SUCCESS) {
    ma_decoder_uninit(&decoder);
    throw std::runtime_error("failed to inspect wav: " + path.string());
  }
  if (channels != 1) {
    ma_decoder_uninit(&decoder);
    throw std::runtime_error("IR must be mono: " + path.string());
  }
  wav.sampleRate = sampleRate;
  wav.channels = channels;
  float chunk[4096];
  for (;;) {
    ma_uint64 framesRead = 0;
    const ma_result result = ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &framesRead);
    if (result != MA_SUCCESS && result != MA_AT_END) {
      ma_decoder_uninit(&decoder);
      throw std::runtime_error("failed to read wav: " + path.string());
    }
    if (framesRead == 0) break;
    wav.samples.insert(wav.samples.end(), chunk, chunk + framesRead);
  }
  ma_decoder_uninit(&decoder);
  return wav;
}

bool prepareMonoIr(MonoWav& wav, size_t maximumSamples, std::string& error)
{
  error.clear();
  if (wav.channels != 1) {
    error = "IR must be mono";
    return false;
  }
  if (wav.sampleRate == 0) {
    error = "IR sample rate is invalid";
    return false;
  }
  if (wav.samples.empty()) {
    error = "IR contains no audio frames";
    return false;
  }
  if (!std::all_of(wav.samples.begin(), wav.samples.end(), [](float sample) { return std::isfinite(sample); })) {
    error = "IR contains non-finite samples";
    return false;
  }

  if (maximumSamples > 0 && wav.samples.size() > maximumSamples) {
    wav.samples.resize(maximumSamples);
    // A 5 ms tail fade preserves the retained IR while removing the abrupt
    // edge that would otherwise add broadband energy at the truncation point.
    const size_t fadeFrames = std::min(wav.samples.size(),
                                       std::max<size_t>(1, static_cast<size_t>(wav.sampleRate / 200)));
    const size_t fadeStart = wav.samples.size() - fadeFrames;
    for (size_t i = 0; i < fadeFrames; ++i) {
      const float gain = static_cast<float>(fadeFrames - 1 - i) / static_cast<float>(fadeFrames);
      wav.samples[fadeStart + i] *= gain;
    }
  }
  return true;
}

} // namespace ardor
