#pragma once

// Lamp Black layout primitives (mockups/lvgl-taste/1-lamp-black.html).
//
// The mockup is CSS. Text there sits on a baseline that CSS derives from the
// font's ascent, while an LVGL label's top is its tallest glyph. Type records
// that ascent, so text placed with these helpers lands on the same baseline
// as the mockup and does not depend on how lv_font_conv cropped the glyphs.
#include "ui/LvglUiStyle.h"
#include "ui/fonts/LampBlackFonts.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <lvgl.h>

namespace ardor::lvgl_ui::lb {

struct Type {
  const lv_font_t* font;
  int16_t size;             // CSS font-size, px
  int16_t ascentPermille;   // hhea ascent / unitsPerEm x 1000
  int16_t trackingPermille; // CSS letter-spacing, em x 1000

  // LVGL spaces letters in whole pixels; layout uses the exact CSS value.
  constexpr int letterSpace() const { return (size * trackingPermille + 500) / 1000; }
  constexpr double tracking() const { return size * trackingPermille / 1000.0; }
};

// Saira and Saira Condensed share one vertical metric set; JetBrains Mono has
// its own. Values from the fonts' hhea tables.
inline constexpr int16_t kSairaAscent = 1135;
inline constexpr int16_t kSairaContent = 1574;
inline constexpr int16_t kMonoAscent = 1020;
inline constexpr int16_t kMonoContent = 1320;

// Shared geometry: a 64 px header over a rule, a 108 px bottom rail under a
// rule, 60 px rail buttons with 12 px gaps and a 24 px screen gutter.
inline constexpr int kHeaderHeight = 64;
inline constexpr int kRailY = 612;
inline constexpr int kRailHeight = 108;
inline constexpr int kRailButtonY = 637;
inline constexpr int kButtonHeight = 60;
inline constexpr int kButtonMinWidth = 124;
inline constexpr int kButtonPad = 22;
inline constexpr int kGap = 12;
inline constexpr int kGutter = 24;

namespace type {
// Headers.
inline constexpr Type bank{&ardor_lb_cond700_30, 30, kSairaAscent, 20};
inline constexpr Type bankName{&ardor_lb_cond500_22, 22, kSairaAscent, 60};
inline constexpr Type headerTitle{&ardor_lb_cond700_28, 28, kSairaAscent, 60};
inline constexpr Type headerSub{&ardor_lb_cond500_22, 22, kSairaAscent, 0};
inline constexpr Type headerRight{&ardor_lb_saira500_18, 18, kSairaAscent, 40};
inline constexpr Type tag{&ardor_lb_cond700_16, 16, kSairaAscent, 160};
// Preset tiles and rails.
inline constexpr Type footswitch{&ardor_lb_cond600_20, 20, kSairaAscent, 140};
inline constexpr Type liveTag{&ardor_lb_cond700_20, 20, kSairaAscent, 200};
inline constexpr Type presetName{&ardor_lb_cond700_76, 76, kSairaAscent, 5};
inline constexpr Type livePresetName{&ardor_lb_cond800_96, 96, kSairaAscent, 5};
inline constexpr Type code{&ardor_lb_mono500_13, 13, kMonoAscent, 40};
inline constexpr Type button{&ardor_lb_cond600_22, 22, kSairaAscent, 100};
inline constexpr Type buttonState{&ardor_lb_cond700_22, 22, kSairaAscent, 120};
inline constexpr Type stepButton{&ardor_lb_cond600_30, 30, kSairaAscent, 100};
inline constexpr Type pairLegend{&ardor_lb_cond600_14, 14, kSairaAscent, 160};
inline constexpr Type legend{&ardor_lb_cond600_16, 16, kSairaAscent, 180};
inline constexpr Type masterValue{&ardor_lb_cond700_52, 52, kSairaAscent, 0};
// Chain cards on the edit screen.
inline constexpr Type jack{&ardor_lb_cond700_18, 18, kSairaAscent, 100};
inline constexpr Type jackSmall{&ardor_lb_cond500_12, 12, kSairaAscent, 140};
inline constexpr Type plus{&ardor_lb_saira600_22, 22, kSairaAscent, 0};
inline constexpr Type cap{&ardor_lb_cond700_17, 17, kSairaAscent, 160};
inline constexpr Type blockName{&ardor_lb_cond700_30, 30, kSairaAscent, 0};
inline constexpr Type paramName{&ardor_lb_saira500_15, 15, kSairaAscent, 0};
inline constexpr Type paramValue{&ardor_lb_saira600_15, 15, kSairaAscent, 0};
inline constexpr Type offTag{&ardor_lb_cond700_14, 14, kSairaAscent, 180};
// Parameter drawer.
inline constexpr Type chip{&ardor_lb_cond700_20, 20, kSairaAscent, 0};
inline constexpr Type chipSmall{&ardor_lb_cond600_12, 12, kSairaAscent, 160};
inline constexpr Type chipIo{&ardor_lb_cond700_18, 18, kSairaAscent, 100};
inline constexpr Type category{&ardor_lb_cond700_16, 16, kSairaAscent, 160};
inline constexpr Type drawerName{&ardor_lb_cond800_40, 40, kSairaAscent, 0};
inline constexpr Type page{&ardor_lb_cond600_16, 16, kSairaAscent, 140};
inline constexpr Type controlLabel{&ardor_lb_cond600_18, 18, kSairaAscent, 160};
inline constexpr Type controlTag{&ardor_lb_cond700_13, 13, kSairaAscent, 160};
inline constexpr Type encoderTag{&ardor_lb_cond800_13, 13, kSairaAscent, 160};
inline constexpr Type controlValue{&ardor_lb_cond700_50, 50, kSairaAscent, 0};
inline constexpr Type controlUnit{&ardor_lb_saira500_18, 18, kSairaAscent, 0};
inline constexpr Type segment{&ardor_lb_cond600_20, 20, kSairaAscent, 80};
// Fallback for choice legends too long for their segment.
inline constexpr Type segmentSmall{&ardor_lb_cond600_16, 16, kSairaAscent, 40};
inline constexpr Type contextName{&ardor_lb_cond600_16, 16, kSairaAscent, 160};
inline constexpr Type contextValue{&ardor_lb_cond700_44, 44, kSairaAscent, 0};
// Fallback for context values wider than their 150 px column.
inline constexpr Type contextValueSmall{&ardor_lb_cond700_28, 28, kSairaAscent, 0};
// Module drawer.
inline constexpr Type drawerTitle{&ardor_lb_cond800_32, 32, kSairaAscent, 60};
inline constexpr Type count{&ardor_lb_cond600_20, 20, kSairaAscent, 0};
inline constexpr Type subtitle{&ardor_lb_cond600_16, 16, kSairaAscent, 140};
inline constexpr Type subtitleBold{&ardor_lb_cond700_16, 16, kSairaAscent, 140};
inline constexpr Type filter{&ardor_lb_cond600_19, 19, kSairaAscent, 80};
inline constexpr Type group{&ardor_lb_cond700_14, 14, kSairaAscent, 200};
inline constexpr Type itemTitle{&ardor_lb_cond700_24, 24, kSairaAscent, 0};
inline constexpr Type itemSubtitle{&ardor_lb_saira400_15, 15, kSairaAscent, 0};
inline constexpr Type add{&ardor_lb_saira600_26, 26, kSairaAscent, 0};
inline constexpr Type marker{&ardor_lb_saira700_26, 26, kSairaAscent, 0};
inline constexpr Type markerLabel{&ardor_lb_cond700_15, 15, kSairaAscent, 180};
inline constexpr Type footer{&ardor_lb_cond600_15, 15, kSairaAscent, 140};
} // namespace type

// LVGL label top for text whose CSS content box starts at contentTop.
int textTop(const Type& type, int contentTop);
// LVGL label top for text centred vertically in a box, as flexbox centres it.
int centeredTextTop(const Type& type, int boxTop, int boxHeight);
// Rendered width including the trailing letter-spacing CSS adds.
int textWidth(const Type& type, const std::string& value);

void applyType(lv_obj_t* label, const Type& type, std::uint32_t color);
lv_obj_t* textLabel(lv_obj_t* parent, const Type& type, const std::string& value,
               std::uint32_t color, int x, int contentTop);
// Text centred in a parent-relative box, horizontally and vertically.
lv_obj_t* centeredText(lv_obj_t* parent, const Type& type, const std::string& value,
                       std::uint32_t color, int boxX, int boxY, int boxWidth, int boxHeight);

// A flat, non-scrolling, non-clickable rectangle.
lv_obj_t* box(lv_obj_t* parent, int x, int y, int width, int height, std::uint32_t fill,
              std::uint32_t border = 0, int borderWidth = 0);
void setBorder(lv_obj_t* object, std::uint32_t color, int width,
               lv_border_side_t sides = LV_BORDER_SIDE_FULL);

enum class ButtonKind { Normal, Primary, Off, Danger };

// Width a rail button needs for its legend: min 124, 22 px side padding.
int buttonWidth(const std::string& value, const Type& type = type::button);
// A 1 px ruled plate button with an upper-case centred legend. width 0 sizes
// the button to its legend.
lv_obj_t* button(lv_obj_t* parent, const std::string& value, ButtonKind kind, int x, int y,
                 int width = 0, int height = kButtonHeight, const Type& type = type::button);
void styleButton(lv_obj_t* button, ButtonKind kind);
lv_obj_t* buttonLabel(lv_obj_t* button);
// Sets a button legend in the first type that fits inside maxWidth, else the
// last type with trailing dots, and centres it.
void fitButtonText(lv_obj_t* button, const std::string& value, const Type& type,
                   const Type& fallback, int maxWidth);
void setButtonText(lv_obj_t* button, const std::string& value);

// 64 px header band with the bottom rule, and the 108 px bottom rail with its
// top rule. Both are plain containers; callers place content in them.
lv_obj_t* header(lv_obj_t* parent);
lv_obj_t* rail(lv_obj_t* parent);

// Dialogs: a full-screen scrim (rgba(8, 9, 10, .72), the module drawer's)
// holding a plate with a 32 px inset, an extra-bold title, a body line and
// a right-aligned row of 60 px actions along its foot.
inline constexpr int kDialogInset = 32;
lv_obj_t* createOverlay(lv_obj_t* root);
lv_obj_t* createDialog(lv_obj_t* overlay, int width, int height, const std::string& title);
lv_obj_t* dialogBody(lv_obj_t* dialog, const std::string& value, int width);
std::vector<lv_obj_t*> dialogActions(
  lv_obj_t* dialog, int width, int height,
  const std::vector<std::pair<std::string, ButtonKind>>& actions);

// Text entry: a ground field with a rule frame that turns bone on focus.
void styleField(lv_obj_t* textarea);
// On-screen keyboard: plate keys on the ground, raised plates for the
// control keys, bone when pressed. The key face falls back to LVGL's
// symbol font so the arrow, enter and backspace glyphs still render.
void styleKeyboard(lv_obj_t* keyboard);

// Master readout for the performance rails: MASTER legend, the value and a
// 16-segment bone meter, right-aligned to the gutter. Returns the value
// label; the meter is its next sibling.
lv_obj_t* masterReadout(lv_obj_t* parent, int volume);
void syncMasterReadout(lv_obj_t* valueLabel, int volume);

// Edit header content, shared by the edit screen and its retained syncs.
// "PRESET 1  CLEAN LEAD", or "SCENE 2  CHORUS" on a scene-enabled preset.
std::string editIdentityText(const UiState& state);
// "6 BLOCKS" on the chain, "BLOCK 5 OF 6" while a block's drawer is open.
std::string editCountText(const UiState& state);
// The MODIFIED tag follows the identity legend with a 20 px gap.
void placeModifiedTag(lv_obj_t* identity, lv_obj_t* tag);

} // namespace ardor::lvgl_ui::lb
