#include "ui/GlobalSettings.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#endif

namespace ardor {
namespace {

std::string trim(std::string value)
{
  const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
    return std::isspace(c) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
    return std::isspace(c) != 0;
  }).base();
  if (first >= last) return {};
  return std::string(first, last);
}

std::string unquote(const std::string& value)
{
  if (value.size() < 2 || value.front() != '"' || value.back() != '"') return value;
  std::string result;
  result.reserve(value.size() - 2);
  bool escaped = false;
  for (std::size_t i = 1; i + 1 < value.size(); ++i) {
    const char c = value[i];
    if (escaped) {
      result.push_back(c);
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else {
      result.push_back(c);
    }
  }
  return result;
}

std::string quote(const std::string& value)
{
  std::string result = "\"";
  for (const char c : value) {
    if (c == '\\' || c == '"') result.push_back('\\');
    result.push_back(c);
  }
  result.push_back('"');
  return result;
}

bool containsControl(const std::string& value)
{
  return std::any_of(value.begin(), value.end(), [](unsigned char c) {
    return std::iscntrl(c) != 0;
  });
}

bool validHexKey(const std::string& password)
{
  return password.size() == 64
    && std::all_of(password.begin(), password.end(), [](unsigned char c) {
      return std::isxdigit(c) != 0;
    });
}

std::filesystem::path settingsPath(const std::filesystem::path& root)
{
  return root / "settings" / "global.json";
}

std::filesystem::path wifiPath(const std::filesystem::path& root)
{
  return root / "wifi" / "wpa_supplicant.conf";
}

bool writeAtomically(const std::filesystem::path& path, const std::string& body,
                     std::filesystem::perms permissions, std::string& error)
{
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    error = "cannot create settings directory: " + ec.message();
    return false;
  }
  const auto temp = path.string() + ".tmp";
  {
    std::ofstream output(temp, std::ios::binary | std::ios::trunc);
    if (!output) {
      error = "cannot open temporary settings file";
      return false;
    }
    output << body;
    output.flush();
    if (!output) {
      error = "cannot write settings file";
      return false;
    }
  }
  std::filesystem::permissions(temp, permissions, std::filesystem::perm_options::replace, ec);
  if (ec) {
    std::filesystem::remove(temp);
    error = "cannot secure settings file: " + ec.message();
    return false;
  }
  std::filesystem::rename(temp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temp, path, ec);
  }
  if (ec) {
    std::filesystem::remove(temp);
    error = "cannot replace settings file: " + ec.message();
    return false;
  }
  return true;
}

} // namespace

GlobalSettingsStore::GlobalSettingsStore(std::filesystem::path dataRoot)
  : dataRoot_(std::move(dataRoot))
{
}

DeviceSettings GlobalSettingsStore::load() const
{
  DeviceSettings settings;
  {
    std::ifstream input(settingsPath(dataRoot_));
    if (input) {
      try {
        const auto json = nlohmann::json::parse(input);
        const auto color = json.value("accentColor", kDefaultAccentColor);
        if (color <= 0xffffffu) settings.accentColor = color;
      } catch (...) {
        // Keep safe defaults when a partial write or manual edit is invalid.
      }
    }
  }
  {
    std::ifstream input(wifiPath(dataRoot_));
    std::string line;
    while (std::getline(input, line)) {
      const auto separator = line.find('=');
      if (separator == std::string::npos) continue;
      const auto key = trim(line.substr(0, separator));
      const auto value = trim(line.substr(separator + 1));
      if (key == "ssid") settings.wifiSSID = unquote(value);
      if (key == "country" && value.size() == 2) settings.wifiCountry = value;
    }
    settings.wifiConfigured = !settings.wifiSSID.empty();
  }
  return settings;
}

bool GlobalSettingsStore::saveAccentColor(std::uint32_t color, std::string& error) const
{
  if (color > 0xffffffu) {
    error = "accent color is out of range";
    return false;
  }
  nlohmann::json json{{"accentColor", color}};
  return writeAtomically(settingsPath(dataRoot_), json.dump(2) + "\n",
                         std::filesystem::perms::owner_read
                           | std::filesystem::perms::owner_write
                           | std::filesystem::perms::group_read
                           | std::filesystem::perms::others_read,
                         error);
}

bool GlobalSettingsStore::saveWifi(const std::string& ssid, const std::string& password,
                                   const std::string& requestedCountry, std::string& error) const
{
  const auto cleanSSID = trim(ssid);
  std::string effectivePassword = password;
  if (effectivePassword.empty()) {
    std::ifstream existing(wifiPath(dataRoot_));
    std::string line;
    while (std::getline(existing, line)) {
      const auto separator = line.find('=');
      if (separator == std::string::npos) continue;
      if (trim(line.substr(0, separator)) == "psk") {
        effectivePassword = unquote(trim(line.substr(separator + 1)));
        break;
      }
    }
  }
  auto country = trim(requestedCountry);
  std::transform(country.begin(), country.end(), country.begin(), [](unsigned char c) {
    return static_cast<char>(std::toupper(c));
  });
  if (cleanSSID.empty() || cleanSSID.size() > 32 || containsControl(cleanSSID)) {
    error = "Network name must be 1-32 characters";
    return false;
  }
  if ((effectivePassword.size() < 8 || effectivePassword.size() > 63)
      && !validHexKey(effectivePassword)) {
    error = "Password must be 8-63 characters";
    return false;
  }
  if (containsControl(effectivePassword)) {
    error = "Password contains an unsupported character";
    return false;
  }
  if (country.size() != 2
      || !std::all_of(country.begin(), country.end(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z';
      })) {
    error = "Country must be a two-letter code";
    return false;
  }

  const auto formattedPassword = validHexKey(effectivePassword)
    ? effectivePassword : quote(effectivePassword);
  std::ostringstream config;
  config << "ctrl_interface=/run/wpa_supplicant\n"
         << "update_config=0\n"
         << "country=" << country << "\n\n"
         << "network={\n"
         << "    ssid=" << quote(cleanSSID) << "\n"
         << "    psk=" << formattedPassword << "\n"
         << "}\n";
  return writeAtomically(wifiPath(dataRoot_), config.str(),
                         std::filesystem::perms::owner_read
                           | std::filesystem::perms::owner_write,
                         error);
}

} // namespace ardor
