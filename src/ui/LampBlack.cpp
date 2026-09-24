#include "ui/LampBlack.h"

#include <algorithm>
#include <cmath>

namespace ardor::lvgl_ui::lb {
namespace {

int contentPermille(const Type& type)
{
  return type.ascentPermille == kMonoAscent ? kMonoContent : kSairaContent;
}

// Distance from an LVGL label's top to its baseline.
int labelBaseline(const Type& type)
{
  return lv_font_get_line_height(type.font) - type.font->base_line;
}

int topForBaseline(const Type& type, double baseline)
{
  return static_cast<int>(std::lround(baseline)) - labelBaseline(type);
}

} // namespace

int textTop(const Type& type, int contentTop)
{
  return topForBaseline(type, contentTop + type.size * type.ascentPermille / 1000.0);
}

int centeredTextTop(const Type& type, int boxTop, int boxHeight)
{
  const double content = type.size * contentPermille(type) / 1000.0;
  const double ascent = type.size * type.ascentPermille / 1000.0;
  return topForBaseline(type, boxTop + (boxHeight - content) / 2.0 + ascent);
}

int textWidth(const Type& type, const std::string& value)
{
  if (value.empty()) return 0;
  lv_point_t size{};
  lv_text_get_size(&size, value.c_str(), type.font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  // CSS adds the letter-spacing after every character, the last one too.
  const auto characters = static_cast<double>(std::count_if(
    value.begin(), value.end(), [](char byte) { return (byte & 0xC0) != 0x80; }));
  return static_cast<int>(std::lround(size.x + characters * type.tracking()));
}

void applyType(lv_obj_t* label, const Type& type, std::uint32_t color)
{
  lv_obj_set_style_text_font(label, type.font, 0);
  lv_obj_set_style_text_letter_space(label, type.letterSpace(), 0);
  lv_obj_set_style_text_line_space(label, 0, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

lv_obj_t* textLabel(lv_obj_t* parent, const Type& type, const std::string& value,
               std::uint32_t color, int x, int contentTop)
{
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, value.c_str());
  applyType(label, type, color);
  lv_obj_set_pos(label, x, textTop(type, contentTop));
  return label;
}

lv_obj_t* centeredText(lv_obj_t* parent, const Type& type, const std::string& value,
                       std::uint32_t color, int boxX, int boxY, int boxWidth, int boxHeight)
{
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, value.c_str());
  applyType(label, type, color);
  // CSS centres the advance width, which includes the trailing letter-spacing.
  const int width = textWidth(type, value);
  lv_obj_set_pos(label, boxX + (boxWidth - width) / 2, centeredTextTop(type, boxY, boxHeight));
  return label;
}

void setBorder(lv_obj_t* object, std::uint32_t color, int width, lv_border_side_t sides)
{
  lv_obj_set_style_border_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_border_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(object, width, 0);
  lv_obj_set_style_border_side(object, sides, 0);
}

lv_obj_t* box(lv_obj_t* parent, int x, int y, int width, int height, std::uint32_t fill,
              std::uint32_t border, int borderWidth)
{
  lv_obj_t* object = lv_obj_create(parent);
  lv_obj_remove_style_all(object);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_size(object, width, height);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(object, lv_color_hex(fill), 0);
  if (borderWidth > 0) setBorder(object, border, borderWidth);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
  return object;
}

int buttonWidth(const std::string& value, const Type& type)
{
  return std::max(kButtonMinWidth, textWidth(type, uppercase(value)) + 2 * kButtonPad + 2);
}

lv_obj_t* buttonLabel(lv_obj_t* button)
{
  return lv_obj_get_child_count(button) > 0 ? lv_obj_get_child(button, 0) : nullptr;
}

void styleButton(lv_obj_t* object, ButtonKind kind)
{
  std::uint32_t fill = panel;
  std::uint32_t border = rule;
  std::uint32_t ink = text;
  switch (kind) {
  case ButtonKind::Primary: fill = text; border = text; ink = bg; break;
  case ButtonKind::Off: ink = disabled; break;
  case ButtonKind::Danger: border = dangerRule; ink = dangerText; break;
  case ButtonKind::Normal: break;
  }
  lv_obj_set_style_bg_color(object, lv_color_hex(fill), 0);
  lv_obj_set_style_border_color(object, lv_color_hex(border), 0);
  if (lv_obj_t* label = buttonLabel(object)) {
    lv_obj_set_style_text_color(label, lv_color_hex(ink), 0);
  }
}

void setButtonText(lv_obj_t* object, const std::string& value)
{
  lv_obj_t* label = buttonLabel(object);
  if (!label) return;
  const std::string legend = uppercase(value);
  lv_label_set_text(label, legend.c_str());
  const auto* font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
  const int space = lv_obj_get_style_text_letter_space(label, LV_PART_MAIN);
  lv_point_t size{};
  lv_text_get_size(&size, legend.c_str(), font, space, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  const int border = lv_obj_get_style_border_width(object, LV_PART_MAIN);
  // The style width is the value set at build; coordinates lag a layout pass.
  lv_obj_set_x(label, (lv_obj_get_style_width(object, LV_PART_MAIN) - (size.x + space)) / 2 - border);
}

lv_obj_t* button(lv_obj_t* parent, const std::string& value, ButtonKind kind, int x, int y,
                 int width, int height, const Type& type)
{
  const std::string legend = uppercase(value);
  const int w = width > 0 ? width : buttonWidth(legend, type);
  lv_obj_t* object = lv_button_create(parent);
  lv_obj_remove_style_all(object);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(object, x, y);
  lv_obj_set_size(object, w, height);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  setBorder(object, rule, 1);
  lv_obj_set_style_border_color(object, lv_color_hex(text), LV_STATE_PRESSED);
  lv_obj_set_style_opa(object, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_t* label = lv_label_create(object);
  lv_label_set_text(label, legend.c_str());
  applyType(label, type, text);
  // Children are placed inside the border; offset by it so the legend is
  // centred on the outer box, as CSS centres it.
  lv_obj_set_pos(label, (w - textWidth(type, legend)) / 2 - 1,
                 centeredTextTop(type, 0, height) - 1);
  styleButton(object, kind);
  return object;
}

void fitButtonText(lv_obj_t* object, const std::string& value, const Type& type,
                   const Type& fallback, int maxWidth)
{
  lv_obj_t* label = buttonLabel(object);
  if (!label) return;
  const Type& chosen = textWidth(type, value) <= maxWidth ? type : fallback;
  applyType(label, chosen, lv_color_to_u32(lv_obj_get_style_text_color(label, LV_PART_MAIN)) & 0xffffff);
  const int height = lv_obj_get_style_height(object, LV_PART_MAIN);
  const int border = lv_obj_get_style_border_width(object, LV_PART_MAIN);
  lv_obj_set_y(label, centeredTextTop(chosen, 0, height) - border);
  if (textWidth(chosen, value) > maxWidth) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label, maxWidth);
    lv_label_set_text(label, value.c_str());
    lv_obj_set_x(label, (lv_obj_get_style_width(object, LV_PART_MAIN) - maxWidth) / 2 - border);
    return;
  }
  lv_obj_set_width(label, LV_SIZE_CONTENT);
  setButtonText(object, value);
}

lv_obj_t* header(lv_obj_t* parent)
{
  lv_obj_t* band = box(parent, 0, 0, kDesignWidth, kHeaderHeight, bg);
  setBorder(band, rule, 1, LV_BORDER_SIDE_BOTTOM);
  return band;
}

lv_obj_t* rail(lv_obj_t* parent)
{
  lv_obj_t* band = box(parent, 0, kRailY, kDesignWidth, kRailHeight, bg);
  setBorder(band, rule, 1, LV_BORDER_SIDE_TOP);
  return band;
}

namespace {
// Dialogs: a plate on the drawer scrim with a 32 px inset.
constexpr std::uint32_t kDialogScrimColor = 0x08090a;
constexpr lv_opa_t kDialogScrimOpa = 184;
constexpr int kDialogTitleTop = 24;
constexpr int kDialogBodyTop = 84;

} // namespace

lv_obj_t* createOverlay(lv_obj_t* root)
{
  lv_obj_t* overlay = box(root, 0, 0, kDesignWidth, kDesignHeight, kDialogScrimColor);
  lv_obj_set_style_bg_opa(overlay, kDialogScrimOpa, 0);
  // The scrim swallows taps so nothing behind a dialog reacts.
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
  return overlay;
}

lv_obj_t* createDialog(lv_obj_t* overlay, int width, int height, const std::string& title)
{
  lv_obj_t* panelObject = box(overlay, (kDesignWidth - width) / 2, (kDesignHeight - height) / 2,
                                  width, height, panel, rule, 1);
  lv_obj_add_flag(panelObject, LV_OBJ_FLAG_CLICKABLE);
  textLabel(panelObject, type::drawerTitle, title, text, kDialogInset - 1,
                kDialogTitleTop - 1);
  return panelObject;
}

lv_obj_t* dialogBody(lv_obj_t* dialog, const std::string& value, int width)
{
  lv_obj_t* body = textLabel(dialog, type::subtitle, value, muted, kDialogInset - 1,
                                 kDialogBodyTop - 1);
  lv_obj_set_width(body, width - 2 * kDialogInset);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  return body;
}

// Right-aligned action row along the dialog's foot, last button rightmost.
std::vector<lv_obj_t*> dialogActions(lv_obj_t* dialog, int width, int height,
                                     const std::vector<std::pair<std::string, ButtonKind>>& actions)
{
  std::vector<lv_obj_t*> buttons;
  int x = width - 2 - kDialogInset;
  const int y = height - 2 - 28 - kButtonHeight;
  for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
    const int w = buttonWidth(it->first);
    x -= w;
    buttons.insert(buttons.begin(), button(dialog, it->first, it->second, x, y, w));
    x -= kGap;
  }
  return buttons;
}


void styleField(lv_obj_t* textarea)
{
  lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(textarea, lv_color_hex(bg), 0);
  lv_obj_set_style_text_color(textarea, lv_color_hex(text), 0);
  lv_obj_set_style_text_font(textarea, &ardor_lb_saira500_18, 0);
  lv_obj_set_style_text_color(textarea, lv_color_hex(disabled), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_border_width(textarea, 1, 0);
  lv_obj_set_style_border_color(textarea, lv_color_hex(rule), 0);
  lv_obj_set_style_border_color(textarea, lv_color_hex(text), LV_STATE_FOCUSED);
  lv_obj_set_style_radius(textarea, 0, 0);
  lv_obj_set_style_shadow_width(textarea, 0, 0);
  lv_obj_set_style_outline_width(textarea, 0, LV_STATE_FOCUSED);
  lv_obj_set_style_pad_hor(textarea, 16, 0);
  lv_obj_set_style_pad_ver(textarea, 19, 0);
  lv_obj_set_style_bg_color(textarea, lv_color_hex(text), LV_PART_CURSOR);
  lv_obj_set_style_border_color(textarea, lv_color_hex(text), LV_PART_CURSOR);
}

void styleKeyboard(lv_obj_t* keyboard)
{
  static lv_font_t keyFont = ardor_lb_cond600_20;
  keyFont.fallback = LV_FONT_DEFAULT;
  const auto items = static_cast<lv_style_selector_t>(LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(keyboard, lv_color_hex(bg), 0);
  lv_obj_set_style_border_width(keyboard, 0, 0);
  lv_obj_set_style_radius(keyboard, 0, 0);
  lv_obj_set_style_pad_all(keyboard, 0, 0);
  lv_obj_set_style_pad_gap(keyboard, 8, 0);
  lv_obj_set_style_text_font(keyboard, &keyFont, items);
  lv_obj_set_style_text_color(keyboard, lv_color_hex(text), items);
  lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, items);
  lv_obj_set_style_bg_color(keyboard, lv_color_hex(panel), items);
  lv_obj_set_style_border_color(keyboard, lv_color_hex(rule), items);
  lv_obj_set_style_border_width(keyboard, 1, items);
  lv_obj_set_style_radius(keyboard, 0, items);
  lv_obj_set_style_shadow_width(keyboard, 0, items);
  lv_obj_set_style_bg_color(keyboard, lv_color_hex(plateHi), items | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(keyboard, lv_color_hex(text), items | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(keyboard, lv_color_hex(text), items | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(keyboard, lv_color_hex(bg), items | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(keyboard, lv_color_hex(text),
                            items | LV_STATE_CHECKED | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(keyboard, lv_color_hex(bg),
                              items | LV_STATE_CHECKED | LV_STATE_PRESSED);
}

namespace {
constexpr int kMasterSegments = 16;
constexpr int kMasterSegmentWidth = 9;
constexpr int kMasterSegmentGap = 3;
constexpr int kMasterSegmentMinHeight = 14;
constexpr int kMasterSegmentStep = 2;
constexpr int kMasterMeterWidth = kMasterSegments * (kMasterSegmentWidth + kMasterSegmentGap)
  - kMasterSegmentGap;
constexpr int kMasterMeterHeight = kMasterSegmentMinHeight
  + (kMasterSegments - 1) * kMasterSegmentStep;
constexpr int kMasterMeterBottom = 689;
constexpr int kMasterValueWidth = 64;
constexpr int kMasterGap = 18;
constexpr int kMasterValueTop = 626;
constexpr int kMasterLegendTop = 653;

int masterValueRight()
{
  return kDesignWidth - kGutter - kMasterMeterWidth - kMasterGap;
}
} // namespace

lv_obj_t* masterReadout(lv_obj_t* parent, int volume)
{
  const int valueRight = masterValueRight();
  lv_obj_t* legend = textLabel(parent, type::legend, "MASTER", muted, 0, kMasterLegendTop);
  lv_obj_set_x(legend, valueRight - kMasterValueWidth - kMasterGap - textWidth(type::legend, "MASTER"));
  lv_obj_t* value = textLabel(parent, type::masterValue, "", text, 0, kMasterValueTop);
  lv_obj_t* meter = lv_obj_create(parent);
  lv_obj_remove_style_all(meter);
  lv_obj_set_size(meter, kMasterMeterWidth, kMasterMeterHeight);
  lv_obj_set_pos(meter, kDesignWidth - kGutter - kMasterMeterWidth,
                 kMasterMeterBottom - kMasterMeterHeight);
  lv_obj_remove_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(meter, LV_OBJ_FLAG_CLICKABLE);
  for (int segment = 0; segment < kMasterSegments; ++segment) {
    const int height = kMasterSegmentMinHeight + segment * kMasterSegmentStep;
    box(meter, segment * (kMasterSegmentWidth + kMasterSegmentGap), kMasterMeterHeight - height,
        kMasterSegmentWidth, height, plateHi);
  }
  syncMasterReadout(value, volume);
  return value;
}

void syncMasterReadout(lv_obj_t* valueLabel, int volume)
{
  if (!valueLabel) return;
  const int clamped = std::clamp(volume, 0, 100);
  const std::string value = std::to_string(clamped);
  lv_label_set_text(valueLabel, value.c_str());
  lv_obj_set_x(valueLabel, masterValueRight() - textWidth(type::masterValue, value));
  lv_obj_t* parent = lv_obj_get_parent(valueLabel);
  lv_obj_t* meter = lv_obj_get_child(parent, static_cast<int32_t>(lv_obj_get_index(valueLabel)) + 1);
  if (!meter) return;
  // A segment lights as soon as the volume reaches into it.
  const int lit = (clamped * kMasterSegments + 99) / 100;
  for (int segment = 0; segment < kMasterSegments; ++segment) {
    lv_obj_set_style_bg_color(lv_obj_get_child(meter, segment),
                              lv_color_hex(segment < lit ? text : plateHi), 0);
  }
}

std::string editIdentityText(const UiState& state)
{
  const auto& preset = state.bank.presets[state.activePreset];
  if (preset.sceneSet && state.editingScene < preset.sceneSet->scenes.size()) {
    return "SCENE " + std::to_string(state.editingScene + 1) + "  "
      + uppercase(preset.sceneSet->scenes[state.editingScene].name);
  }
  return "PRESET " + std::to_string(state.activePreset + 1) + "  " + uppercase(preset.name);
}

std::string editCountText(const UiState& state)
{
  const auto count = state.bank.presets[state.activePreset].blocks.size();
  if (state.paramDrawerOpen && state.paramTarget == UiParamTarget::Block && count > 0
      && state.selectedBlock < count && !selectedBlockIsLaneChild(state)) {
    return "BLOCK " + std::to_string(state.selectedBlock + 1) + " OF " + std::to_string(count);
  }
  return std::to_string(count) + (count == 1 ? " BLOCK" : " BLOCKS");
}

void placeModifiedTag(lv_obj_t* identity, lv_obj_t* tag)
{
  if (!identity || !tag) return;
  lv_obj_set_x(tag, lv_obj_get_style_x(identity, LV_PART_MAIN)
                 + textWidth(type::headerSub, lv_label_get_text(identity)) + 20);
}

} // namespace ardor::lvgl_ui::lb
