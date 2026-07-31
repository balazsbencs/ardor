#include "ui/LvglUi.h"

#include "ui/LvglUiNavigation.h"
#include "ui/LvglUiStatus.h"
#include "ui/LvglUiStyle.h"
#include "ui/fonts/OpenSansSemibold.h"

#include <array>
#include <chrono>
#include <string>
#include <utility>

namespace ardor {

using namespace lvgl_navigation;
using namespace lvgl_ui;

LvglUi::LvglUi(UiActions actions)
  : actions_(std::move(actions))
{
}

UiEventContext* LvglUi::remember(UiState& state, std::size_t index, std::string filter)
{
  contexts_.emplace_back();
  auto& context = contexts_.back();
  context.ui = this;
  context.state = &state;
  context.index = index;
  context.filter = std::move(filter);
  context.region = contextRegion_;
  return &context;
}

void LvglUi::build(lv_obj_t* root, UiState& state)
{
  accent = state.settings.accentColor <= 0xffffffu
    ? state.settings.accentColor : kDefaultAccentColor;
  viewsInitialized_ = false;
  pendingChanges_ = UiChange::None;
  focusedControl_ = nullptr;
  focusedEqGraph_ = nullptr;
  parameterViews_.clear();
  activeParameterLayer_ = nullptr;
  if (state.mode == UiMode::Preset || !state.paramDrawerOpen) {
    resetParameterPage();
  }
  lv_obj_clean(root);
  contexts_.clear();
  lv_obj_set_style_bg_color(root, lv_color_hex(bg), 0);

  // The UI is authored on a 1280x720 design grid. Rather than re-flow every
  // widget for the panel, build it on a fixed 1280x720 canvas and scale that
  // uniformly to fill the active display. LVGL inverse-transforms pointer input
  // for hit-testing, so touches still land; fonts and paddings scale for free.
  // The screen and canvas must never scroll: a scrollable ancestor wins gesture
  // arbitration on a jittery finger touch and cancels child clicks/drags.
  lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* canvas = lv_obj_create(root);
  lv_obj_remove_style_all(canvas);
  lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(canvas, kDesignWidth, kDesignHeight);

  lv_display_t* display = lv_obj_get_display(root);
  const int32_t dispW = lv_display_get_horizontal_resolution(display);
  const int32_t dispH = lv_display_get_vertical_resolution(display);
  const int32_t scale =
    LV_MIN((dispW * 256) / kDesignWidth, (dispH * 256) / kDesignHeight);
  const int32_t offsetX = (dispW - (kDesignWidth * scale) / 256) / 2;
  const int32_t offsetY = (dispH - (kDesignHeight * scale) / 256) / 2;
  lv_obj_set_style_transform_pivot_x(canvas, 0, 0);
  lv_obj_set_style_transform_pivot_y(canvas, 0, 0);
  lv_obj_set_style_transform_scale(canvas, scale, 0);
  lv_obj_set_pos(canvas, offsetX, offsetY);

  canvas_ = canvas;
  canvasScale_ = scale;
  canvasOffset_ = {offsetX, offsetY};

  const auto createLayer = [canvas]() {
    lv_obj_t* layer = lv_obj_create(canvas);
    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, kDesignWidth, kDesignHeight);
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(layer, LV_OBJ_FLAG_CLICKABLE);
    return layer;
  };
  presetLayer_ = createLayer();
  editLayer_ = createLayer();
  tunerLayer_ = createLayer();
  parameterLayer_ = createLayer();
  drawerLayer_ = createLayer();
  statusLayer_ = createLayer();
  settingsLayer_ = createLayer();
  previewOverlay_ = lv_obj_create(canvas);
  lv_obj_set_size(previewOverlay_, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(previewOverlay_, 0, 0);
  lv_obj_set_style_bg_color(previewOverlay_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(previewOverlay_, LV_OPA_70, 0);
  lv_obj_set_style_border_width(previewOverlay_, 0, 0);
  lv_obj_remove_flag(previewOverlay_, LV_OBJ_FLAG_SCROLLABLE);
  previewOverlayLabel_ = lv_label_create(previewOverlay_);
  lv_label_set_text(previewOverlayLabel_, "Applying effect chain...\n\n◌");
  lv_obj_set_style_text_color(previewOverlayLabel_, lv_color_hex(text), 0);
  lv_obj_set_style_text_align(previewOverlayLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(previewOverlayLabel_);
  lv_obj_add_flag(previewOverlay_, LV_OBJ_FLAG_HIDDEN);
  navigationOverlay_ = lv_obj_create(canvas);
  lv_obj_set_size(navigationOverlay_, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(navigationOverlay_, 0, 0);
  lv_obj_set_style_bg_color(navigationOverlay_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(navigationOverlay_, LV_OPA_70, 0);
  lv_obj_set_style_border_width(navigationOverlay_, 0, 0);
  lv_obj_remove_flag(navigationOverlay_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* prompt = lv_label_create(navigationOverlay_);
  lv_label_set_text(prompt, "Unsaved changes");
  lv_obj_set_style_text_color(prompt, lv_color_hex(text), 0);
  lv_obj_set_style_text_font(prompt, &ardor_font_open_sans_semibold_22, 0);
  lv_obj_align(prompt, LV_ALIGN_CENTER, 0, -92);
  lv_obj_t* guidance = lv_label_create(navigationOverlay_);
  lv_label_set_text(guidance, "Save changes before switching presets?");
  lv_obj_set_style_text_color(guidance, lv_color_hex(muted), 0);
  lv_obj_align(guidance, LV_ALIGN_CENTER, 0, -52);
  const std::array<std::string, 3> choices = {"Save", "Discard", "Cancel"};
  for (std::size_t i = 0; i < choices.size(); ++i) {
    lv_obj_t* choice = button(navigationOverlay_, choices[i]);
    lv_obj_set_size(choice, 150, 56);
    lv_obj_align(choice, LV_ALIGN_CENTER, static_cast<int>(i) * 166 - 166, 18);
    if (i == 0) styleSurface(choice, 0x25442a);
    lv_obj_add_event_cb(choice, onNavigationDecision, LV_EVENT_CLICKED, remember(state, i));
  }
  lv_obj_add_flag(navigationOverlay_, LV_OBJ_FLAG_HIDDEN);

  rebuildPresetView(state);
  rebuildEditView(state);
  contextRegion_ = UiContextRegion::Tuner;
  renderTunerMode(tunerLayer_, state);
  contextRegion_ = UiContextRegion::None;
  rebuildParameterView(state);
  rebuildDrawerView(state);
  contextRegion_ = UiContextRegion::Status;
  renderStatusBar(this, statusLayer_, state, &telemetryLabel_, &masterVolumeLabel_,
                  &statusMessageLabel_, &undoButton_);
  contextRegion_ = UiContextRegion::None;
  contextRegion_ = UiContextRegion::Settings;
  renderSettingsView(settingsLayer_, state);
  contextRegion_ = UiContextRegion::None;

  renderedRevisions_ = state.revisions;
  renderedBank_ = state.activeBank;
  renderedPreset_ = state.activePreset;
  viewsInitialized_ = true;
  syncPersistentViews(state);
}





void LvglUi::syncModeVisibility(const UiState& state)
{
  if (!viewsInitialized_) return;
  if (state.mode == UiMode::Preset) {
    lv_obj_remove_flag(presetLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(editLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tunerLayer_, LV_OBJ_FLAG_HIDDEN);
  } else if (state.mode == UiMode::Edit) {
    lv_obj_add_flag(presetLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(editLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tunerLayer_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(presetLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(editLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(tunerLayer_, LV_OBJ_FLAG_HIDDEN);
  }

  const bool showParameters = state.mode == UiMode::Edit && state.paramDrawerOpen;
  if (showParameters) lv_obj_remove_flag(parameterLayer_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(parameterLayer_, LV_OBJ_FLAG_HIDDEN);
  const bool showDrawer = state.mode == UiMode::Edit && state.blockDrawerOpen;
  if (showDrawer) lv_obj_remove_flag(drawerLayer_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(drawerLayer_, LV_OBJ_FLAG_HIDDEN);
  if (state.mode == UiMode::Tuner) lv_obj_add_flag(statusLayer_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(statusLayer_, LV_OBJ_FLAG_HIDDEN);
}




void LvglUi::syncPersistentViews(UiState& state)
{
  if (!viewsInitialized_) return;
  syncModeVisibility(state);
  syncTunerView(state);
  syncHeaderView(state);
  syncPresetCards(state);
  syncStatusView(state);
  syncBlockingOverlays(state);
}

void LvglUi::syncBlockingOverlays(const UiState& state)
{
  if (previewOverlay_) {
    if (previewIsSynchronized(state)) lv_obj_add_flag(previewOverlay_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(previewOverlay_, LV_OBJ_FLAG_HIDDEN);
  }
  if (navigationOverlay_) {
    if (state.navigationPrompt.has_value()) lv_obj_remove_flag(navigationOverlay_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(navigationOverlay_, LV_OBJ_FLAG_HIDDEN);
  }
}

lv_point_t LvglUi::toCanvas(lv_point_t displayPoint) const
{
  const int32_t scale = canvasScale_ == 0 ? 256 : canvasScale_;
  return {((displayPoint.x - canvasOffset_.x) * 256) / scale,
          ((displayPoint.y - canvasOffset_.y) * 256) / scale};
}

void LvglUi::refresh(lv_obj_t* root, UiState& state)
{
  if (!highlightedBlockId_.empty() && std::chrono::steady_clock::now() >= highlightUntil_) {
    highlightedBlockId_.clear();
    invalidate(UiChange::Chain);
  }
  if (!viewsInitialized_ || root != lv_obj_get_parent(canvas_)) {
    build(root, state);
    return;
  }

  UiChange changes = pendingChanges_;
  const auto add = [&changes](UiChange change) { changes = changes | change; };
  if (state.revisions.navigation != renderedRevisions_.navigation) add(UiChange::Navigation);
  if (state.revisions.header != renderedRevisions_.header) add(UiChange::Header);
  if (state.revisions.presets != renderedRevisions_.presets) add(UiChange::Presets);
  if (state.revisions.chain != renderedRevisions_.chain) add(UiChange::Chain);
  if (state.revisions.parameters != renderedRevisions_.parameters) add(UiChange::Parameters);
  if (state.revisions.assets != renderedRevisions_.assets) add(UiChange::Assets);
  if (state.revisions.drawers != renderedRevisions_.drawers) add(UiChange::Drawers);
  if (state.revisions.status != renderedRevisions_.status) add(UiChange::Status);
  if (state.revisions.telemetry != renderedRevisions_.telemetry) add(UiChange::Telemetry);

  // The control loop intentionally services LVGL every 5 ms for responsive
  // touch input. Most ticks carry no model revision, so leave the retained
  // scene untouched instead of reapplying every label, style, and visibility
  // flag at 200 Hz.
  if (changes == UiChange::None) {
    return;
  }

  // Text-only regions are always safe while an input device owns a widget.
  if (hasUiChange(changes, UiChange::Status) || hasUiChange(changes, UiChange::Telemetry)) {
    syncStatusView(state);
    if (state.mode == UiMode::Tuner) {
      syncTunerView(state);
    }
    renderedRevisions_.status = state.revisions.status;
    renderedRevisions_.telemetry = state.revisions.telemetry;
  }
  // Blocking overlays must become visible even while a slider or drag owns an
  // input device. The control loop forces this retained state to the display
  // before it starts synchronous engine preparation.
  syncBlockingOverlays(state);
  if (activeInteractions_ > 0) {
    pendingChanges_ = changes;
    return;
  }

  const bool presetChanged = state.activePreset != renderedPreset_ || state.activeBank != renderedBank_;
  if (presetChanged || hasUiChange(changes, UiChange::Presets)) {
    syncChainCards(state);
    syncParameterView(state);
    if (hasUiChange(changes, UiChange::Assets)) syncDrawerAssets(state);
    syncDrawerView(state);
  } else {
    if (hasUiChange(changes, UiChange::Chain)) syncChainCards(state);
    if (hasUiChange(changes, UiChange::Parameters)) syncParameterView(state);
    if (hasUiChange(changes, UiChange::Assets)) {
      syncDrawerAssets(state);
      syncDrawerView(state);
    } else if (hasUiChange(changes, UiChange::Drawers)) {
      syncDrawerView(state);
    }
  }
  syncPersistentViews(state);
  renderedRevisions_ = state.revisions;
  renderedBank_ = state.activeBank;
  renderedPreset_ = state.activePreset;
  pendingChanges_ = UiChange::None;
}

void LvglUi::invalidate(UiChange changes)
{
  pendingChanges_ = pendingChanges_ | changes;
}

void LvglUi::endInteraction(bool requestUiRebuild)
{
  if (activeInteractions_ > 0) {
    --activeInteractions_;
  }
  if (requestUiRebuild) {
    invalidate(UiChange::Parameters);
  }
}





} // namespace ardor
