#pragma once

#include "control/ControlEvents.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ardor {

// The event number depends on input-device enumeration order. Find the two
// devices created by the pedal's gpio-keys and rotary-encoder device tree nodes.
std::vector<std::filesystem::path> discoverPedalControlDevices(
    const std::filesystem::path& sysInputRoot = "/sys/class/input",
    const std::filesystem::path& devInputRoot = "/dev/input");

class LinuxInputDevice {
public:
  ~LinuxInputDevice();

  bool open(const std::filesystem::path& path, std::string& error);
  bool poll(ControlEvent& event);
  void close();

private:
  int fd_ = -1;
};

} // namespace ardor
