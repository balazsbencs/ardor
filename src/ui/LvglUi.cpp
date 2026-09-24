#include "ui/LvglUi.h"

#include "ui/LvglUiNavigation.h"
#include "ui/LvglUiStatus.h"
#include "ui/LampBlack.h"
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
  parameterChipStrip_ = nullptr;
  parameterChipContext_ = nullptr;
  renderedChipKeys_.clear();
  if (state.mode == UiMode::Preset || state.mode == UiMode::Scenes || !state.paramDrawerOpen) {
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
  scenesLayer_ = createLayer();
  editLayer_ = createLayer();
  tunerLayer_ = createLayer();
  looperLayer_ = createLayer();
  parameterLayer_ = createLayer();
  drawerLayer_ = createLayer();
  statusLayer_ = createLayer();
  settingsLayer_ = createLayer();
  // Unsaved-changes prompt before a preset switch.
  {
    constexpr int kWidth = 660;
    constexpr int kHeight = 250;
    navigationOverlay_ = lb::createOverlay(canvas);
    lv_obj_t* prompt = lb::createDialog(navigationOverlay_, kWidth, kHeight, "UNSAVED CHANGES");
    lb::dialogBody(prompt, "SAVE CHANGES BEFORE SWITCHING PRESETS?", kWidth);
    const auto choices = lb::dialogActions(prompt, kWidth, kHeight,
      {{"Save", lb::ButtonKind::Primary}, {"Discard", lb::ButtonKind::Danger},
       {"Cancel", lb::ButtonKind::Normal}});
    for (std::size_t i = 0; i < choices.size(); ++i) {
      lv_obj_add_event_cb(choices[i], onNavigationDecision, LV_EVENT_CLICKED, remember(state, i));
    }
    lv_obj_add_flag(navigationOverlay_, LV_OBJ_FLAG_HIDDEN);
  }

  // MIDI learn: listen for a controller, optionally shape the mapping.
  constexpr int kLearnWidth = 800;
  constexpr int kLearnHeight = 440;
  midiLearnOverlay_ = lb::createOverlay(canvas);
  lv_obj_t* learnCard = lb::createDialog(midiLearnOverlay_, kLearnWidth, kLearnHeight, "MIDI LEARN");
  midiLearnGuidanceLabel_ = lb::dialogBody(learnCard, "", kLearnWidth);
  lv_obj_set_style_text_font(midiLearnGuidanceLabel_, &ardor_lb_saira400_15, 0);
  lv_obj_set_style_text_letter_space(midiLearnGuidanceLabel_, 0, 0);
  midiLearnCaptureLabel_ = lb::textLabel(learnCard, lb::type::controlLabel, "", text,
                                         lb::kDialogInset - 1, 120);

  midiLearnAdvancedGroup_ = lv_obj_create(learnCard);
  lv_obj_remove_style_all(midiLearnAdvancedGroup_);
  lv_obj_set_size(midiLearnAdvancedGroup_, kLearnWidth - 2 - 2 * lb::kDialogInset, 170);
  lv_obj_set_pos(midiLearnAdvancedGroup_, lb::kDialogInset - 1, 160);
  lv_obj_remove_flag(midiLearnAdvancedGroup_, LV_OBJ_FLAG_SCROLLABLE);
  midiLearnModeButton_ = lb::button(midiLearnAdvancedGroup_, "Continuous", lb::ButtonKind::Normal,
                                    0, 0, lb::buttonWidth("Continuous"));
  lv_obj_add_event_cb(midiLearnModeButton_, onMidiLearnMode, LV_EVENT_CLICKED, remember(state));
  const int sliderX = lb::buttonWidth("Continuous") + 48;
  const int sliderWidth = kLearnWidth - 2 - 2 * lb::kDialogInset - sliderX - 96;
  for (std::size_t endpoint = 0; endpoint < 2; ++endpoint) {
    const int rowY = static_cast<int>(endpoint) * 72;
    lb::textLabel(midiLearnAdvancedGroup_, lb::type::controlLabel,
                  std::to_string(endpoint + 1), muted, sliderX - 28, rowY + 16);
    auto* slider = lv_slider_create(midiLearnAdvancedGroup_);
    lv_obj_set_size(slider, sliderWidth, 8);
    lv_obj_set_pos(slider, sliderX, rowY + 26);
    lv_slider_set_range(slider, 0, 1000);
    lv_obj_set_style_radius(slider, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_hex(plateHi), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(muted), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(text), LV_PART_KNOB);
    lv_obj_set_style_pad_hor(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_ver(slider, 12, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_ext_click_area(slider, 20);
    lv_obj_add_event_cb(slider, onMidiLearnSlider, LV_EVENT_VALUE_CHANGED,
                        remember(state, endpoint));
    midiLearnSliders_[endpoint] = slider;
    auto* valueLabel = lb::textLabel(midiLearnAdvancedGroup_, lb::type::controlLabel, "", text,
                                     sliderX + sliderWidth + 20, rowY + 16);
    lv_obj_set_width(valueLabel, 76);
    lv_obj_set_style_text_align(valueLabel, LV_TEXT_ALIGN_RIGHT, 0);
    midiLearnValueLabels_[endpoint] = valueLabel;
  }

  const auto learnActions = lb::dialogActions(learnCard, kLearnWidth, kLearnHeight,
    {{"Advanced", lb::ButtonKind::Normal}, {"Save", lb::ButtonKind::Primary},
     {"Cancel", lb::ButtonKind::Normal}});
  midiLearnAdvancedButton_ = learnActions[0];
  lv_obj_add_event_cb(midiLearnAdvancedButton_, onMidiLearnAdvanced,
                      LV_EVENT_CLICKED, remember(state));
  midiLearnSaveButton_ = learnActions[1];
  lv_obj_add_event_cb(midiLearnSaveButton_, onMidiLearnSave,
                      LV_EVENT_CLICKED, remember(state));
  lv_obj_add_event_cb(learnActions[2], onMidiLearnCancel, LV_EVENT_CLICKED, remember(state));
  lv_obj_add_flag(midiLearnOverlay_, LV_OBJ_FLAG_HIDDEN);

  rebuildPresetView(state);
  rebuildScenesView(state);
  rebuildEditView(state);
  contextRegion_ = UiContextRegion::Tuner;
  renderTunerMode(tunerLayer_, state);
  contextRegion_ = UiContextRegion::Looper;
  renderLooperMode(looperLayer_, state);
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
    lv_obj_add_flag(scenesLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(editLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tunerLayer_, LV_OBJ_FLAG_HIDDEN);
  } else if (state.mode == UiMode::Scenes) {
    lv_obj_add_flag(presetLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(scenesLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(editLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tunerLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(looperLayer_, LV_OBJ_FLAG_HIDDEN);
  } else if (state.mode == UiMode::Edit) {
    lv_obj_add_flag(presetLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scenesLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(editLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tunerLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(looperLayer_, LV_OBJ_FLAG_HIDDEN);
  } else if (state.mode == UiMode::Tuner) {
    lv_obj_add_flag(presetLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scenesLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(editLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(tunerLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(looperLayer_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(presetLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(scenesLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(editLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(tunerLayer_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(looperLayer_, LV_OBJ_FLAG_HIDDEN);
  }
  if (state.mode == UiMode::Preset) lv_obj_add_flag(looperLayer_, LV_OBJ_FLAG_HIDDEN);

  const bool showParameters = state.mode == UiMode::Edit && state.paramDrawerOpen;
  if (showParameters) lv_obj_remove_flag(parameterLayer_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(parameterLayer_, LV_OBJ_FLAG_HIDDEN);
  const bool showDrawer = state.mode == UiMode::Edit && state.blockDrawerOpen;
  if (showDrawer) lv_obj_remove_flag(drawerLayer_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(drawerLayer_, LV_OBJ_FLAG_HIDDEN);
  // Preset and Edit now carry their own top+bottom rails (per
  // docs/lvgl-ui-redesign-spec.md §4f); the shared status bar stays for the
  // screens not yet migrated to their own rails.
  // Only the legacy bar hides; the layer stays up so status toasts show on
  // every screen.
  lv_obj_remove_flag(statusLayer_, LV_OBJ_FLAG_HIDDEN);
  if (lv_obj_t* bar = lv_obj_get_child(statusLayer_, 0)) {
    if (state.mode == UiMode::Tuner || state.mode == UiMode::Preset
        || state.mode == UiMode::Scenes
        || state.mode == UiMode::Edit || state.mode == UiMode::Looper) {
      lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_flag(bar, LV_OBJ_FLAG_HIDDEN);
    }
  }
}




void LvglUi::syncPersistentViews(UiState& state)
{
  if (!viewsInitialized_) return;
  syncModeVisibility(state);
  syncTunerView(state);
  syncLooperView(state);
  syncHeaderView(state);
  syncPresetCards(state);
  syncScenesView(state);
  syncStatusView(state);
  syncParameterChipStrip(state);
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
        ? "CC " + std::to_string(learn.controlChange) + "  ·  CHANNEL "
            + std::to_string(learn.channel + 1)
        : "LISTENING...";
      lv_label_set_text(midiLearnCaptureLabel_, capture.c_str());
      if (captured) {
        lv_obj_remove_flag(midiLearnSaveButton_, LV_OBJ_FLAG_HIDDEN);
        if (advanced) lv_obj_add_flag(midiLearnAdvancedButton_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(midiLearnAdvancedButton_, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(midiLearnSaveButton_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(midiLearnAdvancedButton_, LV_OBJ_FLAG_HIDDEN);
      }
      // The dialog grows only when the advanced mapping controls show.
      lv_obj_t* card = lv_obj_get_parent(midiLearnAdvancedGroup_);
      const int height = advanced ? 440 : 260;
      lv_obj_set_height(card, height);
      lv_obj_set_y(card, (kDesignHeight - height) / 2);
      for (lv_obj_t* action : {midiLearnAdvancedButton_, midiLearnSaveButton_,
                               lv_obj_get_child(card, static_cast<int32_t>(
                                 lv_obj_get_child_count(card)) - 1)}) {
        lv_obj_set_y(action, height - 2 - 28 - lb::kButtonHeight);
      }
      if (advanced) {
        lv_obj_remove_flag(midiLearnAdvancedGroup_, LV_OBJ_FLAG_HIDDEN);
        const bool toggle = learn.mode == PresetMidiBindingMode::Toggle;
        lb::setButtonText(midiLearnModeButton_, toggle ? "Toggle" : "Continuous");
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
  if (state.revisions.looper != renderedRevisions_.looper) add(UiChange::Looper);

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
    if (state.mode == UiMode::Looper) {
      syncLooperView(state);
    }
    renderedRevisions_.status = state.revisions.status;
    renderedRevisions_.telemetry = state.revisions.telemetry;
  }
  if (hasUiChange(changes, UiChange::Looper)) {
    syncLooperView(state);
    renderedRevisions_.looper = state.revisions.looper;
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
    rebuildScenesView(state);
    const bool rebuildSceneEditor = state.mode == UiMode::Edit
      && hasUiChange(changes, UiChange::Presets);
    if (rebuildSceneEditor) {
      rebuildEditView(state);
      rebuildParameterView(state);
    } else {
      syncChainCards(state);
      syncParameterView(state);
    }
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
