#pragma once

#include "equalizer/EqParameters.h"
#include "preset/Preset.h"
#include "preset/PresetStore.h"
#include "preset/RuntimeState.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
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
  Edit,
  Tuner
};

enum class UiParamTarget {
  Block,
  Globals
};

enum class UiChange : uint32_t {
  None = 0,
  Navigation = 1u << 0,
  Header = 1u << 1,
  Presets = 1u << 2,
  Chain = 1u << 3,
  Parameters = 1u << 4,
  Assets = 1u << 5,
  Drawers = 1u << 6,
  Status = 1u << 7,
  Telemetry = 1u << 8,
  All = (1u << 9) - 1,
};

constexpr UiChange operator|(UiChange left, UiChange right)
{
  return static_cast<UiChange>(static_cast<uint32_t>(left) | static_cast<uint32_t>(right));
}

constexpr bool hasUiChange(UiChange value, UiChange flag)
{
  return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

struct UiRevisions {
  uint64_t navigation = 0;
  uint64_t header = 0;
  uint64_t presets = 0;
  uint64_t chain = 0;
  uint64_t parameters = 0;
  uint64_t assets = 0;
  uint64_t drawers = 0;
  uint64_t status = 0;
  uint64_t telemetry = 0;
};

struct UiBlockEditSnapshot {
  std::vector<UiBlock> blocks;
  std::size_t selectedBlock = 0;
  UiParamTarget paramTarget = UiParamTarget::Block;
  bool dirty = false;
  bool requiresEngineReload = false;
  bool blockDrawerOpen = false;
  bool paramDrawerOpen = false;
};

struct UiClipDebugTelemetry {
  bool enabled = false;
  bool overloaded = false;
  std::string firstStage;
  float peakDb = -120.0f;
  uint64_t overloadFrames = 0;
  uint64_t limiterFrames = 0;
};

struct UiTunerTelemetry {
  bool signalDetected = false;
  float frequencyHz = 0.0f;
  float cents = 0.0f;
  float confidence = 0.0f;
  std::string note = "--";
  int octave = 0;
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
  int32_t assetScrollOffset = 0;
  RuntimeTelemetry telemetry;
  UiClipDebugTelemetry clipDebug;
  UiTunerTelemetry tuner;
  std::string statusMessage;
  bool statusIsError = false;
  std::optional<UiBlockEditSnapshot> blockEditUndo;
  int pendingSlotRequest = -1;
  UiRevisions revisions;
};

void markUiChanged(UiState& state, UiChange changes);

UiState makeDemoUiState();
void selectPreset(UiState& state, std::size_t index);
// Updates the visible preset after the audio engine has already loaded it.
// Unlike selectPreset(), this never queues another audio-engine swap.
void synchronizePresetSelection(UiState& state, std::size_t index);
void enterPresetMode(UiState& state);
void enterEditMode(UiState& state);
void enterTunerMode(UiState& state);
void updateTunerTelemetry(UiState& state, UiTunerTelemetry telemetry);
void openBlockDrawer(UiState& state);
void closeBlockDrawer(UiState& state);
void selectBlock(UiState& state, std::size_t blockIndex);
void appendAssetBlock(UiState& state, std::size_t assetIndex);
void insertAssetBlock(UiState& state, std::size_t assetIndex, std::size_t blockIndex);
void moveBlock(UiState& state, std::size_t from, std::size_t to);
bool deleteSelectedBlock(UiState& state);
bool undoLastBlockEdit(UiState& state);
void closeParamDrawer(UiState& state);
void setCategoryFilter(UiState& state, std::string filter);
Preset activePresetToPreset(const UiState& state);
void replaceActivePreset(UiState& state, const Preset& preset);

void selectGlobalParams(UiState& state);
void setSelectedBlockEnabled(UiState& state, bool enabled);
void setActiveInputGainDb(UiState& state, float db);
void setActiveOutputGainDb(UiState& state, float db);
void setMasterVolume(UiState& state, int volume);
void setSelectedBlockParam(UiState& state, const std::string& key, float value);
void setSelectedBlockParamValue(UiState& state, const std::string& key, nlohmann::json value);
ParametricEqParams selectedParametricEqParams(const UiState& state);
bool setSelectedEqBand(UiState& state, std::size_t bandIndex, EqBandParams params);
bool resetSelectedEqBand(UiState& state, std::size_t bandIndex);

void updateRealtimeTelemetry(UiState& state, const RuntimeTelemetry& telemetry);
void updateClipDebugTelemetry(UiState& state, UiClipDebugTelemetry telemetry);
void setUiStatus(UiState& state, std::string message, bool isError = false);
int consumePendingSlotRequest(UiState& state);
void loadAssetsFromDataRoot(UiState& state, const std::filesystem::path& dataRoot);
void loadBankFromStore(UiState& state, const PresetStore& store, int bank);
bool loadPresetSlotFromStore(UiState& state, const PresetStore& store, PresetSlot slot, std::string& error);
bool saveActivePresetToStore(UiState& state, const PresetStore& store, int bank, std::string& error);

} // namespace ardor
