#include "ui/LvglUi.h"

#include "ui/LampBlack.h"
#include "ui/LvglUiStyle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace ardor {
namespace {

using namespace lvgl_ui;

// THESIS: four synchronized tracks read as one stage instrument, never as a waveform editor.
// OWN-WORLD: hard graphite plates, bone engraving, amber armed headers, one live-red record lamp.
// STORY: glance at phase and track state, then act with the matching physical corner switch.
// FIRST VIEWPORT: locked preset rail, physical 1/2/3/4 plate map, fixed transport rail.
// FORM: established Ardor Panel language extended by the approved looper specification.

// Lamp Black looper: the preset map's 2 x 2 plates (shorter, to leave a
// notice line above the rail), the 64 px header and the 108 px rail.
constexpr int kTrackX = lb::kGutter;
constexpr int kTrackY = 80;
constexpr int kTrackWidth = 610;
constexpr int kTrackHeight = 236;
constexpr int kTrackGap = 12;
constexpr int kTrackPadX = 26;
constexpr int kTrackTitleTop = 19;
constexpr int kTrackStateTop = 58;
constexpr int kTrackDetailTop = 158;
constexpr int kTrackProgressBottom = 22;
constexpr int kTrackProgressHeight = 8;
constexpr int kNoticeTop = kTrackY + 2 * kTrackHeight + kTrackGap + 14;
constexpr std::size_t kLibraryRows = 4;
using lb::createDialog;
using lb::createOverlay;
using lb::dialogActions;
using lb::dialogBody;
using lb::kDialogInset;

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
  lb::box(root, 0, 0, kDesignWidth, kDesignHeight, bg);
  lb::header(root);
  lb::textLabel(root, lb::type::headerTitle, "LOOPER", text, 28, 9);
  looperPresetLabel_ = lb::textLabel(root, lb::type::headerSub, "LOCKED · --", muted,
                                     28 + lb::textWidth(lb::type::headerTitle, "LOOPER") + 20, 13);
  looperPositionLabel_ = lb::textLabel(root, lb::type::headerTitle, "00:00 / 00:00", text, 0, 9);
  looperMemoryLabel_ = lb::textLabel(root, lb::type::headerRight, "128 MB", disabled, 0, 18);

  const std::array<const char*, kLooperTrackCount> legends = {
    "FS1 · UNDO", "FS2 · TRACK / HOLD CLEAR", "FS3 · REC / DUB", "FS4 · PLAY / MUTE"
  };
  for (std::size_t index = 0; index < kLooperTrackCount; ++index) {
    // Column-major, as on the preset map: FS1/FS2 left, FS3/FS4 right.
    const int column = static_cast<int>(index / 2);
    const int row = static_cast<int>(index % 2);
    auto* plate = lv_button_create(root);
    lv_obj_remove_style_all(plate);
    lv_obj_remove_flag(plate, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(plate, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_pos(plate, kTrackX + column * (kTrackWidth + kTrackGap),
                   kTrackY + row * (kTrackHeight + kTrackGap));
    lv_obj_set_size(plate, kTrackWidth, kTrackHeight);
    lv_obj_set_style_bg_opa(plate, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(plate, lv_color_hex(panel), 0);
    lb::setBorder(plate, rule, 1);
    looperTrackPlates_[index] = plate;
    lv_obj_add_event_cb(plate, onTrackClicked, LV_EVENT_CLICKED, remember(state, index));

    // The header container keeps the title and footswitch legend together
    // so the sync can recolour them as one.
    auto* header = lv_obj_create(plate);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, kTrackWidth - 2, 50);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);
    looperTrackHeaders_[index] = header;
    lb::textLabel(header, lb::type::footswitch, "TRACK " + std::to_string(index + 1), disabled,
                  kTrackPadX, kTrackTitleTop);
    lv_obj_t* legend = lb::textLabel(header, lb::type::legend, legends[index], disabled, 0,
                                     kTrackTitleTop + 2);
    lv_obj_set_x(legend, kTrackWidth - 2 - kTrackPadX - lb::textWidth(lb::type::legend, legends[index]));

    looperTrackStateLabels_[index] = lb::textLabel(plate, lb::type::presetName, "EMPTY", disabled,
                                                   kTrackPadX, kTrackStateTop);
    looperTrackDetailLabels_[index] = lb::textLabel(plate, lb::type::legend, "0 DB · C", muted,
                                                    kTrackPadX, kTrackDetailTop);

    const int progressWidth = kTrackWidth - 2 - 2 * kTrackPadX;
    auto* progressTrack = lb::box(plate, kTrackPadX,
      kTrackHeight - 2 - kTrackProgressBottom - kTrackProgressHeight, progressWidth,
      kTrackProgressHeight, plateHi);
    looperProgressFills_[index] = lb::box(progressTrack, 0, 0, 0, kTrackProgressHeight, text);
  }

  looperNoticeLabel_ = lb::textLabel(root, lb::type::legend,
                                     "TRACK 1 SELECTED · FS2 NEXT · HOLD FS2 CLEAR", muted, 0,
                                     kNoticeTop);
  lv_obj_set_width(looperNoticeLabel_, kDesignWidth - 2 * lb::kGutter);
  lv_obj_set_x(looperNoticeLabel_, lb::kGutter);
  lv_obj_set_style_text_align(looperNoticeLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(looperNoticeLabel_, LV_LABEL_LONG_CLIP);

  lb::rail(root);
  int railX = lb::kGutter;
  const auto leftButton = [&](const char* legend) {
    lv_obj_t* btn = lb::button(root, legend, lb::ButtonKind::Normal, railX, lb::kRailButtonY);
    railX += lv_obj_get_style_width(btn, LV_PART_MAIN) + lb::kGap;
    return btn;
  };
  looperNewButton_ = leftButton("NEW");
  auto* newContext = remember(state);
  lv_obj_add_event_cb(looperNewButton_, onNewClicked, LV_EVENT_PRESSED, newContext);
  looperSaveButton_ = leftButton("SAVE");
  lv_obj_add_event_cb(looperSaveButton_, onSaveClicked, LV_EVENT_PRESSED, remember(state));
  looperLoadButton_ = leftButton("LOAD");
  lv_obj_add_event_cb(looperLoadButton_, onLoadClicked, LV_EVENT_PRESSED, remember(state));

  // Right group: Close rightmost, then Exit, then the transport.
  int rightX = kDesignWidth - lb::kGutter;
  const auto rightButton = [&](const char* legend, lb::ButtonKind kind, int width = 0) {
    const int w = width > 0 ? width : lb::buttonWidth(legend);
    rightX -= w;
    lv_obj_t* btn = lb::button(root, legend, kind, rightX, lb::kRailButtonY, w);
    rightX -= lb::kGap;
    return btn;
  };
  auto* closeButton = rightButton("CLOSE", lb::ButtonKind::Normal);
  looperCloseButton_ = closeButton;
  auto* closeContext = remember(state);
  lv_obj_add_event_cb(closeButton, onCloseClicked, LV_EVENT_PRESSED, closeContext);
  looperExitButton_ = rightButton("EXIT", lb::ButtonKind::Normal);
  lv_obj_add_event_cb(looperExitButton_, onExitClicked, LV_EVENT_PRESSED, remember(state));
  looperStopButton_ = rightButton("STOP ALL", lb::ButtonKind::Primary, lb::buttonWidth("STOP ALL"));
  looperStopLabel_ = lb::buttonLabel(looperStopButton_);
  lv_obj_add_event_cb(looperStopButton_, onStopClicked, LV_EVENT_PRESSED, remember(state));

  // ---- dialogs ----
  constexpr int kConfirmWidth = 660;
  constexpr int kConfirmHeight = 250;
  looperCloseOverlay_ = createOverlay(root);
  auto* confirmation = createDialog(looperCloseOverlay_, kConfirmWidth, kConfirmHeight,
                                    "DISCARD UNSAVED LOOP?");
  dialogBody(confirmation, "SAVE KEEPS THE LOOP OPEN; PRESS CLOSE AGAIN AFTER SAVING.",
             kConfirmWidth);
  auto closeActions = dialogActions(confirmation, kConfirmWidth, kConfirmHeight,
    {{"CANCEL", lb::ButtonKind::Normal}, {"SAVE", lb::ButtonKind::Normal},
     {"DISCARD", lb::ButtonKind::Danger}});
  auto* cancelContext = remember(state);
  cancelContext->controlledObject = looperCloseOverlay_;
  lv_obj_add_event_cb(closeActions[0], onCancelCloseClicked, LV_EVENT_CLICKED, cancelContext);
  auto* saveCloseContext = remember(state);
  saveCloseContext->controlledObject = looperCloseOverlay_;
  lv_obj_add_event_cb(closeActions[1], onSaveCloseClicked, LV_EVENT_CLICKED, saveCloseContext);
  auto* discardContext = remember(state);
  discardContext->controlledObject = looperCloseOverlay_;
  lv_obj_add_event_cb(closeActions[2], onDiscardCloseClicked, LV_EVENT_CLICKED, discardContext);
  closeContext->controlledObject = looperCloseOverlay_;
  lv_obj_add_flag(looperCloseOverlay_, LV_OBJ_FLAG_HIDDEN);

  looperNewOverlay_ = createOverlay(root);
  auto* newPanel = createDialog(looperNewOverlay_, kConfirmWidth, kConfirmHeight,
                                "START A NEW LOOP?");
  dialogBody(newPanel, "SAVE KEEPS THIS LOOP OPEN; PRESS NEW AGAIN AFTER SAVING.", kConfirmWidth);
  auto newActions = dialogActions(newPanel, kConfirmWidth, kConfirmHeight,
    {{"CANCEL", lb::ButtonKind::Normal}, {"SAVE", lb::ButtonKind::Normal},
     {"DISCARD", lb::ButtonKind::Danger}});
  auto* newCancelContext = remember(state);
  newCancelContext->controlledObject = looperNewOverlay_;
  lv_obj_add_event_cb(newActions[0], onCancelCloseClicked, LV_EVENT_CLICKED, newCancelContext);
  auto* newSaveContext = remember(state);
  newSaveContext->controlledObject = looperNewOverlay_;
  lv_obj_add_event_cb(newActions[1], onSaveThenRetryClicked, LV_EVENT_CLICKED, newSaveContext);
  auto* newDiscardContext = remember(state);
  newDiscardContext->controlledObject = looperNewOverlay_;
  lv_obj_add_event_cb(newActions[2], onDiscardNewClicked, LV_EVENT_CLICKED, newDiscardContext);
  newContext->controlledObject = looperNewOverlay_;
  lv_obj_add_flag(looperNewOverlay_, LV_OBJ_FLAG_HIDDEN);

  constexpr int kLibraryWidth = 1080;
  constexpr int kLibraryHeight = 640;
  constexpr int kLibraryRowHeight = 100;
  constexpr int kLibraryRowGap = 12;
  constexpr int kLibraryRowTop = 96;
  looperLibraryOverlay_ = createOverlay(root);
  auto* libraryPanel = createDialog(looperLibraryOverlay_, kLibraryWidth, kLibraryHeight, "SAVED LOOPS");
  auto* libraryClose = lb::button(libraryPanel, "CLOSE", lb::ButtonKind::Normal,
                                  kLibraryWidth - 2 - kDialogInset - lb::kButtonMinWidth, 20);
  lv_obj_add_event_cb(libraryClose, onLibraryCloseClicked, LV_EVENT_CLICKED, remember(state));
  const int rowWidth = kLibraryWidth - 2 - 2 * kDialogInset;
  for (std::size_t row = 0; row < kLooperLibraryRows; ++row) {
    auto* plate = lb::box(libraryPanel, kDialogInset - 1,
                          kLibraryRowTop + static_cast<int>(row) * (kLibraryRowHeight + kLibraryRowGap),
                          rowWidth, kLibraryRowHeight, bg, rule, 1);
    looperLibraryRows_[row] = plate;
    looperLibraryNameLabels_[row] = lb::textLabel(plate, lb::type::itemTitle, "--", text, 19, 16);
    looperLibraryMetaLabels_[row] = lb::textLabel(plate, lb::type::itemSubtitle, "--", muted, 19, 54);
    lv_obj_set_width(looperLibraryNameLabels_[row], rowWidth - 340);
    lv_obj_set_width(looperLibraryMetaLabels_[row], rowWidth - 340);
    lv_label_set_long_mode(looperLibraryNameLabels_[row], LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_long_mode(looperLibraryMetaLabels_[row], LV_LABEL_LONG_MODE_DOTS);
    const int buttonY = (kLibraryRowHeight - 2 - lb::kButtonHeight) / 2;
    auto* remove = lb::button(plate, "DELETE", lb::ButtonKind::Danger,
                              rowWidth - 2 - 19 - lb::kButtonMinWidth, buttonY);
    auto* load = lb::button(plate, "LOAD", lb::ButtonKind::Normal,
                            rowWidth - 2 - 19 - 2 * lb::kButtonMinWidth - lb::kGap, buttonY);
    looperLibraryLoadButtons_[row] = load;
    looperLibraryDeleteButtons_[row] = remove;
    lv_obj_add_event_cb(load, onLibraryLoadClicked, LV_EVENT_CLICKED, remember(state, row));
    lv_obj_add_event_cb(remove, onLibraryDeleteClicked, LV_EVENT_CLICKED, remember(state, row));
  }
  const std::string empty = "NO SAVED LOOPS · SAVE A PAUSED LOOP FIRST";
  looperLibraryEmptyLabel_ = lb::textLabel(libraryPanel, lb::type::controlLabel, empty, muted,
    (kLibraryWidth - 2 - lb::textWidth(lb::type::controlLabel, empty)) / 2, 280);
  const int pagerY = kLibraryHeight - 2 - 24 - lb::kButtonHeight;
  looperLibraryPreviousButton_ = lb::button(libraryPanel, "PREVIOUS", lb::ButtonKind::Normal,
                                            kDialogInset - 1, pagerY);
  lv_obj_add_event_cb(looperLibraryPreviousButton_, onLibraryPageClicked,
                      LV_EVENT_CLICKED, remember(state, 0));
  looperLibraryNextButton_ = lb::button(libraryPanel, "NEXT", lb::ButtonKind::Normal,
                                        kLibraryWidth - 2 - kDialogInset - lb::kButtonMinWidth, pagerY);
  lv_obj_add_event_cb(looperLibraryNextButton_, onLibraryPageClicked,
                      LV_EVENT_CLICKED, remember(state, 1));
  looperLibraryPageLabel_ = lb::textLabel(libraryPanel, lb::type::page, "PAGE 1 / 1", muted, 0, 0);
  lv_obj_set_width(looperLibraryPageLabel_, kLibraryWidth - 2);
  lv_obj_set_style_text_align(looperLibraryPageLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_y(looperLibraryPageLabel_, lb::centeredTextTop(lb::type::page, pagerY, lb::kButtonHeight));
  lv_obj_add_flag(looperLibraryOverlay_, LV_OBJ_FLAG_HIDDEN);

  looperDeleteOverlay_ = createOverlay(looperLibraryOverlay_);
  auto* deletePanel = createDialog(looperDeleteOverlay_, kConfirmWidth, kConfirmHeight,
                                   "DELETE SAVED LOOP?");
  looperDeleteNameLabel_ = dialogBody(deletePanel, "--", kConfirmWidth);
  auto deleteActions = dialogActions(deletePanel, kConfirmWidth, kConfirmHeight,
    {{"CANCEL", lb::ButtonKind::Normal}, {"DELETE", lb::ButtonKind::Danger}});
  lv_obj_add_event_cb(deleteActions[0], onLibraryDeleteCancelClicked,
                      LV_EVENT_CLICKED, remember(state));
  lv_obj_add_event_cb(deleteActions[1], onLibraryDeleteConfirmClicked,
                      LV_EVENT_CLICKED, remember(state));
  lv_obj_add_flag(looperDeleteOverlay_, LV_OBJ_FLAG_HIDDEN);

  // Track mix: two control cards (level, balance) over Mute and Clear.
  constexpr int kMixerWidth = 900;
  constexpr int kMixerHeight = 440;
  constexpr int kMixerCardTop = 100;
  constexpr int kMixerCardHeight = 196;
  looperMixerOverlay_ = createOverlay(root);
  auto* mixerPanel = createDialog(looperMixerOverlay_, kMixerWidth, kMixerHeight, "TRACK 1 MIX");
  looperMixerTitleLabel_ = lv_obj_get_child(mixerPanel, 0);
  auto* mixerClose = lb::button(mixerPanel, "CLOSE", lb::ButtonKind::Normal,
                                kMixerWidth - 2 - kDialogInset - lb::kButtonMinWidth, 20);
  lv_obj_add_event_cb(mixerClose, onMixerCloseClicked, LV_EVENT_CLICKED, remember(state));
  const int cardWidth = (kMixerWidth - 2 - 2 * kDialogInset - lb::kGap) / 2;
  const auto mixerCard = [&](int column, const char* title, const char* down, const char* up,
                             std::size_t downIndex, std::size_t upIndex, lv_obj_t** valueOut) {
    lv_obj_t* card = lb::box(mixerPanel, kDialogInset - 1 + column * (cardWidth + lb::kGap),
                             kMixerCardTop, cardWidth, kMixerCardHeight, bg, rule, 1);
    lb::textLabel(card, lb::type::controlLabel, title, muted, 20, 15);
    *valueOut = lb::textLabel(card, lb::type::contextValue, "", text, 20, 46);
    const int buttonY = kMixerCardHeight - 2 - 20 - lb::kButtonHeight;
    const int buttonWidth = (cardWidth - 2 - 40 - lb::kGap) / 2;
    auto* downButton = lb::button(card, down, lb::ButtonKind::Normal, 20, buttonY, buttonWidth);
    lv_obj_add_event_cb(downButton, onMixerCommandClicked, LV_EVENT_PRESSED,
                        remember(state, downIndex));
    auto* upButton = lb::button(card, up, lb::ButtonKind::Normal, 20 + buttonWidth + lb::kGap,
                                buttonY, buttonWidth);
    lv_obj_add_event_cb(upButton, onMixerCommandClicked, LV_EVENT_PRESSED, remember(state, upIndex));
  };
  mixerCard(0, "LEVEL", "-1 DB", "+1 DB", 0, 1, &looperMixerLevelLabel_);
  mixerCard(1, "STEREO BALANCE", "LEFT", "RIGHT", 2, 3, &looperMixerBalanceLabel_);
  const int mixerActionY = kMixerHeight - 2 - 28 - lb::kButtonHeight;
  auto* mixerMute = lb::button(mixerPanel, "UNMUTE", lb::ButtonKind::Normal, kDialogInset - 1,
                               mixerActionY);
  lb::setButtonText(mixerMute, "MUTE");
  looperMixerMuteLabel_ = lb::buttonLabel(mixerMute);
  lv_obj_add_event_cb(mixerMute, onMixerCommandClicked, LV_EVENT_PRESSED, remember(state, 4));
  const int clearWidth = lb::buttonWidth("CLEAR TRACK");
  auto* mixerClear = lb::button(mixerPanel, "CLEAR TRACK", lb::ButtonKind::Danger,
                                kMixerWidth - 2 - kDialogInset - clearWidth, mixerActionY, clearWidth);
  lv_obj_add_event_cb(mixerClear, onMixerCommandClicked, LV_EVENT_PRESSED, remember(state, 5));
  lv_obj_add_flag(looperMixerOverlay_, LV_OBJ_FLAG_HIDDEN);

  looperClearTrackOverlay_ = createOverlay(looperMixerOverlay_);
  auto* clearPanel = createDialog(looperClearTrackOverlay_, kConfirmWidth, kConfirmHeight,
                                  "CLEAR SELECTED TRACK?");
  dialogBody(clearPanel, "THIS REMOVES ITS AUDIO. THE OTHER TRACKS KEEP PLAYING.", kConfirmWidth);
  auto clearActions = dialogActions(clearPanel, kConfirmWidth, kConfirmHeight,
    {{"CANCEL", lb::ButtonKind::Normal}, {"CLEAR", lb::ButtonKind::Danger}});
  lv_obj_add_event_cb(clearActions[0], onClearTrackCancelClicked, LV_EVENT_CLICKED, remember(state));
  lv_obj_add_event_cb(clearActions[1], onClearTrackConfirmClicked, LV_EVENT_CLICKED, remember(state));
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
  const auto position = clockText(telemetry.playheadFrame) + " / " + clockText(telemetry.masterFrames);
  lv_label_set_text(looperPositionLabel_, position.c_str());
  const int positionX = kDesignWidth - 28 - lb::textWidth(lb::type::headerTitle, position);
  lv_obj_set_x(looperPositionLabel_, positionX);

  const auto memoryMiB = looper.memoryBudgetBytes / (1024 * 1024);
  const auto remainingFrames = telemetry.maximumFrames > telemetry.masterFrames
    ? telemetry.maximumFrames - telemetry.masterFrames : 0;
  const auto memoryText = std::to_string(memoryMiB) + " MB · "
    + clockText(remainingFrames) + " LEFT · "
    + (looper.ioBusy ? "SAVING" : looper.modified ? "MODIFIED" : "SAVED");
  lv_label_set_text(looperMemoryLabel_, memoryText.c_str());
  lv_obj_set_x(looperMemoryLabel_, positionX - 24 - lb::textWidth(lb::type::headerRight, memoryText));

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
    // A recording track floods with the lamp, like the LIVE preset tile.
    const std::uint32_t stateColor = isRecording ? lampInk : isArmed ? warning
      : track.state == LooperTrackState::Muted ? disabled
      : populated(track.state) ? text : disabled;
    lv_obj_t* plate = looperTrackPlates_[index];
    lv_obj_set_style_bg_color(plate, lv_color_hex(isRecording ? lamp : panel), 0);
    const int border = selected ? 3 : 1;
    lv_obj_set_style_border_width(plate, border, 0);
    lv_obj_set_style_border_color(plate, lv_color_hex(
      selected ? (isRecording ? lampInk : text) : (isRecording ? lamp : isArmed ? warning : rule)), 0);
    // Children sit inside the border; keep them on the 1 px grid.
    lv_obj_set_pos(looperTrackHeaders_[index], 1 - border, 1 - border);
    const auto headerChildCount = lv_obj_get_child_count(looperTrackHeaders_[index]);
    for (uint32_t childIndex = 0; childIndex < headerChildCount; ++childIndex) {
      lv_obj_set_style_text_color(
        lv_obj_get_child(looperTrackHeaders_[index], static_cast<int32_t>(childIndex)),
        lv_color_hex(isRecording ? lampInk : isArmed ? warning : disabled), 0);
    }
    lv_obj_t* stateLabel = looperTrackStateLabels_[index];
    lv_label_set_text(stateLabel, stateText(track.state));
    // Long states drop to the smaller face so they stay inside the plate.
    const auto& stateType = lb::textWidth(lb::type::presetName, stateText(track.state))
        <= kTrackWidth - 2 * kTrackPadX ? lb::type::presetName : lb::type::masterValue;
    lb::applyType(stateLabel, stateType, stateColor);
    const int stateBaseline = kTrackStateTop + lb::type::presetName.size * lb::kSairaAscent / 1000;
    lv_obj_set_pos(stateLabel, kTrackPadX - border,
                   stateBaseline - (lv_font_get_line_height(stateType.font) - stateType.font->base_line)
                     - border);
    lv_obj_set_pos(looperTrackDetailLabels_[index], kTrackPadX - border,
                   lb::textTop(lb::type::legend, kTrackDetailTop) - border);
    lv_obj_set_style_text_color(looperTrackDetailLabels_[index],
                                lv_color_hex(isRecording ? lampInk : muted), 0);
    lv_obj_t* progressTrack = lv_obj_get_parent(looperProgressFills_[index]);
    lv_obj_set_pos(progressTrack, kTrackPadX - border,
                   kTrackHeight - 1 - border - kTrackProgressBottom - kTrackProgressHeight);
    // On the flooded tile the track is a 30 % ink tint and the fill full ink.
    lv_obj_set_style_bg_color(progressTrack, isRecording
      ? lv_color_mix(lv_color_hex(lampInk), lv_color_hex(lamp), 77) : lv_color_hex(plateHi), 0);

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
    const int progressWidth = kTrackWidth - 2 - 2 * kTrackPadX;
    lv_obj_set_width(looperProgressFills_[index],
                     populated(track.state) || recording(track.state)
                       ? static_cast<int>(phase * progressWidth) : 0);
    lv_obj_set_style_bg_color(looperProgressFills_[index],
                              lv_color_hex(isRecording ? lampInk : isArmed ? warning : stateColor), 0);
  }

  if (looperStopLabel_) {
    const bool paused = telemetry.sessionState == LooperSessionState::Paused;
    lb::setButtonText(looperStopButton_, paused ? "RESUME" : "STOP ALL");
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
  lb::setButtonText(lv_obj_get_parent(looperMixerMuteLabel_), selectedTrack.audible ? "MUTE" : "UNMUTE");
  if (looper.clearTrackConfirmationOpen) {
    lv_obj_remove_flag(looperClearTrackOverlay_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(looperClearTrackOverlay_);
  } else {
    lv_obj_add_flag(looperClearTrackOverlay_, LV_OBJ_FLAG_HIDDEN);
  }
}

} // namespace ardor
