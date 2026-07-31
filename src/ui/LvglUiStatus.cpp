#include "ui/LvglUiStatus.h"

#include "ui/LvglUi.h"
#include "ui/LvglUiStyle.h"
#include "ui/UiStatusPresentation.h"
#include "ui/fonts/OpenSansRegular.h"
#include "ui/fonts/OpenSansSemibold.h"

#include <string>

namespace ardor {
namespace {

using namespace lvgl_ui;

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
    const auto telemetry = makeTelemetryPresentation(state, accent);
    lv_label_set_text(telemetryLabel_, telemetry.text.c_str());
    lv_obj_set_style_text_color(telemetryLabel_, lv_color_hex(telemetry.color), 0);
  }
  if (statusMessageLabel_) {
    lv_label_set_text(statusMessageLabel_, state.statusMessage.c_str());
    lv_obj_set_style_text_color(statusMessageLabel_,
                                lv_color_hex(state.statusIsError ? 0xf97373 : accent), 0);
    lv_obj_align(statusMessageLabel_, LV_ALIGN_RIGHT_MID,
                 state.blockEditUndo.has_value() ? -132 : -18, 0);
    lv_obj_set_width(statusMessageLabel_, state.blockEditUndo.has_value() ? 340 : 480);
    if (state.statusMessage.empty()) lv_obj_add_flag(statusMessageLabel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(statusMessageLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  if (undoButton_) {
    if (state.blockEditUndo.has_value()) lv_obj_remove_flag(undoButton_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(undoButton_, LV_OBJ_FLAG_HIDDEN);
  }
}

void renderStatusBar(LvglUi* ui, lv_obj_t* root, UiState& state,
                     lv_obj_t** telemetryOut, lv_obj_t** masterOut,
                     lv_obj_t** messageOut, lv_obj_t** undoOut)
{
  lv_obj_t* bar = lv_obj_create(root);
  lv_obj_set_size(bar, kDesignWidth, kStatusBarHeight);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  styleSurface(bar, 0x111111);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(0x343434), 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);

  const auto telemetry = makeTelemetryPresentation(state, accent);
  lv_obj_t* telemetryLabel = label(bar, telemetry.text, LV_ALIGN_LEFT_MID, 18, 0,
                                   &ardor_font_open_sans_regular_18, telemetry.color);
  lv_obj_set_width(telemetryLabel, 460);
  lv_label_set_long_mode(telemetryLabel, LV_LABEL_LONG_CLIP);
  if (telemetryOut) *telemetryOut = telemetryLabel;

  lv_obj_t* master = label(bar, "Master " + std::to_string(state.masterVolume) + "%",
                           LV_ALIGN_CENTER, 0, 0,
                           &ardor_font_open_sans_semibold_22, text);
  lv_obj_set_width(master, 200);
  lv_obj_set_style_text_align(master, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(master, LV_LABEL_LONG_CLIP);
  if (masterOut) *masterOut = master;

  const bool canUndo = state.blockEditUndo.has_value();
  lv_obj_t* message = label(bar, state.statusMessage, LV_ALIGN_RIGHT_MID,
                            canUndo ? -132 : -18, 0,
                            &ardor_font_open_sans_regular_18,
                            state.statusIsError ? 0xf97373 : accent);
  lv_obj_set_width(message, canUndo ? 340 : 480);
  lv_label_set_long_mode(message, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_RIGHT, 0);
  if (state.statusMessage.empty()) lv_obj_add_flag(message, LV_OBJ_FLAG_HIDDEN);
  if (messageOut) *messageOut = message;

  lv_obj_t* undo = button(bar, "Undo");
  lv_obj_set_size(undo, 108, 40);
  lv_obj_align(undo, LV_ALIGN_RIGHT_MID, -8, 0);
  styleSurface(undo, 0x25442a);
  lv_obj_set_style_text_color(lv_obj_get_child(undo, 0), lv_color_hex(accent), 0);
  lv_obj_add_event_cb(undo, onUndoBlockEdit, LV_EVENT_CLICKED, ui->remember(state));
  if (!canUndo) lv_obj_add_flag(undo, LV_OBJ_FLAG_HIDDEN);
  if (undoOut) *undoOut = undo;
}

} // namespace ardor
