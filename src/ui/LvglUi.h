#pragma once

#include "ui/EqEditorModel.h"
#include "ui/ParameterControls.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <lvgl.h>

namespace ardor {

class LvglUi;

enum class UiContextRegion { None, Preset, Edit, Tuner, Parameters, Drawer, Status, Settings };

struct UiEventContext {
  LvglUi* ui = nullptr;
  UiState* state = nullptr;
  std::size_t index = 0;
  std::size_t parentIndex = 0;
  std::size_t laneIndex = 0;
  std::string filter = "all";
  lv_obj_t* ghost = nullptr;
  lv_obj_t* indicator = nullptr;
  lv_obj_t* controlledObject = nullptr;
  lv_point_t pressPoint{};
  bool dragging = false;
  bool suppressClick = false;
  std::string dragText;
  UiContextRegion region = UiContextRegion::None;
};

struct UiActions {
  std::function<void(std::size_t)> selectPreset;
  std::function<void()> savePreset;
  std::function<bool(const std::string&, std::size_t, const EqBandParams&)> updateEqBand;
  std::function<bool(const std::string&, const std::string&, float)> updateDaisyParameter;
  std::function<bool(const std::string&, const std::string&, float)> updateCompressorParameter;
  std::function<bool(const std::string&, const std::string&, float)> updateNoiseGateParameter;
  std::function<void(float, float)> updateGlobalGains;
  std::function<void(float, float)> updateCabParameters;
  std::function<void(int)> changeBank;
  std::function<void(UiNavigationDecision)> resolveNavigation;
  // Requests the host-level tuner transition so audio muting and analyzer
  // routing change together with the visible screen.
  std::function<void(bool)> setTunerMode;
  std::function<bool(PaletteId, std::string&)> savePalette;
  std::function<bool(const std::string&, const std::string&, const std::string&, std::string&)>
    saveWifiSettings;
  std::function<void(const std::optional<PresetExpression>&)> updateExpressionAssignment;
  std::function<void(const std::vector<PresetMidiBinding>&)> updateMidiBindings;
  std::function<bool(const DeviceSettings&, std::string&)> saveControlInputSettings;
  std::function<bool(const std::string&, bool)> updateBlockEnabled;
};

struct UiLaneDropTarget {
  std::size_t rigIndex = 0;
  std::size_t laneIndex = 0;
  std::size_t blockIndex = 0;
};

class LvglUi {
public:
  explicit LvglUi(UiActions actions = {});

  void build(lv_obj_t* root, UiState& state);
  void refresh(lv_obj_t* root, UiState& state);
  void invalidate(UiChange changes);
  void beginInteraction() { ++activeInteractions_; }
  void endInteraction(bool requestUiRebuild = true);
  void beginParameterInteraction() { beginInteraction(); }
  void endParameterInteraction() { endInteraction(); }
  void selectPreset(UiState& state, std::size_t presetIndex);
  void selectBlock(UiState& state, std::size_t blockIndex);
  void selectLaneBlock(UiState& state, std::size_t rigIndex, std::size_t laneIndex,
                       std::size_t blockIndex);
  void selectGlobalParams(UiState& state);
  void highlightBlock(std::string blockId);
  bool isBlockHighlighted(const std::string& blockId) const;
  void focusParameter(std::string key)
  {
    focusedEqField_.reset();
    focusedKey_ = std::move(key);
    if (!focusedControl_) {
      invalidate(UiChange::Parameters);
    }
  }
  void focusEqBandField(EqBandField field)
  {
    focusedKey_.clear();
    focusedEqField_ = field;
    if (!focusedControl_ && !focusedEqGraph_) {
      invalidate(UiChange::Parameters);
    }
  }
  void selectEqBand(std::size_t bandIndex)
  {
    selectedEqBand_ = std::min(bandIndex, kParametricEqBandCount - 1);
    // The band editor highlights Q by default (spec 9.1) so the encoder
    // always has a live target as soon as a band is selected, without
    // clobbering an in-progress drag/encoder focus on another field.
    if (!focusedEqField_) {
      focusedEqField_ = EqBandField::Q;
    }
    if (!focusedEqGraph_) {
      invalidate(UiChange::Parameters);
    }
  }
  std::size_t selectedEqBand() const { return selectedEqBand_; }
  bool isEqBandFieldFocused(EqBandField field) const { return focusedEqField_ == field; }
  bool updateSelectedEqBand(UiState& state, EqBandParams params, bool requestUiRefresh = true);
  // Graph-side drags (node/grip) mutate the model and repaint the curve
  // directly, bypassing the interaction-gated refresh() path below -- so
  // they must resync the Frequency/Q/Gain sliders themselves or those
  // widgets go stale until the drag ends.
  void syncEqSliders(const UiState& state);
  UiEventContext* remember(UiState& state, std::size_t index = 0, std::string filter = "all");
  bool isParameterFocused(const std::string& key) const { return focusedKey_ == key; }
  void resetParameterPage()
  {
    focusedKey_.clear();
    focusedEqField_.reset();
    focusedControl_ = nullptr;
    focusedEqGraph_ = nullptr;
    parameterPage_ = 0;
  }
  void setParameterPage(std::size_t page) { parameterPage_ = page; }
  std::size_t parameterPage() const { return parameterPage_; }
  static std::size_t chainSlotForPoint(std::size_t blockCount, lv_point_t canvasPoint);
  static std::size_t chainInsertionSlotForPoint(std::size_t blockCount, lv_point_t canvasPoint);
  static lv_point_t chainIndicatorPosition(std::size_t blockCount, std::size_t slot);
  static lv_point_t chainReorderIndicatorPosition(std::size_t blockCount, std::size_t source,
                                                   std::size_t target);
  std::size_t chainSlotAtPoint(lv_point_t canvasPoint) const;
  std::size_t chainInsertionSlotAtPoint(lv_point_t canvasPoint) const;
  lv_point_t chainIndicatorForSlot(std::size_t slot) const;
  void scrollChainToStart(UiState& state);
  void scrollChainToEnd(UiState& state);
  void scrollChainBlockIntoView(std::size_t blockIndex);
  void setChainDragActive(bool active);
  void autoScrollChainForDrag(UiState& state, lv_point_t canvasPoint);
  std::optional<UiLaneDropTarget> laneDropTargetAtPoint(lv_point_t canvasPoint) const;
  lv_point_t laneIndicatorForTarget(const UiLaneDropTarget& target) const;
  bool applyFocusedParameterDelta(UiState& state, int delta, bool continuousTouch = false);
  void setFocusedWidgets(lv_obj_t* control, lv_obj_t* eqGraph = nullptr)
  {
    focusedControl_ = control;
    focusedEqGraph_ = eqGraph;
  }

  const UiActions& actions() const { return actions_; }

  // The UI is built on a scaled canvas (see build()). Drag handlers read the
  // pointer in display space; these translate to the canvas' design space so
  // hit-testing and overlay placement stay correct at any resolution.
  lv_obj_t* canvas() const { return canvas_; }
  lv_point_t toCanvas(lv_point_t displayPoint) const;
  void openSettings(UiState& state);
  void closeSettings(UiState& state);
  void showSettingsSection(UiState& state, std::size_t section);
  void selectPalette(UiState& state, std::size_t paletteIndex);
  void saveWifiSettings(UiState& state);
  void toggleWifiPassword();
  void toggleExpressionAssignment(UiState& state, const ParameterControl& control);
  void adjustMidiChannel(UiState& state, int delta);
  void adjustMidiTunerCc(UiState& state, int delta);
  void captureExpressionEndpoint(UiState& state, bool heel);

private:
  void renderPresetMode(lv_obj_t* root, UiState& state);
  void renderEditMode(lv_obj_t* root, UiState& state);
  void renderTunerMode(lv_obj_t* root, UiState& state);
  void renderBlockDrawer(lv_obj_t* root, UiState& state);
  void renderSettingsView(lv_obj_t* root, UiState& state);
  void rebuildPresetView(UiState& state);
  void rebuildEditView(UiState& state);
  void rebuildParameterView(UiState& state);
  void rebuildDrawerView(UiState& state);
  void syncChainCards(UiState& state);
  void syncDrawerAssets(UiState& state);
  void syncDrawerView(UiState& state);
  void syncParameterView(UiState& state);
  // Reads state.compressorGainReductionDb into the meter widget directly.
  // Called from refresh() BEFORE the activeInteractions_ gate (same carve-out
  // as syncBlockingOverlays) so the meter keeps moving while a slider drag
  // owns the input device -- that gate exists to protect widgets a drag is
  // actively touching, and this widget never is one of them.
  void syncCompressorGainMeter(UiState& state);
  void syncModeVisibility(const UiState& state);
  void syncHeaderView(const UiState& state);
  void syncPresetCards(const UiState& state);
  void syncStatusView(const UiState& state);
  void syncPersistentViews(UiState& state);
  void syncBlockingOverlays(const UiState& state);
  void syncTunerView(UiState& state);

  UiActions actions_;
  std::list<UiEventContext> contexts_;
  UiContextRegion contextRegion_ = UiContextRegion::None;
  UiChange pendingChanges_ = UiChange::None;
  unsigned activeInteractions_ = 0;
  std::string focusedKey_;
  std::optional<EqBandField> focusedEqField_;
  std::size_t selectedEqBand_ = 0;
  std::size_t parameterPage_ = 0;
  lv_obj_t* focusedControl_ = nullptr;
  lv_obj_t* focusedEqGraph_ = nullptr;
  lv_obj_t* canvas_ = nullptr;
  lv_obj_t* presetLayer_ = nullptr;
  lv_obj_t* editLayer_ = nullptr;
  lv_obj_t* tunerLayer_ = nullptr;
  lv_obj_t* parameterLayer_ = nullptr;
  lv_obj_t* drawerLayer_ = nullptr;
  lv_obj_t* statusLayer_ = nullptr;
  lv_obj_t* settingsLayer_ = nullptr;
  lv_obj_t* navigationOverlay_ = nullptr;
  lv_obj_t* midiLearnOverlay_ = nullptr;
  lv_obj_t* midiLearnGuidanceLabel_ = nullptr;
  lv_obj_t* midiLearnCaptureLabel_ = nullptr;
  lv_obj_t* midiLearnAdvancedGroup_ = nullptr;
  lv_obj_t* midiLearnAdvancedButton_ = nullptr;
  lv_obj_t* midiLearnSaveButton_ = nullptr;
  lv_obj_t* midiLearnModeButton_ = nullptr;
  std::array<lv_obj_t*, 2> midiLearnSliders_{};
  std::array<lv_obj_t*, 2> midiLearnValueLabels_{};
  lv_obj_t* wifiSSIDField_ = nullptr;
  lv_obj_t* wifiPasswordField_ = nullptr;
  lv_obj_t* wifiCountryField_ = nullptr;
  lv_obj_t* wifiKeyboard_ = nullptr;
  lv_obj_t* wifiPasswordToggleLabel_ = nullptr;
  bool settingsOpen_ = false;
  std::size_t settingsSection_ = 0;
  bool wifiPasswordVisible_ = false;
  std::string settingsMessage_;
  bool settingsMessageIsError_ = false;
  lv_obj_t* presetBankLabel_ = nullptr;
  lv_obj_t* masterVolumeLabel_ = nullptr;
  lv_obj_t* masterVolumeScaleFill_ = nullptr;
  lv_obj_t* bankDownButton_ = nullptr;
  lv_obj_t* bankUpButton_ = nullptr;
  // Preset screen's own top legend rail + bottom control rail (per
  // docs/lvgl-ui-redesign-spec.md §4f). Distinct from the shared status-bar
  // members above, which remain in use by the screens not yet migrated.
  lv_obj_t* presetMidiLamp_ = nullptr;
  lv_obj_t* presetMidiLabel_ = nullptr;
  lv_obj_t* presetMasterValueLabel_ = nullptr;
  lv_obj_t* presetMasterScaleFill_ = nullptr;
  lv_obj_t* presetMasterPointer_ = nullptr;
  lv_obj_t* editPresetLabel_ = nullptr;
  // Edit screen's own rails, mirroring the preset-screen members above.
  lv_obj_t* editModifiedLabel_ = nullptr;
  lv_obj_t* editModuleCountLabel_ = nullptr;
  lv_obj_t* saveButtonLabel_ = nullptr;
  lv_obj_t* telemetryLabel_ = nullptr;
  lv_obj_t* expressionStatusLabel_ = nullptr;
  lv_obj_t* midiStatusLabel_ = nullptr;
  lv_obj_t* settingsButton_ = nullptr;
  lv_obj_t* statusMessageLabel_ = nullptr;
  lv_obj_t* undoButton_ = nullptr;
  std::uint64_t statusToastRevision_ = 0;
  const UiState* statusToastState_ = nullptr;
  lv_obj_t* tunerNoteLabel_ = nullptr;
  lv_obj_t* tunerFrequencyLabel_ = nullptr;
  lv_obj_t* tunerCentsLabel_ = nullptr;
  lv_obj_t* tunerGuidanceLabel_ = nullptr;
  lv_obj_t* tunerNeedle_ = nullptr;
  std::array<lv_obj_t*, 3> tunerVerdictLamps_{};
  std::array<lv_obj_t*, 4> presetCardLabels_{};
  std::array<lv_obj_t*, 4> presetCardButtons_{};
  std::array<lv_obj_t*, 4> presetHeaderStrips_{};
  std::array<lv_obj_t*, 4> presetLamps_{};
  std::array<lv_obj_t*, 4> presetNumerals_{};
  std::array<lv_obj_t*, 4> presetWarningLabels_{};
  std::array<lv_obj_t*, kMaxEffectBlocks> chainCards_{};
  std::array<lv_obj_t*, kMaxEffectBlocks> chainCategoryLabels_{};
  std::array<lv_obj_t*, kMaxEffectBlocks> chainAssetLabels_{};
  std::array<lv_obj_t*, kMaxEffectBlocks> chainBypassLabels_{};
  std::array<UiEventContext*, kMaxEffectBlocks> chainClickContexts_{};
  std::array<UiEventContext*, kMaxEffectBlocks> chainDragContexts_{};
  std::vector<std::string> renderedBlockIds_;
  lv_obj_t* chainViewport_ = nullptr;
  lv_obj_t* chainWorld_ = nullptr;
  bool chainDragActive_ = false;
  std::vector<int32_t> chainItemStarts_;
  std::vector<int32_t> chainItemEnds_;
  std::vector<int32_t> chainInsertionXs_;
  std::optional<std::size_t> renderedRigIndex_;
  std::array<std::vector<int32_t>, 2> laneInsertionXs_;
  static constexpr std::size_t kDrawerCategoryCount = 7;
  std::array<lv_obj_t*, kDrawerCategoryCount> drawerCategoryButtons_{};
  std::vector<lv_obj_t*> drawerAssetButtons_;
  std::vector<UiEventContext*> drawerAssetContexts_;
  std::vector<lv_obj_t*> drawerAssetSubtitleLabels_;
  std::vector<std::string> renderedAssetKeys_;
  lv_obj_t* drawerAssetList_ = nullptr;
  lv_obj_t* drawerInstructionLabel_ = nullptr;
  lv_obj_t* drawerCountLabel_ = nullptr;
  lv_obj_t* drawerFooterCountLabel_ = nullptr;
  std::vector<lv_obj_t*> parameterControls_;
  lv_obj_t* parameterTitleLabel_ = nullptr;
  lv_obj_t* parameterBypassControl_ = nullptr;
  lv_obj_t* parameterMappingToolbar_ = nullptr;
  lv_obj_t* eqGraph_ = nullptr;
  std::array<lv_obj_t*, kParametricEqBandCount> eqBandButtons_{};
  std::array<lv_obj_t*, 3> eqSliders_{};
  lv_obj_t* eqEnabledButton_ = nullptr;
  UiEventContext* eqEnabledContext_ = nullptr;
  UiEventContext* eqResetContext_ = nullptr;
  std::array<UiEventContext*, 3> eqSliderContexts_{};
  lv_obj_t* compressorGainMeterFill_ = nullptr;
  lv_obj_t* compressorGainMeterLabel_ = nullptr;
  struct ParameterViewRefs {
    lv_obj_t* layer = nullptr;
    std::vector<lv_obj_t*> controls;
    lv_obj_t* titleLabel = nullptr;
    lv_obj_t* bypassControl = nullptr;
    lv_obj_t* mappingToolbar = nullptr;
    lv_obj_t* eqGraph = nullptr;
    std::array<lv_obj_t*, kParametricEqBandCount> eqBandButtons{};
    std::array<lv_obj_t*, 3> eqSliders{};
    lv_obj_t* eqEnabledButton = nullptr;
    UiEventContext* eqEnabledContext = nullptr;
    UiEventContext* eqResetContext = nullptr;
    std::array<UiEventContext*, 3> eqSliderContexts{};
    lv_obj_t* gainMeterFill = nullptr;
    lv_obj_t* gainMeterLabel = nullptr;
  };
  std::unordered_map<std::string, ParameterViewRefs> parameterViews_;
  lv_obj_t* activeParameterLayer_ = nullptr;
  UiRevisions renderedRevisions_{};
  bool viewsInitialized_ = false;
  int renderedBank_ = -1;
  std::size_t renderedPreset_ = static_cast<std::size_t>(-1);
  std::string renderedParameterSignature_;
  int32_t canvasScale_ = 256;  // 8.8 fixed point; 256 == 1.0
  lv_point_t canvasOffset_{};
  std::string highlightedBlockId_;
  std::chrono::steady_clock::time_point highlightUntil_{};
};

} // namespace ardor
