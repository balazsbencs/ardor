#pragma once

#include <lvgl.h>

namespace ardor {

class LvglUi;
struct UiState;

void renderStatusBar(LvglUi* ui, lv_obj_t* root, UiState& state,
                     lv_obj_t** telemetryOut, lv_obj_t** masterOut,
                     lv_obj_t** messageOut, lv_obj_t** undoOut);

} // namespace ardor
