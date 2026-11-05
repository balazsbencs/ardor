#pragma once

#include <filesystem>
#include <stdexcept>

namespace test_input {

// These local tools intentionally accept operator-selected files and directories.
// Resolve an existing directory entry before passing it to a reader, rather than
// forwarding raw command-line text. Files must be regular (never FIFOs/devices).
inline std::filesystem::path existingPath(const std::filesystem::path& requested, bool directory) {
  const auto target = std::filesystem::canonical(requested);
  if (directory ? !std::filesystem::is_directory(target) : !std::filesystem::is_regular_file(target))
    throw std::runtime_error("test input has the wrong file type: " + requested.string());
  // The filesystem root has no parent entry to enumerate.
  if (directory && target == target.root_path())
    throw std::runtime_error("choose a test data directory below the filesystem root");
  for (const auto& entry : std::filesystem::directory_iterator(target.parent_path())) {
    if (entry.path() == target)
      return entry.path();
  }
  throw std::runtime_error("test input is no longer available: " + requested.string());
}

inline std::filesystem::path file(const std::filesystem::path& requested) {
  return existingPath(requested, false);
}

inline std::filesystem::path directory(const std::filesystem::path& requested) {
  return existingPath(requested, true);
}

inline std::filesystem::path asset(const std::filesystem::path& root,
                                   const std::filesystem::path& relative) {
  if (relative.empty() || relative.is_absolute())
    throw std::runtime_error("test preset assets must be relative to the data root");
  const auto target = file(root / relative);
  const auto within = target.lexically_relative(root);
  if (within.empty() || *within.begin() == "..")
    throw std::runtime_error("test preset asset escapes the data root");
  return target;
}

} // namespace test_input
