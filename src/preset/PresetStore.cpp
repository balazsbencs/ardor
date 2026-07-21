#include "preset/PresetStore.h"

#include <fstream>
#include <iomanip>
#include <ios>
#include <sstream>
#include <stdexcept>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

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

#ifndef _WIN32
void fsyncPath(const std::filesystem::path& path)
{
  const int fd = ::open(path.string().c_str(), O_RDONLY);
  if (fd < 0) {
    throw std::runtime_error("failed to open for sync: " + path.string());
  }
  if (::fsync(fd) != 0) {
    ::close(fd);
    throw std::runtime_error("failed to sync: " + path.string());
  }
  ::close(fd);
}
#else
void fsyncPath(const std::filesystem::path&)
{
}
#endif

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

Preset PresetStore::loadOrEmpty(PresetSlot slot) const
{
  const auto path = pathFor(slot);
  std::error_code error;
  const bool exists = std::filesystem::exists(path, error);
  if (error) {
    throw std::filesystem::filesystem_error("failed to inspect preset", path, error);
  }
  if (exists) {
    return load(slot);
  }

  Preset preset;
  preset.name = "Empty " + std::to_string(slot.preset + 1);
  return preset;
}

void PresetStore::save(PresetSlot slot, const Preset& preset) const
{
  const auto path = pathFor(slot);
  std::filesystem::create_directories(path.parent_path());
  const auto tmp = path.parent_path() / (path.filename().string() + ".tmp");
  std::filesystem::remove(tmp);

  std::ofstream out(tmp, std::ios::trunc);
  if (!out) {
    throw std::runtime_error("failed to write preset: " + tmp.string());
  }

  out << toJson(preset).dump(2) << '\n';
  if (!out.good()) {
    throw std::runtime_error("failed to write preset: " + tmp.string());
  }

  out.flush();
  if (!out.good()) {
    throw std::runtime_error("failed to flush preset: " + tmp.string());
  }

  out.close();
  if (out.fail()) {
    throw std::runtime_error("failed to close preset: " + tmp.string());
  }

  fsyncPath(tmp);
  std::filesystem::rename(tmp, path);
  fsyncPath(path.parent_path());
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
