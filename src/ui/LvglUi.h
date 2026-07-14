#pragma once

#include "ui/EqEditorModel.h"
#include "ui/ParameterControls.h"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include <lvgl.h>

namespace ardor {

class LvglUi;

struct UiEventContext {
  LvglUi* ui = nullptr;
  UiState* state = nullptr;
  std::size_t index = 0;
  std::string filter = "all";
  lv_obj_t* ghost = nullptr;
  lv_obj_t* indicator = nullptr;
  lv_point_t pressPoint{};
  bool dragging = false;
  bool suppressClick = false;
  std::string dragText;
};

struct UiActions {
  std::function<void(std::size_t)> selectPreset;
  std::function<void()> savePreset;
  std::function<void(const std::string&, std::size_t, const EqBandParams&)> updateEqBand;
};

class LvglUi {
public:
  explicit LvglUi(UiActions actions = {});

  void build(lv_obj_t* root, UiState& state);
  void refresh(lv_obj_t* root, UiState& state);
  void requestRebuild();
  void beginKnobInteraction() { knobInteractionActive_ = true; }
  void endKnobInteraction()
  {
    knobInteractionActive_ = false;
    requestRebuild();
  }
  void selectPreset(UiState& state, std::size_t presetIndex);
  void selectBlock(UiState& state, std::size_t blockIndex);
  void selectGlobalParams(UiState& state);
  void focusParameter(std::string key)
  {
    focusedEqField_.reset();
    focusedKey_ = std::move(key);
    requestRebuild();
  }
  void focusEqBandField(EqBandField field)
  {
    focusedKey_.clear();
    focusedEqField_ = field;
    requestRebuild();
  }
  void selectEqBand(std::size_t bandIndex)
  {
    selectedEqBand_ = std::min(bandIndex, kParametricEqBandCount - 1);
    requestRebuild();
  }
  std::size_t selectedEqBand() const { return selectedEqBand_; }
  bool isEqBandFieldFocused(EqBandField field) const { return focusedEqField_ == field; }
  bool updateSelectedEqBand(UiState& state, EqBandParams params);
  UiEventContext* remember(UiState& state, std::size_t index = 0, std::string filter = "all");
  bool isParameterFocused(const std::string& key) const { return focusedKey_ == key; }
  void resetParameterPage()
  {
    focusedKey_.clear();
    focusedEqField_.reset();
    parameterPage_ = 0;
  }
  void setParameterPage(std::size_t page) { parameterPage_ = page; }
  std::size_t parameterPage() const { return parameterPage_; }
  static std::size_t chainSlotForPoint(std::size_t blockCount, lv_point_t canvasPoint);
  static std::size_t chainInsertionSlotForPoint(std::size_t blockCount, lv_point_t canvasPoint);
  static lv_point_t chainIndicatorPosition(std::size_t blockCount, std::size_t slot);
  bool applyFocusedParameterDelta(UiState& state, int delta)
  {
    if (focusedEqField_.has_value()) {
      const auto& blocks = state.bank.presets[state.activePreset].blocks;
      if (state.paramTarget != UiParamTarget::Block || state.selectedBlock >= blocks.size()
          || blocks[state.selectedBlock].type != "eq" || !isParametricEqMode(blocks[state.selectedBlock].params)) {
        return false;
      }
      auto params = selectedParametricEqParams(state);
      adjustEqBandField(params.bands[selectedEqBand_], *focusedEqField_, delta);
      return updateSelectedEqBand(state, params.bands[selectedEqBand_]);
    }
    if (focusedKey_.empty()) {
      return false;
    }
    for (const auto& control : ardor::parameterPage(state, parameterPage_)) {
      if (control.key == focusedKey_) {
        if (applyParameterDelta(state, control, delta)) {
          requestRebuild();
        }
        return true;
      }
    }
    return false;
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
  void renderBlockDrawer(lv_obj_t* root, UiState& state);
  void renderParamDrawer(lv_obj_t* root, UiState& state);

  UiActions actions_;
  std::deque<UiEventContext> contexts_;
  bool rebuildPending_ = false;
  bool knobInteractionActive_ = false;
  std::string focusedKey_;
  std::optional<EqBandField> focusedEqField_;
  std::size_t selectedEqBand_ = 0;
  std::size_t parameterPage_ = 0;
  lv_obj_t* canvas_ = nullptr;
  int32_t canvasScale_ = 256;  // 8.8 fixed point; 256 == 1.0
  lv_point_t canvasOffset_{};
};

} // namespace ardor
