#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace pog3_test {
// Offline IEEE-float WAVs. Preserve raw peaks: no normalization or limiting.
template<class Stereo>
void writeRender(const std::filesystem::path& path, const std::vector<Stereo>& frames) {
  if (frames.empty() || frames.size() > (UINT32_MAX - 48) / 8)
    throw std::runtime_error("invalid render length");
  std::ofstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("cannot open render " + path.string());
  const auto word = [&](std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) file.put(static_cast<char>((value >> (8 * i)) & 255));
  };
  const auto dataBytes = static_cast<std::uint32_t>(frames.size() * 8);
  file.write("RIFF", 4); word(48 + dataBytes, 4); file.write("WAVEfmt ", 8);
  word(16, 4); word(3, 2); word(2, 2); word(48000, 4); word(48000 * 8, 4); word(8, 2); word(32, 2);
  file.write("fact", 4); word(4, 4); word(static_cast<std::uint32_t>(frames.size()), 4);
  file.write("data", 4); word(dataBytes, 4);
  double energy = 0;
  float peak = 0;
  for (const auto frame : frames) for (const auto sample : {frame.left, frame.right}) {
    if (!std::isfinite(sample)) throw std::runtime_error("render contains nonfinite sample");
    word(std::bit_cast<std::uint32_t>(sample), 4);
    energy += static_cast<double>(sample) * sample;
    peak = std::max(peak, std::fabs(sample));
  }
  file.flush();
  if (!file) throw std::runtime_error("cannot write render " + path.string());
  std::cout << path.filename().string() << " peak=" << peak
            << " RMS=" << std::sqrt(energy / (2 * frames.size())) << '\n';
}
} // namespace pog3_test
