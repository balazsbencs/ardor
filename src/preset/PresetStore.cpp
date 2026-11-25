#include "preset/PresetStore.h"

#include <fstream>
#include <iomanip>
#include <ios>
#include <iterator>
#include <cstdint>
#include <sstream>
#include <stdexcept>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#else
#include <windows.h>
#endif

namespace ardor {

namespace {

// Opaque change token for the exact persisted bytes. Keep in sync with
// managerd/internal/presets. It is used for optimistic concurrency.
std::string documentRevision(const std::string& bytes)
{
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  std::ostringstream result;
  result << std::hex << std::setfill('0') << std::setw(16) << hash;
  return result.str();
}

std::string readDocument(const std::filesystem::path& path)
{
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("failed to open preset: " + path.string());
  std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  if (input.bad()) throw std::runtime_error("failed to read preset: " + path.string());
  return bytes;
}

class PresetWriteLock {
public:
  explicit PresetWriteLock(const std::filesystem::path& path)
  {
#ifndef _WIN32
    fd_ = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
    if (fd_ >= 0 && ::flock(fd_, LOCK_EX) == 0) return;
    if (fd_ >= 0) ::close(fd_);
#else
    handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
    OVERLAPPED lock{};
    if (handle_ != INVALID_HANDLE_VALUE && LockFileEx(handle_, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &lock)) return;
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#endif
    throw std::runtime_error("failed to lock preset: " + path.string());
  }

  ~PresetWriteLock()
  {
#ifndef _WIN32
    ::close(fd_);
#else
    CloseHandle(handle_);
#endif
  }

  PresetWriteLock(const PresetWriteLock&) = delete;
  PresetWriteLock& operator=(const PresetWriteLock&) = delete;

private:
#ifndef _WIN32
  int fd_ = -1;
#else
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#endif
};

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

Preset PresetStore::load(PresetSlot slot, std::string* revision) const
{
  const auto bytes = readDocument(pathFor(slot));
  Preset preset = presetFromJson(nlohmann::json::parse(bytes));
  if (revision) *revision = documentRevision(bytes);
  return preset;
}

Preset PresetStore::loadOrEmpty(PresetSlot slot, std::string* revision) const
{
  if (std::filesystem::exists(pathFor(slot))) return load(slot, revision);
  if (revision) *revision = "missing";
  Preset preset;
  preset.name = "Empty " + std::to_string(slot.preset + 1);
  return preset;
}

std::string PresetStore::revision(PresetSlot slot) const
{
  const auto path = pathFor(slot);
  return std::filesystem::exists(path) ? documentRevision(readDocument(path)) : "missing";
}

std::string PresetStore::save(PresetSlot slot, const Preset& preset,
                              const std::string& expectedRevision) const
{
  const auto bytes = toJson(preset).dump(2) + '\n';
  const auto path = pathFor(slot);
  std::filesystem::create_directories(path.parent_path());
  auto lockPath = path;
  lockPath += ".lock";
  const PresetWriteLock lock(lockPath);
  if (!expectedRevision.empty() && revision(slot) != expectedRevision) {
    throw std::runtime_error("Preset changed since it was opened. Reload it or save to another slot.");
  }
  const auto tmp = path.parent_path() / (path.filename().string() + ".tmp");
  std::filesystem::remove(tmp);

  std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
  if (!out) {
    throw std::runtime_error("failed to write preset: " + tmp.string());
  }

  out << bytes;
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
#ifdef _WIN32
  // Windows filesystem::rename cannot replace an existing preset file.
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw std::filesystem::filesystem_error("failed to replace preset", tmp, path,
      std::error_code(static_cast<int>(GetLastError()), std::system_category()));
#else
  std::filesystem::rename(tmp, path);
#endif
  fsyncPath(path.parent_path());
  return documentRevision(bytes);
}

void PresetSession::load(const PresetStore& store, PresetSlot slot)
{
  store_ = &store;
  slot_ = slot;
  saved_ = store.load(slot, &revision_);
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
  revision_ = store_->save(slot_, working_, revision_);
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
