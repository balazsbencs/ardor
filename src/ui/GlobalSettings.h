#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ardor {

inline constexpr std::uint32_t kDefaultAccentColor = 0x43f05a;

struct DeviceSettings {
  std::uint32_t accentColor = kDefaultAccentColor;
  bool wifiConfigured = false;
  std::string wifiSSID;
  std::string wifiCountry = "HU";
  int midiChannel = -1;
  int midiTunerCc = 20;
  int expressionMinimumRaw = 0;
  int expressionMaximumRaw = 26400;
  float expressionSmoothing = 0.25f;
  float expressionDeadband = 0.002f;
};

class GlobalSettingsStore {
public:
  explicit GlobalSettingsStore(std::filesystem::path dataRoot);

  DeviceSettings load() const;
  bool saveAccentColor(std::uint32_t color, std::string& error) const;
  bool saveWifi(const std::string& ssid, const std::string& password,
                const std::string& country, std::string& error) const;
  bool saveControlInputs(const DeviceSettings& settings, std::string& error) const;

private:
  std::filesystem::path dataRoot_;
};

} // namespace ardor
