#include "desktop/DesktopLibrary.h"

#include "preset/PresetStore.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace ardor {
namespace {

std::filesystem::path requiredEnvironmentPath(const char* name)
{
  const auto* value = std::getenv(name);
  if (!value || !*value || !std::filesystem::path(value).is_absolute()) {
    throw std::runtime_error(std::string("Cannot locate the user library: ") + name
                             + " must contain an absolute path.");
  }
  return value;
}

} // namespace

std::filesystem::path desktopDataRoot()
{
#if defined(__APPLE__)
  return requiredEnvironmentPath("HOME") / "Library/Application Support/Ardor";
#elif defined(_WIN32)
  return requiredEnvironmentPath("LOCALAPPDATA") / "Ardor";
#else
  const auto* xdg = std::getenv("XDG_DATA_HOME");
  if (xdg && *xdg && std::filesystem::path(xdg).is_absolute()) {
    return std::filesystem::path(xdg) / "ardor";
  }
  return requiredEnvironmentPath("HOME") / ".local/share/ardor";
#endif
}

bool validateDesktopSettings(const DesktopSettings& settings, std::string& error)
{
  error.clear();
  if (settings.blockSize != 32 && settings.blockSize != 64
      && settings.blockSize != 128 && settings.blockSize != 256) {
    error = "Choose a buffer of 32, 64, 128, or 256 frames.";
  } else if (settings.inputChannel >= 32) {
    error = "Choose an input channel between 1 and 32.";
  } else if (settings.bank < 0 || settings.bank >= 100 || settings.slot < 0 || settings.slot >= 4) {
    error = "The saved bank or preset slot is outside its supported range.";
  } else if (settings.captureDeviceId.size() > 2048 || settings.playbackDeviceId.size() > 2048) {
    error = "The saved audio device identifier is invalid.";
  }
  return error.empty();
}

void initializeDesktopLibrary(const std::filesystem::path& root)
{
  if (root.empty()) throw std::runtime_error("The user library path is empty.");
  for (const auto* directory : {"models", "irs", "reverb-irs", "loops", "settings"}) {
    std::filesystem::create_directories(root / directory);
  }
  // Factory presets use only Ardor's own algorithms. Do not redistribute an
  // arbitrary NAM capture or IR from a developer's checkout.
  PresetStore store(root);
  for (int slot = 0; slot < 4; ++slot) {
    if (std::filesystem::exists(store.pathFor({0, slot}))) continue;
    Preset preset;
    const char* names[] = {"Clean", "Tremolo", "Chorus", "Delay"};
    preset.name = names[slot];
    if (slot != 0) {
      const char* modes[] = {"", "vintage_trem", "chorus", "digital"};
      preset.blocks.push_back({"factory-fx", slot == 3 ? "delay" : "mod", true, "",
                               {{"mode", modes[slot]}, {"mix", slot == 3 ? 0.25f : 0.5f}}});
    }
    store.save({0, slot}, preset);
  }
}

DesktopSettings loadDesktopSettings(const std::filesystem::path& root)
{
  const auto path = root / "settings/desktop.json";
  if (!std::filesystem::exists(path)) return {};
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot read desktop audio settings.");
  nlohmann::json json;
  input >> json;
  if (!json.is_object() || json.value("version", 0) != 1) {
    throw std::runtime_error("Desktop settings have an unsupported format.");
  }
  DesktopSettings settings;
  settings.captureDeviceId = json.value("captureDeviceId", std::string{});
  settings.playbackDeviceId = json.value("playbackDeviceId", std::string{});
  // Read signed values first; do not permit a negative channel to wrap.
  const int channel = json.value("inputChannel", 0);
  const int block = json.value("blockSize", 128);
  if (channel < 0 || block < 0) throw std::runtime_error("Desktop audio settings contain a negative value.");
  settings.inputChannel = static_cast<std::uint32_t>(channel);
  settings.blockSize = static_cast<std::uint32_t>(block);
  settings.bank = json.value("bank", 0);
  settings.slot = json.value("slot", 0);
  std::string error;
  if (!validateDesktopSettings(settings, error)) throw std::runtime_error(error);
  return settings;
}

void saveDesktopSettings(const std::filesystem::path& root, const DesktopSettings& settings)
{
  std::string error;
  if (!validateDesktopSettings(settings, error)) throw std::runtime_error(error);
  const auto directory = root / "settings";
  std::filesystem::create_directories(directory);
  const auto path = directory / "desktop.json";
  const auto temporary = directory / "desktop.json.tmp";
  std::ofstream output(temporary, std::ios::trunc);
  if (!output) throw std::runtime_error("Cannot write desktop audio settings.");
  output << nlohmann::json({{"version", 1}, {"captureDeviceId", settings.captureDeviceId},
    {"playbackDeviceId", settings.playbackDeviceId}, {"inputChannel", settings.inputChannel},
    {"blockSize", settings.blockSize}, {"bank", settings.bank}, {"slot", settings.slot}}).dump(2) << '\n';
  output.flush();
  output.close();
  if (output.fail()) throw std::runtime_error("Cannot finish writing desktop audio settings.");
  std::filesystem::rename(temporary, path);
}

} // namespace ardor
