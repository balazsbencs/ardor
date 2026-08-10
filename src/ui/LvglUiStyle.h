#pragma once

#include "ui/UiModel.h"
#include "ui/fonts/SairaCondMedium18.h"
#include "ui/fonts/SairaCondSemibold11.h"
#include "ui/fonts/SairaCondSemibold22.h"
#include "ui/fonts/SairaCondSemibold28.h"
#include "ui/fonts/SairaCondSemiboldTuner110.h"
#include "ui/fonts/SairaLight12.h"
#include "ui/fonts/SairaLight44.h"

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

struct PanelPalette {
  std::uint32_t plate, plate2, plate3;
  std::uint32_t engrave, engraveLo, engraveOff, rule;
  std::uint32_t lamp, warn, faultLine, faultText;
  std::uint32_t laneL, laneR;
  std::uint32_t family[6];
};

const PanelPalette& palette();
const PanelPalette& palette(PaletteId id);
void setPalette(PaletteId id);

// Named semantic tokens keep the retained views compact. They are refreshed
// together by setPalette(), so a scene rebuild cannot mix palette values.
extern std::uint32_t bg, panel, panelAlt, text, muted, disabled, rule;
extern std::uint32_t lamp, warning, danger, laneL, laneR;

void setText(lv_obj_t* object, int color = text,
             const lv_font_t* font = &ardor_font_saira_cond_medium_18);
void styleSurface(lv_obj_t* object, int color = panel);
lv_obj_t* label(lv_obj_t* parent, const std::string& value, lv_align_t align,
                int x, int y,
                const lv_font_t* font = &ardor_font_saira_cond_medium_18,
                int color = text);
lv_obj_t* button(lv_obj_t* parent, const std::string& value);

// Family colour for a block type or drawer filter key (amp, cabinet, utility,
// modulation, delay, reverb). Unknown keys fall back to engraved secondary text.
int categoryColor(const std::string& key);

// The redesign renders every on-panel string in caps (engraved-plate
// convention); LVGL has no CSS text-transform equivalent, so callers
// uppercase the text itself before handing it to label()/lv_label_set_text().
std::string uppercase(std::string value);

} // namespace ardor::lvgl_ui
