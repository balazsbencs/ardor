#pragma once

#include "ui/UiModel.h"

#include <cstddef>
#include <deque>
#include <string>

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

class LvglUi {
public:
  void build(lv_obj_t* root, UiState& state);

private:
  void renderPresetMode(lv_obj_t* root, UiState& state);
  void renderEditMode(lv_obj_t* root, UiState& state);
  void renderBlockDrawer(lv_obj_t* root, UiState& state);
  void renderParamDrawer(lv_obj_t* root, UiState& state);

  UiEventContext* remember(UiState& state, std::size_t index = 0, std::string filter = "all");

  std::deque<UiEventContext> contexts_;
};

} // namespace ardor
