#include "control/Midi.h"

namespace ardor {

namespace {

std::uint8_t messageDataLength(std::uint8_t status)
{
  const auto kind = static_cast<std::uint8_t>(status & 0xf0U);
  return (kind == 0xc0U || kind == 0xd0U) ? 1U : 2U;
}

} // namespace

std::optional<MidiMessage> MidiStreamParser::push(std::uint8_t byte)
{
  if (byte >= 0xf8U) {
    return std::nullopt;
  }

  if ((byte & 0x80U) != 0U) {
    dataCount_ = 0;
    if (byte >= 0xf0U) {
      status_ = 0;
      return std::nullopt;
    }
    status_ = byte;
    return std::nullopt;
  }

  if (status_ == 0) {
    return std::nullopt;
  }

  data_[dataCount_++] = byte;
  if (dataCount_ < messageDataLength(status_)) {
    return std::nullopt;
  }
  dataCount_ = 0;

  const auto kind = static_cast<std::uint8_t>(status_ & 0xf0U);
  const auto channel = static_cast<std::uint8_t>(status_ & 0x0fU);
  if (kind == 0xc0U) {
    return MidiMessage{MidiMessageType::ProgramChange, channel, data_[0], 0};
  }
  if (kind == 0xb0U) {
    return MidiMessage{MidiMessageType::ControlChange, channel, data_[0], data_[1]};
  }
  return std::nullopt;
}

void MidiStreamParser::reset()
{
  status_ = 0;
  dataCount_ = 0;
}

MidiControlMapper::MidiControlMapper(MidiControlMapping mapping)
  : mapping_(mapping)
{
}

std::optional<MidiAction> MidiControlMapper::map(const MidiMessage& message) const
{
  if (mapping_.channel >= 0 && message.channel != mapping_.channel) {
    return std::nullopt;
  }

  if (message.type == MidiMessageType::ProgramChange) {
    if (message.data1 < 4) {
      return MidiAction{MidiActionType::SelectPreset, message.data1};
    }
    return std::nullopt;
  }

  if ((message.data1 == 0 || message.data1 == 32) && message.data2 < 100) {
    return MidiAction{MidiActionType::SelectBank, message.data2};
  }
  if (message.data1 == mapping_.tunerControlChange) {
    return MidiAction{MidiActionType::SetTuner, message.data2 >= 64 ? 1 : 0};
  }
  return std::nullopt;
}

} // namespace ardor
