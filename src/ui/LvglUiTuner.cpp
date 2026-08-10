#include "ui/LvglUi.h"

#include "ui/LvglUiStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ardor {
namespace {

using namespace lvgl_ui;

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
  lv_obj_t* exit = button(root, "Exit");
  lv_obj_set_size(exit, 120, kHeaderButtonHeight);
  lv_obj_align(exit, LV_ALIGN_TOP_LEFT, 28, 20);
  styleSurface(exit, panel);
  lv_obj_add_event_cb(exit, onTunerExit, LV_EVENT_PRESSED, remember(state));

  label(root, "TUNER", LV_ALIGN_TOP_MID, 0, 24,
        &ardor_font_saira_cond_semibold_28, lamp);
  label(root, "OUTPUT MUTED", LV_ALIGN_TOP_RIGHT, -34, 30,
        &ardor_font_saira_cond_semibold_22, danger);

  // Real tuner-designator face (subsetted A-G/#/0-9), per
  // docs/lvgl-ui-redesign-spec.md §4c/§5 — no bitmap-scaling hack.
  tunerNoteLabel_ = label(root, "--", LV_ALIGN_TOP_MID, 0, 112,
                          &ardor_font_saira_cond_semibold_tuner_110, text);
  lv_obj_set_width(tunerNoteLabel_, 360);
  lv_obj_set_style_text_align(tunerNoteLabel_, LV_TEXT_ALIGN_CENTER, 0);

  tunerFrequencyLabel_ = label(root, "Play a string", LV_ALIGN_TOP_MID, 0, 286,
                               &ardor_font_saira_cond_semibold_22, muted);
  tunerCentsLabel_ = label(root, "", LV_ALIGN_TOP_MID, 0, 332,
                           &ardor_font_saira_cond_semibold_28, muted);

  lv_obj_t* meter = lv_obj_create(root);
  lv_obj_set_size(meter, 760, 112);
  lv_obj_set_pos(meter, 260, 398);
  lv_obj_remove_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(meter, panelAlt);
  lv_obj_set_style_border_color(meter, lv_color_hex(rule), 0);
  lv_obj_set_style_border_width(meter, 1, 0);
  for (int tick = -5; tick <= 5; ++tick) {
    lv_obj_t* line = lv_obj_create(meter);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, tick == 0 ? 4 : 2,
                    tick == 0 ? 76 : (tick % 5 == 0 ? 54 : 34));
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(line, lv_color_hex(tick == 0 ? lamp : muted), 0);
    lv_obj_align(line, LV_ALIGN_CENTER, tick * 70, 0);
  }
  label(meter, "-50", LV_ALIGN_BOTTOM_LEFT, 12, -7,
        &ardor_font_saira_cond_medium_18, muted);
  label(meter, "0", LV_ALIGN_BOTTOM_MID, 0, -7,
        &ardor_font_saira_cond_medium_18, muted);
  label(meter, "+50", LV_ALIGN_BOTTOM_RIGHT, -12, -7,
        &ardor_font_saira_cond_medium_18, muted);

  tunerNeedle_ = lv_obj_create(root);
  lv_obj_remove_style_all(tunerNeedle_);
  lv_obj_set_size(tunerNeedle_, 8, 92);
  lv_obj_set_style_radius(tunerNeedle_, 0, 0);
  lv_obj_set_style_bg_opa(tunerNeedle_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(tunerNeedle_, lv_color_hex(lamp), 0);

  tunerGuidanceLabel_ = label(root, "PLAY A STRING", LV_ALIGN_TOP_MID, 0, 536,
                              &ardor_font_saira_cond_semibold_28, muted);
  constexpr std::array<const char*, 3> kVerdicts = {"FLAT", "IN TUNE", "SHARP"};
  constexpr std::array<int, 3> kVerdictX = {-160, 0, 160};
  for (std::size_t i = 0; i < kVerdicts.size(); ++i) {
    lv_obj_t* lampCell = lv_obj_create(root);
    lv_obj_set_size(lampCell, 11, 11);
    lv_obj_align(lampCell, LV_ALIGN_TOP_MID, kVerdictX[i] - 42, 588);
    styleSurface(lampCell, rule);
    lv_obj_set_style_border_width(lampCell, 0, 0);
    lv_obj_remove_flag(lampCell, LV_OBJ_FLAG_CLICKABLE);
    tunerVerdictLamps_[i] = lampCell;
    label(root, kVerdicts[i], LV_ALIGN_TOP_MID, kVerdictX[i] + 10, 582,
          &ardor_font_saira_cond_medium_18, muted);
  }
  label(root, "Press any footswitch to exit", LV_ALIGN_BOTTOM_MID, 0, -30,
        &ardor_font_saira_cond_semibold_22, muted);
  syncTunerView(state);
}

void LvglUi::syncTunerView(UiState& state)
{
  if (!tunerNoteLabel_ || !tunerNeedle_) return;
  const auto& tuner = state.tuner;
  if (!tuner.signalDetected) {
    lv_label_set_text(tunerNoteLabel_, "--");
    lv_label_set_text(tunerFrequencyLabel_, "Play a string");
    lv_label_set_text(tunerCentsLabel_, "");
    lv_label_set_text(tunerGuidanceLabel_, "PLAY A STRING");
    lv_obj_set_style_text_color(tunerGuidanceLabel_, lv_color_hex(muted), 0);
    for (lv_obj_t* verdict : tunerVerdictLamps_) {
      if (verdict) lv_obj_set_style_bg_color(verdict, lv_color_hex(rule), 0);
    }
    lv_obj_add_flag(tunerNeedle_, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  char note[16]{};
  char frequency[32]{};
  char cents[32]{};
  std::snprintf(note, sizeof(note), "%s%d", tuner.note.c_str(), tuner.octave);
  std::snprintf(frequency, sizeof(frequency), "%.1f Hz", tuner.frequencyHz);
  std::snprintf(cents, sizeof(cents), "%+.1f cents", tuner.cents);
  lv_label_set_text(tunerNoteLabel_, note);
  lv_label_set_text(tunerFrequencyLabel_, frequency);
  lv_label_set_text(tunerCentsLabel_, cents);

  const float absoluteCents = std::fabs(tuner.cents);
  const int color = absoluteCents <= 3.0f
    ? lamp : (absoluteCents <= 10.0f ? warning : danger);
  const char* guidance = absoluteCents <= 3.0f ? "IN TUNE"
    : (tuner.cents < 0.0f ? "FLAT  -  TUNE UP" : "SHARP  -  TUNE DOWN");
  lv_label_set_text(tunerGuidanceLabel_, guidance);
  lv_obj_set_style_text_color(tunerGuidanceLabel_, lv_color_hex(color), 0);
  const std::size_t verdictIndex = absoluteCents <= 3.0f ? 1 : (tuner.cents < 0.0f ? 0 : 2);
  for (std::size_t i = 0; i < tunerVerdictLamps_.size(); ++i) {
    if (!tunerVerdictLamps_[i]) continue;
    const int verdictColor = i == verdictIndex ? color : rule;
    lv_obj_set_style_bg_color(tunerVerdictLamps_[i], lv_color_hex(verdictColor), 0);
  }
  lv_obj_set_style_bg_color(tunerNeedle_, lv_color_hex(color), 0);
  const int needleX = 636 + static_cast<int>(
    std::lround(std::clamp(tuner.cents, -50.0f, 50.0f) * 7.0f));
  lv_obj_set_pos(tunerNeedle_, needleX, 408);
  lv_obj_remove_flag(tunerNeedle_, LV_OBJ_FLAG_HIDDEN);
}

} // namespace ardor
