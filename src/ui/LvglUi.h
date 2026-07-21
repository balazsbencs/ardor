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

enum class UiContextRegion { None, Preset, Edit, Tuner, Parameters, Drawer, Status };

struct UiEventContext {
  LvglUi* ui = nullptr;
  UiState* state = nullptr;
  std::size_t index = 0;
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
  std::function<void(float, float)> updateGlobalGains;
  std::function<void(float, float)> updateCabParameters;
  std::function<void(int)> changeBank;
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
    if (!focusedEqGraph_) {
      invalidate(UiChange::Parameters);
    }
  }
  std::size_t selectedEqBand() const { return selectedEqBand_; }
  bool isEqBandFieldFocused(EqBandField field) const { return focusedEqField_ == field; }
  bool updateSelectedEqBand(UiState& state, EqBandParams params, bool requestUiRefresh = true);
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

private:
  void renderPresetMode(lv_obj_t* root, UiState& state);
  void renderEditMode(lv_obj_t* root, UiState& state);
  void renderTunerMode(lv_obj_t* root, UiState& state);
  void renderBlockDrawer(lv_obj_t* root, UiState& state);
  void rebuildPresetView(UiState& state);
  void rebuildEditView(UiState& state);
  void rebuildParameterView(UiState& state);
  void rebuildDrawerView(UiState& state);
  void syncChainCards(UiState& state);
  void syncDrawerAssets(UiState& state);
  void syncDrawerView(UiState& state);
  void syncParameterView(UiState& state);
  void syncPersistentViews(UiState& state);
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
  lv_obj_t* presetBankLabel_ = nullptr;
  lv_obj_t* masterVolumeLabel_ = nullptr;
  lv_obj_t* bankDownButton_ = nullptr;
  lv_obj_t* bankUpButton_ = nullptr;
  lv_obj_t* editPresetLabel_ = nullptr;
  lv_obj_t* saveButtonLabel_ = nullptr;
  lv_obj_t* telemetryLabel_ = nullptr;
  lv_obj_t* statusMessageLabel_ = nullptr;
  lv_obj_t* undoButton_ = nullptr;
  lv_obj_t* tunerNoteLabel_ = nullptr;
  lv_obj_t* tunerFrequencyLabel_ = nullptr;
  lv_obj_t* tunerCentsLabel_ = nullptr;
  lv_obj_t* tunerGuidanceLabel_ = nullptr;
  lv_obj_t* tunerNeedle_ = nullptr;
  std::array<lv_obj_t*, 4> presetCardLabels_{};
  std::array<lv_obj_t*, 4> presetCardButtons_{};
  std::array<lv_obj_t*, 4> presetIndicators_{};
  std::array<lv_obj_t*, kMaxEffectBlocks> chainCards_{};
  std::array<lv_obj_t*, kMaxEffectBlocks> chainCategoryLabels_{};
  std::array<lv_obj_t*, kMaxEffectBlocks> chainAssetLabels_{};
  std::array<lv_obj_t*, kMaxEffectBlocks> chainBypassLabels_{};
  std::array<lv_obj_t*, kMaxEffectBlocks> chainSelectionIndicators_{};
  std::array<UiEventContext*, kMaxEffectBlocks> chainClickContexts_{};
  std::array<UiEventContext*, kMaxEffectBlocks> chainDragContexts_{};
  std::vector<std::string> renderedBlockIds_;
  lv_obj_t* chainWrapConnector_ = nullptr;
  static constexpr std::size_t kDrawerCategoryCount = 8;
  std::array<lv_obj_t*, kDrawerCategoryCount> drawerCategoryButtons_{};
  std::vector<lv_obj_t*> drawerAssetButtons_;
  std::vector<UiEventContext*> drawerAssetContexts_;
  std::vector<std::string> renderedAssetKeys_;
  lv_obj_t* drawerAssetList_ = nullptr;
  lv_obj_t* drawerInstructionLabel_ = nullptr;
  std::vector<lv_obj_t*> parameterControls_;
  lv_obj_t* parameterTitleLabel_ = nullptr;
  lv_obj_t* parameterBypassControl_ = nullptr;
  lv_obj_t* eqGraph_ = nullptr;
  std::array<lv_obj_t*, kParametricEqBandCount> eqBandButtons_{};
  std::array<lv_obj_t*, 3> eqSliders_{};
  lv_obj_t* eqEnabledButton_ = nullptr;
  UiEventContext* eqEnabledContext_ = nullptr;
  UiEventContext* eqResetContext_ = nullptr;
  std::array<UiEventContext*, 3> eqSliderContexts_{};
  struct ParameterViewRefs {
    lv_obj_t* layer = nullptr;
    std::vector<lv_obj_t*> controls;
    lv_obj_t* titleLabel = nullptr;
    lv_obj_t* bypassControl = nullptr;
    lv_obj_t* eqGraph = nullptr;
    std::array<lv_obj_t*, kParametricEqBandCount> eqBandButtons{};
    std::array<lv_obj_t*, 3> eqSliders{};
    lv_obj_t* eqEnabledButton = nullptr;
    UiEventContext* eqEnabledContext = nullptr;
    UiEventContext* eqResetContext = nullptr;
    std::array<UiEventContext*, 3> eqSliderContexts{};
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
