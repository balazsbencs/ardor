#include "control/LinuxInput.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <vector>

#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

namespace ardor {

std::vector<std::filesystem::path> discoverPedalControlDevices(
    const std::filesystem::path& sysInputRoot,
    const std::filesystem::path& devInputRoot)
{
  std::vector<std::filesystem::path> devices;
  std::error_code error;
  for (std::filesystem::directory_iterator it(sysInputRoot, error), end;
       !error && it != end; it.increment(error)) {
    const auto node = it->path().filename().string();
    if (node.rfind("event", 0) != 0) continue;
    std::ifstream nameFile(it->path() / "device" / "name");
    std::string name;
    std::getline(nameFile, name);
    if (name != "gpio-keys" && name != "rotary-encoder") continue;
    const auto path = devInputRoot / node;
    if (std::filesystem::exists(path, error)) devices.push_back(path);
    error.clear();
  }
  return devices;
}

LinuxInputDevice::~LinuxInputDevice()
{
  close();
}

bool LinuxInputDevice::open(const std::filesystem::path& path, std::string& error)
{
  close();
  fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
  if (fd_ < 0) {
    error = std::strerror(errno);
    return false;
  }
  return true;
}

bool LinuxInputDevice::poll(ControlEvent& event)
{
  if (fd_ < 0) {
    return false;
  }

  // evdev emits EV_SYN after every logical report. Keep draining ignored
  // records here so callers can drain an entire device without being stopped
  // by the synchronization marker immediately after the first useful event.
  for (;;) {
    input_event input{};
    const auto bytes = ::read(fd_, &input, sizeof(input));
    if (bytes != static_cast<ssize_t>(sizeof(input))) {
      if (bytes < 0 && errno == EINTR) {
        continue;
      }
      return false;
    }

    if (input.type == EV_KEY && (input.value == 0 || input.value == 1)
        && input.code >= KEY_F1 && input.code <= KEY_F4) {
      event = {input.value == 1 ? ControlEventType::FootswitchPressed
                                : ControlEventType::FootswitchReleased,
               static_cast<int>(input.code - KEY_F1), 0};
      return true;
    }

    if (input.type == EV_REL && input.value != 0) {
      event = {ControlEventType::EncoderTurned, 0, static_cast<int>(input.value)};
      return true;
    }
  }
}

void LinuxInputDevice::close()
{
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

} // namespace ardor
