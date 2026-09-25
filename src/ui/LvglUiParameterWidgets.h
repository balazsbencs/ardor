#pragma once

#include "ui/ParameterControls.h"

#include <cstddef>

#include <lvgl.h>

namespace ardor {

struct UiEventContext;
struct UiState;

namespace parameter_widgets {

inline constexpr int panelWidth = 1280;
inline constexpr int panelEdgeInset = 24;
inline constexpr int sliderWidth = 403;
inline constexpr int sliderHeight = 166;
inline constexpr int sliderColumnGap = 12;
inline constexpr int sliderGridX = 24;

// x of a control-grid column: CSS splits 1232 px into 402.67 px columns.
int columnX(int column);

float sliderRatioForInput(lv_obj_t* slider, lv_indev_t* input);
lv_obj_t* createSlider(lv_obj_t* parent, const ParameterControl& control,
                       int x, int y, bool focused, UiEventContext* context,
                       lv_event_cb_t pressedCallback,
                       lv_event_cb_t pressingCallback,
                       std::size_t controlIndex);
// The Done-only context rail, on the screen-sized view layer.
lv_obj_t* renderCloseButton(lv_obj_t* parent, UiEventContext* context);
// Makes a button close the parameter drawer on touch-down.
void bindClose(lv_obj_t* button, UiEventContext* context);
void renderBlockActions(lv_obj_t* parent, UiState& state,
                        UiEventContext* context, lv_obj_t** bypassOut);

} // namespace parameter_widgets
} // namespace ardor
