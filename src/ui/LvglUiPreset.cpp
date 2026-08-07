#include "ui/LvglUi.h"

#include "ui/LvglUiNavigation.h"
#include "ui/LvglUiStyle.h"
#include "ui/UiStatusPresentation.h"
#include "ui/fonts/SairaCondSemibold38.h"

#include <algorithm>
#include <cctype>
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
  presetHeaderStrips_.fill(nullptr);
  presetLamps_.fill(nullptr);
  presetNumerals_.fill(nullptr);
  presetWarningLabels_.fill(nullptr);
  presetBankLabel_ = nullptr;
  bankDownButton_ = nullptr;
  bankUpButton_ = nullptr;
  presetMidiLamp_ = nullptr;
  presetMidiLabel_ = nullptr;
  presetBufferUsageLabel_ = nullptr;
  presetMasterValueLabel_ = nullptr;
  presetMasterScaleFill_ = nullptr;
  presetMasterPointer_ = nullptr;
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
  if (masterVolumeScaleFill_) {
    lv_obj_set_width(masterVolumeScaleFill_, std::clamp(state.masterVolume, 0, 100) * 120 / 100);
  }
  if (presetMidiLamp_) {
    styleSurface(presetMidiLamp_,
                 state.controlInputs.midiConnected ? palette().family[3] : rule);
    lv_obj_set_style_border_width(presetMidiLamp_, 0, 0);
  }
  if (presetMidiLabel_) {
    lv_label_set_text(presetMidiLabel_, state.controlInputs.midiConnected ? "MIDI ON" : "MIDI");
    lv_obj_set_style_text_color(presetMidiLabel_,
      lv_color_hex(state.controlInputs.midiConnected ? palette().family[3] : muted), 0);
  }
  const auto bufferUsage = makeBufferUsagePresentation(state);
  if (presetBufferUsageLabel_) {
    lv_label_set_text(presetBufferUsageLabel_, bufferUsage.text.c_str());
    lv_obj_set_style_text_color(
      presetBufferUsageLabel_, lv_color_hex(bufferUsage.color), 0);
  }
  if (editBufferUsageLabel_) {
    lv_label_set_text(editBufferUsageLabel_, bufferUsage.text.c_str());
    lv_obj_set_style_text_color(
      editBufferUsageLabel_, lv_color_hex(bufferUsage.color), 0);
  }
  const int masterPct = std::clamp(state.masterVolume, 0, 100);
  if (presetMasterValueLabel_) {
    lv_label_set_text(presetMasterValueLabel_, std::to_string(masterPct).c_str());
  }
  if (presetMasterScaleFill_) {
    lv_obj_set_width(presetMasterScaleFill_, masterPct * kMasterScaleWidth / 100);
  }
  if (presetMasterPointer_) {
    lv_obj_align(presetMasterPointer_, LV_ALIGN_TOP_LEFT,
                masterPct * kMasterScaleWidth / 100, -3);
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
    lv_obj_set_style_text_color(presetCardLabels_[i], lv_color_hex(unavailable ? danger : text), 0);
    lv_obj_set_style_border_color(presetCardButtons_[i], lv_color_hex(unavailable ? palette().faultLine : (isActive ? lamp : rule)), 0);
    lv_obj_set_style_border_opa(presetCardButtons_[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(presetHeaderStrips_[i], lv_color_hex(isActive ? lamp : panelAlt), 0);
    lv_obj_set_style_bg_color(presetLamps_[i], lv_color_hex(unavailable ? palette().family[5] : (isActive ? lamp : rule)), 0);
    lv_obj_set_style_text_color(presetNumerals_[i], lv_color_hex(isActive ? lamp : muted), 0);
    if (presetWarningLabels_[i]) {
      if (unavailable) lv_obj_remove_flag(presetWarningLabels_[i], LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(presetWarningLabels_[i], LV_OBJ_FLAG_HIDDEN);
    }
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

  label(topRail, "ARDOR", LV_ALIGN_LEFT_MID, kRailEdgeInset, 0,
        &ardor_font_saira_cond_semibold_22, text);
  presetBankLabel_ = label(topRail, state.bank.name, LV_ALIGN_LEFT_MID, 128, 0,
                           &ardor_font_saira_cond_medium_18, muted);
  // The fixed sample rate and active persisted audio quantum remain visible at
  // a glance so the player can confirm a latency-tuning change after restart.
  label(topRail, "48 KHZ \xC2\xB7 BLK " + std::to_string(state.settings.audioBlockSize),
        LV_ALIGN_RIGHT_MID, -76, 0,
        &ardor_font_saira_cond_medium_18, muted);
  const auto bufferUsage = makeBufferUsagePresentation(state);
  presetBufferUsageLabel_ = label(
    topRail, bufferUsage.text, LV_ALIGN_RIGHT_MID, -250, 0,
    &ardor_font_saira_cond_medium_18, bufferUsage.color);
  presetMidiLamp_ = lv_obj_create(topRail);
  lv_obj_set_size(presetMidiLamp_, 11, 11);
  lv_obj_align(presetMidiLamp_, LV_ALIGN_RIGHT_MID, -60, 0);
  lv_obj_set_style_border_width(presetMidiLamp_, 0, 0);
  lv_obj_remove_flag(presetMidiLamp_, LV_OBJ_FLAG_CLICKABLE);
  presetMidiLabel_ = label(topRail, "MIDI", LV_ALIGN_RIGHT_MID, -kRailEdgeInset, 0,
                           &ardor_font_saira_cond_medium_18, muted);

  // ---- preset grid: fills the 580 px band between the two rails ----
  lv_obj_t* grid = lv_obj_create(root);
  lv_obj_set_size(grid, kDesignWidth - 2 * kRailEdgeInset,
                  kDesignHeight - kTopRailHeight - kBottomRailHeight - 36);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(grid, kRailEdgeInset, kTopRailHeight + 18);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
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
    if (populated && i == state.activePreset) {
      lv_obj_set_style_border_color(preset, lv_color_hex(lamp), 0);
      lv_obj_set_style_border_opa(preset, LV_OPA_COVER, 0);
    }
    lv_obj_t* presetName = lv_obj_get_child(preset, 0);
    presetCardLabels_[i] = presetName;
    lv_obj_set_style_text_color(presetName, lv_color_hex(text), 0);
    // Real 38 px Saira Condensed face, per docs/lvgl-ui-redesign-spec.md §4c
    // ("If Phase 2 has landed, use the real 38 px face and delete the three
    // transform calls") — Phase 2 has landed, so no bitmap-scaling hack.
    lv_obj_set_style_text_font(presetName, &ardor_font_saira_cond_semibold_38, 0);
    lv_obj_set_style_text_letter_space(presetName, 1, 0);
    // The numeral, name, and family row read as one engraved group centred
    // on the card, not spread across its full height (mockup §1 · Preset
    // tiles, .mod .bd{justify-content:center;gap:10px}).
    lv_obj_align(presetName, LV_ALIGN_LEFT_MID, 22, 6);
    lv_obj_t* header = lv_obj_create(preset);
    lv_obj_set_size(header, LV_PCT(100), 26);
    lv_obj_set_pos(header, 0, 0);
    styleSurface(header, i == state.activePreset ? lamp : panelAlt);
    lv_obj_set_style_border_width(header, 0, 0);
    // The default theme's own padding would otherwise inset the title and
    // lamp from the header's true edges (this bit the lamp specifically:
    // it stayed ~30px short of the corner despite the header spanning the
    // full card width).
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* titleLabel = label(header, "PRESET " + std::to_string(i + 1), LV_ALIGN_LEFT_MID, 13, 0,
                                 &ardor_font_saira_cond_medium_18, i == state.activePreset ? bg : muted);
    lv_obj_set_style_text_letter_space(titleLabel, 5, 0);
    lv_obj_t* liveLamp = lv_obj_create(header);
    lv_obj_set_size(liveLamp, 11, 11);
    lv_obj_align(liveLamp, LV_ALIGN_RIGHT_MID, -12, 0);
    styleSurface(liveLamp, i == state.activePreset ? lamp : rule);
    lv_obj_set_style_border_width(liveLamp, 0, 0);
    lv_obj_remove_flag(liveLamp, LV_OBJ_FLAG_CLICKABLE);
    presetHeaderStrips_[i] = header;
    presetLamps_[i] = liveLamp;
    // Numerals read in Saira Light, not the condensed nomenclature cut — see
    // docs/lvgl-ui-redesign-spec.md §5.
    lv_obj_t* numeral = label(preset, std::to_string(i + 1), LV_ALIGN_LEFT_MID, 22, -46,
                               &ardor_font_saira_light_44,
                               i == state.activePreset ? lamp : muted);
    presetNumerals_[i] = numeral;

    if (populated) {
      // Engraved chain nomenclature: bar-and-label pairs sized to their own
      // text, separated by a printed divider rather than fixed columns, so a
      // long category name (Modulation, Dual Rig) never clips mid-word.
      constexpr std::size_t kFamilyTicks = 3;
      constexpr int kFamilyLetterSpace = 2;
      const auto& blocks = state.bank.presets[i].blocks;
      const std::size_t tickCount = std::min(kFamilyTicks, blocks.size());
      int x = 22;
      for (std::size_t tick = 0; tick < tickCount; ++tick) {
        if (tick > 0) {
          lv_obj_t* divider = lv_obj_create(preset);
          lv_obj_remove_style_all(divider);
          lv_obj_set_size(divider, 1, 16);
          lv_obj_align(divider, LV_ALIGN_LEFT_MID, x, 58);
          lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
          lv_obj_set_style_bg_color(divider, lv_color_hex(rule), 0);
          lv_obj_remove_flag(divider, LV_OBJ_FLAG_CLICKABLE);
          x += 12;
        }
        const std::string family = uppercase(blocks[tick].label);
        lv_point_t textSize{};
        lv_text_get_size(&textSize, family.c_str(), &ardor_font_saira_cond_medium_18,
                         kFamilyLetterSpace, 0, LV_COORD_MAX, LV_TEXT_FLAG_EXPAND);
        lv_obj_t* familyTick = lv_obj_create(preset);
        lv_obj_set_size(familyTick, textSize.x, 3);
        lv_obj_align(familyTick, LV_ALIGN_LEFT_MID, x, 46);
        styleSurface(familyTick, categoryColor(blocks[tick].type));
        lv_obj_set_style_border_width(familyTick, 0, 0);
        lv_obj_remove_flag(familyTick, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t* familyLabel = label(preset, family, LV_ALIGN_LEFT_MID, x, 58,
                                      &ardor_font_saira_cond_medium_18, muted);
        lv_obj_set_style_text_letter_space(familyLabel, kFamilyLetterSpace, 0);
        x += textSize.x + 22;
      }
    }
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
  lv_obj_set_size(scaleGroup, kMasterScaleWidth, 62);
  lv_obj_align(scaleGroup, LV_ALIGN_RIGHT_MID, -kRailEdgeInset, 0);
  lv_obj_remove_flag(scaleGroup, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(scaleGroup, LV_OBJ_FLAG_CLICKABLE);
  label(scaleGroup, "MASTER", LV_ALIGN_TOP_LEFT, 0, 4,
        &ardor_font_saira_cond_medium_18, muted);
  presetMasterValueLabel_ = label(scaleGroup, std::to_string(state.masterVolume),
                                  LV_ALIGN_TOP_RIGHT, 0, 0, &ardor_font_saira_light_44, text);
  lv_obj_t* track = lv_obj_create(scaleGroup);
  lv_obj_remove_style_all(track);
  lv_obj_set_size(track, kMasterScaleWidth, 14);
  lv_obj_align(track, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_border_width(track, 1, 0);
  lv_obj_set_style_border_color(track, lv_color_hex(rule), 0);
  lv_obj_set_style_border_side(track, LV_BORDER_SIDE_BOTTOM, 0);
  for (int t = 0; t <= 8; ++t) {
    const bool majorTick = t % 4 == 0;
    lv_obj_t* tick = lv_obj_create(track);
    lv_obj_remove_style_all(tick);
    lv_obj_set_size(tick, 1, majorTick ? 11 : 6);
    lv_obj_align(tick, LV_ALIGN_BOTTOM_LEFT, t * kMasterScaleWidth / 8, 0);
    lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tick, lv_color_hex(majorTick ? muted : rule), 0);
  }
  lv_obj_t* fill = lv_obj_create(track);
  lv_obj_remove_style_all(fill);
  lv_obj_set_size(fill, state.masterVolume * kMasterScaleWidth / 100, 3);
  lv_obj_align(fill, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(fill, lv_color_hex(text), 0);
  presetMasterScaleFill_ = fill;
  lv_obj_t* pointer = lv_obj_create(track);
  lv_obj_remove_style_all(pointer);
  lv_obj_set_size(pointer, 3, 20);
  lv_obj_align(pointer, LV_ALIGN_TOP_LEFT, state.masterVolume * kMasterScaleWidth / 100, -3);
  lv_obj_set_style_bg_opa(pointer, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(pointer, lv_color_hex(lamp), 0);
  lv_obj_set_style_translate_x(pointer, -1, 0);
  presetMasterPointer_ = pointer;
}

} // namespace ardor
