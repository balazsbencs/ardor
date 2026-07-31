#pragma once

#include "ui/UiModel.h"
#include "ui/fonts/OpenSansRegular.h"
#include "ui/fonts/OpenSansSemibold.h"

#include <cstdint>
#include <string>

#include <lvgl.h>

namespace ardor::lvgl_ui {

inline constexpr int32_t kDesignWidth = 1280;
inline constexpr int32_t kDesignHeight = 720;
inline constexpr int kStatusBarHeight = 48;
inline constexpr int kHeaderButtonHeight = 60;
inline constexpr int kHeaderBlocksButtonWidth = 164;
inline constexpr int kBlockDrawerWidth = 480;

inline constexpr int bg = 0x000000;
inline constexpr int panel = 0x242424;
inline constexpr int panelAlt = 0x242424;
inline constexpr int text = 0xf5f5f5;
inline constexpr int muted = 0xa6a6a6;
inline constexpr int rigRight = 0x67a6ff;
inline constexpr int warning = 0xffb347;
inline constexpr int danger = 0xf97373;

// The current UI is rendered on one LVGL thread. Keeping this internal theme
// value in one module removes duplicated globals while screen components are
// split out; a later theme object can replace it without changing view code.
extern std::uint32_t accent;

void setText(lv_obj_t* object, int color = text,
             const lv_font_t* font = &ardor_font_open_sans_regular_18);
void styleSurface(lv_obj_t* object, int color = panel);
lv_obj_t* label(lv_obj_t* parent, const std::string& value, lv_align_t align,
                int x, int y,
                const lv_font_t* font = &ardor_font_open_sans_regular_18,
                int color = text);
lv_obj_t* button(lv_obj_t* parent, const std::string& value);

} // namespace ardor::lvgl_ui
