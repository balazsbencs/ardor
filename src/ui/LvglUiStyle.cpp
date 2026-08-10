#include "ui/LvglUiStyle.h"

#include <algorithm>
#include <cctype>

namespace ardor::lvgl_ui {

namespace {
constexpr PanelPalette kSlate{0x212528, 0x2a2f33, 0x191c1f,
  0xe2e4e3, 0x8d9499, 0x5b6266, 0x3b4247,
  0xd8422f, 0xc9973f, 0x6b463c, 0xbb9186, 0x7fa6c8, 0xc9a06a,
  {0xa8814e, 0x939a9e, 0x5f7f9c, 0x5d8f80, 0x8175a0, 0xa8785c}};
constexpr PanelPalette kInk{0x10161f, 0x182130, 0x0b1017,
  0xdde6ee, 0x7e8fa3, 0x4d5b6b, 0x2a3646,
  0x5fd0e8, 0xd9a04e, 0x5c3946, 0xc08e97, 0x6d8fd0, 0xc99050,
  {0xc99050, 0x8296ab, 0x6d8fd0, 0x4fa89a, 0x8d7fc4, 0xcf7a86}};
constexpr PanelPalette kSodium{0x0c0b09, 0x16140f, 0x070605,
  0xf0e4cd, 0x9a8f7a, 0x5e574a, 0x302b22,
  0xffb01f, 0xc98a3c, 0x5d3a2e, 0xc19183, 0x7f9ab5, 0xc9924f,
  {0xb06a3a, 0x9a8f7a, 0x6f8296, 0x5f9280, 0x8b7aa5, 0xb3705f}};
// Nord (arctic-ice-studio.github.io/nord): frost-blue plates with the
// signature nord8 cyan reserved for LIVE, matching this palette's own
// convention of a single warm/bright accent against cool, muted greys.
constexpr PanelPalette kNord{0x2e3440, 0x3b4252, 0x242933,
  0xd8dee9, 0x8b96a8, 0x4c566a, 0x434c5e,
  0x88c0d0, 0xebcb8b, 0x5c3a40, 0xc38f96, 0x81a1c1, 0xd08770,
  {0xd6975f, 0x8fa0b8, 0x5e81ac, 0x8fbcbb, 0xb48ead, 0xc17a72}};
const PanelPalette* currentPalette = &kSlate;
}

std::uint32_t bg = kSlate.plate;
std::uint32_t panel = kSlate.plate2;
std::uint32_t panelAlt = kSlate.plate3;
std::uint32_t text = kSlate.engrave;
std::uint32_t muted = kSlate.engraveLo;
std::uint32_t disabled = kSlate.engraveOff;
std::uint32_t rule = kSlate.rule;
std::uint32_t lamp = kSlate.lamp;
std::uint32_t warning = kSlate.warn;
std::uint32_t danger = kSlate.faultText;
std::uint32_t laneL = kSlate.laneL;
std::uint32_t laneR = kSlate.laneR;

const PanelPalette& palette() { return *currentPalette; }
const PanelPalette& palette(PaletteId id)
{
  switch (id) {
  case PaletteId::Ink: return kInk;
  case PaletteId::Sodium: return kSodium;
  case PaletteId::Nord: return kNord;
  case PaletteId::Slate: return kSlate;
  }
  return kSlate;
}

void setPalette(PaletteId id)
{
  currentPalette = &palette(id);
  const auto& colors = *currentPalette;
  bg = colors.plate; panel = colors.plate2; panelAlt = colors.plate3;
  text = colors.engrave; muted = colors.engraveLo; disabled = colors.engraveOff;
  rule = colors.rule; lamp = colors.lamp; warning = colors.warn; danger = colors.faultText;
  laneL = colors.laneL; laneR = colors.laneR;
}

void setText(lv_obj_t* object, int color, const lv_font_t* font)
{
  lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(object, font, 0);
}

void styleSurface(lv_obj_t* object, int color)
{
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_color(object, lv_color_hex(rule), 0);
  lv_obj_set_style_border_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(object, 1, 0);
  lv_obj_set_style_radius(object, 0, 0);
  lv_obj_set_style_shadow_width(object, 0, 0);
}

lv_obj_t* label(lv_obj_t* parent, const std::string& value, lv_align_t align,
                int x, int y, const lv_font_t* font, int color)
{
  lv_obj_t* object = lv_label_create(parent);
  lv_label_set_text(object, value.c_str());
  setText(object, color, font);
  lv_obj_align(object, align, x, y);
  return object;
}

lv_obj_t* button(lv_obj_t* parent, const std::string& value)
{
  lv_obj_t* object = lv_button_create(parent);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
  styleSurface(object);
  lv_obj_set_style_border_color(object, lv_color_hex(text), LV_STATE_PRESSED);
  lv_obj_set_style_border_opa(object, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_opa(object, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_t* buttonLabel = lv_label_create(object);
  lv_label_set_text(buttonLabel, value.c_str());
  setText(buttonLabel, text, &ardor_font_saira_cond_semibold_22);
  lv_obj_center(buttonLabel);
  return object;
}

int categoryColor(const std::string& key)
{
  if (key == "nam" || key == "amp" || key == "amps") return palette().family[0];
  if (key == "cab" || key == "cabinet" || key == "cabs") return palette().family[1];
  if (key == "mod" || key == "modulation") return palette().family[3];
  if (key == "delay") return palette().family[4];
  if (key == "reverb") return palette().family[5];
  if (key == "dynamics" || key == "eq" || key == "utility") return palette().family[2];
  return static_cast<int>(muted);
}

std::string uppercase(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return value;
}

} // namespace ardor::lvgl_ui
