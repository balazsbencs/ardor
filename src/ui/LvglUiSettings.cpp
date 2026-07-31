#include "ui/LvglUi.h"

#include "ui/LvglUiStyle.h"
#include "ui/fonts/OpenSansRegular.h"
#include "ui/fonts/OpenSansSemibold.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace ardor {
namespace {

using namespace lvgl_ui;

constexpr std::array<std::pair<const char*, std::uint32_t>, 6> kAccentColors = {{
  {"Ardor", kDefaultAccentColor},
  {"Blue", 0x67a6ff},
  {"Amber", 0xffb347},
  {"Violet", 0xb88cff},
  {"Coral", 0xff7b6b},
  {"Ice", 0x55d9d1},
}};

void onSettingsClosed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->closeSettings(*context->state);
}

void onSettingsSectionClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->showSettingsSection(*context->state, context->index);
}

void onAccentColorClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectAccentColor(*context->state, context->index);
}

void onWifiFieldFocused(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->controlledObject) {
    lv_keyboard_set_textarea(context->controlledObject, lv_event_get_target_obj(event));
  }
}

void onWifiSaveClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->saveWifiSettings(*context->state);
}

void onWifiPasswordVisibilityClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->toggleWifiPassword();
}

} // namespace

void LvglUi::openSettings(UiState&)
{
  settingsOpen_ = true;
  settingsSection_ = 0;
  settingsMessage_.clear();
  viewsInitialized_ = false;
}

void LvglUi::closeSettings(UiState&)
{
  settingsOpen_ = false;
  settingsMessage_.clear();
  wifiPasswordVisible_ = false;
  viewsInitialized_ = false;
}

void LvglUi::showSettingsSection(UiState&, std::size_t section)
{
  settingsSection_ = std::min<std::size_t>(section, 1);
  settingsMessage_.clear();
  wifiPasswordVisible_ = false;
  viewsInitialized_ = false;
}

void LvglUi::selectAccentColor(UiState& state, std::size_t colorIndex)
{
  if (colorIndex >= kAccentColors.size()) return;
  const auto selected = kAccentColors[colorIndex].second;
  state.settings.accentColor = selected;
  accent = selected;
  std::string error;
  if (actions_.saveAccentColor && !actions_.saveAccentColor(selected, error)) {
    settingsMessage_ = "Color changed, but could not be saved: " + error;
    settingsMessageIsError_ = true;
  } else {
    settingsMessage_ = "Accent color saved";
    settingsMessageIsError_ = false;
  }
  viewsInitialized_ = false;
}

void LvglUi::saveWifiSettings(UiState& state)
{
  if (!wifiSSIDField_ || !wifiPasswordField_ || !wifiCountryField_) return;
  const std::string ssid = lv_textarea_get_text(wifiSSIDField_);
  const std::string password = lv_textarea_get_text(wifiPasswordField_);
  const std::string country = lv_textarea_get_text(wifiCountryField_);
  std::string error;
  if (!actions_.saveWifiSettings) {
    error = "Wi-Fi service is unavailable";
  } else if (actions_.saveWifiSettings(ssid, password, country, error)) {
    state.settings.wifiConfigured = true;
    state.settings.wifiSSID = ssid;
    state.settings.wifiCountry = country;
    settingsMessage_ = "Wi-Fi saved - reconnecting now";
    settingsMessageIsError_ = false;
    wifiPasswordVisible_ = false;
    viewsInitialized_ = false;
    return;
  }
  settingsMessage_ = error.empty() ? "Could not save Wi-Fi" : error;
  settingsMessageIsError_ = true;
  viewsInitialized_ = false;
}

void LvglUi::toggleWifiPassword()
{
  if (!wifiPasswordField_) return;
  wifiPasswordVisible_ = !wifiPasswordVisible_;
  lv_textarea_set_password_mode(wifiPasswordField_, !wifiPasswordVisible_);
  if (wifiPasswordToggleLabel_) {
    lv_label_set_text(
      wifiPasswordToggleLabel_, wifiPasswordVisible_ ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(
      wifiPasswordToggleLabel_, lv_color_hex(wifiPasswordVisible_ ? accent : muted), 0);
  }
}

void LvglUi::renderSettingsView(lv_obj_t* root, UiState& state)
{
  wifiSSIDField_ = nullptr;
  wifiPasswordField_ = nullptr;
  wifiCountryField_ = nullptr;
  wifiKeyboard_ = nullptr;
  wifiPasswordToggleLabel_ = nullptr;
  if (!settingsOpen_) {
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* backdrop = lv_obj_create(root);
  lv_obj_set_size(backdrop, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(backdrop, 0, 0);
  styleSurface(backdrop, bg);
  lv_obj_set_style_radius(backdrop, 0, 0);
  lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* header = lv_obj_create(root);
  lv_obj_set_size(header, kDesignWidth, 82);
  lv_obj_set_pos(header, 0, 0);
  styleSurface(header, 0x111111);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_border_width(header, 1, 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(header, lv_color_hex(0x343434), 0);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* mark = lv_obj_create(header);
  lv_obj_set_size(mark, 52, 52);
  lv_obj_align(mark, LV_ALIGN_LEFT_MID, 24, 0);
  styleSurface(mark, accent);
  lv_obj_t* markIcon = lv_label_create(mark);
  lv_label_set_text(markIcon, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_color(markIcon, lv_color_hex(bg), 0);
  lv_obj_set_style_text_font(markIcon, LV_FONT_DEFAULT, 0);
  lv_obj_center(markIcon);

  label(header, "Settings", LV_ALIGN_LEFT_MID, 92, -10,
        &ardor_font_open_sans_semibold_28);
  label(header, "Pedal preferences and connectivity", LV_ALIGN_LEFT_MID, 92, 20,
        &ardor_font_open_sans_regular_18, muted);

  lv_obj_t* close = button(header, "Close");
  lv_obj_set_size(close, 116, 54);
  lv_obj_align(close, LV_ALIGN_RIGHT_MID, -22, 0);
  lv_obj_add_event_cb(close, onSettingsClosed, LV_EVENT_PRESSED, remember(state));

  lv_obj_t* sidebar = lv_obj_create(root);
  lv_obj_set_size(sidebar, 222, 614);
  lv_obj_set_pos(sidebar, 20, 92);
  styleSurface(sidebar, 0x111111);
  lv_obj_set_style_pad_all(sidebar, 14, 0);
  lv_obj_remove_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

  const std::array<std::string, 2> sections = {"Appearance", "Wi-Fi"};
  for (std::size_t i = 0; i < sections.size(); ++i) {
    lv_obj_t* section = button(sidebar, sections[i]);
    lv_obj_set_size(section, 190, 68);
    lv_obj_set_pos(section, 0, static_cast<int>(i) * 78);
    styleSurface(section, i == settingsSection_ ? accent : 0x242424);
    lv_obj_set_style_text_color(lv_obj_get_child(section, 0),
                                lv_color_hex(i == settingsSection_ ? bg : text), 0);
    lv_obj_add_event_cb(section, onSettingsSectionClicked, LV_EVENT_PRESSED,
                        remember(state, i));
  }

  lv_obj_t* content = lv_obj_create(root);
  lv_obj_set_size(content, 1000, 614);
  lv_obj_set_pos(content, 256, 92);
  styleSurface(content, 0x181818);
  lv_obj_set_style_pad_all(content, 0, 0);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

  if (settingsSection_ == 0) {
    label(content, "Accent color", LV_ALIGN_TOP_LEFT, 28, 24,
          &ardor_font_open_sans_semibold_28);
    label(content, "Choose the color used for selections, live state and focus.",
          LV_ALIGN_TOP_LEFT, 28, 62, &ardor_font_open_sans_regular_18, muted);

    for (std::size_t i = 0; i < kAccentColors.size(); ++i) {
      const int x = 28 + static_cast<int>(i) * 158;
      const bool selected = state.settings.accentColor == kAccentColors[i].second;
      lv_obj_t* choice = lv_button_create(content);
      lv_obj_set_size(choice, 142, 146);
      lv_obj_set_pos(choice, x, 118);
      styleSurface(choice, 0x242424);
      lv_obj_set_style_border_width(choice, selected ? 3 : 1, 0);
      lv_obj_set_style_border_color(
        choice, lv_color_hex(selected ? kAccentColors[i].second : 0x404040), 0);
      lv_obj_t* swatch = lv_obj_create(choice);
      lv_obj_set_size(swatch, 92, 72);
      lv_obj_align(swatch, LV_ALIGN_TOP_MID, 0, 8);
      styleSurface(swatch, kAccentColors[i].second);
      lv_obj_remove_flag(swatch, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_t* name = label(choice, kAccentColors[i].first, LV_ALIGN_BOTTOM_MID, 0, -12,
                             &ardor_font_open_sans_regular_18,
                             selected ? kAccentColors[i].second : text);
      lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(choice, onAccentColorClicked, LV_EVENT_PRESSED, remember(state, i));
    }

    lv_obj_t* preview = lv_obj_create(content);
    lv_obj_set_size(preview, 944, 146);
    lv_obj_set_pos(preview, 28, 304);
    styleSurface(preview, accent);
    lv_obj_remove_flag(preview, LV_OBJ_FLAG_SCROLLABLE);
    const auto red = (accent >> 16) & 0xff;
    const auto green = (accent >> 8) & 0xff;
    const auto blue = accent & 0xff;
    const auto luminance = red * 299 + green * 587 + blue * 114;
    const int previewInk = luminance > 150000 ? bg : text;
    label(preview, "LIVE PREVIEW", LV_ALIGN_LEFT_MID, 28, -18,
          &ardor_font_open_sans_semibold_22, previewInk);
    label(preview, "The selected accent is applied across the touchscreen.",
          LV_ALIGN_LEFT_MID, 28, 20, &ardor_font_open_sans_regular_18, previewInk);
  } else {
    label(content, "Wi-Fi", LV_ALIGN_TOP_LEFT, 28, 20,
          &ardor_font_open_sans_semibold_28);
    label(content, state.settings.wifiConfigured
            ? "Update the network or leave the password blank to keep it."
            : "Connect the pedal without rebuilding the system image.",
          LV_ALIGN_TOP_LEFT, 28, 58, &ardor_font_open_sans_regular_18, muted);

    const auto makeField = [&](const char* title, const char* placeholder, int x, int y,
                               int width, std::size_t maxLength) {
      label(content, title, LV_ALIGN_TOP_LEFT, x, y,
            &ardor_font_open_sans_regular_18, muted);
      lv_obj_t* field = lv_textarea_create(content);
      lv_obj_set_pos(field, x, y + 28);
      lv_textarea_set_one_line(field, true);
      lv_obj_set_size(field, width, 62);
      lv_textarea_set_max_length(field, maxLength);
      lv_textarea_set_placeholder_text(field, placeholder);
      lv_obj_set_style_bg_color(field, lv_color_hex(0x0f0f0f), 0);
      lv_obj_set_style_text_color(field, lv_color_hex(text), 0);
      lv_obj_set_style_text_font(field, &ardor_font_open_sans_regular_18, 0);
      lv_obj_set_style_pad_top(field, 20, 0);
      lv_obj_set_style_pad_bottom(field, 20, 0);
      lv_obj_set_style_border_width(field, 1, 0);
      lv_obj_set_style_border_color(field, lv_color_hex(0x4b4b4b), 0);
      lv_obj_set_style_border_color(field, lv_color_hex(accent), LV_STATE_FOCUSED);
      lv_obj_set_style_radius(field, 5, 0);
      return field;
    };

    constexpr int wifiFieldLabelY = 102;
    constexpr int wifiFieldY = wifiFieldLabelY + 28;
    wifiSSIDField_ = makeField(
      "Network name", "Wi-Fi network (SSID)", 28, wifiFieldLabelY, 300, 32);
    lv_textarea_set_text(wifiSSIDField_, state.settings.wifiSSID.c_str());
    wifiPasswordField_ = makeField("Password", state.settings.wifiConfigured
      ? "Leave blank to keep current" : "8 characters minimum",
      344, wifiFieldLabelY, 300, 64);
    lv_textarea_set_password_mode(wifiPasswordField_, !wifiPasswordVisible_);
    lv_obj_set_style_pad_right(wifiPasswordField_, 64, 0);
    lv_obj_t* showPassword = button(
      content, wifiPasswordVisible_ ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    wifiPasswordToggleLabel_ = lv_obj_get_child(showPassword, 0);
    lv_obj_set_size(showPassword, 54, 58);
    lv_obj_set_pos(showPassword, 588, wifiFieldY + 2);
    styleSurface(showPassword, 0x242424);
    lv_obj_set_style_text_font(wifiPasswordToggleLabel_, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(
      wifiPasswordToggleLabel_, lv_color_hex(wifiPasswordVisible_ ? accent : muted), 0);
    lv_obj_add_event_cb(showPassword, onWifiPasswordVisibilityClicked, LV_EVENT_CLICKED,
                        remember(state));

    wifiCountryField_ = makeField(
      "Country code", "HU", 660, wifiFieldLabelY, 90, 2);
    lv_textarea_set_text(wifiCountryField_, state.settings.wifiCountry.c_str());

    lv_obj_t* save = button(content, state.settings.wifiConfigured
      ? "Save & reconnect" : "Connect pedal");
    lv_obj_set_size(save, 206, 62);
    lv_obj_set_pos(save, 766, wifiFieldY);
    styleSurface(save, accent);
    lv_obj_set_style_text_color(lv_obj_get_child(save, 0), lv_color_hex(bg), 0);
    lv_obj_set_style_text_font(
      lv_obj_get_child(save, 0), &ardor_font_open_sans_regular_18, 0);
    lv_obj_add_event_cb(save, onWifiSaveClicked, LV_EVENT_PRESSED, remember(state));

    wifiKeyboard_ = lv_keyboard_create(content);
    lv_obj_set_size(wifiKeyboard_, 944, 360);
    lv_obj_align(wifiKeyboard_, LV_ALIGN_TOP_LEFT, 28, 224);
    lv_obj_set_style_bg_color(wifiKeyboard_, lv_color_hex(0x111111), 0);
    lv_obj_set_style_text_color(wifiKeyboard_, lv_color_hex(text), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(wifiKeyboard_, lv_color_hex(0x2b2b2b), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(
      wifiKeyboard_, lv_color_hex(accent),
      static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_PRESSED);

    for (lv_obj_t* field : {wifiSSIDField_, wifiPasswordField_, wifiCountryField_}) {
      auto* context = remember(state);
      context->controlledObject = wifiKeyboard_;
      lv_obj_add_event_cb(field, onWifiFieldFocused, LV_EVENT_FOCUSED, context);
      lv_obj_add_event_cb(field, onWifiFieldFocused, LV_EVENT_CLICKED, context);
    }
    lv_keyboard_set_textarea(wifiKeyboard_, wifiSSIDField_);
  }

  if (!settingsMessage_.empty()) {
    lv_obj_t* message = label(content, settingsMessage_, LV_ALIGN_BOTTOM_RIGHT, -28, -28,
                              &ardor_font_open_sans_regular_18,
                              settingsMessageIsError_ ? danger : accent);
    lv_obj_set_width(message, settingsSection_ == 0 ? 944 : 590);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(message, LV_LABEL_LONG_CLIP);
  }
}

} // namespace ardor
