#include "ui/LvglUi.h"

#include "ui/LampBlack.h"
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
// Setup layout: a 220 px section list and a content plate between the
// 64 px header and the 108 px rail.
constexpr int kSettingsNavTop = 80;
constexpr int kSettingsNavWidth = 220;
constexpr int kSettingsContentX = lb::kGutter + kSettingsNavWidth + lb::kGap;
constexpr int kSettingsContentY = 80;
constexpr int kSettingsContentWidth = kDesignWidth - lb::kGutter - kSettingsContentX;
constexpr int kSettingsContentHeight = lb::kRailY - 12 - kSettingsContentY;
constexpr int kSettingsPad = 27;
constexpr int kWifiLabelTop = 104;
constexpr int kWifiFieldTop = 132;
constexpr int kWifiKeyboardTop = 208;
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

void onSceneLayerChordToggled(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->toggleSceneLayerChord(*context->state);
}

void onExpressionEndpointCaptured(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->captureExpressionEndpoint(*context->state, context->index == 0);
}

void onUpdateCheckClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->checkForUpdate(*context->state);
}

void onUpdateInstallClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->installUpdate(*context->state);
}

} // namespace

void LvglUi::openSettings(UiState& state)
{
  settingsOpen_ = true;
  settingsSection_ = 0;
  updateInstallArmed_ = false;
  audioBlockSizeDraft_ = state.settings.audioBlockSize;
  settingsMessage_.clear();
  if (actions_.readUpdateStatus) {
    std::string error;
    if (!actions_.readUpdateStatus(updateStatus_, error) && !error.empty()) {
      settingsMessage_ = error;
      settingsMessageIsError_ = true;
    }
  }
  viewsInitialized_ = false;
}

void LvglUi::closeSettings(UiState&)
{
  settingsOpen_ = false;
  updateInstallArmed_ = false;
  settingsMessage_.clear();
  wifiPasswordVisible_ = false;
  viewsInitialized_ = false;
}

void LvglUi::showSettingsSection(UiState&, std::size_t section)
{
  settingsSection_ = std::min<std::size_t>(section, 4);
  updateInstallArmed_ = false;
  settingsMessage_.clear();
  wifiPasswordVisible_ = false;
  viewsInitialized_ = false;
}

void LvglUi::checkForUpdate(UiState&)
{
  updateInstallArmed_ = false;
  std::string error;
  if (!actions_.checkForUpdate || !actions_.checkForUpdate(updateStatus_, error)) {
    settingsMessage_ = error.empty() ? "Could not check for updates" : error;
    settingsMessageIsError_ = true;
  } else {
    settingsMessage_ = updateStatus_.availableVersion.empty()
      ? "This pedal is up to date" : "A new Ardor release is available";
    settingsMessageIsError_ = false;
  }
  viewsInitialized_ = false;
}

void LvglUi::installUpdate(UiState&)
{
  if (updateStatus_.availableVersion.empty() || updateStatus_.reflashRequired) return;
  if (!updateInstallArmed_) {
    updateInstallArmed_ = true;
    settingsMessage_ = "Audio will mute and the pedal will restart. Tap confirm to continue.";
    settingsMessageIsError_ = false;
    viewsInitialized_ = false;
    return;
  }
  updateInstallArmed_ = false;
  std::string error;
  if (!actions_.installUpdate
      || !actions_.installUpdate(updateStatus_.availableVersion, updateStatus_, error)) {
    settingsMessage_ = error.empty() ? "Could not start the update" : error;
    settingsMessageIsError_ = true;
  } else {
    settingsMessage_ = "Update started - keep the pedal powered";
    settingsMessageIsError_ = false;
  }
  viewsInitialized_ = false;
}

void LvglUi::selectAudioBlockSize(UiState&, std::size_t optionIndex)
{
  if (optionIndex >= kAudioBlockSizes.size()) return;
  audioBlockSizeDraft_ = kAudioBlockSizes[optionIndex];
  settingsMessage_.clear();
  viewsInitialized_ = false;
}

void LvglUi::applyAudioBlockSize(UiState& state)
{
  if (audioBlockSizeDraft_ == state.settings.audioBlockSize) {
    settingsMessage_ = "This buffer size is already active";
    settingsMessageIsError_ = false;
    viewsInitialized_ = false;
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
  viewsInitialized_ = false;
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
  viewsInitialized_ = false;
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
  viewsInitialized_ = false;
}

void LvglUi::toggleSceneLayerChord(UiState& state)
{
  const bool previous = state.settings.sceneLayerChordEnabled;
  state.settings.sceneLayerChordEnabled = !previous;
  std::string error;
  if (actions_.saveControlInputSettings
      && actions_.saveControlInputSettings(state.settings, error)) {
    settingsMessage_ = state.settings.sceneLayerChordEnabled
      ? "Scene layer chord enabled" : "Scene layer chord disabled";
    settingsMessageIsError_ = false;
  } else {
    state.settings.sceneLayerChordEnabled = previous;
    settingsMessage_ = error.empty() ? "Could not save scene layer chord" : error;
    settingsMessageIsError_ = true;
  }
  viewsInitialized_ = false;
}

void LvglUi::captureExpressionEndpoint(UiState& state, bool heel)
{
  if (!state.controlInputs.expressionRawKnown) {
    settingsMessage_ = "No expression pedal reading is available";
    settingsMessageIsError_ = true;
    viewsInitialized_ = false;
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
    viewsInitialized_ = false;
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
  viewsInitialized_ = false;
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

  lb::box(root, 0, 0, kDesignWidth, kDesignHeight, bg);
  lb::header(root);
  lb::textLabel(root, lb::type::headerTitle, "SETUP", text, 28, 9);
  lb::textLabel(root, lb::type::headerSub, "PEDAL PREFERENCES AND CONNECTIVITY", muted,
                28 + lb::textWidth(lb::type::headerTitle, "SETUP") + 20, 13);

  // Section list on the left; the chosen section is the bone button.
  const std::array<std::string, 5> sections = {"Appearance", "Wi-Fi", "Audio", "Control I/O", "Updates"};
  for (std::size_t i = 0; i < sections.size(); ++i) {
    lv_obj_t* section = lb::button(root, sections[i],
      i == settingsSection_ ? lb::ButtonKind::Primary : lb::ButtonKind::Normal,
      lb::kGutter, kSettingsNavTop + static_cast<int>(i) * (lb::kButtonHeight + lb::kGap),
      kSettingsNavWidth);
    lv_obj_add_event_cb(section, onSettingsSectionClicked, LV_EVENT_PRESSED,
                        remember(state, i));
  }

  lv_obj_t* content = lb::box(root, kSettingsContentX, kSettingsContentY, kSettingsContentWidth,
                              kSettingsContentHeight, panel, rule, 1);
  lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE);
  const int inner = kSettingsContentWidth - 2 - 2 * kSettingsPad;
  const auto heading = [&](const std::string& title, const std::string& description) {
    lb::textLabel(content, lb::type::drawerTitle, uppercase(title), text, kSettingsPad, 22);
    lv_obj_t* body = lb::textLabel(content, lb::type::itemSubtitle, description, muted,
                                   kSettingsPad, 66);
    lv_obj_set_width(body, inner);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  };
  // A ground card with a ruled frame and a small-caps legend.
  const auto card = [&](int x, int y, int width, int height, const std::string& title) {
    lv_obj_t* result = lb::box(content, x, y, width, height, bg, rule, 1);
    if (!title.empty()) lb::textLabel(result, lb::type::controlLabel, uppercase(title), muted, 19, 14);
    return result;
  };
  const auto selectCard = [](lv_obj_t* object, bool selected) {
    lv_obj_set_style_border_width(object, selected ? 3 : 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(selected ? text : rule), 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(selected ? plateHi : bg), 0);
  };

  if (settingsSection_ == 0) {
    heading("Panel palette",
            "Each named palette keeps the plate, lettering, LIVE lamp and family colours in balance.");
    const int tileWidth = (inner - 3 * lb::kGap) / 4;
    for (std::size_t i = 0; i < kPalettes.size(); ++i) {
      const int x = kSettingsPad + static_cast<int>(i) * (tileWidth + lb::kGap);
      const bool selected = state.settings.paletteId == kPalettes[i].second;
      const auto& candidate = palette(kPalettes[i].second);
      lv_obj_t* choice = lv_button_create(content);
      lv_obj_remove_style_all(choice);
      lv_obj_set_pos(choice, x, 110);
      lv_obj_set_size(choice, tileWidth, 166);
      lv_obj_set_style_bg_opa(choice, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(choice, lv_color_hex(candidate.plate), 0);
      lb::setBorder(choice, selected ? text : rule, selected ? 3 : 1);
      const int border = selected ? 3 : 1;
      // Swatch: the candidate's plate, its lamp as a flooded bar, and its
      // six family colours as a chain strip.
      lb::box(choice, 16 - border, 16 - border, tileWidth - 32, 56, candidate.plate2, candidate.rule, 1);
      lb::box(choice, 16 - border, 16 - border, 56, 56, candidate.lamp);
      const int familyWidth = (tileWidth - 32 - 5 * 4) / 6;
      for (int family = 0; family < 6; ++family) {
        lb::box(choice, 16 - border + family * (familyWidth + 4), 84 - border, familyWidth, 16,
                candidate.family[family]);
      }
      lv_obj_t* name = lb::textLabel(choice, lb::type::footswitch, uppercase(kPalettes[i].first),
                                     candidate.engrave, 16 - border, 118 - border);
      lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);
      for (uint32_t child = 0; child < lv_obj_get_child_count(choice); ++child) {
        lv_obj_remove_flag(lv_obj_get_child(choice, static_cast<int32_t>(child)), LV_OBJ_FLAG_CLICKABLE);
      }
      lv_obj_add_event_cb(choice, onPaletteClicked, LV_EVENT_PRESSED, remember(state, i));
    }

    lv_obj_t* preview = card(kSettingsPad, 296, inner, 130, "");
    lv_obj_t* live = lb::box(preview, 19, 24, lb::textWidth(lb::type::liveTag, "LIVE") + 24, 37, lamp);
    lb::textLabel(live, lb::type::liveTag, "LIVE", lampInk, 12, 2);
    lv_obj_t* note = lb::textLabel(preview, lb::type::itemSubtitle,
      "Reserved for the running preset, the recording loop track and the selected parameter.",
      muted, 19, 78);
    lv_obj_set_width(note, inner - 40);
  } else if (settingsSection_ == 1) {
    heading("Wi-Fi", state.settings.wifiConfigured
            ? "Update the network or leave the password blank to keep it."
            : "Connect the pedal without rebuilding the system image.");

    const auto makeField = [&](const char* title, const char* placeholder, int x, int width,
                               std::size_t maxLength) {
      lb::textLabel(content, lb::type::controlLabel, uppercase(title), muted, x, kWifiLabelTop);
      lv_obj_t* field = lv_textarea_create(content);
      lv_obj_set_pos(field, x, kWifiFieldTop);
      lv_textarea_set_one_line(field, true);
      lv_obj_set_size(field, width, lb::kButtonHeight);
      lv_textarea_set_max_length(field, maxLength);
      lv_textarea_set_placeholder_text(field, placeholder);
      lb::styleField(field);
      return field;
    };

    wifiSSIDField_ = makeField("Network name", "Wi-Fi network (SSID)", kSettingsPad, 300, 32);
    lv_textarea_set_text(wifiSSIDField_, state.settings.wifiSSID.c_str());
    wifiPasswordField_ = makeField("Password", state.settings.wifiConfigured
      ? "Leave blank to keep current" : "8 characters minimum",
      kSettingsPad + 312, 300, 64);
    lv_textarea_set_password_mode(wifiPasswordField_, !wifiPasswordVisible_);
    lv_obj_set_style_pad_right(wifiPasswordField_, 64, 0);
    lv_obj_t* showPassword = lb::button(content, "", lb::ButtonKind::Normal,
                                        kSettingsPad + 312 + 300 - 56, kWifiFieldTop + 4, 52, 52);
    lv_obj_set_style_border_width(showPassword, 0, 0);
    lv_obj_set_style_bg_opa(showPassword, LV_OPA_TRANSP, 0);
    wifiPasswordToggleLabel_ = lb::buttonLabel(showPassword);
    lv_label_set_text(wifiPasswordToggleLabel_,
                      wifiPasswordVisible_ ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_font(wifiPasswordToggleLabel_, LV_FONT_DEFAULT, 0);
    lv_obj_center(wifiPasswordToggleLabel_);
    lv_obj_set_style_text_color(
      wifiPasswordToggleLabel_, lv_color_hex(wifiPasswordVisible_ ? text : muted), 0);
    lv_obj_add_event_cb(showPassword, onWifiPasswordVisibilityClicked, LV_EVENT_CLICKED,
                        remember(state));

    wifiCountryField_ = makeField("Country", "HU", kSettingsPad + 624, 92, 2);
    lv_textarea_set_text(wifiCountryField_, state.settings.wifiCountry.c_str());

    const std::string saveLegend = state.settings.wifiConfigured ? "Save & reconnect" : "Connect";
    const int saveWidth = inner - 736;
    lv_obj_t* save = lb::button(content, saveLegend, lb::ButtonKind::Primary,
                                kSettingsPad + 736, kWifiFieldTop, saveWidth);
    lv_obj_add_event_cb(save, onWifiSaveClicked, LV_EVENT_PRESSED, remember(state));

    wifiKeyboard_ = lv_keyboard_create(content);
    lv_obj_set_size(wifiKeyboard_, inner, kSettingsContentHeight - 2 - kWifiKeyboardTop - kSettingsPad);
    lv_obj_align(wifiKeyboard_, LV_ALIGN_TOP_LEFT, kSettingsPad, kWifiKeyboardTop);
    lb::styleKeyboard(wifiKeyboard_);

    for (lv_obj_t* field : {wifiSSIDField_, wifiPasswordField_, wifiCountryField_}) {
      auto* context = remember(state);
      context->controlledObject = wifiKeyboard_;
      lv_obj_add_event_cb(field, onWifiFieldFocused, LV_EVENT_FOCUSED, context);
      lv_obj_add_event_cb(field, onWifiFieldFocused, LV_EVENT_CLICKED, context);
    }
    lv_keyboard_set_textarea(wifiKeyboard_, wifiSSIDField_);
  } else if (settingsSection_ == 2) {
    heading("Audio", "Choose how much audio the engine processes at once.");
    const int choiceWidth = (inner - 2 * lb::kGap) / 3;
    for (std::size_t i = 0; i < kAudioBlockSizes.size(); ++i) {
      const bool selected = audioBlockSizeDraft_ == kAudioBlockSizes[i];
      lv_obj_t* choice = lv_button_create(content);
      lv_obj_remove_style_all(choice);
      lv_obj_set_pos(choice, kSettingsPad + static_cast<int>(i) * (choiceWidth + lb::kGap), 110);
      lv_obj_set_size(choice, choiceWidth, 132);
      lv_obj_set_style_bg_opa(choice, LV_OPA_COVER, 0);
      selectCard(choice, selected);
      const int border = selected ? 3 : 1;
      lv_obj_t* sizeLabel = lb::textLabel(choice, lb::type::controlValue,
                                          std::to_string(kAudioBlockSizes[i]), text,
                                          20 - border, 18 - border);
      lv_obj_t* unit = lb::textLabel(choice, lb::type::controlUnit, "samples", muted,
        20 - border + lb::textWidth(lb::type::controlValue, std::to_string(kAudioBlockSizes[i])) + 6,
        48 - border);
      lv_obj_t* timeLabel = lb::textLabel(choice, lb::type::controlLabel,
                                          uppercase(std::string(kAudioBlockTimes[i]) + " block time"),
                                          selected ? text : muted, 20 - border, 92 - border);
      for (lv_obj_t* child : {sizeLabel, unit, timeLabel}) lv_obj_remove_flag(child, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(choice, onAudioBlockSizeSelected, LV_EVENT_PRESSED,
                          remember(state, i));
    }

    lv_obj_t* explanation = card(kSettingsPad, 262, inner, 112, "Latency and stability");
    lv_obj_t* detail = lb::textLabel(explanation, lb::type::itemSubtitle,
      "Smaller blocks respond sooner but leave less DSP time. If audio crackles, choose 64 or 128 samples.",
      muted, 19, 50);
    lv_obj_set_width(detail, inner - 40);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);

    lv_obj_t* footnote = lb::textLabel(content, lb::type::itemSubtitle,
      "Block time excludes audio-interface and driver delay; it is not total round-trip latency.",
      disabled, kSettingsPad, 394);
    lv_obj_set_width(footnote, inner);

    const std::string applyLegend = "Apply & restart audio";
    const int applyWidth = lb::buttonWidth(applyLegend);
    lv_obj_t* apply = lb::button(content, applyLegend, lb::ButtonKind::Primary,
                                 kSettingsPad + inner - applyWidth,
                                 kSettingsContentHeight - 2 - kSettingsPad - lb::kButtonHeight,
                                 applyWidth);
    if (audioBlockSizeDraft_ == state.settings.audioBlockSize) {
      lv_obj_add_state(apply, LV_STATE_DISABLED);
    }
    lv_obj_add_event_cb(apply, onAudioBlockSizeApplied, LV_EVENT_PRESSED, remember(state));
  } else if (settingsSection_ == 3) {
    heading("Control I/O", "MIDI over 3.5 mm TRS Type A and expression-pedal calibration.");

    const int stepperWidth = (inner - lb::kGap) / 2;
    const auto makeStepper = [&](const std::string& title, const std::string& value, int x,
                                 lv_event_cb_t callback) {
      lv_obj_t* stepper = card(x, 110, stepperWidth, 120, title);
      const int buttonY = 120 - 2 - 16 - lb::kButtonHeight + 4;
      // Child order: title, minus, value, plus.
      lv_obj_t* minus = lb::button(stepper, "-", lb::ButtonKind::Normal, 19, buttonY - 4, 72,
                                   lb::kButtonHeight, lb::type::stepButton);
      lv_obj_add_event_cb(minus, callback, LV_EVENT_CLICKED, remember(state, 0));
      const std::string legend = uppercase(value);
      lb::centeredText(stepper, lb::type::contextValueSmall, legend, text, 19 + 72, buttonY - 4,
                       stepperWidth - 2 - 38 - 144, lb::kButtonHeight);
      lv_obj_t* plus = lb::button(stepper, "+", lb::ButtonKind::Normal,
                                  stepperWidth - 2 - 19 - 72, buttonY - 4, 72, lb::kButtonHeight,
                                  lb::type::stepButton);
      lv_obj_add_event_cb(plus, callback, LV_EVENT_CLICKED, remember(state, 1));
    };

    const std::string channel = state.settings.midiChannel < 0
      ? "Omni" : "Channel " + std::to_string(state.settings.midiChannel + 1);
    makeStepper("MIDI receive channel", channel, kSettingsPad, onMidiChannelAdjusted);
    makeStepper("Tuner on/off CC", "CC " + std::to_string(state.settings.midiTunerCc),
                kSettingsPad + stepperWidth + lb::kGap, onMidiTunerCcAdjusted);

    lv_obj_t* expression = card(kSettingsPad, 242, inner, 238, "Expression pedal");
    const std::string liveRaw = state.controlInputs.expressionRawKnown
      ? "LIVE ADC  " + std::to_string(state.controlInputs.expressionRaw)
      : "LIVE ADC  --";
    lb::textLabel(expression, lb::type::controlTag, liveRaw,
                  state.controlInputs.expressionConnected ? palette().family[3] : disabled,
                  inner - 2 - 19 - lb::textWidth(lb::type::controlTag, liveRaw), 16);
    lb::textLabel(expression, lb::type::itemSubtitle,
                  "Move to heel, capture; then move to toe and capture.", muted, 19, 46);
    const int endpointWidth = (inner - 2 - 38 - lb::kGap) / 2;
    const auto endpoint = [&](const char* name, int raw, bool heel, int x) {
      lv_obj_t* capture = lb::button(expression,
        std::string("Capture ") + name + ":  " + std::to_string(raw), lb::ButtonKind::Normal,
        x, 80, endpointWidth);
      lv_obj_add_event_cb(capture, onExpressionEndpointCaptured, LV_EVENT_CLICKED,
                          remember(state, heel ? 0 : 1));
    };
    endpoint("heel", state.settings.expressionMinimumRaw, true, 19);
    endpoint("toe", state.settings.expressionMaximumRaw, false, 19 + endpointWidth + lb::kGap);
    lv_obj_t* sceneChord = lb::button(expression,
      std::string("Scene layer chord:  ") + (state.settings.sceneLayerChordEnabled ? "On" : "Off"),
      lb::ButtonKind::Normal, 19, 152, endpointWidth);
    lv_obj_add_event_cb(sceneChord, onSceneLayerChordToggled, LV_EVENT_CLICKED,
                        remember(state));
    lv_obj_t* stored = lb::textLabel(expression, lb::type::itemSubtitle,
      "Calibration is stored globally; parameter assignment is stored per preset.", disabled,
      19 + endpointWidth + lb::kGap, 170);
    lv_obj_set_width(stored, endpointWidth);
    lv_label_set_long_mode(stored, LV_LABEL_LONG_WRAP);
  } else {
    heading("Device software", "Check for and install signed Ardor application releases.");

    const int versionWidth = (inner - lb::kGap) / 2;
    const auto versionCard = [&](const char* title, const std::string& value, int x) {
      lv_obj_t* result = card(x, 110, versionWidth, 104, title);
      lb::textLabel(result, lb::type::contextValueSmall,
                    uppercase(value.empty() ? "Unknown" : value), value.empty() ? disabled : text,
                    19, 50);
    };
    versionCard("Installed version", updateStatus_.installedVersion, kSettingsPad);
    versionCard("Base image", updateStatus_.baseVersion, kSettingsPad + versionWidth + lb::kGap);

    lv_obj_t* release = card(kSettingsPad, 226, inner, 150, "");
    const bool updateInProgress = updateStatus_.state == "downloading"
      || updateStatus_.state == "verifying" || updateStatus_.state == "staged"
      || updateStatus_.state == "restarting" || updateStatus_.state == "validating";
    std::string releaseTitle;
    std::string releaseDetail;
    std::uint32_t detailColor = muted;
    if (!updateStatus_.enabled) {
      releaseTitle = "Updates require a bootstrap image";
      releaseDetail = "Flash an OTA-capable Ardor image before installing releases here.";
    } else if (updateInProgress) {
      releaseTitle = "Update in progress";
      releaseDetail = "Keep the pedal powered. Audio and Manager may disconnect during restart.";
    } else if (!updateStatus_.availableVersion.empty()) {
      releaseTitle = "Ardor " + updateStatus_.availableVersion;
      releaseDetail = updateStatus_.reflashRequired
        ? (updateStatus_.incompatibility.empty() ? "This release requires reflashing the SD card."
                                                : updateStatus_.incompatibility)
        : "Compatible signed application update. Presets, assets and settings are preserved.";
      if (updateStatus_.reflashRequired) detailColor = dangerText;
    } else {
      releaseTitle = "No update check yet";
      releaseDetail = "Updates are manual. Nothing downloads or installs until you choose it.";
    }
    lb::textLabel(release, lb::type::itemTitle, uppercase(releaseTitle), text, 19, 18);
    lv_obj_t* releaseBody = lb::textLabel(release, lb::type::itemSubtitle, releaseDetail,
                                          detailColor, 19, 58);
    lv_obj_set_width(releaseBody, inner - 40);
    lv_label_set_long_mode(releaseBody, LV_LABEL_LONG_WRAP);

    const int actionY = kSettingsContentHeight - 2 - kSettingsPad - lb::kButtonHeight;
    lv_obj_t* check = lb::button(content, "Check for updates", lb::ButtonKind::Normal,
                                 kSettingsPad, actionY);
    lv_obj_add_event_cb(check, onUpdateCheckClicked, LV_EVENT_PRESSED, remember(state));
    if (!updateStatus_.enabled || !actions_.checkForUpdate) lv_obj_add_state(check, LV_STATE_DISABLED);

    if (!updateStatus_.availableVersion.empty() && !updateStatus_.reflashRequired
        && updateStatus_.state == "available") {
      const std::string installLegend = updateInstallArmed_ ? "Confirm install" : "Install & restart";
      const int installWidth = lb::buttonWidth("Install & restart");
      lv_obj_t* install = lb::button(content, installLegend, lb::ButtonKind::Primary,
                                     kSettingsPad + inner - installWidth, actionY, installWidth);
      lv_obj_add_event_cb(install, onUpdateInstallClicked, LV_EVENT_PRESSED, remember(state));
    }
  }

  if (!settingsMessage_.empty()) {
    lv_obj_t* message = lb::textLabel(root, lb::type::legend, uppercase(settingsMessage_),
                                      settingsMessageIsError_ ? dangerText : text, 0, 653);
    lv_obj_set_width(message, 700);
    lv_obj_set_x(message, kDesignWidth - lb::kGutter - lb::kButtonMinWidth - lb::kGap - 700);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_DOTS);
  }

  lb::rail(root);
  lv_obj_t* close = lb::button(root, "DONE", lb::ButtonKind::Primary,
                               kDesignWidth - lb::kGutter - lb::kButtonMinWidth, lb::kRailButtonY);
  lv_obj_add_event_cb(close, onSettingsClosed, LV_EVENT_PRESSED, remember(state));
}

} // namespace ardor
