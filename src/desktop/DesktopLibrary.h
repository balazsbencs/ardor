#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ardor {

struct DesktopSettings {
  std::string captureDeviceId;
  std::string playbackDeviceId;
  std::uint32_t inputChannel = 0;
  std::uint32_t blockSize = 128;
  int bank = 0;
  int slot = 0;
};

std::filesystem::path desktopDataRoot();
bool validateDesktopSettings(const DesktopSettings& settings, std::string& error);
void initializeDesktopLibrary(const std::filesystem::path& root);
DesktopSettings loadDesktopSettings(const std::filesystem::path& root);
void saveDesktopSettings(const std::filesystem::path& root, const DesktopSettings& settings);

} // namespace ardor
