#include "control/LinuxControlIo.h"

#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fstream>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

namespace ardor {

LinuxMidiInput::~LinuxMidiInput()
{
  close();
}

bool LinuxMidiInput::open(const std::filesystem::path& path, std::string& error)
{
  close();
  fd_ = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
  if (fd_ < 0) {
    error = std::strerror(errno);
    return false;
  }

  termios options{};
  if (::tcgetattr(fd_, &options) != 0) {
    error = std::strerror(errno);
    close();
    return false;
  }
  ::cfmakeraw(&options);
  ::cfsetispeed(&options, B38400);
  ::cfsetospeed(&options, B38400);
  options.c_cflag |= CLOCAL | CREAD;
  options.c_cflag &= static_cast<tcflag_t>(~(CSTOPB | PARENB));
#if defined(CRTSCTS)
  options.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
#endif
  options.c_cflag = static_cast<tcflag_t>((options.c_cflag & ~CSIZE) | CS8);
  if (::tcsetattr(fd_, TCSANOW, &options) != 0) {
    error = std::strerror(errno);
    close();
    return false;
  }
  ::tcflush(fd_, TCIFLUSH);
  parser_.reset();
  return true;
}

bool LinuxMidiInput::poll(MidiMessage& message)
{
  if (fd_ < 0) return false;

  for (;;) {
    std::uint8_t byte = 0;
    const auto count = ::read(fd_, &byte, 1);
    if (count == 1) {
      if (const auto parsed = parser_.push(byte)) {
        message = *parsed;
        return true;
      }
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    return false;
  }
}

void LinuxMidiInput::close()
{
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  parser_.reset();
}

LinuxExpressionInput::~LinuxExpressionInput()
{
  close();
}

bool LinuxExpressionInput::open(const std::filesystem::path& path, std::string& error)
{
  close();
  const auto rawPath = std::filesystem::is_directory(path)
    ? path / "in_voltage0_raw" : path;
  fd_ = ::open(rawPath.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd_ < 0) {
    error = std::strerror(errno);
    return false;
  }
  return true;
}

bool LinuxExpressionInput::readRaw(int& raw)
{
  if (fd_ < 0) return false;
  if (::lseek(fd_, 0, SEEK_SET) < 0) return false;

  std::array<char, 64> buffer{};
  ssize_t count = 0;
  do {
    count = ::read(fd_, buffer.data(), buffer.size() - 1);
  } while (count < 0 && errno == EINTR);
  if (count <= 0) return false;

  const char* begin = buffer.data();
  const char* end = begin + count;
  while (begin != end && (*begin == ' ' || *begin == '\t' || *begin == '\n')) ++begin;
  const auto result = std::from_chars(begin, end, raw);
  return result.ec == std::errc{};
}

void LinuxExpressionInput::close()
{
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

std::filesystem::path LinuxExpressionInput::findIioDevice(std::string_view deviceName)
{
  const std::filesystem::path root{"/sys/bus/iio/devices"};
  std::error_code error;
  for (std::filesystem::directory_iterator it{root, error}, end;
       !error && it != end; it.increment(error)) {
    if (it->path().filename().string().rfind("iio:device", 0) != 0) continue;
    std::ifstream nameFile{it->path() / "name"};
    std::string name;
    std::getline(nameFile, name);
    if (name == deviceName
        && std::filesystem::exists(it->path() / "in_voltage0_raw", error)
        && !error) {
      return it->path();
    }
    error.clear();
  }
  return {};
}

} // namespace ardor
