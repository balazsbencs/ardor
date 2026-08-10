#include "ui/LvglUiStatus.h"

#include "ui/LvglUi.h"
#include "ui/LvglUiNavigation.h"
#include "ui/LvglUiStyle.h"
#include "ui/UiStatusPresentation.h"

#include <string>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace ardor {
namespace {

using namespace lvgl_ui;
using namespace lvgl_navigation;

constexpr std::uint32_t kStatusToastHoldMs = 1950;

void setToastOffset(void* object, std::int32_t offset)
{
  lv_obj_set_style_translate_y(static_cast<lv_obj_t*>(object), offset, 0);
}

void hideCompletedToast(lv_anim_t* animation)
{
  auto* toast = static_cast<lv_obj_t*>(animation->var);
  lv_obj_add_flag(toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_translate_y(toast, 0, 0);
}

void animateToast(lv_obj_t* toast)
{
  lv_anim_delete(toast, setToastOffset);
  lv_obj_set_style_translate_y(toast, 16, 0);
  lv_obj_remove_flag(toast, LV_OBJ_FLAG_HIDDEN);
  lv_anim_t animation;
  lv_anim_init(&animation);
  lv_anim_set_var(&animation, toast);
  lv_anim_set_exec_cb(&animation, setToastOffset);
  lv_anim_set_values(&animation, 16, 0);
  lv_anim_set_duration(&animation, 120);
  lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
  lv_anim_set_reverse_delay(&animation, kStatusToastHoldMs);
  lv_anim_set_reverse_duration(&animation, 120);
  lv_anim_set_completed_cb(&animation, hideCompletedToast);
  lv_anim_start(&animation);
}

std::string upperWords(std::string value)
{
  for (char& c : value) {
    if (c == '_' || c == '-') c = ' ';
    else c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return value;
}

std::string statusToastText(const UiState& state)
{
  if (state.statusMessage.empty()) return {};
  return state.statusIsError ? "FAULT  ·  " + state.statusMessage : state.statusMessage;
}

std::string expressionStatus(const UiState& state)
{
  const auto& preset = state.bank.presets[state.activePreset];
  if (!preset.expression.has_value()) return {};
  const auto& assignment = *preset.expression;
  std::string blockLabel;
  const auto find = [&](const auto& self, const UiBlock& block) -> bool {
    if (block.id == assignment.blockId) {
      blockLabel = block.label;
      return true;
    }
    for (const auto& lane : block.lanes) {
      for (const auto& child : lane) if (self(self, child)) return true;
    }
    return false;
  };
  for (const auto& block : preset.blocks) if (find(find, block)) break;
  std::string result = "EXP: ";
  if (!blockLabel.empty()) result += upperWords(blockLabel) + " ";
  result += upperWords(assignment.parameter);
  if (state.controlInputs.expressionPositionKnown) {
    result += " " + std::to_string(static_cast<int>(std::lround(
      state.controlInputs.expressionPosition * 100.0f))) + "%";
  }
  return result;
}

void redraw(UiEventContext* context)
{
  context->ui->invalidate(UiChange::None);
}

void onUndoBlockEdit(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (undoLastBlockEdit(*context->state)) {
    context->ui->resetParameterPage();
    redraw(context);
  }
}

} // namespace

void LvglUi::syncStatusView(const UiState& state)
{
  if (!viewsInitialized_) return;
  if (telemetryLabel_) {
    const auto telemetry = makeTelemetryPresentation(state);
    lv_label_set_text(telemetryLabel_, telemetry.text.c_str());
    lv_obj_set_style_text_color(telemetryLabel_, lv_color_hex(telemetry.color), 0);
  }
  if (masterVolumeScaleFill_) {
    lv_obj_set_width(masterVolumeScaleFill_, std::clamp(state.masterVolume, 0, 100) * 120 / 100);
  }
  const bool live = state.mode == UiMode::Preset;
  if (expressionStatusLabel_) {
    const auto status = expressionStatus(state);
    lv_label_set_text(expressionStatusLabel_, status.c_str());
    lv_obj_set_style_text_color(expressionStatusLabel_,
      lv_color_hex(state.controlInputs.expressionConnected ? palette().family[3] : muted), 0);
    if (live && !status.empty()) lv_obj_remove_flag(expressionStatusLabel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(expressionStatusLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  if (midiStatusLabel_) {
    lv_label_set_text(midiStatusLabel_, state.controlInputs.midiConnected ? "MIDI ON" : "MIDI");
    lv_obj_set_style_text_color(midiStatusLabel_,
      lv_color_hex(state.controlInputs.midiConnected ? palette().family[3] : muted), 0);
    if (live) lv_obj_remove_flag(midiStatusLabel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(midiStatusLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  if (statusMessageLabel_) {
    if (statusToastState_ != &state || state.revisions.status != statusToastRevision_) {
      statusToastState_ = &state;
      statusToastRevision_ = state.revisions.status;
      const auto toastText = statusToastText(state);
      lv_label_set_text(statusMessageLabel_, toastText.c_str());
      lv_obj_set_style_text_color(statusMessageLabel_,
                                  lv_color_hex(state.statusIsError ? danger : text), 0);
      lv_obj_set_style_bg_color(statusMessageLabel_,
                                lv_color_hex(state.statusIsError ? panelAlt : panel), 0);
      lv_obj_set_style_border_color(statusMessageLabel_,
                                    lv_color_hex(state.statusIsError ? palette().faultLine : rule), 0);
      if (state.statusMessage.empty()) lv_obj_add_flag(statusMessageLabel_, LV_OBJ_FLAG_HIDDEN);
      else animateToast(statusMessageLabel_);
    }
  }
  if (undoButton_) {
    if (state.mode == UiMode::Edit && state.blockEditUndo.has_value()) {
      lv_obj_remove_flag(undoButton_, LV_OBJ_FLAG_HIDDEN);
    }
    else lv_obj_add_flag(undoButton_, LV_OBJ_FLAG_HIDDEN);
  }
}

void renderStatusBar(LvglUi* ui, lv_obj_t* root, UiState& state,
                     lv_obj_t** telemetryOut, lv_obj_t** masterOut,
                     lv_obj_t** masterScaleFillOut,
                     lv_obj_t** expressionOut, lv_obj_t** midiOut,
                     lv_obj_t** settingsOut, lv_obj_t** messageOut, lv_obj_t** undoOut)
{
  lv_obj_t* bar = lv_obj_create(root);
  lv_obj_set_size(bar, kDesignWidth, kStatusBarHeight);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(bar, panelAlt);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(rule), 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);

  const auto telemetry = makeTelemetryPresentation(state);
  lv_obj_t* telemetryLabel = label(bar, telemetry.text, LV_ALIGN_LEFT_MID, 18, 0,
                                   &ardor_font_saira_cond_medium_18, telemetry.color);
  lv_obj_set_width(telemetryLabel, 300);
  lv_label_set_long_mode(telemetryLabel, LV_LABEL_LONG_CLIP);
  if (telemetryOut) *telemetryOut = telemetryLabel;

  lv_obj_t* master = label(bar, "Master " + std::to_string(state.masterVolume) + "%",
                           LV_ALIGN_CENTER, 0, 0,
                           &ardor_font_saira_cond_semibold_22, text);
  lv_obj_set_width(master, 200);
  lv_obj_set_style_text_align(master, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(master, LV_LABEL_LONG_CLIP);
  if (masterOut) *masterOut = master;

  lv_obj_t* masterScale = lv_obj_create(bar);
  lv_obj_set_size(masterScale, 120, 3);
  lv_obj_align(masterScale, LV_ALIGN_BOTTOM_MID, 0, -5);
  styleSurface(masterScale, rule);
  lv_obj_set_style_border_width(masterScale, 0, 0);
  lv_obj_remove_flag(masterScale, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* masterScaleFill = lv_obj_create(masterScale);
  lv_obj_set_size(masterScaleFill, std::clamp(state.masterVolume, 0, 100) * 120 / 100, 3);
  lv_obj_set_pos(masterScaleFill, 0, 0);
  styleSurface(masterScaleFill, text);
  lv_obj_set_style_border_width(masterScaleFill, 0, 0);
  lv_obj_remove_flag(masterScaleFill, LV_OBJ_FLAG_CLICKABLE);
  if (masterScaleFillOut) *masterScaleFillOut = masterScaleFill;

  const auto expText = expressionStatus(state);
  lv_obj_t* expression = label(bar, expText, LV_ALIGN_RIGHT_MID, -184, 0,
                                &ardor_font_saira_cond_medium_18,
                                state.controlInputs.expressionConnected ? palette().family[3] : muted);
  lv_obj_set_width(expression, 360);
  lv_label_set_long_mode(expression, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(expression, LV_TEXT_ALIGN_RIGHT, 0);
  if (state.mode != UiMode::Preset || expText.empty()) lv_obj_add_flag(expression, LV_OBJ_FLAG_HIDDEN);
  if (expressionOut) *expressionOut = expression;

  lv_obj_t* midi = label(bar, state.controlInputs.midiConnected ? "MIDI ON" : "MIDI",
                         LV_ALIGN_RIGHT_MID, -82, 0,
                         &ardor_font_saira_cond_medium_18,
                         state.controlInputs.midiConnected ? palette().family[3] : muted);
  lv_obj_set_width(midi, 92);
  lv_obj_set_style_text_align(midi, LV_TEXT_ALIGN_RIGHT, 0);
  if (state.mode != UiMode::Preset) lv_obj_add_flag(midi, LV_OBJ_FLAG_HIDDEN);
  if (midiOut) *midiOut = midi;

  lv_obj_t* settings = lv_button_create(bar);
  lv_obj_set_size(settings, 58, 40);
  lv_obj_align(settings, LV_ALIGN_RIGHT_MID, -8, 0);
  styleSurface(settings, panel);
  lv_obj_t* settingsIcon = lv_label_create(settings);
  lv_label_set_text(settingsIcon, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_color(settingsIcon, lv_color_hex(text), 0);
  lv_obj_set_style_text_font(settingsIcon, LV_FONT_DEFAULT, 0);
  lv_obj_center(settingsIcon);
  lv_obj_add_event_cb(settings, onSettingsClicked, LV_EVENT_PRESSED, ui->remember(state));
  if (settingsOut) *settingsOut = settings;

  lv_obj_t* message = label(root, statusToastText(state), LV_ALIGN_TOP_MID, 0, 88,
                            &ardor_font_saira_cond_semibold_22,
                            state.statusIsError ? danger : text);
  lv_obj_set_size(message, 700, 54);
  lv_label_set_long_mode(message, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(message, 13, 0);
  lv_obj_set_style_pad_left(message, 20, 0);
  lv_obj_set_style_pad_right(message, 20, 0);
  lv_obj_set_style_bg_opa(message, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(message,
                            lv_color_hex(state.statusIsError ? panelAlt : panel), 0);
  lv_obj_set_style_border_width(message, 1, 0);
  lv_obj_set_style_border_color(message,
                                lv_color_hex(state.statusIsError ? palette().faultLine : rule), 0);
  lv_obj_set_style_radius(message, 0, 0);
  lv_obj_add_flag(message, LV_OBJ_FLAG_HIDDEN);
  if (messageOut) *messageOut = message;

  const bool canUndo = state.mode == UiMode::Edit && state.blockEditUndo.has_value();
  lv_obj_t* undo = button(bar, "Undo");
  lv_obj_set_size(undo, 108, 40);
  lv_obj_align(undo, LV_ALIGN_RIGHT_MID, -76, 0);
  styleSurface(undo, panel);
  lv_obj_set_style_text_color(lv_obj_get_child(undo, 0), lv_color_hex(text), 0);
  lv_obj_add_event_cb(undo, onUndoBlockEdit, LV_EVENT_CLICKED, ui->remember(state));
  if (!canUndo) lv_obj_add_flag(undo, LV_OBJ_FLAG_HIDDEN);
  if (undoOut) *undoOut = undo;
}

} // namespace ardor
