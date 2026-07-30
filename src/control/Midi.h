#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace ardor {

enum class MidiMessageType {
  ProgramChange,
  ControlChange,
};

struct MidiMessage {
  MidiMessageType type = MidiMessageType::ProgramChange;
  std::uint8_t channel = 0;
  std::uint8_t data1 = 0;
  std::uint8_t data2 = 0;
};

// Incremental parser for a raw 31.25 kbaud MIDI byte stream. It supports
// running status and ignores realtime/system traffic without losing channel
// message synchronization.
class MidiStreamParser {
public:
  std::optional<MidiMessage> push(std::uint8_t byte);
  void reset();

private:
  std::uint8_t status_ = 0;
  std::array<std::uint8_t, 2> data_{};
  std::uint8_t dataCount_ = 0;
};

enum class MidiActionType {
  SelectPreset,
  SelectBank,
  SetTuner,
};

struct MidiAction {
  MidiActionType type = MidiActionType::SelectPreset;
  int value = 0;
};

struct MidiControlMapping {
  // -1 listens on all channels; 0..15 selects one MIDI channel.
  int channel = -1;
  std::uint8_t tunerControlChange = 20;
};

// Ardor's initial MIDI contract:
// - Program Change 0..3 selects one of the four preset slots.
// - Bank Select MSB (CC 0) or LSB (CC 32), value 0..99, selects a bank.
// - CC 20 by default controls tuner off/on (<64 / >=64).
class MidiControlMapper {
public:
  explicit MidiControlMapper(MidiControlMapping mapping = {});
  std::optional<MidiAction> map(const MidiMessage& message) const;

private:
  MidiControlMapping mapping_;
};

} // namespace ardor
