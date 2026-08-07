#include "ui/LvglUi.h"

#include "ui/LvglUiStyle.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace ardor {
namespace {

using namespace lvgl_ui;

constexpr std::array<std::pair<const char*, PaletteId>, 4> kPalettes = {{
  {"Slate", PaletteId::Slate},
  {"Ink", PaletteId::Ink},
  {"Sodium", PaletteId::Sodium},
  {"Nord", PaletteId::Nord},
}};
// Four tiles across the same 944 px band three used to share unevenly (290
// wide with room to spare) -- shrunk just enough to fit the new palette
// without spilling past the content column's right edge.
constexpr int kPaletteTileWidth = 224;
constexpr int kPaletteTileStep = 240;
constexpr std::array<std::uint32_t, 3> kAudioBlockSizes = {32, 64, 128};
constexpr std::array<const char*, 3> kAudioBlockTimes = {"0.67 ms", "1.33 ms", "2.67 ms"};

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

void onPaletteClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectPalette(*context->state, context->index);
}

void onAudioBlockSizeSelected(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectAudioBlockSize(*context->state, context->index);
}

void onAudioBlockSizeApplied(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->applyAudioBlockSize(*context->state);
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

void onMidiChannelAdjusted(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->adjustMidiChannel(*context->state, context->index == 0 ? -1 : 1);
}

void onMidiTunerCcAdjusted(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->adjustMidiTunerCc(*context->state, context->index == 0 ? -1 : 1);
}

void onExpressionEndpointCaptured(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->captureExpressionEndpoint(*context->state, context->index == 0);
}

} // namespace

void LvglUi::rebuildSettingsView(UiState& state)
{
  if (!settingsLayer_) return;
  lv_obj_clean(settingsLayer_);
  contexts_.remove_if([](const UiEventContext& context) {
    return context.region == UiContextRegion::Settings;
  });
  contextRegion_ = UiContextRegion::Settings;
  renderSettingsView(settingsLayer_, state);
  contextRegion_ = UiContextRegion::None;
}

void LvglUi::openSettings(UiState& state)
{
  settingsOpen_ = true;
  settingsSection_ = 0;
  audioBlockSizeDraft_ = state.settings.audioBlockSize;
  settingsMessage_.clear();
  settingsViewDirty_ = true;
}

void LvglUi::closeSettings(UiState&)
{
  settingsOpen_ = false;
  settingsMessage_.clear();
  wifiPasswordVisible_ = false;
  settingsViewDirty_ = true;
}

void LvglUi::showSettingsSection(UiState&, std::size_t section)
{
  settingsSection_ = std::min<std::size_t>(section, 3);
  settingsMessage_.clear();
  wifiPasswordVisible_ = false;
  settingsViewDirty_ = true;
}

void LvglUi::selectAudioBlockSize(UiState&, std::size_t optionIndex)
{
  if (optionIndex >= kAudioBlockSizes.size()) return;
  audioBlockSizeDraft_ = kAudioBlockSizes[optionIndex];
  settingsMessage_.clear();
  settingsViewDirty_ = true;
}

void LvglUi::applyAudioBlockSize(UiState& state)
{
  if (audioBlockSizeDraft_ == state.settings.audioBlockSize) {
    settingsMessage_ = "This buffer size is already active";
    settingsMessageIsError_ = false;
    settingsViewDirty_ = true;
    return;
  }
  std::string error;
  if (actions_.saveAudioBlockSize
      && actions_.saveAudioBlockSize(audioBlockSizeDraft_, error)) {
    state.settings.audioBlockSize = audioBlockSizeDraft_;
    settingsMessage_ = "Buffer saved - restarting audio";
    settingsMessageIsError_ = false;
  } else {
    settingsMessage_ = error.empty() ? "Could not save audio buffer" : error;
    settingsMessageIsError_ = true;
  }
  settingsViewDirty_ = true;
}

void LvglUi::adjustMidiChannel(UiState& state, int delta)
{
  state.settings.midiChannel = std::clamp(state.settings.midiChannel + delta, -1, 15);
  std::string error;
  if (actions_.saveControlInputSettings
      && actions_.saveControlInputSettings(state.settings, error)) {
    settingsMessage_ = "MIDI channel saved and applied";
    settingsMessageIsError_ = false;
  } else {
    settingsMessage_ = error.empty() ? "Could not save MIDI channel" : error;
    settingsMessageIsError_ = true;
  }
  settingsViewDirty_ = true;
}

void LvglUi::adjustMidiTunerCc(UiState& state, int delta)
{
  state.settings.midiTunerCc = std::clamp(state.settings.midiTunerCc + delta, 0, 127);
  std::string error;
  if (actions_.saveControlInputSettings
      && actions_.saveControlInputSettings(state.settings, error)) {
    settingsMessage_ = "Tuner CC saved and applied";
    settingsMessageIsError_ = false;
  } else {
    settingsMessage_ = error.empty() ? "Could not save tuner CC" : error;
    settingsMessageIsError_ = true;
  }
  settingsViewDirty_ = true;
}

void LvglUi::captureExpressionEndpoint(UiState& state, bool heel)
{
  if (!state.controlInputs.expressionRawKnown) {
    settingsMessage_ = "No expression pedal reading is available";
    settingsMessageIsError_ = true;
    settingsViewDirty_ = true;
    return;
  }
  const int previous = heel ? state.settings.expressionMinimumRaw
                            : state.settings.expressionMaximumRaw;
  if (heel) state.settings.expressionMinimumRaw = state.controlInputs.expressionRaw;
  else state.settings.expressionMaximumRaw = state.controlInputs.expressionRaw;
  if (state.settings.expressionMaximumRaw <= state.settings.expressionMinimumRaw) {
    if (heel) state.settings.expressionMinimumRaw = previous;
    else state.settings.expressionMaximumRaw = previous;
    settingsMessage_ = "Toe must read higher than heel";
    settingsMessageIsError_ = true;
    settingsViewDirty_ = true;
    return;
  }
  std::string error;
  if (actions_.saveControlInputSettings
      && actions_.saveControlInputSettings(state.settings, error)) {
    settingsMessage_ = std::string(heel ? "Heel" : "Toe") + " position captured";
    settingsMessageIsError_ = false;
  } else {
    settingsMessage_ = error.empty() ? "Could not save expression calibration" : error;
    settingsMessageIsError_ = true;
  }
  settingsViewDirty_ = true;
}

void LvglUi::selectPalette(UiState& state, std::size_t paletteIndex)
{
  if (paletteIndex >= kPalettes.size()) return;
  const auto selected = kPalettes[paletteIndex].second;
  state.settings.paletteId = selected;
  setPalette(selected);
  std::string error;
  if (actions_.savePalette && !actions_.savePalette(selected, error)) {
    settingsMessage_ = "Palette changed, but could not be saved: " + error;
    settingsMessageIsError_ = true;
  } else {
    settingsMessage_ = "Palette saved";
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
    settingsViewDirty_ = true;
    return;
  }
  settingsMessage_ = error.empty() ? "Could not save Wi-Fi" : error;
  settingsMessageIsError_ = true;
  settingsViewDirty_ = true;
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
      wifiPasswordToggleLabel_, lv_color_hex(wifiPasswordVisible_ ? text : muted), 0);
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
  styleSurface(header, panelAlt);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_border_width(header, 1, 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(header, lv_color_hex(rule), 0);
  lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* mark = lv_obj_create(header);
  lv_obj_set_size(mark, 52, 52);
  lv_obj_align(mark, LV_ALIGN_LEFT_MID, 24, 0);
  styleSurface(mark, panel);
  lv_obj_t* markIcon = lv_label_create(mark);
  lv_label_set_text(markIcon, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_color(markIcon, lv_color_hex(bg), 0);
  lv_obj_set_style_text_font(markIcon, LV_FONT_DEFAULT, 0);
  lv_obj_center(markIcon);

  label(header, "Settings", LV_ALIGN_LEFT_MID, 92, -10,
        &ardor_font_saira_cond_semibold_28);
  label(header, "Pedal preferences and connectivity", LV_ALIGN_LEFT_MID, 92, 20,
        &ardor_font_saira_cond_medium_18, muted);

  lv_obj_t* close = button(header, "Close");
  lv_obj_set_size(close, 116, 54);
  lv_obj_align(close, LV_ALIGN_RIGHT_MID, -22, 0);
  lv_obj_add_event_cb(close, onSettingsClosed, LV_EVENT_PRESSED, remember(state));

  lv_obj_t* sidebar = lv_obj_create(root);
  lv_obj_set_size(sidebar, 222, 614);
  lv_obj_set_pos(sidebar, 20, 92);
  styleSurface(sidebar, panelAlt);
  lv_obj_set_style_pad_all(sidebar, 14, 0);
  lv_obj_remove_flag(sidebar, LV_OBJ_FLAG_SCROLLABLE);

  const std::array<std::string, 4> sections = {"Appearance", "Wi-Fi", "Audio", "Control I/O"};
  for (std::size_t i = 0; i < sections.size(); ++i) {
    lv_obj_t* section = button(sidebar, sections[i]);
    lv_obj_set_size(section, 190, 68);
    lv_obj_set_pos(section, 0, static_cast<int>(i) * 78);
    styleSurface(section, i == settingsSection_ ? panel : panelAlt);
    lv_obj_set_style_text_color(lv_obj_get_child(section, 0),
                                lv_color_hex(i == settingsSection_ ? bg : text), 0);
    lv_obj_add_event_cb(section, onSettingsSectionClicked, LV_EVENT_PRESSED,
                        remember(state, i));
  }

  lv_obj_t* content = lv_obj_create(root);
  lv_obj_set_size(content, 1000, 614);
  lv_obj_set_pos(content, 256, 92);
  styleSurface(content, panel);
  lv_obj_set_style_pad_all(content, 0, 0);
  lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);

  if (settingsSection_ == 0) {
    label(content, "Panel palette", LV_ALIGN_TOP_LEFT, 28, 24,
          &ardor_font_saira_cond_semibold_28);
    label(content, "Each named palette keeps the plate, lettering, LIVE lamp and family colours in balance.",
          LV_ALIGN_TOP_LEFT, 28, 62, &ardor_font_saira_cond_medium_18, muted);

    for (std::size_t i = 0; i < kPalettes.size(); ++i) {
      const int x = 28 + static_cast<int>(i) * kPaletteTileStep;
      const bool selected = state.settings.paletteId == kPalettes[i].second;
      const auto& candidate = palette(kPalettes[i].second);
      lv_obj_t* choice = lv_button_create(content);
      lv_obj_set_size(choice, kPaletteTileWidth, 146);
      lv_obj_set_pos(choice, x, 118);
      styleSurface(choice, candidate.plate2);
      lv_obj_set_style_border_width(choice, selected ? 2 : 1, 0);
      lv_obj_set_style_border_color(
        choice, lv_color_hex(selected ? candidate.engrave : candidate.rule), 0);
      lv_obj_t* swatch = lv_obj_create(choice);
      lv_obj_set_size(swatch, kPaletteTileWidth - 52, 52);
      lv_obj_align(swatch, LV_ALIGN_TOP_MID, 0, 8);
      styleSurface(swatch, candidate.plate);
      lv_obj_set_style_border_color(swatch, lv_color_hex(candidate.lamp), 0);
      lv_obj_remove_flag(swatch, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_t* name = label(choice, kPalettes[i].first, LV_ALIGN_BOTTOM_MID, 0, -12,
                             &ardor_font_saira_cond_medium_18,
                             candidate.engrave);
      lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(choice, onPaletteClicked, LV_EVENT_PRESSED, remember(state, i));
    }

    lv_obj_t* preview = lv_obj_create(content);
    lv_obj_set_size(preview, 944, 146);
    lv_obj_set_pos(preview, 28, 304);
    styleSurface(preview, panelAlt);
    lv_obj_remove_flag(preview, LV_OBJ_FLAG_SCROLLABLE);
    label(preview, "LIVE", LV_ALIGN_LEFT_MID, 28, -18,
          &ardor_font_saira_cond_semibold_22, lamp);
    label(preview, "Reserved for the running preset and selected parameter.",
          LV_ALIGN_LEFT_MID, 28, 20, &ardor_font_saira_cond_medium_18, muted);
  } else if (settingsSection_ == 1) {
    label(content, "Wi-Fi", LV_ALIGN_TOP_LEFT, 28, 20,
          &ardor_font_saira_cond_semibold_28);
    label(content, state.settings.wifiConfigured
            ? "Update the network or leave the password blank to keep it."
            : "Connect the pedal without rebuilding the system image.",
          LV_ALIGN_TOP_LEFT, 28, 58, &ardor_font_saira_cond_medium_18, muted);

    const auto makeField = [&](const char* title, const char* placeholder, int x, int y,
                               int width, std::size_t maxLength) {
      label(content, title, LV_ALIGN_TOP_LEFT, x, y,
            &ardor_font_saira_cond_medium_18, muted);
      lv_obj_t* field = lv_textarea_create(content);
      lv_obj_set_pos(field, x, y + 28);
      lv_textarea_set_one_line(field, true);
      lv_obj_set_size(field, width, 62);
      lv_textarea_set_max_length(field, maxLength);
      lv_textarea_set_placeholder_text(field, placeholder);
      lv_obj_set_style_bg_color(field, lv_color_hex(panelAlt), 0);
      lv_obj_set_style_text_color(field, lv_color_hex(text), 0);
      lv_obj_set_style_text_font(field, &ardor_font_saira_cond_medium_18, 0);
      lv_obj_set_style_pad_top(field, 20, 0);
      lv_obj_set_style_pad_bottom(field, 20, 0);
      lv_obj_set_style_border_width(field, 1, 0);
      lv_obj_set_style_border_color(field, lv_color_hex(rule), 0);
      lv_obj_set_style_border_color(field, lv_color_hex(text), LV_STATE_FOCUSED);
      lv_obj_set_style_radius(field, 0, 0);
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
    styleSurface(showPassword, panel);
    lv_obj_set_style_text_font(wifiPasswordToggleLabel_, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_color(
      wifiPasswordToggleLabel_, lv_color_hex(wifiPasswordVisible_ ? text : muted), 0);
    lv_obj_add_event_cb(showPassword, onWifiPasswordVisibilityClicked, LV_EVENT_CLICKED,
                        remember(state));

    wifiCountryField_ = makeField(
      "Country code", "HU", 660, wifiFieldLabelY, 90, 2);
    lv_textarea_set_text(wifiCountryField_, state.settings.wifiCountry.c_str());

    lv_obj_t* save = button(content, state.settings.wifiConfigured
      ? "Save & reconnect" : "Connect pedal");
    lv_obj_set_size(save, 206, 62);
    lv_obj_set_pos(save, 766, wifiFieldY);
    styleSurface(save, text);
    lv_obj_set_style_text_color(lv_obj_get_child(save, 0), lv_color_hex(bg), 0);
    lv_obj_set_style_text_font(
      lv_obj_get_child(save, 0), &ardor_font_saira_cond_medium_18, 0);
    lv_obj_add_event_cb(save, onWifiSaveClicked, LV_EVENT_PRESSED, remember(state));

    wifiKeyboard_ = lv_keyboard_create(content);
    lv_obj_set_size(wifiKeyboard_, 944, 360);
    lv_obj_align(wifiKeyboard_, LV_ALIGN_TOP_LEFT, 28, 224);
    lv_obj_set_style_bg_color(wifiKeyboard_, lv_color_hex(panelAlt), 0);
    lv_obj_set_style_text_color(wifiKeyboard_, lv_color_hex(text), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(wifiKeyboard_, lv_color_hex(panel), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(
      wifiKeyboard_, lv_color_hex(text),
      static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_PRESSED);

    for (lv_obj_t* field : {wifiSSIDField_, wifiPasswordField_, wifiCountryField_}) {
      auto* context = remember(state);
      context->controlledObject = wifiKeyboard_;
      lv_obj_add_event_cb(field, onWifiFieldFocused, LV_EVENT_FOCUSED, context);
      lv_obj_add_event_cb(field, onWifiFieldFocused, LV_EVENT_CLICKED, context);
    }
    lv_keyboard_set_textarea(wifiKeyboard_, wifiSSIDField_);
  } else if (settingsSection_ == 2) {
    label(content, "Audio", LV_ALIGN_TOP_LEFT, 28, 22,
          &ardor_font_saira_cond_semibold_28);
    label(content, "Choose how much audio the engine processes at once.",
          LV_ALIGN_TOP_LEFT, 28, 60, &ardor_font_saira_cond_medium_18, muted);

    for (std::size_t i = 0; i < kAudioBlockSizes.size(); ++i) {
      const bool selected = audioBlockSizeDraft_ == kAudioBlockSizes[i];
      lv_obj_t* choice = lv_button_create(content);
      lv_obj_set_size(choice, 288, 132);
      lv_obj_set_pos(choice, 28 + static_cast<int>(i) * 306, 112);
      styleSurface(choice, selected ? text : panelAlt);
      lv_obj_set_style_border_width(choice, 1, 0);
      lv_obj_set_style_border_color(choice, lv_color_hex(selected ? text : rule), 0);
      lv_obj_set_style_pad_all(choice, 0, 0);
      lv_obj_t* sizeLabel = label(
        choice, std::to_string(kAudioBlockSizes[i]) + " samples",
        LV_ALIGN_TOP_LEFT, 18, 16, &ardor_font_saira_cond_semibold_22,
        selected ? bg : text);
      lv_obj_remove_flag(sizeLabel, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_t* timeLabel = label(
        choice, std::string(kAudioBlockTimes[i]) + " block time",
        LV_ALIGN_BOTTOM_LEFT, 18, -18, &ardor_font_saira_cond_medium_18,
        selected ? bg : muted);
      lv_obj_remove_flag(timeLabel, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(choice, onAudioBlockSizeSelected, LV_EVENT_PRESSED,
                          remember(state, i));
    }

    lv_obj_t* explanation = lv_obj_create(content);
    lv_obj_set_size(explanation, 900, 112);
    lv_obj_set_pos(explanation, 28, 274);
    styleSurface(explanation, panelAlt);
    lv_obj_set_style_pad_all(explanation, 0, 0);
    lv_obj_remove_flag(explanation, LV_OBJ_FLAG_SCROLLABLE);
    label(explanation, "Latency and stability", LV_ALIGN_TOP_LEFT, 20, 16,
          &ardor_font_saira_cond_semibold_22);
    label(explanation,
          "Smaller blocks respond sooner but leave less DSP time. If audio crackles, choose 64 or 128 samples.",
          LV_ALIGN_TOP_LEFT, 20, 54, &ardor_font_saira_cond_medium_18, muted);

    label(content,
          "Block time excludes audio-interface and driver delay; it is not total round-trip latency.",
          LV_ALIGN_TOP_LEFT, 28, 410, &ardor_font_saira_cond_medium_18, muted);

    lv_obj_t* apply = button(content, "Apply & restart audio");
    lv_obj_set_size(apply, 252, 62);
    lv_obj_set_pos(apply, 676, 470);
    styleSurface(apply, text);
    lv_obj_set_style_text_color(lv_obj_get_child(apply, 0), lv_color_hex(bg), 0);
    lv_obj_set_style_text_font(
      lv_obj_get_child(apply, 0), &ardor_font_saira_cond_medium_18, 0);
    if (audioBlockSizeDraft_ == state.settings.audioBlockSize) {
      lv_obj_add_state(apply, LV_STATE_DISABLED);
    }
    lv_obj_add_event_cb(apply, onAudioBlockSizeApplied, LV_EVENT_PRESSED, remember(state));
  } else {
    label(content, "Control I/O", LV_ALIGN_TOP_LEFT, 28, 22,
          &ardor_font_saira_cond_semibold_28);
    label(content, "MIDI over 3.5 mm TRS Type A and expression-pedal calibration.",
          LV_ALIGN_TOP_LEFT, 28, 60, &ardor_font_saira_cond_medium_18, muted);

    const auto makeStepper = [&](const std::string& title, const std::string& value,
                                 int x, int y, lv_event_cb_t callback) {
      lv_obj_t* card = lv_obj_create(content);
      lv_obj_set_size(card, 450, 112);
      lv_obj_set_pos(card, x, y);
      styleSurface(card, panel);
      lv_obj_set_style_pad_all(card, 0, 0);
      lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t* titleLabel = lv_label_create(card);
      lv_label_set_text(titleLabel, title.c_str());
      setText(titleLabel, muted, &ardor_font_saira_cond_medium_18);
      lv_obj_set_pos(titleLabel, 18, 10);
      lv_obj_set_size(titleLabel, 414, 28);
      lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_CLIP);
      lv_obj_t* minus = button(card, "-");
      lv_obj_set_size(minus, 58, 46);
      lv_obj_set_pos(minus, 10, 54);
      lv_obj_set_style_pad_all(minus, 0, 0);
      lv_obj_add_event_cb(minus, callback, LV_EVENT_CLICKED, remember(state, 0));
      lv_obj_t* valueLabel = lv_label_create(card);
      lv_label_set_text(valueLabel, value.c_str());
      setText(valueLabel, text, &ardor_font_saira_cond_semibold_22);
      lv_obj_set_pos(valueLabel, 82, 62);
      lv_obj_set_size(valueLabel, 286, 30);
      lv_obj_set_style_text_align(valueLabel, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_t* plus = button(card, "+");
      lv_obj_set_size(plus, 58, 46);
      lv_obj_set_pos(plus, 382, 54);
      lv_obj_set_style_pad_all(plus, 0, 0);
      lv_obj_add_event_cb(plus, callback, LV_EVENT_CLICKED, remember(state, 1));
    };

    const std::string channel = state.settings.midiChannel < 0
      ? "Omni" : "Channel " + std::to_string(state.settings.midiChannel + 1);
    makeStepper("MIDI receive channel", channel, 28, 104, onMidiChannelAdjusted);
    makeStepper("Tuner on/off CC", "CC " + std::to_string(state.settings.midiTunerCc),
                494, 104, onMidiTunerCcAdjusted);

    lv_obj_t* expression = lv_obj_create(content);
    lv_obj_set_size(expression, 916, 254);
    lv_obj_set_pos(expression, 28, 234);
    styleSurface(expression, panel);
    lv_obj_set_style_pad_all(expression, 0, 0);
    lv_obj_remove_flag(expression, LV_OBJ_FLAG_SCROLLABLE);
    label(expression, "Expression pedal", LV_ALIGN_TOP_LEFT, 20, 16,
          &ardor_font_saira_cond_semibold_22);
    const std::string liveRaw = state.controlInputs.expressionRawKnown
      ? "LIVE ADC  " + std::to_string(state.controlInputs.expressionRaw)
      : "LIVE ADC  --";
    label(expression, liveRaw, LV_ALIGN_TOP_RIGHT, -20, 18,
          &ardor_font_saira_cond_medium_18,
          state.controlInputs.expressionConnected ? palette().family[3] : muted);
    label(expression, "Move to heel, capture; then move to toe and capture.",
          LV_ALIGN_TOP_LEFT, 20, 54, &ardor_font_saira_cond_medium_18, muted);

    const auto endpoint = [&](const char* name, int raw, bool heel, int x) {
      lv_obj_t* capture = button(expression,
        std::string("Capture ") + name + ":  " + std::to_string(raw));
      lv_obj_set_size(capture, 420, 72);
      lv_obj_set_pos(capture, x, 104);
      styleSurface(capture, panel);
      lv_obj_add_event_cb(capture, onExpressionEndpointCaptured, LV_EVENT_CLICKED,
                          remember(state, heel ? 0 : 1));
    };
    endpoint("heel", state.settings.expressionMinimumRaw, true, 20);
    endpoint("toe", state.settings.expressionMaximumRaw, false, 456);
    label(expression, "Calibration is stored globally; parameter assignment is stored per preset.",
          LV_ALIGN_BOTTOM_LEFT, 20, -18, &ardor_font_saira_cond_medium_18, muted);
  }

  if (!settingsMessage_.empty()) {
    lv_obj_t* message = label(content, settingsMessage_, LV_ALIGN_BOTTOM_RIGHT, -28, -28,
                              &ardor_font_saira_cond_medium_18,
                              settingsMessageIsError_ ? danger : text);
    lv_obj_set_width(message, settingsSection_ == 0 ? 944 : 590);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(message, LV_LABEL_LONG_CLIP);
  }
}

} // namespace ardor
