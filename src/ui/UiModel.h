#pragma once

#include "equalizer/EqParameters.h"
#include "preset/Preset.h"
#include "preset/PresetStore.h"
#include "preset/RuntimeState.h"

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace ardor {

inline constexpr std::size_t kMaxEffectBlocks = 10;

struct UiAsset {
  std::string name;
  std::string path;
  std::string type;
  std::string blockType;
  std::string mode;
};

struct UiBlock {
  std::string id;
  std::string type;
  std::string label;
  std::string assetName;
  std::string assetPath;
  bool enabled = true;
  nlohmann::json params = nlohmann::json::object();
};

struct UiPreset {
  std::string name;
  std::vector<UiBlock> blocks;
  PresetGlobal global;
};

struct UiBank {
  std::string name;
  std::array<UiPreset, 4> presets;
};

enum class UiMode {
  Preset,
  Edit
};

enum class UiParamTarget {
  Block,
  Globals
};

struct UiState {
  UiBank bank;
  std::vector<UiAsset> assets;
  std::size_t activePreset = 0;
  int activeBank = 0;
  std::size_t selectedBlock = 0;
  UiMode mode = UiMode::Preset;
  UiParamTarget paramTarget = UiParamTarget::Block;
  bool dirty = false;
  // Topology, asset, and discrete-mode changes cannot be applied to the
  // running program incrementally. Continuous targets leave this false.
  bool requiresEngineReload = false;
  bool blockDrawerOpen = false;
  bool paramDrawerOpen = false;
  bool effectsBypassed = false;
  int masterVolume = 82;
  std::string categoryFilter = "all";
  // The Blocks category strip is rebuilt when a filter changes; retain its
  // horizontal position so selecting an off-screen category does not snap back.
  int categoryScrollOffset = 0;
  RuntimeTelemetry telemetry;
  std::string statusMessage;
  bool statusIsError = false;
  int pendingSlotRequest = -1;
};

UiState makeDemoUiState();
void selectPreset(UiState& state, std::size_t index);
// Updates the visible preset after the audio engine has already loaded it.
// Unlike selectPreset(), this never queues another audio-engine swap.
void synchronizePresetSelection(UiState& state, std::size_t index);
void enterPresetMode(UiState& state);
void enterEditMode(UiState& state);
void openBlockDrawer(UiState& state);
void closeBlockDrawer(UiState& state);
void selectBlock(UiState& state, std::size_t blockIndex);
void appendAssetBlock(UiState& state, std::size_t assetIndex);
void insertAssetBlock(UiState& state, std::size_t assetIndex, std::size_t blockIndex);
void moveBlock(UiState& state, std::size_t from, std::size_t to);
bool deleteSelectedBlock(UiState& state);
void closeParamDrawer(UiState& state);
void setCategoryFilter(UiState& state, std::string filter);
Preset activePresetToPreset(const UiState& state);
void replaceActivePreset(UiState& state, const Preset& preset);

void selectGlobalParams(UiState& state);
void setSelectedBlockEnabled(UiState& state, bool enabled);
void setActiveInputGainDb(UiState& state, float db);
void setActiveOutputGainDb(UiState& state, float db);
void setSelectedBlockParam(UiState& state, const std::string& key, float value);
void setSelectedBlockParamValue(UiState& state, const std::string& key, nlohmann::json value);
ParametricEqParams selectedParametricEqParams(const UiState& state);
bool setSelectedEqBand(UiState& state, std::size_t bandIndex, EqBandParams params);
bool resetSelectedEqBand(UiState& state, std::size_t bandIndex);

void updateRealtimeTelemetry(UiState& state, const RuntimeTelemetry& telemetry);
void setUiStatus(UiState& state, std::string message, bool isError = false);
int consumePendingSlotRequest(UiState& state);
void loadAssetsFromDataRoot(UiState& state, const std::filesystem::path& dataRoot);
void loadBankFromStore(UiState& state, const PresetStore& store, int bank);
bool loadPresetSlotFromStore(UiState& state, const PresetStore& store, PresetSlot slot, std::string& error);
bool saveActivePresetToStore(UiState& state, const PresetStore& store, int bank, std::string& error);

} // namespace ardor
