#include "ui/LvglUi.h"

#include "ui/LampBlack.h"
#include "ui/LvglUiStyle.h"

#include <array>
#include <utility>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ardor {
namespace {

using namespace lvgl_ui;

// Tuner stage: a note plate over a cents scale over the verdict row.
constexpr int kTunerPlateWidth = 520;
constexpr int kTunerPlateHeight = 250;
constexpr int kTunerPlateX = (kDesignWidth - kTunerPlateWidth) / 2;
constexpr int kTunerPlateY = 92;
constexpr int kTunerScaleWidth = 1000;
constexpr int kTunerScaleX = (kDesignWidth - kTunerScaleWidth) / 2;
constexpr int kTunerScaleY = 370;
constexpr int kTunerScaleMargin = 24;
constexpr int kTunerVerdictWidth = 200;
constexpr int kTunerVerdictY = 470;

void onTunerExit(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().setTunerMode) {
    context->ui->actions().setTunerMode(false);
  } else {
    enterPresetMode(*context->state);
  }
  context->ui->invalidate(UiChange::None);
}

} // namespace

void LvglUi::renderTunerMode(lv_obj_t* root, UiState& state)
{
  lb::box(root, 0, 0, kDesignWidth, kDesignHeight, bg);
  lb::header(root);
  lb::textLabel(root, lb::type::headerTitle, "TUNER", text, 28, 9);
  const int tagX = 28 + lb::textWidth(lb::type::headerTitle, "TUNER") + 20;
  lv_obj_t* mutedTag = lb::box(root, tagX, 15,
                               lb::textWidth(lb::type::tag, "OUTPUT MUTED") + 20, 33, warning);
  lb::textLabel(mutedTag, lb::type::tag, "OUTPUT MUTED", warnInk, 10, 3);
  const std::string hint = "PRESS ANY FOOTSWITCH TO EXIT";
  lb::textLabel(root, lb::type::headerRight, hint, disabled,
                kDesignWidth - 28 - lb::textWidth(lb::type::headerRight, hint), 18);

  // Note plate: floods with the lamp when the string is in tune, the same
  // gesture as the LIVE preset tile.
  tunerPlate_ = lb::box(root, kTunerPlateX, kTunerPlateY, kTunerPlateWidth, kTunerPlateHeight,
                        panel, rule, 1);
  tunerNoteLabel_ = label(tunerPlate_, "--", LV_ALIGN_TOP_MID, 0, 20,
                          &ardor_font_saira_cond_semibold_tuner_110, text);
  lv_obj_set_width(tunerNoteLabel_, kTunerPlateWidth - 2);
  lv_obj_set_style_text_align(tunerNoteLabel_, LV_TEXT_ALIGN_CENTER, 0);
  tunerFrequencyLabel_ = lb::textLabel(tunerPlate_, lb::type::controlLabel, "PLAY A STRING",
                                       muted, 0, kTunerPlateHeight - 62);
  lv_obj_set_width(tunerFrequencyLabel_, kTunerPlateWidth - 2);
  lv_obj_set_style_text_align(tunerFrequencyLabel_, LV_TEXT_ALIGN_CENTER, 0);
  tunerCentsLabel_ = lb::textLabel(tunerPlate_, lb::type::contextValue, "", text, 0,
                                   kTunerPlateHeight + 20);
  lv_obj_add_flag(tunerCentsLabel_, LV_OBJ_FLAG_HIDDEN);

  // Cents scale: a travel scale from -50 to +50 with a tick every 5 cents.
  lv_obj_t* scale = lv_obj_create(root);
  lv_obj_remove_style_all(scale);
  // The box is wider than the scale so the end legends are not clipped.
  lv_obj_set_pos(scale, kTunerScaleX - kTunerScaleMargin, kTunerScaleY);
  lv_obj_set_size(scale, kTunerScaleWidth + 2 * kTunerScaleMargin, 80);
  lv_obj_remove_flag(scale, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(scale, LV_OBJ_FLAG_CLICKABLE);
  for (int tick = 0; tick <= 20; ++tick) {
    const bool major = tick % 10 == 0;
    const int x = kTunerScaleMargin + static_cast<int>(std::lround(kTunerScaleWidth * tick / 20.0));
    lb::box(scale, x - (major ? 1 : 0), major ? 0 : 6, major ? 4 : 2, major ? 20 : 8,
            tick == 10 ? text : disabled);
  }
  lb::box(scale, kTunerScaleMargin, 28, kTunerScaleWidth, 8, plateHi);
  for (const auto& [legend, fraction] : {std::pair{"-50", 0.0}, std::pair{"0", 0.5},
                                         std::pair{"+50", 1.0}}) {
    const int x = kTunerScaleMargin + static_cast<int>(std::lround(kTunerScaleWidth * fraction));
    lb::textLabel(scale, lb::type::page, legend, disabled,
                  x - lb::textWidth(lb::type::page, legend) / 2, 48);
  }
  tunerNeedle_ = lb::box(scale, 0, 14, 8, 36, text);

  // Verdict row: FLAT / IN TUNE / SHARP as segment cells; the active one
  // lifts to the raised plate with a coloured foot.
  const int rowX = (kDesignWidth - kTunerVerdictWidth * 3 + 2) / 2;
  constexpr std::array<const char*, 3> kVerdicts = {"FLAT", "IN TUNE", "SHARP"};
  for (std::size_t i = 0; i < kVerdicts.size(); ++i) {
    lv_obj_t* cell = lb::button(root, kVerdicts[i], lb::ButtonKind::Off,
                                rowX + static_cast<int>(i) * (kTunerVerdictWidth - 1), kTunerVerdictY,
                                kTunerVerdictWidth, 56, lb::type::segment);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lb::box(cell, 0, 56 - 2 - 5, kTunerVerdictWidth - 2, 5, disabled);
    tunerVerdictLamps_[i] = cell;
  }
  tunerGuidanceLabel_ = lb::textLabel(root, lb::type::controlLabel, "PLAY A STRING", disabled,
                                      0, kTunerVerdictY + 72);

  lb::rail(root);
  lv_obj_t* exit = lb::button(root, "EXIT", lb::ButtonKind::Primary, lb::kGutter, lb::kRailButtonY);
  lv_obj_add_event_cb(exit, onTunerExit, LV_EVENT_PRESSED, remember(state));
  syncTunerView(state);
}

void LvglUi::syncTunerView(UiState& state)
{
  if (!tunerNoteLabel_ || !tunerNeedle_) return;
  const auto& tuner = state.tuner;
  const auto centreGuidance = [this](const char* guidance, std::uint32_t color) {
    lv_label_set_text(tunerGuidanceLabel_, guidance);
    lv_obj_set_style_text_color(tunerGuidanceLabel_, lv_color_hex(color), 0);
    lv_obj_set_x(tunerGuidanceLabel_,
                 (kDesignWidth - lb::textWidth(lb::type::controlLabel, guidance)) / 2);
  };
  const auto styleVerdicts = [this](int active, std::uint32_t color) {
    for (std::size_t i = 0; i < tunerVerdictLamps_.size(); ++i) {
      lv_obj_t* cell = tunerVerdictLamps_[i];
      if (!cell) continue;
      const bool on = static_cast<int>(i) == active;
      lv_obj_set_style_bg_color(cell, lv_color_hex(on ? plateHi : panel), 0);
      lv_obj_set_style_text_color(lb::buttonLabel(cell), lv_color_hex(on ? text : disabled), 0);
      lv_obj_set_style_bg_color(lv_obj_get_child(cell, 1), lv_color_hex(on ? color : rule), 0);
    }
  };
  if (!tuner.signalDetected) {
    lv_label_set_text(tunerNoteLabel_, "--");
    lv_label_set_text(tunerFrequencyLabel_, "PLAY A STRING");
    lv_label_set_text(tunerCentsLabel_, "");
    centreGuidance("PLAY A STRING", disabled);
    styleVerdicts(-1, rule);
    lv_obj_set_style_bg_color(tunerPlate_, lv_color_hex(panel), 0);
    lv_obj_set_style_border_color(tunerPlate_, lv_color_hex(rule), 0);
    lv_obj_set_style_text_color(tunerNoteLabel_, lv_color_hex(disabled), 0);
    lv_obj_set_style_text_color(tunerFrequencyLabel_, lv_color_hex(muted), 0);
    lv_obj_add_flag(tunerNeedle_, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  char note[16]{};
  char detail[64]{};
  std::snprintf(note, sizeof(note), "%s%d", tuner.note.c_str(), tuner.octave);
  std::snprintf(detail, sizeof(detail), "%.1f HZ  \xC2\xB7  %+.1f CENTS", tuner.frequencyHz,
                tuner.cents);
  lv_label_set_text(tunerNoteLabel_, note);
  lv_label_set_text(tunerFrequencyLabel_, detail);
  lv_label_set_text(tunerCentsLabel_, detail);

  const float absoluteCents = std::fabs(tuner.cents);
  const bool inTune = absoluteCents <= 3.0f;
  const std::uint32_t color = inTune ? lamp : (absoluteCents <= 10.0f ? warning : dangerText);
  centreGuidance(inTune ? "IN TUNE" : (tuner.cents < 0.0f ? "FLAT  -  TUNE UP" : "SHARP  -  TUNE DOWN"),
                 inTune ? text : color);
  styleVerdicts(inTune ? 1 : (tuner.cents < 0.0f ? 0 : 2), color);
  lv_obj_set_style_bg_color(tunerPlate_, lv_color_hex(inTune ? lamp : panel), 0);
  lv_obj_set_style_border_color(tunerPlate_, lv_color_hex(inTune ? lamp : rule), 0);
  lv_obj_set_style_text_color(tunerNoteLabel_, lv_color_hex(inTune ? lampInk : text), 0);
  lv_obj_set_style_text_color(tunerFrequencyLabel_, lv_color_hex(inTune ? lampInk : muted), 0);
  lv_obj_set_style_bg_color(tunerNeedle_, lv_color_hex(inTune ? text : color), 0);
  const int needleX = kTunerScaleMargin + static_cast<int>(std::lround(
    (std::clamp(tuner.cents, -50.0f, 50.0f) + 50.0f) / 100.0f * kTunerScaleWidth)) - 4;
  lv_obj_set_x(tunerNeedle_, needleX);
  lv_obj_remove_flag(tunerNeedle_, LV_OBJ_FLAG_HIDDEN);
}

} // namespace ardor
