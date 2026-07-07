#pragma once

#include "Preset.h"

#include <filesystem>

namespace ardor {

struct PresetSlot {
  int bank = 0;
  int preset = 0;
};

bool samePreset(const Preset& left, const Preset& right);

class PresetStore {
public:
  explicit PresetStore(std::filesystem::path root);

  std::filesystem::path pathFor(PresetSlot slot) const;
  Preset load(PresetSlot slot) const;
  void save(PresetSlot slot, const Preset& preset) const;

private:
  std::filesystem::path root_;
};

class PresetSession {
public:
  void load(const PresetStore& store, PresetSlot slot);
  Preset& working();
  const Preset& working() const;
  const Preset& saved() const;
  bool isDirty() const;
  void save();
  void discard();

private:
  const PresetStore* store_ = nullptr;
  PresetSlot slot_;
  Preset saved_;
  Preset working_;
};

} // namespace ardor
