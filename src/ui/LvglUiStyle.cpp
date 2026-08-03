#include "ui/LvglUiStyle.h"

namespace ardor::lvgl_ui {

std::uint32_t accent = kDefaultAccentColor;

void setText(lv_obj_t* object, int color, const lv_font_t* font)
{
  lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(object, font, 0);
}

void styleSurface(lv_obj_t* object, int color)
{
  const lv_color_t base = lv_color_hex(color);
  // A barely-there top-lit gradient — enough to round the surface, not enough
  // to read as a gradient. ~6% lift at the top, ~8% fall at the bottom.
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(object, lv_color_lighten(base, 16), 0);
  lv_obj_set_style_bg_grad_color(object, lv_color_darken(base, 20), 0);
  lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_VER, 0);
  // Hairline rim reads as a crisp machined edge.
  lv_obj_set_style_border_color(object, lv_color_white(), 0);
  lv_obj_set_style_border_opa(object, LV_OPA_10, 0);
  lv_obj_set_style_border_width(object, 1, 0);
  lv_obj_set_style_radius(object, 8, 0);
  // Soft contact shadow lifts the surface off the ground.
  lv_obj_set_style_shadow_color(object, lv_color_black(), 0);
  lv_obj_set_style_shadow_width(object, 10, 0);
  lv_obj_set_style_shadow_opa(object, LV_OPA_20, 0);
  lv_obj_set_style_shadow_offset_y(object, 3, 0);
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
  // Tactile feedback: the button presses in and its shadow collapses, and a
  // faint accent rim confirms the touch. Disabled controls dim out.
  lv_obj_set_style_translate_y(object, 2, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(object, 3, LV_STATE_PRESSED);
  lv_obj_set_style_shadow_offset_y(object, 1, LV_STATE_PRESSED);
  lv_obj_set_style_border_color(object, lv_color_hex(accent), LV_STATE_PRESSED);
  lv_obj_set_style_border_opa(object, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_set_style_opa(object, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_t* buttonLabel = lv_label_create(object);
  lv_label_set_text(buttonLabel, value.c_str());
  setText(buttonLabel, text, &ardor_font_open_sans_semibold_22);
  lv_obj_center(buttonLabel);
  return object;
}

int categoryColor(const std::string& key)
{
  if (key == "nam" || key == "amp" || key == "amps") return 0xff8a5c;
  if (key == "cab" || key == "cabinet" || key == "cabs") return 0xf5b451;
  if (key == "mod" || key == "modulation") return 0x59c2e6;
  if (key == "delay") return 0x3ce0a6;
  if (key == "reverb") return 0xc58cff;
  if (key == "dynamics" || key == "eq" || key == "utility") return 0x9aa7ff;
  return static_cast<int>(accent);
}

} // namespace ardor::lvgl_ui
