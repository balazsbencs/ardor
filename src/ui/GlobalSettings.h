#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ardor {

enum class PaletteId : std::uint8_t { Slate, Ink, Sodium, Nord };

struct DeviceSettings {
  PaletteId paletteId = PaletteId::Slate;
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
  bool savePalette(PaletteId palette, std::string& error) const;
  bool saveWifi(const std::string& ssid, const std::string& password,
                const std::string& country, std::string& error) const;
  bool saveControlInputs(const DeviceSettings& settings, std::string& error) const;

private:
  std::filesystem::path dataRoot_;
};

} // namespace ardor
