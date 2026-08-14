#include "ui/LvglUi.h"

#include "ui/LvglUiNavigation.h"
#include "ui/LvglUiStatus.h"
#include "ui/LvglUiStyle.h"

#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace ardor {

using namespace lvgl_navigation;
using namespace lvgl_ui;

namespace {

std::string midiLearnValueText(float value)
{
  std::ostringstream out;
  out << std::fixed << std::setprecision(std::fabs(value) < 10.0f ? 2 : 1) << value;
  return out.str();
}

void onMidiLearnCancel(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  cancelMidiLearn(*context->state);
  context->ui->invalidate(UiChange::Parameters | UiChange::Status);
}

void onMidiLearnAdvanced(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  showAdvancedMidiLearn(*context->state);
  context->ui->invalidate(UiChange::Parameters | UiChange::Status);
}

void onMidiLearnSave(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (!commitMidiLearn(*context->state)) return;
  if (context->ui->actions().updateMidiBindings) {
    context->ui->actions().updateMidiBindings(
      context->state->bank.presets[context->state->activePreset].midiBindings);
  }
  context->ui->invalidate(UiChange::Parameters | UiChange::Header | UiChange::Status);
}

void onMidiLearnMode(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto mode = context->state->midiLearn.mode == PresetMidiBindingMode::Continuous
    ? PresetMidiBindingMode::Toggle : PresetMidiBindingMode::Continuous;
  setMidiLearnMode(*context->state, mode);
  context->ui->invalidate(UiChange::Parameters);
}

void onMidiLearnSlider(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  auto* slider = lv_event_get_target_obj(event);
  const float ratio = static_cast<float>(lv_slider_get_value(slider)) / 1000.0f;
  const auto& learn = context->state->midiLearn;
  setMidiLearnEndpoint(*context->state, context->index,
    learn.targetMinimum + ratio * (learn.targetMaximum - learn.targetMinimum));
}

} // namespace

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
  lvgl_ui::setPalette(state.settings.paletteId);
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
  lv_obj_set_style_text_font(prompt, &ardor_font_saira_cond_semibold_22, 0);
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
    if (i == 0) styleSurface(choice, panel);
    lv_obj_add_event_cb(choice, onNavigationDecision, LV_EVENT_CLICKED, remember(state, i));
  }
  lv_obj_add_flag(navigationOverlay_, LV_OBJ_FLAG_HIDDEN);

  midiLearnOverlay_ = lv_obj_create(canvas);
  lv_obj_set_size(midiLearnOverlay_, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(midiLearnOverlay_, 0, 0);
  lv_obj_set_style_bg_color(midiLearnOverlay_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(midiLearnOverlay_, LV_OPA_80, 0);
  lv_obj_set_style_border_width(midiLearnOverlay_, 0, 0);
  lv_obj_remove_flag(midiLearnOverlay_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* learnCard = lv_obj_create(midiLearnOverlay_);
  lv_obj_set_size(learnCard, 760, 430);
  lv_obj_align(learnCard, LV_ALIGN_CENTER, 0, -10);
  styleSurface(learnCard, panelAlt);
  lv_obj_remove_flag(learnCard, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* learnTitle = lv_label_create(learnCard);
  lv_label_set_text(learnTitle, "MIDI Learn");
  lv_obj_set_style_text_color(learnTitle, lv_color_hex(text), 0);
  lv_obj_set_style_text_font(learnTitle, &ardor_font_saira_cond_semibold_22, 0);
  lv_obj_align(learnTitle, LV_ALIGN_TOP_LEFT, 28, 22);
  midiLearnGuidanceLabel_ = lv_label_create(learnCard);
  lv_obj_set_width(midiLearnGuidanceLabel_, 690);
  lv_obj_set_style_text_color(midiLearnGuidanceLabel_, lv_color_hex(muted), 0);
  lv_obj_set_style_text_align(midiLearnGuidanceLabel_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(midiLearnGuidanceLabel_, LV_ALIGN_TOP_MID, 0, 72);
  midiLearnCaptureLabel_ = lv_label_create(learnCard);
  lv_obj_set_style_text_color(midiLearnCaptureLabel_, lv_color_hex(lamp), 0);
  lv_obj_set_style_text_font(midiLearnCaptureLabel_, &ardor_font_saira_cond_semibold_22, 0);
  lv_obj_align(midiLearnCaptureLabel_, LV_ALIGN_TOP_MID, 0, 116);

  midiLearnAdvancedGroup_ = lv_obj_create(learnCard);
  lv_obj_remove_style_all(midiLearnAdvancedGroup_);
  lv_obj_set_size(midiLearnAdvancedGroup_, 690, 170);
  lv_obj_align(midiLearnAdvancedGroup_, LV_ALIGN_TOP_MID, 0, 155);
  midiLearnModeButton_ = button(midiLearnAdvancedGroup_, "Continuous");
  lv_obj_set_size(midiLearnModeButton_, 170, 48);
  lv_obj_align(midiLearnModeButton_, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_add_event_cb(midiLearnModeButton_, onMidiLearnMode, LV_EVENT_CLICKED, remember(state));
  for (std::size_t endpoint = 0; endpoint < 2; ++endpoint) {
    auto* endpointLabel = lv_label_create(midiLearnAdvancedGroup_);
    lv_label_set_text_fmt(endpointLabel, "%d", static_cast<int>(endpoint + 1));
    lv_obj_set_style_text_color(endpointLabel, lv_color_hex(text), 0);
    lv_obj_align(endpointLabel, LV_ALIGN_TOP_LEFT, 205, 14 + static_cast<int>(endpoint) * 72);
    auto* slider = lv_slider_create(midiLearnAdvancedGroup_);
    lv_obj_set_size(slider, 360, 28);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 240, 10 + static_cast<int>(endpoint) * 72);
    lv_slider_set_range(slider, 0, 1000);
    lv_obj_set_style_bg_color(slider, lv_color_hex(panelAlt), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(lamp), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(text), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, onMidiLearnSlider, LV_EVENT_VALUE_CHANGED,
                        remember(state, endpoint));
    midiLearnSliders_[endpoint] = slider;
    auto* valueLabel = lv_label_create(midiLearnAdvancedGroup_);
    lv_obj_set_width(valueLabel, 76);
    lv_obj_set_style_text_align(valueLabel, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(valueLabel, lv_color_hex(text), 0);
    lv_obj_align(valueLabel, LV_ALIGN_TOP_RIGHT, 0, 13 + static_cast<int>(endpoint) * 72);
    midiLearnValueLabels_[endpoint] = valueLabel;
  }

  midiLearnAdvancedButton_ = button(learnCard, "Advanced");
  lv_obj_set_size(midiLearnAdvancedButton_, 150, 56);
  lv_obj_align(midiLearnAdvancedButton_, LV_ALIGN_BOTTOM_MID, -166, -22);
  lv_obj_add_event_cb(midiLearnAdvancedButton_, onMidiLearnAdvanced,
                      LV_EVENT_CLICKED, remember(state));
  midiLearnSaveButton_ = button(learnCard, "Save");
  lv_obj_set_size(midiLearnSaveButton_, 150, 56);
  lv_obj_align(midiLearnSaveButton_, LV_ALIGN_BOTTOM_MID, 0, -22);
  styleSurface(midiLearnSaveButton_, panel);
  lv_obj_add_event_cb(midiLearnSaveButton_, onMidiLearnSave,
                      LV_EVENT_CLICKED, remember(state));
  lv_obj_t* learnCancel = button(learnCard, "Cancel");
  lv_obj_set_size(learnCancel, 150, 56);
  lv_obj_align(learnCancel, LV_ALIGN_BOTTOM_MID, 166, -22);
  lv_obj_add_event_cb(learnCancel, onMidiLearnCancel, LV_EVENT_CLICKED, remember(state));
  lv_obj_add_flag(midiLearnOverlay_, LV_OBJ_FLAG_HIDDEN);

  rebuildPresetView(state);
  rebuildEditView(state);
  contextRegion_ = UiContextRegion::Tuner;
  renderTunerMode(tunerLayer_, state);
  contextRegion_ = UiContextRegion::None;
  rebuildParameterView(state);
  rebuildDrawerView(state);
  contextRegion_ = UiContextRegion::Status;
  renderStatusBar(this, statusLayer_, state, &telemetryLabel_, &masterVolumeLabel_,
                  &masterVolumeScaleFill_,
                  &expressionStatusLabel_, &midiStatusLabel_, &settingsButton_,
                  &statusMessageLabel_, &undoButton_);
  contextRegion_ = UiContextRegion::None;
  contextRegion_ = UiContextRegion::Settings;
  renderSettingsView(settingsLayer_, state);
  contextRegion_ = UiContextRegion::None;
  contextRegion_ = UiContextRegion::PresetName;
  renderPresetNameEditor(canvas, state);
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
  // Preset and Edit now carry their own top+bottom rails (per
  // docs/lvgl-ui-redesign-spec.md §4f); the shared status bar stays for the
  // screens not yet migrated to their own rails.
  if (state.mode == UiMode::Tuner || state.mode == UiMode::Preset
      || state.mode == UiMode::Edit) {
    lv_obj_add_flag(statusLayer_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(statusLayer_, LV_OBJ_FLAG_HIDDEN);
  }
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
  if (navigationOverlay_) {
    if (state.navigationPrompt.has_value()) lv_obj_remove_flag(navigationOverlay_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(navigationOverlay_, LV_OBJ_FLAG_HIDDEN);
  }
  if (midiLearnOverlay_) {
    const auto& learn = state.midiLearn;
    if (learn.stage == UiMidiLearnStage::None) {
      lv_obj_add_flag(midiLearnOverlay_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_flag(midiLearnOverlay_, LV_OBJ_FLAG_HIDDEN);
      const bool captured = learn.controlChange >= 0;
      const bool advanced = learn.stage == UiMidiLearnStage::Advanced;
      lv_label_set_text(midiLearnGuidanceLabel_, captured
        ? (advanced ? "Choose pedal range or the two scene values."
                    : "Keep moving the pedal, or save this control.")
        : "Move a pedal or press a footswitch on your MIDI controller.");
      const std::string capture = captured
        ? "CC " + std::to_string(learn.controlChange) + "  ·  Channel "
            + std::to_string(learn.channel + 1)
        : "Listening...";
      lv_label_set_text(midiLearnCaptureLabel_, capture.c_str());
      if (captured) {
        lv_obj_remove_flag(midiLearnSaveButton_, LV_OBJ_FLAG_HIDDEN);
        if (advanced) lv_obj_add_flag(midiLearnAdvancedButton_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(midiLearnAdvancedButton_, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(midiLearnSaveButton_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(midiLearnAdvancedButton_, LV_OBJ_FLAG_HIDDEN);
      }
      if (advanced) {
        lv_obj_remove_flag(midiLearnAdvancedGroup_, LV_OBJ_FLAG_HIDDEN);
        const bool toggle = learn.mode == PresetMidiBindingMode::Toggle;
        lv_label_set_text(lv_obj_get_child(midiLearnModeButton_, 0),
                          toggle ? "Toggle / Scene" : "Continuous");
        const std::array values = {learn.action.value1, learn.action.value2};
        const float range = learn.targetMaximum - learn.targetMinimum;
        for (std::size_t endpoint = 0; endpoint < 2; ++endpoint) {
          const int sliderValue = range > 0.0f
            ? static_cast<int>(std::lround(
                (values[endpoint] - learn.targetMinimum) / range * 1000.0f)) : 0;
          if (lv_slider_get_value(midiLearnSliders_[endpoint]) != sliderValue) {
            lv_slider_set_value(midiLearnSliders_[endpoint], sliderValue, LV_ANIM_OFF);
          }
          const auto label = midiLearnValueText(values[endpoint]);
          lv_label_set_text(midiLearnValueLabels_[endpoint], label.c_str());
        }
      } else {
        lv_obj_add_flag(midiLearnAdvancedGroup_, LV_OBJ_FLAG_HIDDEN);
      }
    }
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
  const auto now = std::chrono::steady_clock::now();
  if (!highlightedBlockId_.empty() && now >= highlightUntil_) {
    highlightedBlockId_.clear();
    invalidate(UiChange::Chain);
  }
  if (!viewsInitialized_ || root != lv_obj_get_parent(canvas_)) {
    build(root, state);
    return;
  }

  // Sampled telemetry, not a discrete UI event -- it carries no revision of
  // its own, so it must be synced unconditionally rather than behind the
  // `changes == UiChange::None` short-circuit below (which exists precisely
  // to skip ticks that changed nothing).
  syncCompressorGainMeter(state);

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
