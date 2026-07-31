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
  lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_radius(object, 5, 0);
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
  lv_obj_t* buttonLabel = lv_label_create(object);
  lv_label_set_text(buttonLabel, value.c_str());
  setText(buttonLabel, text, &ardor_font_open_sans_semibold_22);
  lv_obj_center(buttonLabel);
  return object;
}

} // namespace ardor::lvgl_ui
