#pragma once

#include "control/ControlEvents.h"

#include <filesystem>
#include <string>

namespace ardor {

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
