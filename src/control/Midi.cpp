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

void PresetMidiMapper::load(const std::vector<PresetMidiBinding>& bindings)
{
  bindings_.clear();
  bindings_.reserve(bindings.size());
  for (const auto& binding : bindings) {
    bindings_.push_back({binding, false, false});
  }
}

std::vector<PresetMidiValue> PresetMidiMapper::reset()
{
  std::vector<PresetMidiValue> values;
  for (auto& state : bindings_) {
    state.inputHigh = false;
    state.scene2 = false;
    if (state.binding.mode != PresetMidiBindingMode::Toggle) continue;
    for (const auto& action : state.binding.actions) {
      values.push_back({action, action.value1});
    }
  }
  return values;
}

bool PresetMidiMapper::handles(const MidiMessage& message) const
{
  if (message.type != MidiMessageType::ControlChange) return false;
  for (const auto& state : bindings_) {
    if (state.binding.controlChange == message.data1
        && (state.binding.channel < 0 || state.binding.channel == message.channel)) return true;
  }
  return false;
}

std::vector<PresetMidiValue> PresetMidiMapper::map(const MidiMessage& message)
{
  std::vector<PresetMidiValue> values;
  if (message.type != MidiMessageType::ControlChange) return values;
  for (auto& state : bindings_) {
    const auto& binding = state.binding;
    if (binding.controlChange != message.data1
        || (binding.channel >= 0 && binding.channel != message.channel)) continue;
    if (binding.mode == PresetMidiBindingMode::Continuous) {
      for (const auto& action : binding.actions) {
        values.push_back({action, midiActionValueAt(action, message.data2)});
      }
      continue;
    }

    const bool high = message.data2 >= 64;
    if (high && !state.inputHigh) {
      state.scene2 = !state.scene2;
      for (const auto& action : binding.actions) {
        values.push_back({action, state.scene2 ? action.value2 : action.value1});
      }
    }
    state.inputHigh = high;
  }
  return values;
}

} // namespace ardor
