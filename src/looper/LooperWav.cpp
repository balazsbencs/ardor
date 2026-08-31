#include "looper/LooperWav.h"

#include <array>
#include <bit>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>

namespace ardor {
namespace {

void writeU16(std::ostream& output, uint16_t value)
{
  const std::array<char, 2> bytes = {
    static_cast<char>(value & 0xffU), static_cast<char>((value >> 8U) & 0xffU),
  };
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeU32(std::ostream& output, uint32_t value)
{
  const std::array<char, 4> bytes = {
    static_cast<char>(value & 0xffU), static_cast<char>((value >> 8U) & 0xffU),
    static_cast<char>((value >> 16U) & 0xffU), static_cast<char>((value >> 24U) & 0xffU),
  };
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool readU16(std::istream& input, uint16_t& value)
{
  std::array<unsigned char, 2> bytes {};
  input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input) return false;
  value = static_cast<uint16_t>(bytes[0])
    | static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8U);
  return true;
}

bool readU32(std::istream& input, uint32_t& value)
{
  std::array<unsigned char, 4> bytes {};
  input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input) return false;
  value = static_cast<uint32_t>(bytes[0])
    | (static_cast<uint32_t>(bytes[1]) << 8U)
    | (static_cast<uint32_t>(bytes[2]) << 16U)
    | (static_cast<uint32_t>(bytes[3]) << 24U);
  return true;
}

bool fourccEquals(const std::array<char, 4>& value, const char* expected)
{
  return value[0] == expected[0] && value[1] == expected[1]
      && value[2] == expected[2] && value[3] == expected[3];
}

bool spansCover(const LooperPausedTrackView& track, std::size_t frames)
{
  if (track.baseLeft.size() < frames || track.baseRight.size() < frames) return false;
  return !track.lastTakeValid
      || (track.lastTakeLeft.size() >= frames && track.lastTakeRight.size() >= frames);
}

} // namespace

bool writeLooperFloatWav(const std::filesystem::path& path,
                         const LooperPausedTrackView& track,
                         uint64_t loopFrames,
                         uint32_t sampleRate,
                         std::string& error)
{
  error.clear();
  if (!track.present || loopFrames == 0 || sampleRate != 48000
      || loopFrames > std::numeric_limits<uint32_t>::max() / (2U * sizeof(float))) {
    error = "invalid looper WAV description";
    return false;
  }
  const auto frames = static_cast<std::size_t>(loopFrames);
  if (!spansCover(track, frames)) {
    error = "looper track buffers are shorter than the master loop";
    return false;
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error = "failed to create loop audio: " + path.string();
    return false;
  }
  const auto dataBytes = static_cast<uint32_t>(loopFrames * 2U * sizeof(float));
  output.write("RIFF", 4);
  writeU32(output, 36U + dataBytes);
  output.write("WAVEfmt ", 8);
  writeU32(output, 16);
  writeU16(output, 3); // IEEE float
  writeU16(output, 2);
  writeU32(output, sampleRate);
  writeU32(output, sampleRate * 2U * sizeof(float));
  writeU16(output, 2U * sizeof(float));
  writeU16(output, 32);
  output.write("data", 4);
  writeU32(output, dataBytes);

  for (std::size_t frame = 0; frame < frames; ++frame) {
    float left = track.baseLeft[frame];
    float right = track.baseRight[frame];
    if (track.lastTakeValid && track.lastTakeAudible) {
      left += track.lastTakeLeft[frame];
      right += track.lastTakeRight[frame];
    }
    if (!std::isfinite(left) || !std::isfinite(right)) {
      error = "loop audio contains a non-finite sample";
      output.close();
      return false;
    }
    writeU32(output, std::bit_cast<uint32_t>(left));
    writeU32(output, std::bit_cast<uint32_t>(right));
  }
  output.flush();
  if (!output.good()) {
    error = "failed to write loop audio: " + path.string();
    return false;
  }
  output.close();
  if (output.fail()) {
    error = "failed to close loop audio: " + path.string();
    return false;
  }
  return true;
}

bool readLooperFloatWav(const std::filesystem::path& path,
                        uint64_t maximumFrames,
                        LooperStereoAudio& audio,
                        std::string& error)
{
  error.clear();
  audio = {};
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "failed to open loop audio: " + path.string();
    return false;
  }
  std::array<char, 4> id {};
  uint32_t riffSize = 0;
  input.read(id.data(), 4);
  if (!input || !fourccEquals(id, "RIFF") || !readU32(input, riffSize)) {
    error = "loop audio is not a RIFF file";
    return false;
  }
  input.read(id.data(), 4);
  if (!input || !fourccEquals(id, "WAVE")) {
    error = "loop audio is not a WAVE file";
    return false;
  }

  bool formatFound = false;
  bool dataFound = false;
  uint16_t format = 0;
  uint16_t channels = 0;
  uint16_t blockAlign = 0;
  uint16_t bitsPerSample = 0;
  uint32_t sampleRate = 0;
  uint32_t dataBytes = 0;
  std::streampos dataPosition {};
  while (input && (!formatFound || !dataFound)) {
    input.read(id.data(), 4);
    uint32_t chunkBytes = 0;
    if (!input || !readU32(input, chunkBytes)) break;
    const auto chunkStart = input.tellg();
    if (fourccEquals(id, "fmt ")) {
      uint32_t byteRate = 0;
      if (chunkBytes < 16 || !readU16(input, format) || !readU16(input, channels)
          || !readU32(input, sampleRate) || !readU32(input, byteRate)
          || !readU16(input, blockAlign) || !readU16(input, bitsPerSample)) {
        error = "loop audio has a malformed format chunk";
        return false;
      }
      formatFound = true;
    } else if (fourccEquals(id, "data")) {
      dataBytes = chunkBytes;
      dataPosition = input.tellg();
      dataFound = true;
    }
    const auto paddedBytes = static_cast<std::streamoff>(chunkBytes)
      + static_cast<std::streamoff>(chunkBytes & 1U);
    input.seekg(chunkStart + paddedBytes);
  }
  (void)riffSize;
  if (!formatFound || !dataFound || format != 3 || channels != 2
      || sampleRate != 48000 || bitsPerSample != 32 || blockAlign != 8
      || dataBytes == 0 || dataBytes % 8U != 0) {
    error = "loop audio must be 48 kHz stereo IEEE-float WAV";
    return false;
  }
  const uint64_t frames = dataBytes / 8U;
  if (frames > maximumFrames || frames > std::numeric_limits<std::size_t>::max()) {
    error = "loop audio exceeds the prepared memory tier";
    return false;
  }
  try {
    audio.left.resize(static_cast<std::size_t>(frames));
    audio.right.resize(static_cast<std::size_t>(frames));
  } catch (const std::exception&) {
    error = "unable to allocate loop audio while loading";
    audio = {};
    return false;
  }
  audio.sampleRate = sampleRate;
  input.clear();
  input.seekg(dataPosition);
  for (std::size_t frame = 0; frame < audio.left.size(); ++frame) {
    uint32_t leftBits = 0;
    uint32_t rightBits = 0;
    if (!readU32(input, leftBits) || !readU32(input, rightBits)) {
      error = "loop audio ended before its declared frame count";
      audio = {};
      return false;
    }
    audio.left[frame] = std::bit_cast<float>(leftBits);
    audio.right[frame] = std::bit_cast<float>(rightBits);
    if (!std::isfinite(audio.left[frame]) || !std::isfinite(audio.right[frame])) {
      error = "loop audio contains a non-finite sample";
      audio = {};
      return false;
    }
  }
  return true;
}

} // namespace ardor
