#pragma once

#include "equalizer/EqParameters.h"
#include "preset/Preset.h"
#include "preset/PresetStore.h"
#include "preset/RuntimeState.h"
#include "ui/GlobalSettings.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ardor {

struct ParameterControl;

inline constexpr std::size_t kMaxEffectBlocks = 10;

struct UiAsset {
  std::string name;
  std::string path;
  std::string type;
  std::string blockType;
  std::string mode;
  // Drawer list secondary line, e.g. "Modulation · 6 controls". Empty is fine
  // -- the row just shows the name.
  std::string subtitle;
};

struct UiBlock {
  std::string id;
  std::string type;
  std::string label;
  std::string assetName;
  std::string assetPath;
  bool enabled = true;
  nlohmann::json params = nlohmann::json::object();
  std::array<std::vector<UiBlock>, 2> lanes;
};

struct UiPreset {
  std::string name;
  std::vector<UiBlock> blocks;
  PresetGlobal global;
  int version = 1;
  std::optional<PresetExpression> expression;
  std::vector<PresetMidiBinding> midiBindings;
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

enum class UiNavigationDecision { Save, Discard, Cancel };

struct UiNavigationTarget {
  int bank = 0;
  std::size_t preset = 0;
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
  std::optional<PresetExpression> expression;
  std::vector<PresetMidiBinding> midiBindings;
  std::size_t selectedBlock = 0;
  std::string selectedBlockId;
  UiParamTarget paramTarget = UiParamTarget::Block;
  bool dirty = false;
  bool blockDrawerOpen = false;
  bool paramDrawerOpen = false;
};

enum class UiMidiLearnStage { None, Waiting, Captured, Advanced };

struct UiMidiLearnState {
  UiMidiLearnStage stage = UiMidiLearnStage::None;
  PresetMidiAction action;
  PresetMidiBindingMode mode = PresetMidiBindingMode::Continuous;
  int channel = -1;
  int controlChange = -1;
  int observedMinimum = 127;
  int observedMaximum = 0;
  int observedCount = 0;
  float targetMinimum = 0.0f;
  float targetMaximum = 1.0f;
  float targetCurrent = 0.0f;
  bool modeExplicit = false;
};

struct UiPreviewSnapshot {
  UiPreset preset;
  std::size_t selectedBlock = 0;
  std::string selectedBlockId;
  UiParamTarget paramTarget = UiParamTarget::Block;
  bool dirty = false;
  bool blockDrawerOpen = false;
  bool paramDrawerOpen = false;
  std::optional<UiBlockEditSnapshot> blockEditUndo;
};

struct UiPreviewTransaction {
  UiPreviewSnapshot rollback;
  std::string operation;
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

struct UiControlInputTelemetry {
  bool midiConfigured = false;
  bool midiConnected = false;
  bool expressionConnected = false;
  bool expressionPositionKnown = false;
  float expressionPosition = 0.0f;
  bool expressionRawKnown = false;
  int expressionRaw = 0;
};

struct UiState {
  UiBank bank;
  DeviceSettings settings;
  std::vector<UiAsset> assets;
  std::size_t activePreset = 0;
  int activeBank = 0;
  std::size_t selectedBlock = 0;
  // The top-level index keeps chain scrolling/reordering stable. The ID can
  // point either at that block or at one of its Dual Rig lane children.
  std::string selectedBlockId;
  UiMode mode = UiMode::Preset;
  UiParamTarget paramTarget = UiParamTarget::Block;
  bool dirty = false;
  // A pending preview is the draft/audible boundary for a structural edit. It
  // deliberately has no bearing on whether the preset has been saved.
  std::optional<UiPreviewTransaction> pendingPreview;
  bool blockDrawerOpen = false;
  bool paramDrawerOpen = false;
  bool effectsBypassed = false;
  int masterVolume = 82;
  std::string categoryFilter = "all";
  int32_t assetScrollOffset = 0;
  std::array<int32_t, 4> chainScrollOffsets{};
  std::size_t blockInsertIndex = 0;
  std::optional<std::size_t> blockInsertRig;
  std::optional<std::size_t> blockInsertLane;
  RuntimeTelemetry telemetry;
  UiClipDebugTelemetry clipDebug;
  UiTunerTelemetry tuner;
  UiControlInputTelemetry controlInputs;
  // The selected compressor block's current gain reduction in dB (<= 0),
  // sampled continuously while its parameter drawer is open. Unlike the
  // other telemetry above, this has no revision of its own -- the meter
  // widget reads it directly on every refresh() tick (see
  // LvglUi::syncCompressorGainMeter) so it keeps moving even while a slider
  // drag is holding off the revision-gated sync path.
  float compressorGainReductionDb = 0.0f;
  std::string statusMessage;
  bool statusIsError = false;
  std::optional<UiBlockEditSnapshot> blockEditUndo;
  int pendingSlotRequest = -1;
  std::optional<UiNavigationTarget> navigationPrompt;
  UiMidiLearnState midiLearn;
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
void openBlockDrawerAt(UiState& state, std::size_t blockIndex);
void openLaneBlockDrawer(UiState& state, std::size_t rigIndex, std::size_t laneIndex,
                         std::size_t blockIndex);
void closeBlockDrawer(UiState& state);
void selectBlock(UiState& state, std::size_t blockIndex);
void selectLaneBlock(UiState& state, std::size_t rigIndex, std::size_t laneIndex,
                     std::size_t blockIndex);
UiBlock* selectedUiBlock(UiState& state);
const UiBlock* selectedUiBlock(const UiState& state);
bool selectedBlockIsLaneChild(const UiState& state);
void appendAssetBlock(UiState& state, std::size_t assetIndex);
void insertAssetBlock(UiState& state, std::size_t assetIndex, std::size_t blockIndex);
void insertLaneAssetBlock(UiState& state, std::size_t assetIndex, std::size_t rigIndex,
                          std::size_t laneIndex, std::size_t blockIndex);
bool moveLaneBlock(UiState& state, std::size_t rigIndex, std::size_t sourceLane,
                   std::size_t sourceIndex, std::size_t targetLane, std::size_t targetIndex);
void moveBlock(UiState& state, std::size_t from, std::size_t to);
bool deleteSelectedBlock(UiState& state);
bool undoLastBlockEdit(UiState& state);
void closeParamDrawer(UiState& state);
void setCategoryFilter(UiState& state, std::string filter);
Preset activePresetToPreset(const UiState& state);
void replaceActivePreset(UiState& state, const Preset& preset);
// A preset with an enabled NAM or cab block whose asset is not installed can
// still be opened and edited, but cannot be made audible.
bool presetHasUnavailableAssets(const UiState& state, std::size_t presetIndex);

void selectGlobalParams(UiState& state);
void setSelectedBlockEnabled(UiState& state, bool enabled);
bool setSelectedBlockEnabledLive(UiState& state, bool enabled);
void setActiveInputGainDb(UiState& state, float db);
void setActiveOutputGainDb(UiState& state, float db);
void setMasterVolume(UiState& state, int volume);
void setSelectedBlockParam(UiState& state, const std::string& key, float value);
void setSelectedBlockParamValue(UiState& state, const std::string& key, nlohmann::json value);
ParametricEqParams selectedParametricEqParams(const UiState& state);
bool setSelectedEqBand(UiState& state, std::size_t bandIndex, EqBandParams params);
bool resetSelectedEqBand(UiState& state, std::size_t bandIndex);
bool setSelectedEqPassFilter(UiState& state, EqPassFilterKind kind, EqPassFilterParams params);
bool resetSelectedEqPassFilter(UiState& state, EqPassFilterKind kind);

bool previewIsSynchronized(const UiState& state);
UiPreviewSnapshot captureUiPreviewSnapshot(const UiState& state);
bool queuePreview(UiState& state, UiPreviewSnapshot rollback, std::string operation);
const UiPreviewTransaction* pendingStructuralPreview(const UiState& state);
void completeStructuralPreview(UiState& state);
void failStructuralPreview(UiState& state, std::string error);

// Navigation never mutates the active draft until its destination has been
// activated. Dirty drafts require a Save/Discard/Cancel decision first.
bool requestPresetNavigation(UiState& state, UiNavigationTarget target);
std::optional<UiNavigationTarget> confirmNavigation(UiState& state, UiNavigationDecision decision);

void updateRealtimeTelemetry(UiState& state, const RuntimeTelemetry& telemetry);
void updateClipDebugTelemetry(UiState& state, UiClipDebugTelemetry telemetry);
void updateCompressorGainReduction(UiState& state, float reductionDb);
void updateControlInputTelemetry(UiState& state, UiControlInputTelemetry telemetry);
bool parameterSupportsExpression(const UiState& state, const ParameterControl& control);
bool parameterHasMidiBinding(const UiState& state, const ParameterControl& control);
bool toggleExpressionAssignment(UiState& state, const ParameterControl& control);
bool beginMidiLearn(UiState& state, const ParameterControl& control);
bool beginMidiLearnForBlockEnabled(UiState& state);
bool observeMidiLearnControlChange(UiState& state, int channel, int controlChange, int value);
void showAdvancedMidiLearn(UiState& state);
void setMidiLearnMode(UiState& state, PresetMidiBindingMode mode);
void setMidiLearnEndpoint(UiState& state, std::size_t endpoint, float value);
bool commitMidiLearn(UiState& state);
void cancelMidiLearn(UiState& state);
void setUiStatus(UiState& state, std::string message, bool isError = false);
int consumePendingSlotRequest(UiState& state);
void loadAssetsFromDataRoot(UiState& state, const std::filesystem::path& dataRoot);
void loadBankFromStore(UiState& state, const PresetStore& store, int bank);
bool loadPresetSlotFromStore(UiState& state, const PresetStore& store, PresetSlot slot, std::string& error);
bool saveActivePresetToStore(UiState& state, const PresetStore& store, int bank, std::string& error);

} // namespace ardor
