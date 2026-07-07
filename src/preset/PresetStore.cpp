#include "preset/PresetStore.h"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ardor {

namespace {

std::string bankDir(int bank)
{
  std::ostringstream out;
  out << "bank-" << std::setw(3) << std::setfill('0') << bank;
  return out.str();
}

void validateSlot(PresetSlot slot)
{
  if (slot.bank < 0 || slot.bank >= 100 || slot.preset < 0 || slot.preset >= 4) {
    throw std::out_of_range("preset slot out of range");
  }
}

} // namespace

bool samePreset(const Preset& left, const Preset& right)
{
  return toJson(left) == toJson(right);
}

PresetStore::PresetStore(std::filesystem::path root)
  : root_(std::move(root))
{
}

std::filesystem::path PresetStore::pathFor(PresetSlot slot) const
{
  validateSlot(slot);
  return root_ / "presets" / bankDir(slot.bank) / ("preset-" + std::to_string(slot.preset) + ".json");
}

Preset PresetStore::load(PresetSlot slot) const
{
  const auto path = pathFor(slot);
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("failed to open preset: " + path.string());
  }

  nlohmann::json json;
  in >> json;
  return presetFromJson(json);
}

void PresetStore::save(PresetSlot slot, const Preset& preset) const
{
  const auto path = pathFor(slot);
  std::filesystem::create_directories(path.parent_path());
  const auto tmp = path.string() + ".tmp";

  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      throw std::runtime_error("failed to write preset: " + tmp);
    }
    out << toJson(preset).dump(2) << '\n';
  }

  std::filesystem::rename(tmp, path);
}

void PresetSession::load(const PresetStore& store, PresetSlot slot)
{
  store_ = &store;
  slot_ = slot;
  saved_ = store.load(slot);
  working_ = saved_;
}

Preset& PresetSession::working()
{
  return working_;
}

const Preset& PresetSession::working() const
{
  return working_;
}

const Preset& PresetSession::saved() const
{
  return saved_;
}

bool PresetSession::isDirty() const
{
  return !samePreset(saved_, working_);
}

void PresetSession::save()
{
  if (!store_) {
    throw std::runtime_error("no preset store loaded");
  }
  store_->save(slot_, working_);
  saved_ = working_;
}

void PresetSession::discard()
{
  if (!store_) {
    throw std::runtime_error("no preset store loaded");
  }
  saved_ = store_->load(slot_);
  working_ = saved_;
}

} // namespace ardor
