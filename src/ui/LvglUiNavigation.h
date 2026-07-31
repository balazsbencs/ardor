#pragma once

#include <lvgl.h>

namespace ardor::lvgl_navigation {

void onSaveClicked(lv_event_t* event);
void onNavigationDecision(lv_event_t* event);
void onPresetModeClicked(lv_event_t* event);
void onTunerModeClicked(lv_event_t* event);
void onEditModeClicked(lv_event_t* event);
void onSettingsClicked(lv_event_t* event);

} // namespace ardor::lvgl_navigation
