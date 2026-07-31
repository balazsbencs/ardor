#pragma once

#include <cstddef>

#include <lvgl.h>

namespace ardor {

struct UiEventContext;
struct UiState;

namespace lvgl_drag {

bool pointInVisibleChain(const UiState& state, const lv_point_t& point);
void moveToFront(lv_obj_t* object);
void placeDragIndicatorAtSlot(UiEventContext* context, std::size_t slot);
void placeDragIndicator(UiEventContext* context, const lv_point_t& point);
void placeDragGhost(UiEventContext* context, const lv_point_t& point);
void updateDragVisuals(UiEventContext* context, lv_event_t* event);
void clearDragVisuals(UiEventContext* context);

} // namespace lvgl_drag
} // namespace ardor
