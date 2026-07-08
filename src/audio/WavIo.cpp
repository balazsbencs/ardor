#include "audio/WavIo.h"

#include "miniaudio.h"

#include <stdexcept>

namespace ardor {

MonoWav readMonoWav(const std::filesystem::path& path)
{
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 48000);
  ma_decoder decoder;
  if (ma_decoder_init_file(path.string().c_str(), &cfg, &decoder) != MA_SUCCESS) {
    throw std::runtime_error("failed to open wav: " + path.string());
  }

  MonoWav wav;
  wav.sampleRate = decoder.outputSampleRate;
  float chunk[4096];
  for (;;) {
    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(&decoder, chunk, 4096, &framesRead);
    if (framesRead == 0) break;
    wav.samples.insert(wav.samples.end(), chunk, chunk + framesRead);
  }
  ma_decoder_uninit(&decoder);
  return wav;
}

} // namespace ardor
