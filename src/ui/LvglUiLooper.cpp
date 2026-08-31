#include "ui/LvglUi.h"

#include "ui/LvglUiStyle.h"
#include "ui/fonts/SairaCondSemibold28.h"
#include "ui/fonts/SairaCondSemibold52.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace ardor {
namespace {

using namespace lvgl_ui;

// THESIS: four synchronized tracks read as one stage instrument, never as a waveform editor.
// OWN-WORLD: hard graphite plates, bone engraving, amber armed headers, one live-red record lamp.
// STORY: glance at phase and track state, then act with the matching physical corner switch.
// FIRST VIEWPORT: locked preset rail, physical 1/2/3/4 plate map, fixed transport rail.
// FORM: established Ardor Panel language extended by the approved looper specification.

constexpr int kTopRailHeight = 58;
constexpr int kBottomRailHeight = 92;
constexpr int kEdgeInset = 26;
constexpr std::size_t kLibraryRows = 4;

bool populated(LooperTrackState state)
{
  return state != LooperTrackState::Empty
      && state != LooperTrackState::ArmedRecord
      && state != LooperTrackState::Recording;
}

bool recording(LooperTrackState state)
{
  return state == LooperTrackState::Recording || state == LooperTrackState::Overdubbing;
}

bool armed(LooperTrackState state)
{
  return state == LooperTrackState::ArmedRecord || state == LooperTrackState::ArmedOverdub;
}

const char* stateText(LooperTrackState state)
{
  switch (state) {
  case LooperTrackState::Empty: return "EMPTY";
  case LooperTrackState::ArmedRecord: return "ARMED · NEXT LOOP";
  case LooperTrackState::Recording: return "REC";
  case LooperTrackState::Playing: return "PLAY";
  case LooperTrackState::Muted: return "MUTED";
  case LooperTrackState::ArmedOverdub: return "DUB ARMED · NEXT LOOP";
  case LooperTrackState::Overdubbing: return "OVERDUB";
  }
  return "EMPTY";
}

std::string looperNoticeText(const UiState& state)
{
  const auto& looper = state.looper;
  if (looper.clearHoldProgress > 0.0f) {
    const auto percent = static_cast<int>(std::lround(looper.clearHoldProgress * 100.0f));
    return "HOLD TO CLEAR TRACK " + std::to_string(looper.selectedTrack + 1)
      + " · " + std::to_string(percent) + "% · RELEASE CANCELS";
  }
  switch (looper.telemetry.error) {
  case LooperError::None: break;
  case LooperError::InvalidTrack: return "INVALID TRACK";
  case LooperError::InvalidState:
    if (looper.telemetry.tracks[looper.selectedTrack].state == LooperTrackState::Overdubbing) {
      return "OVERDUB ENDS AT LOOP · FINISH TAKE FIRST";
    }
    if (looper.telemetry.sessionState == LooperSessionState::RecordingMaster
        || std::any_of(looper.telemetry.tracks.begin(), looper.telemetry.tracks.end(),
                       [](const auto& track) {
          return track.state == LooperTrackState::Recording
              || track.state == LooperTrackState::Overdubbing;
        })) {
      return "FINISH TAKE FIRST";
    }
    return "ACTION UNAVAILABLE IN CURRENT STATE";
  case LooperError::MasterTooShort: return "LOOP TOO SHORT · RECORD AT LEAST TWO AUDIO BLOCKS";
  case LooperError::MaximumLengthReached: return "MAX LENGTH · LOOP CLOSED AND PLAYING";
  }
  if (looper.telemetry.sessionState == LooperSessionState::Faulted) {
    return "LOOPER FAULT · CLOSE SESSION TO RECOVER";
  }
  if (!state.statusMessage.empty()) {
    return uppercase(state.statusMessage);
  }
  return "TRACK " + std::to_string(looper.selectedTrack + 1)
    + " SELECTED · FS2 NEXT · HOLD FS2 CLEAR";
}

std::string clockText(uint64_t frames)
{
  const auto seconds = frames / 48000;
  char value[24]{};
  std::snprintf(value, sizeof(value), "%02llu:%02llu",
                static_cast<unsigned long long>(seconds / 60),
                static_cast<unsigned long long>(seconds % 60));
  return value;
}

void onTrackClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->state->looper.selectedTrack = context->index;
  context->state->looper.mixerOpen = populated(
    context->state->looper.telemetry.tracks[context->index].state);
  markUiChanged(*context->state, UiChange::Looper);
  if (context->ui->actions().selectLooperTrack) {
    context->ui->actions().selectLooperTrack(context->index);
  }
}

void onMixerCloseClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->state->looper.mixerOpen = false;
  context->state->looper.clearTrackConfirmationOpen = false;
  markUiChanged(*context->state, UiChange::Looper);
}

void onMixerCommandClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (!context->ui->actions().looperCommand) return;
  const auto trackIndex = context->state->looper.selectedTrack;
  const auto& track = context->state->looper.telemetry.tracks[trackIndex];
  if (context->index == 0 || context->index == 1) {
    const float delta = context->index == 0 ? -1.0f : 1.0f;
    context->ui->actions().looperCommand(
      LooperCommandType::SetTrackLevelDb, trackIndex, track.levelDb + delta);
  } else if (context->index == 2 || context->index == 3) {
    const float delta = context->index == 2 ? -0.1f : 0.1f;
    context->ui->actions().looperCommand(
      LooperCommandType::SetTrackBalance, trackIndex, track.balance + delta);
  } else if (context->index == 4) {
    context->ui->actions().looperCommand(
      LooperCommandType::ToggleTrackAudible, trackIndex, 0.0f);
  } else {
    context->state->looper.clearTrackConfirmationOpen = true;
    markUiChanged(*context->state, UiChange::Looper);
  }
}

void onClearTrackCancelClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->state->looper.clearTrackConfirmationOpen = false;
  markUiChanged(*context->state, UiChange::Looper);
}

void onClearTrackConfirmClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().looperCommand) {
    context->ui->actions().looperCommand(
      LooperCommandType::ClearTrack, context->state->looper.selectedTrack, 0.0f);
  }
  context->state->looper.clearTrackConfirmationOpen = false;
  context->state->looper.mixerOpen = false;
  markUiChanged(*context->state, UiChange::Looper);
}

void onLooperCommand(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (!context->ui->actions().looperCommand) return;
  const auto track = context->state->looper.selectedTrack;
  const auto type = context->index == 0 ? LooperCommandType::ToggleUndo
    : context->index == 1 ? LooperCommandType::RecordOrOverdub
    : LooperCommandType::ToggleTrackAudible;
  context->ui->actions().looperCommand(type, track, 0.0f);
}

void onStopClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (!context->ui->actions().looperCommand) return;
  const auto state = context->state->looper.telemetry.sessionState;
  if (state == LooperSessionState::Running) {
    context->ui->actions().looperCommand(LooperCommandType::Pause, 0, 0.0f);
  } else if (state == LooperSessionState::Paused) {
    context->ui->actions().looperCommand(LooperCommandType::Resume, 0, 0.0f);
  }
}

void onExitClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().exitLooper) context->ui->actions().exitLooper();
  else enterPresetMode(*context->state);
}

void onNewClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->state->looper.modified && context->controlledObject) {
    lv_obj_remove_flag(context->controlledObject, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(context->controlledObject);
    return;
  }
  if (context->ui->actions().newLooper) context->ui->actions().newLooper();
}

void onDiscardNewClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->controlledObject) lv_obj_add_flag(context->controlledObject, LV_OBJ_FLAG_HIDDEN);
  markLooperSaved(*context->state);
  if (context->ui->actions().newLooper) context->ui->actions().newLooper();
}

void onSaveThenRetryClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->controlledObject) lv_obj_add_flag(context->controlledObject, LV_OBJ_FLAG_HIDDEN);
  if (context->ui->actions().saveLooper) context->ui->actions().saveLooper();
}

void onSaveClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().saveLooper) context->ui->actions().saveLooper();
}

void onLoadClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().loadLooper) context->ui->actions().loadLooper();
}

void onLibraryCloseClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  closeLooperLibrary(*context->state);
}

void onLibraryPageClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto pageCount = std::max<std::size_t>(
    1, (context->state->looper.library.size() + kLibraryRows - 1) / kLibraryRows);
  if (context->index == 0 && context->state->looper.libraryPage > 0) {
    --context->state->looper.libraryPage;
  } else if (context->index == 1
             && context->state->looper.libraryPage + 1 < pageCount) {
    ++context->state->looper.libraryPage;
  }
  markUiChanged(*context->state, UiChange::Looper);
}

void onLibraryLoadClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto index = context->state->looper.libraryPage * kLibraryRows
    + context->index;
  if (index >= context->state->looper.library.size()) return;
  const auto& entry = context->state->looper.library[index];
  if (!entry.available || !context->ui->actions().loadLooperSet) return;
  closeLooperLibrary(*context->state);
  context->ui->actions().loadLooperSet(entry.id);
}

void onLibraryDeleteClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto index = context->state->looper.libraryPage * kLibraryRows
    + context->index;
  if (index >= context->state->looper.library.size()) return;
  context->state->looper.deleteCandidateId = context->state->looper.library[index].id;
  markUiChanged(*context->state, UiChange::Looper);
}

void onLibraryDeleteCancelClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->state->looper.deleteCandidateId.reset();
  markUiChanged(*context->state, UiChange::Looper);
}

void onLibraryDeleteConfirmClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (!context->state->looper.deleteCandidateId
      || !context->ui->actions().deleteLooperSet) return;
  const auto id = *context->state->looper.deleteCandidateId;
  context->state->looper.deleteCandidateId.reset();
  context->ui->actions().deleteLooperSet(id);
}

void onCloseClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->state->looper.modified && context->controlledObject) {
    lv_obj_remove_flag(context->controlledObject, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(context->controlledObject);
    return;
  }
  if (context->ui->actions().closeLooper) context->ui->actions().closeLooper();
}

void onDiscardCloseClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->controlledObject) lv_obj_add_flag(context->controlledObject, LV_OBJ_FLAG_HIDDEN);
  if (context->ui->actions().closeLooper) context->ui->actions().closeLooper();
}

void onCancelCloseClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->controlledObject) lv_obj_add_flag(context->controlledObject, LV_OBJ_FLAG_HIDDEN);
}

void onSaveCloseClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->controlledObject) lv_obj_add_flag(context->controlledObject, LV_OBJ_FLAG_HIDDEN);
  if (context->ui->actions().saveLooper) context->ui->actions().saveLooper();
}

} // namespace

void LvglUi::renderLooperMode(lv_obj_t* root, UiState& state)
{
  lv_obj_t* topRail = lv_obj_create(root);
  lv_obj_set_size(topRail, kDesignWidth, kTopRailHeight);
  lv_obj_set_pos(topRail, 0, 0);
  styleSurface(topRail, panel);
  lv_obj_set_style_border_side(topRail, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_pad_all(topRail, 0, 0);
  lv_obj_remove_flag(topRail, LV_OBJ_FLAG_SCROLLABLE);

  label(topRail, "LOOPER", LV_ALIGN_LEFT_MID, kEdgeInset, 0,
        &ardor_font_saira_cond_semibold_28, text);
  looperPresetLabel_ = label(topRail, "LOCKED · --", LV_ALIGN_LEFT_MID, 168, 0,
                             &ardor_font_saira_cond_medium_18, muted);
  looperPositionLabel_ = label(topRail, "00:00 / 00:00", LV_ALIGN_CENTER, 0, 0,
                               &ardor_font_saira_cond_semibold_28, text);
  looperMemoryLabel_ = label(topRail, "128 MB", LV_ALIGN_RIGHT_MID, -kEdgeInset, 0,
                             &ardor_font_saira_cond_medium_18, muted);

  lv_obj_t* grid = lv_obj_create(root);
  lv_obj_set_pos(grid, kEdgeInset, kTopRailHeight + 14);
  lv_obj_set_size(grid, kDesignWidth - 2 * kEdgeInset,
                  kDesignHeight - kTopRailHeight - kBottomRailHeight - 28);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_style_pad_column(grid, 12, 0);
  lv_obj_set_style_pad_row(grid, 12, 0);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  static int32_t columns[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(grid, columns, rows);

  const std::array<const char*, kLooperTrackCount> legends = {
    "FS1 · UNDO", "FS2 · TRACK / HOLD CLEAR", "FS3 · REC / DUB", "FS4 · PLAY / MUTE"
  };
  for (std::size_t index = 0; index < kLooperTrackCount; ++index) {
    auto* plate = lv_obj_create(grid);
    looperTrackPlates_[index] = plate;
    lv_obj_set_grid_cell(plate, LV_GRID_ALIGN_STRETCH, static_cast<int>(index / 2), 1,
                         LV_GRID_ALIGN_STRETCH, static_cast<int>(index % 2), 1);
    styleSurface(plate, panel);
    lv_obj_set_style_pad_all(plate, 0, 0);
    lv_obj_remove_flag(plate, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(plate, onTrackClicked, LV_EVENT_CLICKED, remember(state, index));

    auto* header = lv_obj_create(plate);
    looperTrackHeaders_[index] = header;
    lv_obj_set_size(header, LV_PCT(100), 42);
    lv_obj_set_pos(header, 0, 0);
    styleSurface(header, panelAlt);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);
    label(header, "TRACK " + std::to_string(index + 1), LV_ALIGN_LEFT_MID, 15, 0,
          &ardor_font_saira_cond_semibold_28, text);
    label(header, legends[index], LV_ALIGN_RIGHT_MID, -15, 0,
          &ardor_font_saira_cond_medium_18, muted);

    looperTrackStateLabels_[index] = label(
      plate, "EMPTY", LV_ALIGN_LEFT_MID, 20, 4, &ardor_font_saira_cond_semibold_52, disabled);
    looperTrackDetailLabels_[index] = label(
      plate, "0 DB · C", LV_ALIGN_BOTTOM_LEFT, 20, -24,
      &ardor_font_saira_cond_medium_18, muted);

    auto* progressTrack = lv_obj_create(plate);
    lv_obj_remove_style_all(progressTrack);
    lv_obj_set_size(progressTrack, LV_PCT(100), 5);
    lv_obj_align(progressTrack, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(progressTrack, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(progressTrack, lv_color_hex(rule), 0);
    auto* progress = lv_obj_create(progressTrack);
    lv_obj_remove_style_all(progress);
    lv_obj_set_size(progress, 0, 5);
    lv_obj_align(progress, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(progress, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(progress, lv_color_hex(text), 0);
    looperProgressFills_[index] = progress;
  }

  auto* bottomRail = lv_obj_create(root);
  lv_obj_set_size(bottomRail, kDesignWidth, kBottomRailHeight);
  lv_obj_set_pos(bottomRail, 0, kDesignHeight - kBottomRailHeight);
  styleSurface(bottomRail, bg);
  lv_obj_set_style_border_side(bottomRail, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_pad_all(bottomRail, 0, 0);
  lv_obj_remove_flag(bottomRail, LV_OBJ_FLAG_SCROLLABLE);

  looperNoticeLabel_ = label(bottomRail, "TRACK 1 SELECTED · FS2 NEXT · HOLD FS2 CLEAR",
                             LV_ALIGN_TOP_MID, 0, 3,
                             &ardor_font_saira_cond_medium_18, muted);
  lv_obj_set_width(looperNoticeLabel_, 760);
  lv_obj_set_style_text_align(looperNoticeLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(looperNoticeLabel_, LV_LABEL_LONG_CLIP);

  looperNewButton_ = button(bottomRail, "NEW");
  lv_obj_set_size(looperNewButton_, 150, 54);
  lv_obj_align(looperNewButton_, LV_ALIGN_BOTTOM_LEFT, kEdgeInset, -7);
  auto* newContext = remember(state);
  lv_obj_add_event_cb(looperNewButton_, onNewClicked, LV_EVENT_PRESSED, newContext);

  looperSaveButton_ = button(bottomRail, "SAVE");
  lv_obj_set_size(looperSaveButton_, 150, 54);
  lv_obj_align(looperSaveButton_, LV_ALIGN_BOTTOM_LEFT, kEdgeInset + 162, -7);
  lv_obj_add_event_cb(looperSaveButton_, onSaveClicked, LV_EVENT_PRESSED, remember(state));

  looperLoadButton_ = button(bottomRail, "LOAD");
  lv_obj_set_size(looperLoadButton_, 150, 54);
  lv_obj_align(looperLoadButton_, LV_ALIGN_BOTTOM_LEFT, kEdgeInset + 324, -7);
  lv_obj_add_event_cb(looperLoadButton_, onLoadClicked, LV_EVENT_PRESSED, remember(state));

  looperStopButton_ = button(bottomRail, "STOP ALL");
  lv_obj_set_size(looperStopButton_, 150, 54);
  lv_obj_align(looperStopButton_, LV_ALIGN_BOTTOM_RIGHT, -366, -7);
  looperStopLabel_ = lv_obj_get_child(looperStopButton_, 0);
  lv_obj_add_event_cb(looperStopButton_, onStopClicked, LV_EVENT_PRESSED, remember(state));

  looperExitButton_ = button(bottomRail, "EXIT");
  lv_obj_set_size(looperExitButton_, 150, 54);
  lv_obj_align(looperExitButton_, LV_ALIGN_BOTTOM_RIGHT, -196, -7);
  lv_obj_add_event_cb(looperExitButton_, onExitClicked, LV_EVENT_PRESSED, remember(state));

  auto* closeButton = button(bottomRail, "CLOSE");
  looperCloseButton_ = closeButton;
  lv_obj_set_size(closeButton, 150, 54);
  lv_obj_align(closeButton, LV_ALIGN_BOTTOM_RIGHT, -kEdgeInset, -7);
  auto* closeContext = remember(state);
  lv_obj_add_event_cb(closeButton, onCloseClicked, LV_EVENT_PRESSED, closeContext);

  looperCloseOverlay_ = lv_obj_create(root);
  lv_obj_set_size(looperCloseOverlay_, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(looperCloseOverlay_, 0, 0);
  lv_obj_set_style_bg_color(looperCloseOverlay_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(looperCloseOverlay_, LV_OPA_80, 0);
  lv_obj_set_style_border_width(looperCloseOverlay_, 0, 0);
  lv_obj_remove_flag(looperCloseOverlay_, LV_OBJ_FLAG_SCROLLABLE);
  auto* confirmation = lv_obj_create(looperCloseOverlay_);
  lv_obj_set_size(confirmation, 620, 240);
  lv_obj_align(confirmation, LV_ALIGN_CENTER, 0, 0);
  styleSurface(confirmation, panelAlt);
  lv_obj_remove_flag(confirmation, LV_OBJ_FLAG_SCROLLABLE);
  label(confirmation, "DISCARD UNSAVED LOOP?", LV_ALIGN_TOP_MID, 0, 28,
        &ardor_font_saira_cond_semibold_28, text);
  label(confirmation, "SAVE KEEPS THE LOOP OPEN; PRESS CLOSE AGAIN AFTER SAVING.",
        LV_ALIGN_TOP_MID, 0, 78, &ardor_font_saira_cond_medium_18, muted);
  auto* cancel = button(confirmation, "CANCEL");
  lv_obj_set_size(cancel, 150, 58);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, -166, -28);
  auto* cancelContext = remember(state);
  cancelContext->controlledObject = looperCloseOverlay_;
  lv_obj_add_event_cb(cancel, onCancelCloseClicked, LV_EVENT_CLICKED, cancelContext);
  auto* saveClose = button(confirmation, "SAVE");
  lv_obj_set_size(saveClose, 150, 58);
  lv_obj_align(saveClose, LV_ALIGN_BOTTOM_MID, 0, -28);
  auto* saveCloseContext = remember(state);
  saveCloseContext->controlledObject = looperCloseOverlay_;
  lv_obj_add_event_cb(saveClose, onSaveCloseClicked, LV_EVENT_CLICKED, saveCloseContext);
  auto* discard = button(confirmation, "DISCARD");
  lv_obj_set_size(discard, 150, 58);
  lv_obj_align(discard, LV_ALIGN_BOTTOM_MID, 166, -28);
  styleSurface(discard, text);
  lv_obj_set_style_text_color(lv_obj_get_child(discard, 0), lv_color_hex(bg), 0);
  auto* discardContext = remember(state);
  discardContext->controlledObject = looperCloseOverlay_;
  lv_obj_add_event_cb(discard, onDiscardCloseClicked, LV_EVENT_CLICKED, discardContext);
  closeContext->controlledObject = looperCloseOverlay_;
  lv_obj_add_flag(looperCloseOverlay_, LV_OBJ_FLAG_HIDDEN);

  looperNewOverlay_ = lv_obj_create(root);
  lv_obj_set_size(looperNewOverlay_, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(looperNewOverlay_, 0, 0);
  lv_obj_set_style_bg_color(looperNewOverlay_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(looperNewOverlay_, LV_OPA_80, 0);
  lv_obj_set_style_border_width(looperNewOverlay_, 0, 0);
  lv_obj_remove_flag(looperNewOverlay_, LV_OBJ_FLAG_SCROLLABLE);
  auto* newPanel = lv_obj_create(looperNewOverlay_);
  lv_obj_set_size(newPanel, 650, 260);
  lv_obj_align(newPanel, LV_ALIGN_CENTER, 0, 0);
  styleSurface(newPanel, panelAlt);
  lv_obj_remove_flag(newPanel, LV_OBJ_FLAG_SCROLLABLE);
  label(newPanel, "START A NEW LOOP?", LV_ALIGN_TOP_MID, 0, 24,
        &ardor_font_saira_cond_semibold_28, text);
  label(newPanel, "SAVE KEEPS THIS LOOP OPEN; PRESS NEW AGAIN AFTER SAVING.", LV_ALIGN_TOP_MID, 0, 76,
        &ardor_font_saira_cond_medium_18, muted);
  auto* newCancel = button(newPanel, "CANCEL");
  lv_obj_set_size(newCancel, 150, 58);
  lv_obj_align(newCancel, LV_ALIGN_BOTTOM_MID, -166, -24);
  auto* newCancelContext = remember(state);
  newCancelContext->controlledObject = looperNewOverlay_;
  lv_obj_add_event_cb(newCancel, onCancelCloseClicked, LV_EVENT_CLICKED, newCancelContext);
  auto* newSave = button(newPanel, "SAVE");
  lv_obj_set_size(newSave, 150, 58);
  lv_obj_align(newSave, LV_ALIGN_BOTTOM_MID, 0, -24);
  auto* newSaveContext = remember(state);
  newSaveContext->controlledObject = looperNewOverlay_;
  lv_obj_add_event_cb(newSave, onSaveThenRetryClicked, LV_EVENT_CLICKED, newSaveContext);
  auto* newDiscard = button(newPanel, "DISCARD");
  lv_obj_set_size(newDiscard, 150, 58);
  lv_obj_align(newDiscard, LV_ALIGN_BOTTOM_MID, 166, -24);
  auto* newDiscardContext = remember(state);
  newDiscardContext->controlledObject = looperNewOverlay_;
  lv_obj_add_event_cb(newDiscard, onDiscardNewClicked, LV_EVENT_CLICKED, newDiscardContext);
  newContext->controlledObject = looperNewOverlay_;
  lv_obj_add_flag(looperNewOverlay_, LV_OBJ_FLAG_HIDDEN);

  looperLibraryOverlay_ = lv_obj_create(root);
  lv_obj_set_size(looperLibraryOverlay_, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(looperLibraryOverlay_, 0, 0);
  lv_obj_set_style_bg_color(looperLibraryOverlay_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(looperLibraryOverlay_, LV_OPA_80, 0);
  lv_obj_set_style_border_width(looperLibraryOverlay_, 0, 0);
  lv_obj_remove_flag(looperLibraryOverlay_, LV_OBJ_FLAG_SCROLLABLE);
  auto* libraryPanel = lv_obj_create(looperLibraryOverlay_);
  lv_obj_set_size(libraryPanel, 1080, 620);
  lv_obj_align(libraryPanel, LV_ALIGN_CENTER, 0, 0);
  styleSurface(libraryPanel, panelAlt);
  lv_obj_set_style_pad_all(libraryPanel, 0, 0);
  lv_obj_remove_flag(libraryPanel, LV_OBJ_FLAG_SCROLLABLE);
  label(libraryPanel, "SAVED LOOPS", LV_ALIGN_TOP_LEFT, 28, 20,
        &ardor_font_saira_cond_semibold_28, text);
  auto* libraryClose = button(libraryPanel, "CLOSE");
  lv_obj_set_size(libraryClose, 130, 48);
  lv_obj_align(libraryClose, LV_ALIGN_TOP_RIGHT, -22, 14);
  lv_obj_add_event_cb(libraryClose, onLibraryCloseClicked, LV_EVENT_CLICKED, remember(state));

  for (std::size_t row = 0; row < kLooperLibraryRows; ++row) {
    auto* plate = lv_obj_create(libraryPanel);
    looperLibraryRows_[row] = plate;
    lv_obj_set_size(plate, 1024, 105);
    lv_obj_set_pos(plate, 28, 76 + static_cast<int>(row) * 112);
    styleSurface(plate, panel);
    lv_obj_set_style_pad_all(plate, 0, 0);
    lv_obj_remove_flag(plate, LV_OBJ_FLAG_SCROLLABLE);
    looperLibraryNameLabels_[row] = label(
      plate, "--", LV_ALIGN_TOP_LEFT, 18, 12, &ardor_font_saira_cond_semibold_28, text);
    looperLibraryMetaLabels_[row] = label(
      plate, "--", LV_ALIGN_BOTTOM_LEFT, 18, -14, &ardor_font_saira_cond_medium_18, muted);
    lv_obj_set_width(looperLibraryNameLabels_[row], 610);
    lv_obj_set_width(looperLibraryMetaLabels_[row], 610);
    lv_label_set_long_mode(looperLibraryNameLabels_[row], LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(looperLibraryMetaLabels_[row], LV_LABEL_LONG_CLIP);
    auto* load = button(plate, "LOAD");
    looperLibraryLoadButtons_[row] = load;
    lv_obj_set_size(load, 150, 58);
    lv_obj_align(load, LV_ALIGN_RIGHT_MID, -178, 0);
    lv_obj_add_event_cb(load, onLibraryLoadClicked, LV_EVENT_CLICKED, remember(state, row));
    auto* remove = button(plate, "DELETE");
    looperLibraryDeleteButtons_[row] = remove;
    lv_obj_set_size(remove, 150, 58);
    lv_obj_align(remove, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_add_event_cb(remove, onLibraryDeleteClicked, LV_EVENT_CLICKED, remember(state, row));
  }
  looperLibraryEmptyLabel_ = label(
    libraryPanel, "NO SAVED LOOPS · SAVE A PAUSED LOOP FIRST", LV_ALIGN_CENTER, 0, -8,
    &ardor_font_saira_cond_semibold_28, muted);
  looperLibraryPreviousButton_ = button(libraryPanel, "PREVIOUS");
  lv_obj_set_size(looperLibraryPreviousButton_, 150, 48);
  lv_obj_align(looperLibraryPreviousButton_, LV_ALIGN_BOTTOM_LEFT, 28, -14);
  lv_obj_add_event_cb(looperLibraryPreviousButton_, onLibraryPageClicked,
                      LV_EVENT_CLICKED, remember(state, 0));
  looperLibraryNextButton_ = button(libraryPanel, "NEXT");
  lv_obj_set_size(looperLibraryNextButton_, 150, 48);
  lv_obj_align(looperLibraryNextButton_, LV_ALIGN_BOTTOM_RIGHT, -28, -14);
  lv_obj_add_event_cb(looperLibraryNextButton_, onLibraryPageClicked,
                      LV_EVENT_CLICKED, remember(state, 1));
  looperLibraryPageLabel_ = label(
    libraryPanel, "PAGE 1 / 1", LV_ALIGN_BOTTOM_MID, 0, -27,
    &ardor_font_saira_cond_medium_18, muted);
  lv_obj_add_flag(looperLibraryOverlay_, LV_OBJ_FLAG_HIDDEN);

  looperDeleteOverlay_ = lv_obj_create(looperLibraryOverlay_);
  lv_obj_set_size(looperDeleteOverlay_, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(looperDeleteOverlay_, 0, 0);
  lv_obj_set_style_bg_color(looperDeleteOverlay_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(looperDeleteOverlay_, LV_OPA_80, 0);
  lv_obj_set_style_border_width(looperDeleteOverlay_, 0, 0);
  lv_obj_remove_flag(looperDeleteOverlay_, LV_OBJ_FLAG_SCROLLABLE);
  auto* deletePanel = lv_obj_create(looperDeleteOverlay_);
  lv_obj_set_size(deletePanel, 620, 260);
  lv_obj_align(deletePanel, LV_ALIGN_CENTER, 0, 0);
  styleSurface(deletePanel, panelAlt);
  lv_obj_remove_flag(deletePanel, LV_OBJ_FLAG_SCROLLABLE);
  label(deletePanel, "DELETE SAVED LOOP?", LV_ALIGN_TOP_MID, 0, 24,
        &ardor_font_saira_cond_semibold_28, text);
  looperDeleteNameLabel_ = label(deletePanel, "--", LV_ALIGN_TOP_MID, 0, 76,
                                  &ardor_font_saira_cond_medium_18, muted);
  lv_obj_set_width(looperDeleteNameLabel_, 540);
  lv_obj_set_style_text_align(looperDeleteNameLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(looperDeleteNameLabel_, LV_LABEL_LONG_CLIP);
  auto* deleteCancel = button(deletePanel, "CANCEL");
  lv_obj_set_size(deleteCancel, 170, 58);
  lv_obj_align(deleteCancel, LV_ALIGN_BOTTOM_MID, -96, -24);
  lv_obj_add_event_cb(deleteCancel, onLibraryDeleteCancelClicked,
                      LV_EVENT_CLICKED, remember(state));
  auto* deleteConfirm = button(deletePanel, "DELETE");
  lv_obj_set_size(deleteConfirm, 170, 58);
  lv_obj_align(deleteConfirm, LV_ALIGN_BOTTOM_MID, 96, -24);
  lv_obj_add_event_cb(deleteConfirm, onLibraryDeleteConfirmClicked,
                      LV_EVENT_CLICKED, remember(state));
  lv_obj_add_flag(looperDeleteOverlay_, LV_OBJ_FLAG_HIDDEN);

  looperMixerOverlay_ = lv_obj_create(root);
  lv_obj_set_size(looperMixerOverlay_, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(looperMixerOverlay_, 0, 0);
  lv_obj_set_style_bg_color(looperMixerOverlay_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(looperMixerOverlay_, LV_OPA_80, 0);
  lv_obj_set_style_border_width(looperMixerOverlay_, 0, 0);
  lv_obj_remove_flag(looperMixerOverlay_, LV_OBJ_FLAG_SCROLLABLE);
  auto* mixerPanel = lv_obj_create(looperMixerOverlay_);
  lv_obj_set_size(mixerPanel, 900, 470);
  lv_obj_align(mixerPanel, LV_ALIGN_CENTER, 0, 0);
  styleSurface(mixerPanel, panelAlt);
  lv_obj_remove_flag(mixerPanel, LV_OBJ_FLAG_SCROLLABLE);
  looperMixerTitleLabel_ = label(mixerPanel, "TRACK 1 MIX", LV_ALIGN_TOP_LEFT, 18, 8,
                                  &ardor_font_saira_cond_semibold_28, text);
  auto* mixerClose = button(mixerPanel, "CLOSE");
  lv_obj_set_size(mixerClose, 140, 50);
  lv_obj_align(mixerClose, LV_ALIGN_TOP_RIGHT, -12, 0);
  lv_obj_add_event_cb(mixerClose, onMixerCloseClicked, LV_EVENT_CLICKED, remember(state));

  label(mixerPanel, "LEVEL", LV_ALIGN_TOP_LEFT, 32, 96,
        &ardor_font_saira_cond_medium_18, muted);
  auto* levelDown = button(mixerPanel, "-1 DB");
  lv_obj_set_size(levelDown, 180, 72);
  lv_obj_align(levelDown, LV_ALIGN_LEFT_MID, 24, -30);
  lv_obj_add_event_cb(levelDown, onMixerCommandClicked, LV_EVENT_PRESSED, remember(state, 0));
  looperMixerLevelLabel_ = label(mixerPanel, "+0 DB", LV_ALIGN_CENTER, -220, -30,
                                  &ardor_font_saira_cond_semibold_52, text);
  auto* levelUp = button(mixerPanel, "+1 DB");
  lv_obj_set_size(levelUp, 180, 72);
  lv_obj_align(levelUp, LV_ALIGN_CENTER, -22, -30);
  lv_obj_add_event_cb(levelUp, onMixerCommandClicked, LV_EVENT_PRESSED, remember(state, 1));

  label(mixerPanel, "STEREO BALANCE", LV_ALIGN_TOP_LEFT, 472, 96,
        &ardor_font_saira_cond_medium_18, muted);
  auto* balanceLeft = button(mixerPanel, "LEFT");
  lv_obj_set_size(balanceLeft, 150, 72);
  lv_obj_align(balanceLeft, LV_ALIGN_CENTER, 164, -30);
  lv_obj_add_event_cb(balanceLeft, onMixerCommandClicked, LV_EVENT_PRESSED, remember(state, 2));
  looperMixerBalanceLabel_ = label(mixerPanel, "CENTER", LV_ALIGN_CENTER, 316, -30,
                                    &ardor_font_saira_cond_semibold_28, text);
  auto* balanceRight = button(mixerPanel, "RIGHT");
  lv_obj_set_size(balanceRight, 150, 72);
  lv_obj_align(balanceRight, LV_ALIGN_RIGHT_MID, -18, -30);
  lv_obj_add_event_cb(balanceRight, onMixerCommandClicked, LV_EVENT_PRESSED, remember(state, 3));

  auto* mixerMute = button(mixerPanel, "MUTE");
  lv_obj_set_size(mixerMute, 250, 74);
  lv_obj_align(mixerMute, LV_ALIGN_BOTTOM_LEFT, 24, -28);
  looperMixerMuteLabel_ = lv_obj_get_child(mixerMute, 0);
  lv_obj_add_event_cb(mixerMute, onMixerCommandClicked, LV_EVENT_PRESSED, remember(state, 4));
  auto* mixerClear = button(mixerPanel, "CLEAR TRACK");
  lv_obj_set_size(mixerClear, 250, 74);
  lv_obj_align(mixerClear, LV_ALIGN_BOTTOM_RIGHT, -24, -28);
  lv_obj_add_event_cb(mixerClear, onMixerCommandClicked, LV_EVENT_PRESSED, remember(state, 5));
  lv_obj_add_flag(looperMixerOverlay_, LV_OBJ_FLAG_HIDDEN);

  looperClearTrackOverlay_ = lv_obj_create(looperMixerOverlay_);
  lv_obj_set_size(looperClearTrackOverlay_, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(looperClearTrackOverlay_, 0, 0);
  lv_obj_set_style_bg_color(looperClearTrackOverlay_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(looperClearTrackOverlay_, LV_OPA_80, 0);
  lv_obj_set_style_border_width(looperClearTrackOverlay_, 0, 0);
  lv_obj_remove_flag(looperClearTrackOverlay_, LV_OBJ_FLAG_SCROLLABLE);
  auto* clearPanel = lv_obj_create(looperClearTrackOverlay_);
  lv_obj_set_size(clearPanel, 620, 250);
  lv_obj_align(clearPanel, LV_ALIGN_CENTER, 0, 0);
  styleSurface(clearPanel, panelAlt);
  lv_obj_remove_flag(clearPanel, LV_OBJ_FLAG_SCROLLABLE);
  label(clearPanel, "CLEAR SELECTED TRACK?", LV_ALIGN_TOP_MID, 0, 24,
        &ardor_font_saira_cond_semibold_28, text);
  label(clearPanel, "THIS REMOVES ITS AUDIO. THE OTHER TRACKS KEEP PLAYING.",
        LV_ALIGN_TOP_MID, 0, 76, &ardor_font_saira_cond_medium_18, muted);
  auto* clearCancel = button(clearPanel, "CANCEL");
  lv_obj_set_size(clearCancel, 170, 58);
  lv_obj_align(clearCancel, LV_ALIGN_BOTTOM_MID, -96, -24);
  lv_obj_add_event_cb(clearCancel, onClearTrackCancelClicked,
                      LV_EVENT_CLICKED, remember(state));
  auto* clearConfirm = button(clearPanel, "CLEAR");
  lv_obj_set_size(clearConfirm, 170, 58);
  lv_obj_align(clearConfirm, LV_ALIGN_BOTTOM_MID, 96, -24);
  lv_obj_add_event_cb(clearConfirm, onClearTrackConfirmClicked,
                      LV_EVENT_CLICKED, remember(state));
  lv_obj_add_flag(looperClearTrackOverlay_, LV_OBJ_FLAG_HIDDEN);

  syncLooperView(state);
}

void LvglUi::syncLooperView(const UiState& state)
{
  if (!looperPresetLabel_) return;
  const auto& looper = state.looper;
  const auto& telemetry = looper.telemetry;
  const auto preset = uppercase(looper.lockedPresetName.empty() ? "--" : looper.lockedPresetName);
  lv_label_set_text(looperPresetLabel_, ("LOCKED · " + preset).c_str());
  lv_label_set_text(looperPositionLabel_,
                    (clockText(telemetry.playheadFrame) + " / " + clockText(telemetry.masterFrames)).c_str());

  const auto memoryMiB = looper.memoryBudgetBytes / (1024 * 1024);
  const auto remainingFrames = telemetry.maximumFrames > telemetry.masterFrames
    ? telemetry.maximumFrames - telemetry.masterFrames : 0;
  const auto memoryText = std::to_string(memoryMiB) + " MB · "
    + clockText(remainingFrames) + " LEFT · "
    + (looper.ioBusy ? "SAVING" : looper.modified ? "MODIFIED" : "SAVED");
  lv_label_set_text(looperMemoryLabel_, memoryText.c_str());

  const bool hasNotice = looper.clearHoldProgress > 0.0f
    || telemetry.error != LooperError::None
    || telemetry.sessionState == LooperSessionState::Faulted
    || !state.statusMessage.empty();
  lv_label_set_text(looperNoticeLabel_, looperNoticeText(state).c_str());
  lv_obj_set_style_text_color(looperNoticeLabel_,
                              lv_color_hex(state.statusIsError || telemetry.error != LooperError::None
                                             ? warning : hasNotice ? text : muted), 0);

  const float phase = telemetry.masterFrames > 0
    ? static_cast<float>(telemetry.playheadFrame) / static_cast<float>(telemetry.masterFrames) : 0.0f;
  for (std::size_t index = 0; index < kLooperTrackCount; ++index) {
    if (!looperTrackPlates_[index]) continue;
    const auto& track = telemetry.tracks[index];
    const bool selected = index == looper.selectedTrack;
    const bool isRecording = recording(track.state);
    const bool isArmed = armed(track.state);
    const int stateColor = isRecording ? lamp : isArmed ? warning
      : track.state == LooperTrackState::Muted ? disabled
      : populated(track.state) ? text : disabled;
    lv_obj_set_style_border_width(looperTrackPlates_[index], selected ? 3 : 1, 0);
    lv_obj_set_style_border_color(looperTrackPlates_[index],
                                  lv_color_hex(selected ? text : rule), 0);
    lv_obj_set_style_bg_color(looperTrackHeaders_[index],
                              lv_color_hex(isRecording ? lamp : isArmed ? warning : panelAlt), 0);
    const auto headerChildCount = lv_obj_get_child_count(looperTrackHeaders_[index]);
    for (uint32_t childIndex = 0; childIndex < headerChildCount; ++childIndex) {
      lv_obj_set_style_text_color(
        lv_obj_get_child(looperTrackHeaders_[index], static_cast<int32_t>(childIndex)),
        lv_color_hex((isRecording || isArmed) ? bg : (childIndex == 0 ? text : muted)), 0);
    }
    lv_label_set_text(looperTrackStateLabels_[index], stateText(track.state));
    lv_obj_set_style_text_color(looperTrackStateLabels_[index], lv_color_hex(stateColor), 0);

    char detail[96]{};
    const char* undo = track.undoAvailable ? (track.undoApplied ? "UNDO APPLIED · " : "UNDO READY · ") : "";
    const char* pan = std::fabs(track.balance) < 0.01f ? "C"
      : track.balance < 0.0f ? "L" : "R";
    if (selected && looper.clearHoldProgress > 0.0f) {
      std::snprintf(detail, sizeof(detail), "CLEAR %d%% · RELEASE CANCELS",
                    static_cast<int>(std::lround(looper.clearHoldProgress * 100.0f)));
    } else {
      std::snprintf(detail, sizeof(detail), "%s%+.0f DB · %s", undo, track.levelDb, pan);
    }
    lv_label_set_text(looperTrackDetailLabels_[index], detail);
    const int progressWidth = std::max(0, lv_obj_get_width(looperTrackPlates_[index]) - 2);
    lv_obj_set_width(looperProgressFills_[index],
                     populated(track.state) || recording(track.state)
                       ? static_cast<int>(phase * progressWidth) : 0);
    lv_obj_set_style_bg_color(looperProgressFills_[index], lv_color_hex(stateColor), 0);
  }

  if (looperStopLabel_) {
    const bool paused = telemetry.sessionState == LooperSessionState::Paused;
    lv_label_set_text(looperStopLabel_, paused ? "RESUME" : "STOP ALL");
    if (!looper.ioBusy
        && (telemetry.sessionState == LooperSessionState::Running
            || telemetry.sessionState == LooperSessionState::Paused)) {
      lv_obj_remove_state(looperStopButton_, LV_STATE_DISABLED);
    } else {
      lv_obj_add_state(looperStopButton_, LV_STATE_DISABLED);
    }
  }
  const bool mayExit = telemetry.sessionState == LooperSessionState::Paused
    || telemetry.sessionState == LooperSessionState::EmptyPaused;
  if (mayExit && !looper.ioBusy) {
    lv_obj_remove_state(looperNewButton_, LV_STATE_DISABLED);
    lv_obj_remove_state(looperLoadButton_, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(looperNewButton_, LV_STATE_DISABLED);
    lv_obj_add_state(looperLoadButton_, LV_STATE_DISABLED);
  }
  if (!looper.ioBusy && telemetry.sessionState == LooperSessionState::Paused
      && telemetry.masterFrames > 0) {
    lv_obj_remove_state(looperSaveButton_, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(looperSaveButton_, LV_STATE_DISABLED);
  }
  if (mayExit) lv_obj_remove_state(looperExitButton_, LV_STATE_DISABLED);
  else lv_obj_add_state(looperExitButton_, LV_STATE_DISABLED);
  if (mayExit) lv_obj_remove_state(looperCloseButton_, LV_STATE_DISABLED);
  else lv_obj_add_state(looperCloseButton_, LV_STATE_DISABLED);

  if (looper.libraryOpen) lv_obj_remove_flag(looperLibraryOverlay_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(looperLibraryOverlay_, LV_OBJ_FLAG_HIDDEN);
  const auto pageCount = std::max<std::size_t>(
    1, (looper.library.size() + kLooperLibraryRows - 1) / kLooperLibraryRows);
  const auto page = std::min(looper.libraryPage, pageCount - 1);
  lv_label_set_text(looperLibraryPageLabel_,
                    ("PAGE " + std::to_string(page + 1) + " / " + std::to_string(pageCount)).c_str());
  if (page == 0) lv_obj_add_state(looperLibraryPreviousButton_, LV_STATE_DISABLED);
  else lv_obj_remove_state(looperLibraryPreviousButton_, LV_STATE_DISABLED);
  if (page + 1 >= pageCount) lv_obj_add_state(looperLibraryNextButton_, LV_STATE_DISABLED);
  else lv_obj_remove_state(looperLibraryNextButton_, LV_STATE_DISABLED);
  if (looper.library.empty()) lv_obj_remove_flag(looperLibraryEmptyLabel_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(looperLibraryEmptyLabel_, LV_OBJ_FLAG_HIDDEN);
  for (std::size_t row = 0; row < kLooperLibraryRows; ++row) {
    const auto index = page * kLooperLibraryRows + row;
    if (index >= looper.library.size()) {
      lv_obj_add_flag(looperLibraryRows_[row], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_remove_flag(looperLibraryRows_[row], LV_OBJ_FLAG_HIDDEN);
    const auto& entry = looper.library[index];
    lv_label_set_text(looperLibraryNameLabels_[row], uppercase(entry.name).c_str());
    const auto duration = clockText(entry.loopFrames);
    const auto tracks = std::to_string(entry.populatedTracks)
      + (entry.populatedTracks == 1 ? " TRACK" : " TRACKS");
    const auto detail = entry.available
      ? uppercase(entry.sourcePresetName) + " · " + duration + " · " + tracks + " · " + entry.savedAt
      : "UNAVAILABLE · " + uppercase(entry.unavailableReason);
    lv_label_set_text(looperLibraryMetaLabels_[row], detail.c_str());
    lv_obj_set_style_text_color(looperLibraryMetaLabels_[row],
                                lv_color_hex(entry.available ? muted : warning), 0);
    if (entry.available) lv_obj_remove_state(looperLibraryLoadButtons_[row], LV_STATE_DISABLED);
    else lv_obj_add_state(looperLibraryLoadButtons_[row], LV_STATE_DISABLED);
  }
  if (looper.deleteCandidateId) {
    auto candidate = std::find_if(looper.library.begin(), looper.library.end(), [&](const auto& entry) {
      return entry.id == *looper.deleteCandidateId;
    });
    lv_label_set_text(looperDeleteNameLabel_,
                      candidate == looper.library.end() ? "THIS LOOP SET"
                                                        : uppercase(candidate->name).c_str());
    lv_obj_remove_flag(looperDeleteOverlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(looperDeleteOverlay_);
  } else {
    lv_obj_add_flag(looperDeleteOverlay_, LV_OBJ_FLAG_HIDDEN);
  }

  const auto selected = std::min(looper.selectedTrack, kLooperTrackCount - 1);
  const auto& selectedTrack = telemetry.tracks[selected];
  const bool showMixer = looper.mixerOpen && populated(selectedTrack.state);
  if (showMixer) lv_obj_remove_flag(looperMixerOverlay_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(looperMixerOverlay_, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(looperMixerTitleLabel_,
                    ("TRACK " + std::to_string(selected + 1) + " MIX").c_str());
  char levelText[24]{};
  std::snprintf(levelText, sizeof(levelText), "%+.0f DB", selectedTrack.levelDb);
  lv_label_set_text(looperMixerLevelLabel_, levelText);
  const auto balanceText = std::fabs(selectedTrack.balance) < 0.01f ? "CENTER"
    : selectedTrack.balance < 0.0f
      ? "L " + std::to_string(static_cast<int>(std::lround(-selectedTrack.balance * 100.0f)))
      : "R " + std::to_string(static_cast<int>(std::lround(selectedTrack.balance * 100.0f)));
  lv_label_set_text(looperMixerBalanceLabel_, balanceText.c_str());
  lv_label_set_text(looperMixerMuteLabel_, selectedTrack.audible ? "MUTE" : "UNMUTE");
  if (looper.clearTrackConfirmationOpen) {
    lv_obj_remove_flag(looperClearTrackOverlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(looperClearTrackOverlay_);
  } else {
    lv_obj_add_flag(looperClearTrackOverlay_, LV_OBJ_FLAG_HIDDEN);
  }
}

} // namespace ardor
