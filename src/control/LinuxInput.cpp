#include "control/LinuxInput.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

namespace ardor {

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

  input_event input{};
  const auto bytes = ::read(fd_, &input, sizeof(input));
  if (bytes != static_cast<ssize_t>(sizeof(input))) {
    return false;
  }

  if (input.type == EV_KEY && input.value == 1 && input.code >= KEY_F1 && input.code <= KEY_F4) {
    event = {ControlEventType::FootswitchPressed, static_cast<int>(input.code - KEY_F1), 0};
    return true;
  }

  if (input.type == EV_REL && input.value != 0) {
    event = {ControlEventType::EncoderTurned, 0, static_cast<int>(input.value)};
    return true;
  }

  return false;
}

void LinuxInputDevice::close()
{
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

} // namespace ardor
