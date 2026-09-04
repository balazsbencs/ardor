#include "ui/LvglUi.h"

#include "ui/LvglUiNavigation.h"
#include "ui/LvglUiStyle.h"
#include "ui/fonts/SairaCondSemibold52.h"
#include "ui/fonts/SairaCondSemibold72.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace ardor {
namespace {

using namespace lvgl_navigation;
using namespace lvgl_ui;

// Top legend rail and bottom control rail, per
// docs/lvgl-ui-redesign-spec.md §4f and §6 (Layout): 52 px / 88 px, the
// preset grid fills the 580 px content band between them.
constexpr int kRailEdgeInset = 28;
constexpr int kTopRailHeight = 52;
constexpr int kBottomRailHeight = 88;
constexpr int kBottomRailY = kDesignHeight - kBottomRailHeight;
constexpr int kMasterScaleWidth = 250;
constexpr int kMasterRailHeight = 18;
constexpr int kMasterFillHeight = kMasterRailHeight - 2;
constexpr int kMasterHandleWidth = 44;
// The preset rail has only 88 px of vertical space. Keep the thumb large
// enough to grab while leaving a clean header row above it and a small safety
// inset below it; the parameter editor can use its taller 54 px thumb because
// its cards are 132 px high.
constexpr int kMasterHandleHeight = 40;
constexpr int kMasterScaleGroupWidth = kMasterScaleWidth + kMasterHandleWidth;
constexpr int kMasterRailX = kMasterHandleWidth / 2;
constexpr int kMasterHeaderHeight = 44;
constexpr int kMasterBottomInset = 4;
constexpr int kMasterRailY = kMasterHeaderHeight
  + (kMasterHandleHeight - kMasterRailHeight) / 2 + 1;
constexpr int kPresetHeaderHeight = 44;
constexpr int kPresetNameHeight = 160;
constexpr int kMinBank = 0;
constexpr int kMaxBank = 99;
constexpr std::size_t kPresetNameMaxLength = 32;

std::string presetTelemetryText(const UiState& state)
{
  char value[96]{};
  const double latencyMs = static_cast<double>(state.settings.audioBlockSize) / 48.0;
  if (state.telemetry.budgetMs <= 0.0) {
    std::snprintf(value, sizeof(value), "LATENCY %.2f MS  \xC2\xB7  BUFFER --%% USED", latencyMs);
  } else {
    const double used = std::clamp(100.0 - state.telemetry.bufferFreePercent, 0.0, 100.0);
    std::snprintf(value, sizeof(value), "LATENCY %.2f MS  \xC2\xB7  BUFFER %.0f%% USED",
                  latencyMs, used);
  }
  return value;
}

std::string trimmed(std::string value)
{
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

void redraw(UiEventContext* context)
{
  // Model mutators publish typed revisions. This helper remains at event call
  // sites solely to make local focus/page changes visible.
  context->ui->invalidate(UiChange::None);
}

void onPresetClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectPreset(*context->state, context->index);
  redraw(context);
}

void requestBankChange(UiEventContext* context, int delta)
{
  const int target = context->state->activeBank + delta;
  if (target < kMinBank || target > kMaxBank || !context->ui->actions().changeBank) {
    return;
  }
  context->ui->actions().changeBank(delta);
}

void onBankDownClicked(lv_event_t* event)
{
  requestBankChange(static_cast<UiEventContext*>(lv_event_get_user_data(event)), -1);
}

void onBankUpClicked(lv_event_t* event)
{
  requestBankChange(static_cast<UiEventContext*>(lv_event_get_user_data(event)), 1);
}

void onLooperClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().openLooper) context->ui->actions().openLooper();
  else enterLooperMode(*context->state);
}

void onPresetNameSaveClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->savePresetName(*context->state);
}

void onPresetNameCancelClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->cancelPresetNameEditor();
}

} // namespace

void LvglUi::selectPreset(UiState& state, std::size_t presetIndex)
{
  if (actions_.selectPreset) {
    actions_.selectPreset(presetIndex);
  } else {
    ardor::selectPreset(state, presetIndex);
  }
  resetParameterPage();
}

void LvglUi::rebuildPresetView(UiState& state)
{
  if (!presetLayer_) return;
  lv_obj_clean(presetLayer_);
  contexts_.remove_if([](const UiEventContext& context) {
    return context.region == UiContextRegion::Preset;
  });
  presetCardLabels_.fill(nullptr);
  presetCardButtons_.fill(nullptr);
  presetHeaderStrips_.fill(nullptr);
  presetHeaderLabels_.fill(nullptr);
  presetNumerals_.fill(nullptr);
  presetWarningLabels_.fill(nullptr);
  bankDownButton_ = nullptr;
  bankUpButton_ = nullptr;
  presetTelemetryLabel_ = nullptr;
  presetMasterValueLabel_ = nullptr;
  presetMasterScaleFill_ = nullptr;
  presetMasterPointer_ = nullptr;
  presetLooperLabel_ = nullptr;
  contextRegion_ = UiContextRegion::Preset;
  renderPresetMode(presetLayer_, state);
  contextRegion_ = UiContextRegion::None;
}

void LvglUi::syncHeaderView(const UiState& state)
{
  if (!viewsInitialized_) return;
  if (masterVolumeLabel_) {
    const auto value = "Master " + std::to_string(state.masterVolume) + "%";
    lv_label_set_text(masterVolumeLabel_, value.c_str());
  }
  if (masterVolumeScaleFill_) {
    lv_obj_set_width(masterVolumeScaleFill_, std::clamp(state.masterVolume, 0, 100) * 120 / 100);
  }
  if (presetTelemetryLabel_) {
    const auto value = presetTelemetryText(state);
    lv_label_set_text(presetTelemetryLabel_, value.c_str());
  }
  const int masterPct = std::clamp(state.masterVolume, 0, 100);
  if (presetMasterValueLabel_) {
    lv_label_set_text(presetMasterValueLabel_, std::to_string(masterPct).c_str());
  }
  if (presetMasterScaleFill_) {
    lv_obj_set_width(presetMasterScaleFill_, masterPct * (kMasterScaleWidth - 2) / 100);
  }
  if (presetMasterPointer_) {
    lv_obj_set_x(presetMasterPointer_, kMasterRailX + 1
      + masterPct * (kMasterScaleWidth - 2) / 100 - kMasterHandleWidth / 2);
  }
  if (presetLooperLabel_) {
    lv_label_set_text(presetLooperLabel_,
      state.looper.telemetry.sessionState == LooperSessionState::Inactive ? "Looper" : "Resume Loop");
  }
  if (bankDownButton_) {
    if (state.activeBank == kMinBank) lv_obj_add_state(bankDownButton_, LV_STATE_DISABLED);
    else lv_obj_remove_state(bankDownButton_, LV_STATE_DISABLED);
  }
  if (bankUpButton_) {
    if (state.activeBank == kMaxBank) lv_obj_add_state(bankUpButton_, LV_STATE_DISABLED);
    else lv_obj_remove_state(bankUpButton_, LV_STATE_DISABLED);
  }
  if (editPresetLabel_) {
    lv_label_set_text(editPresetLabel_, state.bank.presets[state.activePreset].name.c_str());
  }
  if (saveButtonLabel_) {
    // Save renders as a primary (inverted) button: dark text on a light
    // plate. `warning` still reads clearly there; `text` would not, so the
    // clean state uses `bg` instead of the usual light-on-dark convention.
    lv_label_set_text(saveButtonLabel_, state.dirty ? "SAVE*" : "SAVE");
    lv_obj_set_style_text_color(saveButtonLabel_, lv_color_hex(state.dirty ? warning : bg), 0);
  }
  if (editModifiedLabel_) {
    if (state.dirty) lv_obj_remove_flag(editModifiedLabel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(editModifiedLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  if (editModuleCountLabel_) {
    const auto count = state.bank.presets[state.activePreset].blocks.size();
    lv_label_set_text(editModuleCountLabel_,
      (std::to_string(count) + (count == 1 ? " MODULE" : " MODULES")).c_str());
  }
}

void LvglUi::syncPresetCards(const UiState& state)
{
  if (!viewsInitialized_) return;
  for (std::size_t i = 0; i < presetCardButtons_.size(); ++i) {
    if (!presetCardButtons_[i]) continue;
    if (i >= state.bank.presets.size()) {
      lv_obj_add_flag(presetCardButtons_[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_label_set_text(presetCardLabels_[i], uppercase(state.bank.presets[i].name).c_str());
    const bool isActive = i == state.activePreset;
    const bool unavailable = presetHasUnavailableAssets(state, i);
    const bool sessionLocked = state.looper.telemetry.sessionState != LooperSessionState::Inactive;
    lv_obj_set_style_text_color(presetCardLabels_[i], lv_color_hex(unavailable ? danger : text), 0);
    lv_obj_set_style_border_color(presetCardButtons_[i], lv_color_hex(unavailable ? palette().faultLine : (isActive ? lamp : rule)), 0);
    lv_obj_set_style_border_width(presetCardButtons_[i], isActive ? 3 : 1, 0);
    lv_obj_set_style_border_opa(presetCardButtons_[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(presetHeaderStrips_[i], lv_color_hex(isActive ? lamp : panelAlt), 0);
    lv_label_set_text(presetHeaderLabels_[i],
      ("FS " + std::to_string(i + 1) + (isActive ? "  \xC2\xB7  LIVE" : "")).c_str());
    lv_obj_set_style_text_color(presetHeaderLabels_[i], lv_color_hex(isActive ? bg : text), 0);
    lv_obj_set_style_text_color(presetNumerals_[i], lv_color_hex(isActive ? lamp : muted), 0);
    if (presetWarningLabels_[i]) {
      if (unavailable) lv_obj_remove_flag(presetWarningLabels_[i], LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(presetWarningLabels_[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (sessionLocked) lv_obj_add_state(presetCardButtons_[i], LV_STATE_DISABLED);
    else lv_obj_remove_state(presetCardButtons_[i], LV_STATE_DISABLED);
    lv_obj_remove_flag(presetCardButtons_[i], LV_OBJ_FLAG_HIDDEN);
  }
}

void LvglUi::renderPresetMode(lv_obj_t* root, UiState& state)
{
  // ---- top legend rail: a panel always names itself ----
  lv_obj_t* topRail = lv_obj_create(root);
  lv_obj_set_size(topRail, kDesignWidth, kTopRailHeight);
  lv_obj_set_pos(topRail, 0, 0);
  lv_obj_set_style_bg_color(topRail, lv_color_hex(panel), 0);
  lv_obj_set_style_bg_opa(topRail, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(topRail, 1, 0);
  lv_obj_set_style_border_side(topRail, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(topRail, lv_color_hex(rule), 0);
  lv_obj_set_style_radius(topRail, 0, 0);
  lv_obj_set_style_pad_all(topRail, 0, 0);
  lv_obj_remove_flag(topRail, LV_OBJ_FLAG_SCROLLABLE);

  // The engine publishes buffer telemetry once per second. Pair that live load
  // with the configured block latency; the fixed sample rate does not need
  // permanent rail space.
  presetTelemetryLabel_ = label(topRail, presetTelemetryText(state),
                                LV_ALIGN_CENTER, 0, 0,
                                &ardor_font_saira_cond_medium_18, text);

  // ---- preset grid: fills the 580 px band between the two rails ----
  lv_obj_t* grid = lv_obj_create(root);
  lv_obj_set_size(grid, kDesignWidth - 2 * kRailEdgeInset,
                  kDesignHeight - kTopRailHeight - kBottomRailHeight - 36);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(grid, kRailEdgeInset, kTopRailHeight + 18);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_style_pad_column(grid, 14, 0);
  lv_obj_set_style_pad_row(grid, 14, 0);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);

  static int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(grid, cols, rows);

  for (std::size_t i = 0; i < presetCardButtons_.size(); ++i) {
    const bool populated = i < state.bank.presets.size();
    lv_obj_t* preset = button(grid, populated ? uppercase(state.bank.presets[i].name) : "");
    presetCardButtons_[i] = preset;
    // Column-major maps each slot to its physical footswitch corner: FS1/FS2
    // on the left, FS3/FS4 on the right. Do not change this to reading order.
    lv_obj_set_grid_cell(preset, LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i / 2), 1,
                         LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i % 2), 1);
    styleSurface(preset, panel);
    // The default button theme's padding would otherwise inset the header
    // strip from the card's true edges, throwing off every child alignment
    // anchored to LV_ALIGN_*_RIGHT/LEFT below.
    lv_obj_set_style_pad_all(preset, 0, 0);
    const bool isActive = populated && i == state.activePreset;
    if (isActive) {
      lv_obj_set_style_border_color(preset, lv_color_hex(lamp), 0);
      lv_obj_set_style_border_width(preset, 3, 0);
      lv_obj_set_style_border_opa(preset, LV_OPA_COVER, 0);
    }
    lv_obj_t* presetName = lv_obj_get_child(preset, 0);
    presetCardLabels_[i] = presetName;
    lv_obj_set_style_text_color(presetName, lv_color_hex(text), 0);
    // Preset identity is the only card content that must survive a one-second
    // glance from standing height. A native 72 px bitmap face keeps the stems
    // crisp on the panel; compact chain metadata is intentionally omitted
    // because it becomes visual noise at that distance.
    lv_obj_set_style_text_font(presetName, &ardor_font_saira_cond_semibold_72, 0);
    lv_obj_set_style_text_letter_space(presetName, 0, 0);
    lv_obj_set_style_text_line_space(presetName, -7, 0);
    lv_obj_set_style_text_align(presetName, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_size(presetName, LV_PCT(84), kPresetNameHeight);
    lv_label_set_long_mode(presetName, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(presetName, 24, 61);
    lv_obj_t* header = lv_obj_create(preset);
    lv_obj_set_size(header, LV_PCT(100), kPresetHeaderHeight);
    lv_obj_set_pos(header, 0, 0);
    styleSurface(header, isActive ? lamp : panelAlt);
    lv_obj_set_style_border_width(header, 0, 0);
    // The default theme's own padding would otherwise inset the title and
    // lamp from the header's true edges (this bit the lamp specifically:
    // it stayed ~30px short of the corner despite the header spanning the
    // full card width).
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* titleLabel = label(header,
      "FS " + std::to_string(i + 1) + (isActive ? "  \xC2\xB7  LIVE" : ""),
      LV_ALIGN_LEFT_MID, 16, 0, &ardor_font_saira_cond_semibold_28,
      isActive ? bg : text);
    lv_obj_set_style_text_letter_space(titleLabel, 3, 0);
    presetHeaderStrips_[i] = header;
    presetHeaderLabels_[i] = titleLabel;
    // The footswitch numeral stays clearly subordinate to the preset name,
    // while using the same crisp condensed family at a smaller size.
    lv_obj_t* numeral = label(preset, std::to_string(i + 1), LV_ALIGN_BOTTOM_RIGHT, -22, -16,
                               &ardor_font_saira_cond_semibold_52,
                               isActive ? lamp : muted);
    presetNumerals_[i] = numeral;
    // Fault legend: a bordered chip inside the body, not a corner overlay —
    // it reads as part of the plate's own nomenclature, not a tooltip.
    lv_obj_t* unavailable = label(preset, "ASSET NOT FOUND", LV_ALIGN_BOTTOM_LEFT, 22, -16,
                                  &ardor_font_saira_cond_medium_18, palette().family[5]);
    lv_obj_set_style_pad_hor(unavailable, 10, 0);
    lv_obj_set_style_pad_ver(unavailable, 4, 0);
    lv_obj_set_style_border_width(unavailable, 1, 0);
    lv_obj_set_style_border_color(unavailable, lv_color_hex(palette().faultLine), 0);
    presetWarningLabels_[i] = unavailable;
    if (!populated || !presetHasUnavailableAssets(state, i)) {
      lv_obj_add_flag(unavailable, LV_OBJ_FLAG_HIDDEN);
    }
    if (state.looper.telemetry.sessionState != LooperSessionState::Inactive) {
      lv_obj_add_state(preset, LV_STATE_DISABLED);
    }
    if (!populated) lv_obj_add_flag(preset, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(preset, onPresetClicked, LV_EVENT_CLICKED, remember(state, i));
  }

  // ---- bottom control rail: transport + the master travel scale ----
  lv_obj_t* bottomRail = lv_obj_create(root);
  lv_obj_set_size(bottomRail, kDesignWidth, kBottomRailHeight);
  lv_obj_set_pos(bottomRail, 0, kBottomRailY);
  lv_obj_set_style_bg_color(bottomRail, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(bottomRail, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bottomRail, 1, 0);
  lv_obj_set_style_border_side(bottomRail, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(bottomRail, lv_color_hex(rule), 0);
  lv_obj_set_style_radius(bottomRail, 0, 0);
  lv_obj_set_style_pad_all(bottomRail, 0, 0);
  lv_obj_remove_flag(bottomRail, LV_OBJ_FLAG_SCROLLABLE);

  int railX = kRailEdgeInset;
  const auto railButton = [&](const std::string& label_, int width, bool primary) {
    lv_obj_t* btn = button(bottomRail, label_);
    lv_obj_set_size(btn, width, 52);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, railX, 0);
    if (primary) {
      styleSurface(btn, text);
      lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), lv_color_hex(bg), 0);
    }
    railX += width + 11;
    return btn;
  };
  lv_obj_t* edit = railButton("Edit", 112, true);
  lv_obj_add_event_cb(edit, onEditModeClicked, LV_EVENT_PRESSED, remember(state));
  lv_obj_t* tuner = railButton("Tuner", 112, false);
  lv_obj_set_style_text_color(lv_obj_get_child(tuner, 0), lv_color_hex(lamp), 0);
  lv_obj_add_event_cb(tuner, onTunerModeClicked, LV_EVENT_PRESSED, remember(state));
  lv_obj_t* looper = railButton(
    state.looper.telemetry.sessionState == LooperSessionState::Inactive ? "Looper" : "Resume Loop",
    142, false);
  presetLooperLabel_ = lv_obj_get_child(looper, 0);
  lv_obj_add_event_cb(looper, onLooperClicked, LV_EVENT_PRESSED, remember(state));
  lv_obj_t* bankDown = railButton("Bank -", 96, false);
  bankDownButton_ = bankDown;
  if (state.activeBank == kMinBank) lv_obj_add_state(bankDown, LV_STATE_DISABLED);
  lv_obj_add_event_cb(bankDown, onBankDownClicked, LV_EVENT_CLICKED, remember(state));
  lv_obj_t* bankUp = railButton("Bank +", 96, false);
  bankUpButton_ = bankUp;
  if (state.activeBank == kMaxBank) lv_obj_add_state(bankUp, LV_STATE_DISABLED);
  lv_obj_add_event_cb(bankUp, onBankUpClicked, LV_EVENT_CLICKED, remember(state));
  lv_obj_t* setup = railButton("Setup", 96, false);
  lv_obj_add_event_cb(setup, onSettingsClicked, LV_EVENT_PRESSED, remember(state));

  // ---- master travel scale, right-aligned ----
  lv_obj_t* scaleGroup = lv_obj_create(bottomRail);
  lv_obj_remove_style_all(scaleGroup);
  lv_obj_set_size(scaleGroup, kMasterScaleGroupWidth, kBottomRailHeight);
  lv_obj_align(scaleGroup, LV_ALIGN_RIGHT_MID, -kRailEdgeInset, 0);
  lv_obj_remove_flag(scaleGroup, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(scaleGroup, LV_OBJ_FLAG_CLICKABLE);
  // Keep both readouts on the same visual span as the rail: the legend is
  // centered over the control, while the value is pinned to the rail's right
  // edge instead of the thumb's safety margin.
  lv_obj_t* masterLabel = label(scaleGroup, "MASTER", LV_ALIGN_TOP_LEFT, kMasterRailX, 4,
                                &ardor_font_saira_cond_medium_18, muted);
  lv_obj_set_width(masterLabel, kMasterScaleWidth);
  lv_obj_set_style_text_align(masterLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(masterLabel, LV_LABEL_LONG_CLIP);
  presetMasterValueLabel_ = label(scaleGroup, std::to_string(state.masterVolume),
                                  LV_ALIGN_TOP_LEFT, kMasterRailX, 0,
                                  &ardor_font_saira_light_44, text);
  lv_obj_set_width(presetMasterValueLabel_, kMasterScaleWidth);
  lv_obj_set_style_text_align(presetMasterValueLabel_, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(presetMasterValueLabel_, LV_LABEL_LONG_CLIP);
  lv_obj_t* track = lv_obj_create(scaleGroup);
  lv_obj_remove_style_all(track);
  lv_obj_set_size(track, kMasterScaleWidth, kMasterRailHeight);
  lv_obj_set_pos(track, kMasterRailX, kMasterRailY);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(track, lv_color_hex(panelAlt), 0);
  lv_obj_set_style_border_width(track, 1, 0);
  lv_obj_set_style_border_color(track, lv_color_hex(rule), 0);
  lv_obj_t* fill = lv_obj_create(scaleGroup);
  lv_obj_remove_style_all(fill);
  lv_obj_set_size(fill, state.masterVolume * (kMasterScaleWidth - 2) / 100,
                  kMasterFillHeight);
  lv_obj_set_pos(fill, kMasterRailX + 1, kMasterRailY + 1);
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(fill, lv_color_hex(lamp), 0);
  presetMasterScaleFill_ = fill;
  lv_obj_t* pointer = lv_obj_create(scaleGroup);
  lv_obj_remove_style_all(pointer);
  lv_obj_set_size(pointer, kMasterHandleWidth, kMasterHandleHeight);
  lv_obj_set_pos(pointer, kMasterRailX + 1
    + state.masterVolume * (kMasterScaleWidth - 2) / 100 - kMasterHandleWidth / 2,
    kMasterRailY - (kMasterHandleHeight - kMasterRailHeight) / 2);
  lv_obj_set_style_bg_opa(pointer, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(pointer, lv_color_hex(lamp), 0);
  lv_obj_set_style_border_width(pointer, 4, 0);
  lv_obj_set_style_border_color(pointer, lv_color_hex(bg), 0);
  lv_obj_t* grip = lv_obj_create(pointer);
  lv_obj_remove_style_all(grip);
  lv_obj_set_size(grip, 2, 26);
  lv_obj_center(grip);
  lv_obj_set_style_bg_opa(grip, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(grip, lv_color_hex(panelAlt), 0);
  presetMasterPointer_ = pointer;
}

void LvglUi::openPresetNameEditor(UiState& state)
{
  if (!presetNameOverlay_ || !presetNameField_
      || state.activePreset >= state.bank.presets.size()) {
    return;
  }
  presetNameEditorOpen_ = true;
  lv_textarea_set_text(presetNameField_, state.bank.presets[state.activePreset].name.c_str());
  lv_label_set_text(presetNameMessageLabel_, "");
  lv_keyboard_set_textarea(presetNameKeyboard_, presetNameField_);
  lv_obj_remove_flag(presetNameOverlay_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(presetNameOverlay_);
}

void LvglUi::cancelPresetNameEditor()
{
  presetNameEditorOpen_ = false;
  if (presetNameOverlay_) lv_obj_add_flag(presetNameOverlay_, LV_OBJ_FLAG_HIDDEN);
}

void LvglUi::savePresetName(UiState& state)
{
  if (!presetNameField_ || state.activePreset >= state.bank.presets.size()) return;
  const std::string name = trimmed(lv_textarea_get_text(presetNameField_));
  if (name.empty()) {
    lv_label_set_text(presetNameMessageLabel_, "Enter a preset name");
    return;
  }

  auto& preset = state.bank.presets[state.activePreset];
  if (preset.name == name) {
    cancelPresetNameEditor();
    return;
  }

  preset.name = name;
  state.dirty = true;
  markUiChanged(state, UiChange::Header | UiChange::Presets);
  cancelPresetNameEditor();
  if (actions_.savePreset) {
    actions_.savePreset();
  } else {
    setUiStatus(state, "Preset renamed - press Save to keep it");
  }
  invalidate(UiChange::Header | UiChange::Presets | UiChange::Status);
}

void LvglUi::renderPresetNameEditor(lv_obj_t* root, UiState& state)
{
  presetNameOverlay_ = lv_obj_create(root);
  lv_obj_set_size(presetNameOverlay_, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(presetNameOverlay_, 0, 0);
  lv_obj_set_style_bg_color(presetNameOverlay_, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(presetNameOverlay_, LV_OPA_80, 0);
  lv_obj_set_style_border_width(presetNameOverlay_, 0, 0);
  lv_obj_set_style_pad_all(presetNameOverlay_, 0, 0);
  lv_obj_remove_flag(presetNameOverlay_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* sheet = lv_obj_create(presetNameOverlay_);
  lv_obj_set_size(sheet, 1120, 650);
  lv_obj_align(sheet, LV_ALIGN_CENTER, 0, 0);
  styleSurface(sheet, panelAlt);
  lv_obj_set_style_pad_all(sheet, 0, 0);
  lv_obj_remove_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);

  label(sheet, "Rename preset", LV_ALIGN_TOP_LEFT, 28, 22,
        &ardor_font_saira_cond_semibold_28, text);
  label(sheet, "The new name is saved to the active slot.", LV_ALIGN_TOP_LEFT, 28, 60,
        &ardor_font_saira_cond_medium_18, muted);

  presetNameField_ = lv_textarea_create(sheet);
  lv_obj_set_pos(presetNameField_, 28, 94);
  lv_obj_set_size(presetNameField_, 760, 64);
  lv_textarea_set_one_line(presetNameField_, true);
  lv_textarea_set_max_length(presetNameField_, kPresetNameMaxLength);
  lv_textarea_set_placeholder_text(presetNameField_, "Preset name");
  lv_textarea_set_text(presetNameField_,
    state.activePreset < state.bank.presets.size()
      ? state.bank.presets[state.activePreset].name.c_str() : "");
  lv_obj_set_style_bg_color(presetNameField_, lv_color_hex(panel), 0);
  lv_obj_set_style_text_color(presetNameField_, lv_color_hex(text), 0);
  lv_obj_set_style_text_font(presetNameField_, &ardor_font_saira_cond_semibold_22, 0);
  lv_obj_set_style_pad_top(presetNameField_, 18, 0);
  lv_obj_set_style_pad_bottom(presetNameField_, 18, 0);
  lv_obj_set_style_border_width(presetNameField_, 1, 0);
  lv_obj_set_style_border_color(presetNameField_, lv_color_hex(rule), 0);
  lv_obj_set_style_border_color(presetNameField_, lv_color_hex(text), LV_STATE_FOCUSED);
  lv_obj_set_style_radius(presetNameField_, 0, 0);

  lv_obj_t* cancel = button(sheet, "CANCEL");
  lv_obj_set_size(cancel, 124, 64);
  lv_obj_set_pos(cancel, 808, 94);
  styleSurface(cancel, panel);
  lv_obj_add_event_cb(cancel, onPresetNameCancelClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* save = button(sheet, "SAVE");
  lv_obj_set_size(save, 124, 64);
  lv_obj_set_pos(save, 952, 94);
  styleSurface(save, text);
  lv_obj_set_style_text_color(lv_obj_get_child(save, 0), lv_color_hex(bg), 0);
  lv_obj_add_event_cb(save, onPresetNameSaveClicked, LV_EVENT_CLICKED, remember(state));

  presetNameMessageLabel_ = label(sheet, "", LV_ALIGN_TOP_LEFT, 28, 164,
                                  &ardor_font_saira_cond_medium_18, danger);

  presetNameKeyboard_ = lv_keyboard_create(sheet);
  lv_obj_set_size(presetNameKeyboard_, 1064, 430);
  // Keyboard widgets default to a bottom alignment. Pin this one explicitly so
  // all rows stay inside the sheet instead of inheriting the default offset.
  lv_obj_align(presetNameKeyboard_, LV_ALIGN_TOP_LEFT, 28, 196);
  lv_obj_set_style_bg_color(presetNameKeyboard_, lv_color_hex(panelAlt), 0);
  lv_obj_set_style_border_width(presetNameKeyboard_, 0, 0);
  lv_obj_set_style_text_color(presetNameKeyboard_, lv_color_hex(text), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(presetNameKeyboard_, lv_color_hex(panel), LV_PART_ITEMS);
  lv_obj_set_style_border_color(presetNameKeyboard_, lv_color_hex(rule), LV_PART_ITEMS);
  lv_obj_set_style_border_width(presetNameKeyboard_, 1, LV_PART_ITEMS);
  lv_obj_set_style_radius(presetNameKeyboard_, 0, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(
    presetNameKeyboard_, lv_color_hex(text),
    static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(
    presetNameKeyboard_, lv_color_hex(bg),
    static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_PRESSED);
  lv_keyboard_set_textarea(presetNameKeyboard_, presetNameField_);
  lv_obj_add_event_cb(presetNameKeyboard_, onPresetNameSaveClicked,
                      LV_EVENT_READY, remember(state));
  lv_obj_add_event_cb(presetNameKeyboard_, onPresetNameCancelClicked,
                      LV_EVENT_CANCEL, remember(state));

  if (!presetNameEditorOpen_) lv_obj_add_flag(presetNameOverlay_, LV_OBJ_FLAG_HIDDEN);
}

} // namespace ardor
