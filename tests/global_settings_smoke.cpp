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

  if (!require(store.saveAccentColor(0x67a6ff, error), error.c_str())) return 1;
  if (!require(store.saveWifi("Stage Network", "pedal-secret", "hu", error), error.c_str())) return 1;

  const auto settings = store.load();
  if (!require(settings.accentColor == 0x67a6ff, "accent color should persist")) return 1;
  if (!require(settings.wifiConfigured && settings.wifiSSID == "Stage Network",
               "Wi-Fi SSID should persist")) return 1;
  if (!require(settings.wifiCountry == "HU", "country should be normalized")) return 1;

  std::ifstream wifi(root / "wifi" / "wpa_supplicant.conf");
  const std::string body((std::istreambuf_iterator<char>(wifi)), std::istreambuf_iterator<char>());
  if (!require(body.find("pedal-secret") != std::string::npos,
               "Wi-Fi config should contain the supplied password")) return 1;
  if (!require(store.load().wifiSSID != "pedal-secret",
               "loaded settings must never expose the password")) return 1;

  std::filesystem::remove_all(root);
  return 0;
}
