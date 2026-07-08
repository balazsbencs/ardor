#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace ardor {

struct UiAsset {
  std::string name;
  std::string path;
  std::string type;
};

struct UiBlock {
  std::string id;
  std::string type;
  std::string label;
  std::string assetName;
  std::string assetPath;
  bool enabled = true;
};

struct UiPreset {
  std::string name;
  std::vector<UiBlock> blocks;
};

struct UiBank {
  std::string name;
  std::array<UiPreset, 4> presets;
};

enum class UiMode {
  Preset,
  Edit
};

struct UiState {
  UiBank bank;
  std::vector<UiAsset> assets;
  std::size_t activePreset = 0;
  std::size_t selectedBlock = 0;
  UiMode mode = UiMode::Preset;
  bool dirty = false;
  bool blockDrawerOpen = false;
  bool paramDrawerOpen = false;
  bool effectsBypassed = false;
  int masterVolume = 82;
  std::string categoryFilter = "all";
};

UiState makeDemoUiState();
void selectPreset(UiState& state, std::size_t index);
void enterPresetMode(UiState& state);
void enterEditMode(UiState& state);
void openBlockDrawer(UiState& state);
void closeBlockDrawer(UiState& state);
void selectBlock(UiState& state, std::size_t blockIndex);
void appendAssetBlock(UiState& state, std::size_t assetIndex);
void insertAssetBlock(UiState& state, std::size_t assetIndex, std::size_t blockIndex);
void moveBlock(UiState& state, std::size_t from, std::size_t to);
void closeParamDrawer(UiState& state);
void setCategoryFilter(UiState& state, std::string filter);

} // namespace ardor
