#pragma once

#include "ui/ParameterControls.h"

#include <cstddef>
#include <deque>
#include <functional>
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
};

class LvglUi {
public:
  explicit LvglUi(UiActions actions = {});

  void build(lv_obj_t* root, UiState& state);
  void refresh(lv_obj_t* root, UiState& state);
  void requestRebuild();
  void selectPreset(UiState& state, std::size_t presetIndex);
  void selectBlock(UiState& state, std::size_t blockIndex);
  void selectGlobalParams(UiState& state);
  void focusParameter(std::string key)
  {
    focusedKey_ = std::move(key);
    requestRebuild();
  }
  bool isParameterFocused(const std::string& key) const { return focusedKey_ == key; }
  void resetParameterPage()
  {
    focusedKey_.clear();
    parameterPage_ = 0;
  }
  void setParameterPage(std::size_t page) { parameterPage_ = page; }
  std::size_t parameterPage() const { return parameterPage_; }
  static std::size_t chainSlotForX(std::size_t blockCount, int canvasX);
  static std::size_t chainInsertionSlotForX(std::size_t blockCount, int canvasX);
  static int chainIndicatorX(std::size_t blockCount, std::size_t slot);
  bool applyFocusedParameterDelta(UiState& state, int delta)
  {
    if (focusedKey_.empty()) {
      return false;
    }
    for (const auto& control : ardor::parameterPage(state, parameterPage_)) {
      if (control.key == focusedKey_) {
        applyParameterDelta(state, control, delta);
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

  UiEventContext* remember(UiState& state, std::size_t index = 0, std::string filter = "all");

  UiActions actions_;
  std::deque<UiEventContext> contexts_;
  bool rebuildPending_ = false;
  std::string focusedKey_;
  std::size_t parameterPage_ = 0;
  lv_obj_t* canvas_ = nullptr;
  int32_t canvasScale_ = 256;  // 8.8 fixed point; 256 == 1.0
  lv_point_t canvasOffset_{};
};

} // namespace ardor
