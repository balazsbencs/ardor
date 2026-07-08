#pragma once

#include "ui/UiModel.h"

#include <cstddef>
#include <string>
#include <vector>

#include <lvgl.h>

namespace ardor {

class LvglUi;

struct UiEventContext {
  LvglUi* ui = nullptr;
  UiState* state = nullptr;
  std::size_t index = 0;
  std::string filter = "all";
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

  std::vector<UiEventContext> contexts_;
};

} // namespace ardor
