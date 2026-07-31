#pragma once

#include "ui/ParameterControls.h"

#include <cstddef>

#include <lvgl.h>

namespace ardor {

struct UiEventContext;
struct UiState;

namespace parameter_widgets {

inline constexpr int panelWidth = 1240;
inline constexpr int panelEdgeInset = 28;
inline constexpr int sliderWidth = 385;
inline constexpr int sliderColumnGap = 14;
inline constexpr int sliderGridX = 28;

float sliderRatioForInput(lv_obj_t* slider, lv_indev_t* input);
lv_obj_t* createSlider(lv_obj_t* parent, const ParameterControl& control,
                       int x, int y, bool focused, UiEventContext* context,
                       lv_event_cb_t pressedCallback,
                       lv_event_cb_t pressingCallback,
                       std::size_t controlIndex);
lv_obj_t* renderCloseButton(lv_obj_t* parent, UiEventContext* context);
void renderBlockActions(lv_obj_t* parent, UiState& state,
                        UiEventContext* context, lv_obj_t** bypassOut);

} // namespace parameter_widgets
} // namespace ardor
