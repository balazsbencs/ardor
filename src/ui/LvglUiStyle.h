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

// Palette carries a slight cool bias so surfaces read against the ground
// instead of floating on flat black. Depth comes from a top-lit gradient, a
// hairline rim, and soft elevation shadows applied in styleSurface/button.
inline constexpr int bg = 0x07090b;      // near-black ground
inline constexpr int panel = 0x18232a;   // primary surface (gradient midpoint)
inline constexpr int panelAlt = 0x11191d; // recessed / nested surface
inline constexpr int text = 0xeef4f2;
inline constexpr int muted = 0x8fa3a0;
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

// Family accent for a block type or drawer filter key (amp, cabinet, utility,
// modulation, delay, reverb). Unknown keys fall back to the current accent.
int categoryColor(const std::string& key);

} // namespace ardor::lvgl_ui
