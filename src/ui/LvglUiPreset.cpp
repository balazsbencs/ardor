#include "ui/LvglUi.h"

#include "ui/LvglUiNavigation.h"
#include "ui/LvglUiStyle.h"
#include "ui/fonts/OpenSansRegular.h"
#include "ui/fonts/OpenSansSemibold.h"

#include <string>

namespace ardor {
namespace {

using namespace lvgl_navigation;
using namespace lvgl_ui;

constexpr int kHeaderButtonTop = 20;
constexpr int kHeaderEdgeInset = 28;
constexpr int kHeaderButtonGap = 12;
constexpr int kHeaderTunerButtonWidth = 120;
constexpr int kHeaderBankButtonWidth = 144;
constexpr int kHeaderEditX = kDesignWidth - kHeaderEdgeInset - kHeaderBlocksButtonWidth;
constexpr int kHeaderBankUpX =
  kHeaderEditX - kHeaderButtonGap - kHeaderBankButtonWidth;
constexpr int kHeaderBankDownX =
  kHeaderEdgeInset + kHeaderTunerButtonWidth + kHeaderButtonGap;
constexpr int kMinBank = 0;
constexpr int kMaxBank = 99;

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
  presetIndicators_.fill(nullptr);
  presetWarningLabels_.fill(nullptr);
  presetBankLabel_ = nullptr;
  bankDownButton_ = nullptr;
  bankUpButton_ = nullptr;
  contextRegion_ = UiContextRegion::Preset;
  renderPresetMode(presetLayer_, state);
  contextRegion_ = UiContextRegion::None;
}

void LvglUi::syncHeaderView(const UiState& state)
{
  if (!viewsInitialized_) return;
  if (presetBankLabel_) lv_label_set_text(presetBankLabel_, state.bank.name.c_str());
  if (masterVolumeLabel_) {
    const auto value = "Master " + std::to_string(state.masterVolume) + "%";
    lv_label_set_text(masterVolumeLabel_, value.c_str());
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
    lv_label_set_text(saveButtonLabel_, state.dirty ? "Save*" : "Save");
    lv_obj_set_style_text_color(saveButtonLabel_, lv_color_hex(state.dirty ? accent : text), 0);
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
    lv_label_set_text(presetCardLabels_[i], state.bank.presets[i].name.c_str());
    const bool isActive = i == state.activePreset;
    lv_obj_set_style_text_color(presetCardLabels_[i], lv_color_hex(isActive ? accent : text), 0);
    lv_obj_set_style_border_color(presetCardButtons_[i], lv_color_hex(isActive ? accent : 0xffffff), 0);
    lv_obj_set_style_border_opa(presetCardButtons_[i], isActive ? LV_OPA_40 : LV_OPA_10, 0);
    if (i == state.activePreset) lv_obj_remove_flag(presetIndicators_[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(presetIndicators_[i], LV_OBJ_FLAG_HIDDEN);
    if (presetWarningLabels_[i]) {
      if (presetHasUnavailableAssets(state, i)) lv_obj_remove_flag(presetWarningLabels_[i], LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(presetWarningLabels_[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_remove_flag(presetCardButtons_[i], LV_OBJ_FLAG_HIDDEN);
  }
}

void LvglUi::renderPresetMode(lv_obj_t* root, UiState& state)
{
  presetBankLabel_ = label(root, state.bank.name, LV_ALIGN_TOP_MID, 0, 28,
                           &ardor_font_open_sans_semibold_28);

  lv_obj_t* tuner = button(root, "Tuner");
  lv_obj_set_size(tuner, kHeaderTunerButtonWidth, kHeaderButtonHeight);
  lv_obj_set_pos(tuner, kHeaderEdgeInset, kHeaderButtonTop);
  styleSurface(tuner, 0x25442a);
  lv_obj_set_style_text_color(lv_obj_get_child(tuner, 0), lv_color_hex(accent), 0);
  lv_obj_add_event_cb(tuner, onTunerModeClicked, LV_EVENT_PRESSED, remember(state));

  lv_obj_t* edit = button(root, "Edit");
  lv_obj_set_size(edit, kHeaderBlocksButtonWidth, kHeaderButtonHeight);
  lv_obj_set_pos(edit, kHeaderEditX, kHeaderButtonTop);
  // Opening an editor is safe on press and does not depend on the release
  // landing on a small target after a finger has shifted on the touchscreen.
  lv_obj_add_event_cb(edit, onEditModeClicked, LV_EVENT_PRESSED, remember(state));

  lv_obj_t* bankDown = button(root, "Bank -");
  bankDownButton_ = bankDown;
  lv_obj_set_size(bankDown, kHeaderBankButtonWidth, kHeaderButtonHeight);
  lv_obj_set_pos(bankDown, kHeaderBankDownX, kHeaderButtonTop);
  if (state.activeBank == kMinBank) {
    lv_obj_add_state(bankDown, LV_STATE_DISABLED);
  }
  lv_obj_add_event_cb(bankDown, onBankDownClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* bankUp = button(root, "Bank +");
  bankUpButton_ = bankUp;
  lv_obj_set_size(bankUp, kHeaderBankButtonWidth, kHeaderButtonHeight);
  lv_obj_set_pos(bankUp, kHeaderBankUpX, kHeaderButtonTop);
  if (state.activeBank == kMaxBank) {
    lv_obj_add_state(bankUp, LV_STATE_DISABLED);
  }
  lv_obj_add_event_cb(bankUp, onBankUpClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* grid = lv_obj_create(root);
  lv_obj_set_size(grid, 1200, 540);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -60);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);

  static int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(grid, cols, rows);

  for (std::size_t i = 0; i < presetCardButtons_.size(); ++i) {
    const bool populated = i < state.bank.presets.size();
    lv_obj_t* preset = button(grid, populated ? state.bank.presets[i].name : "");
    presetCardButtons_[i] = preset;
    lv_obj_set_grid_cell(preset, LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i % 2), 1,
                         LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i / 2), 1);
    styleSurface(preset, panel);
    if (populated && i == state.activePreset) {
      lv_obj_set_style_border_color(preset, lv_color_hex(accent), 0);
      lv_obj_set_style_border_opa(preset, LV_OPA_40, 0);
    }
    lv_obj_t* presetName = lv_obj_get_child(preset, 0);
    presetCardLabels_[i] = presetName;
    lv_obj_set_style_text_color(presetName, lv_color_hex(i == state.activePreset ? accent : text), 0);
    lv_obj_set_style_text_font(presetName, &ardor_font_open_sans_semibold_28, 0);
    // Use the ample card space for the preset name: 28 px at 2x scale gives
    // a 56 px label, more than triple the standard 18 px button text.
    lv_obj_set_style_transform_pivot_x(presetName, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(presetName, LV_PCT(50), 0);
    lv_obj_set_style_transform_scale(presetName, 2 * LV_SCALE_NONE, 0);
    lv_obj_t* indicator = lv_obj_create(preset);
    lv_obj_set_size(indicator, 4, LV_PCT(100));
    lv_obj_align(indicator, LV_ALIGN_LEFT_MID, 0, 0);
    styleSurface(indicator, accent);
    // A clean, softly glowing accent bar — no rim or drop shadow.
    lv_obj_set_style_border_width(indicator, 0, 0);
    lv_obj_set_style_radius(indicator, 2, 0);
    lv_obj_set_style_shadow_color(indicator, lv_color_hex(accent), 0);
    lv_obj_set_style_shadow_width(indicator, 16, 0);
    lv_obj_set_style_shadow_opa(indicator, LV_OPA_40, 0);
    lv_obj_set_style_shadow_offset_y(indicator, 0, 0);
    lv_obj_remove_flag(indicator, LV_OBJ_FLAG_CLICKABLE);
    presetIndicators_[i] = indicator;
    if (!populated || i != state.activePreset) lv_obj_add_flag(indicator, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t* unavailable = label(preset, "!  MISSING ASSET", LV_ALIGN_TOP_RIGHT, -18, 18,
                                  &ardor_font_open_sans_regular_18, warning);
    presetWarningLabels_[i] = unavailable;
    if (!populated || !presetHasUnavailableAssets(state, i)) {
      lv_obj_add_flag(unavailable, LV_OBJ_FLAG_HIDDEN);
    }
    if (!populated) lv_obj_add_flag(preset, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(preset, onPresetClicked, LV_EVENT_CLICKED, remember(state, i));
  }
}

} // namespace ardor
