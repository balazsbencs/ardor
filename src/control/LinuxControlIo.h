#pragma once

#include "control/Midi.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace ardor {

class LinuxMidiInput {
public:
  ~LinuxMidiInput();

  bool open(const std::filesystem::path& path, std::string& error);
  bool poll(MidiMessage& message);
  void close();

private:
  int fd_ = -1;
  MidiStreamParser parser_;
};

class LinuxExpressionInput {
public:
  ~LinuxExpressionInput();

  // Accepts either an IIO device directory or its in_voltage0_raw file.
  bool open(const std::filesystem::path& path, std::string& error);
  bool readRaw(int& raw);
  void close();

  static std::filesystem::path findIioDevice(std::string_view deviceName = "ads1115");

private:
  int fd_ = -1;
};

} // namespace ardor
