#include "ui/GlobalSettings.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool require(bool condition, const char* message)
{
  if (condition) return true;
  std::cerr << "FAIL: " << message << "\n";
  return false;
}

} // namespace

int main()
{
  const auto root = std::filesystem::temp_directory_path() / "ardor-global-settings-smoke";
  std::filesystem::remove_all(root);
  ardor::GlobalSettingsStore store(root);
  std::string error;

  if (!require(store.savePalette(ardor::PaletteId::Ink, error), error.c_str())) return 1;
  ardor::DeviceSettings controls;
  controls.midiChannel = 4;
  controls.midiTunerCc = 64;
  controls.expressionMinimumRaw = 123;
  controls.expressionMaximumRaw = 24567;
  controls.expressionSmoothing = 0.4f;
  controls.expressionDeadband = 0.01f;
  if (!require(store.saveControlInputs(controls, error), error.c_str())) return 1;
  if (!require(store.saveWifi("Stage Network", "pedal-secret", "hu", error), error.c_str())) return 1;

  const auto settings = store.load();
  if (!require(settings.paletteId == ardor::PaletteId::Ink, "palette should persist")) return 1;
  if (!require(settings.wifiConfigured && settings.wifiSSID == "Stage Network",
               "Wi-Fi SSID should persist")) return 1;
  if (!require(settings.wifiCountry == "HU", "country should be normalized")) return 1;
  if (!require(settings.midiChannel == 4 && settings.midiTunerCc == 64,
               "MIDI settings should persist")) return 1;
  if (!require(settings.expressionMinimumRaw == 123
                && settings.expressionMaximumRaw == 24567,
               "expression calibration should persist")) return 1;
  if (!require(settings.paletteId == ardor::PaletteId::Ink,
               "saving control inputs must preserve appearance settings")) return 1;

  std::ifstream wifi(root / "wifi" / "wpa_supplicant.conf");
  const std::string body((std::istreambuf_iterator<char>(wifi)), std::istreambuf_iterator<char>());
  if (!require(body.find("pedal-secret") != std::string::npos,
               "Wi-Fi config should contain the supplied password")) return 1;
  if (!require(store.load().wifiSSID != "pedal-secret",
               "loaded settings must never expose the password")) return 1;

  std::filesystem::remove_all(root);
  return 0;
}
