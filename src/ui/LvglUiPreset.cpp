#include "ui/LvglUi.h"

#include "ui/LvglUiNavigation.h"
#include "ui/LampBlack.h"
#include "ui/LvglUiStyle.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>

namespace ardor {
namespace {

using namespace lvgl_navigation;
using namespace lvgl_ui;

// Lamp Black preset screen (mockups/lvgl-taste/1-lamp-black.html): a 64 px
// header, a 2 x 2 map of 610 x 254 tiles with 12 px gaps, and the 108 px rail.
constexpr int kTileX = 24;
constexpr int kTileY = 80;
constexpr int kTileWidth = 610;
constexpr int kTileHeight = 254;
constexpr int kTileGap = 12;
// Tile content sits 26 px in from the 1 px border and 20 px down from it.
constexpr int kTilePadX = 27;
constexpr int kFootswitchTop = 20;
constexpr int kTileInnerWidth = kTileWidth - 2 * kTilePadX;
constexpr int kLiveTagPadX = 12;
constexpr int kLiveTagHeight = 37;
constexpr int kLiveTagY = 19;
constexpr int kNameTop = 49;
constexpr int kLiveNameTop = 35;
// Chain strip: one family-coloured segment per block along the tile's foot.
constexpr int kChainStripY = 201;
constexpr int kChainStripHeight = 30;
constexpr int kChainStripGap = 4;
constexpr int kChainStripCodeInset = 8;
constexpr int kChainStripCodeTop = 7;
// CSS gives amp segments 1.6 and cab segments 1.2 of the others' width.
constexpr int kAmpSegmentWeight = 16;
constexpr int kCabSegmentWeight = 12;
constexpr int kSegmentWeight = 10;
// Off-lamp tiles print their strip at 78 %.
constexpr lv_opa_t kChainStripOpa = 199;
constexpr int kBankPairStepWidth = 76;
constexpr int kBankPairLegendPad = 16;
constexpr int kMinBank = 0;
constexpr int kMaxBank = 99;
constexpr std::size_t kPresetNameMaxLength = 32;

std::string presetTelemetryText(const UiState& state)
{
  char value[96]{};
  const double latencyMs = static_cast<double>(state.settings.audioBlockSize) / 48.0;
  if (state.telemetry.budgetMs <= 0.0) {
    std::snprintf(value, sizeof(value), "%.2f MS", latencyMs);
  } else {
    const double used = std::clamp(100.0 - state.telemetry.bufferFreePercent, 0.0, 100.0);
    std::snprintf(value, sizeof(value), "%.2f MS  \xC2\xB7  BUFFER %.0f%%", latencyMs, used);
  }
  return value;
}

// "BANK 01" on the left of the header; the bank's own name, when the store
// gives one after " - ", sits beside it in bone-2.
std::string bankNumberText(const UiState& state)
{
  char value[16]{};
  std::snprintf(value, sizeof(value), "BANK %02d", std::clamp(state.activeBank, kMinBank, kMaxBank));
  return value;
}

std::string bankTitleText(const UiState& state)
{
  const auto separator = state.bank.name.find(" - ");
  return separator == std::string::npos ? std::string{}
                                        : uppercase(state.bank.name.substr(separator + 3));
}

void placeRightAligned(lv_obj_t* label, const lb::Type& type, int right)
{
  lv_obj_set_x(label, right - lb::textWidth(type, lv_label_get_text(label)));
}

int chainSegmentWeight(const std::string& type)
{
  const auto color = categoryColor(type);
  if (color == static_cast<int>(palette().family[0])) return kAmpSegmentWeight;
  if (color == static_cast<int>(palette().family[1])) return kCabSegmentWeight;
  return kSegmentWeight;
}

// On the flooded LIVE tile the strip inverts: lamp-ink cells with lamp codes.
// Elsewhere each cell takes its family colour at 78 %; bypassed blocks drop
// to the rule colour.
void renderChainStrip(lv_obj_t* strip, const std::vector<ChainStripSegment>& segments, bool live)
{
  lv_obj_clean(strip);
  if (segments.empty()) return;
  // CSS flex: each cell keeps its 8 px code inset and shares what is left of
  // the strip by weight. Positions accumulate before rounding, as a browser's
  // do, so the last cell ends exactly on the strip's right edge.
  int totalWeight = 0;
  for (const auto& segment : segments) totalWeight += chainSegmentWeight(segment.type);
  const auto count = static_cast<int>(segments.size());
  const double share = static_cast<double>(kTileInnerWidth - (count - 1) * kChainStripGap
                                           - count * kChainStripCodeInset) / totalWeight;
  double x = 0.0;
  for (const auto& segment : segments) {
    const std::uint32_t fill = live ? lampInk
      : (segment.enabled ? static_cast<std::uint32_t>(categoryColor(segment.type)) : rule);
    const std::uint32_t ink = live ? (segment.enabled ? lamp : disabled)
      : (segment.enabled ? bg : muted);
    const double width = kChainStripCodeInset + share * chainSegmentWeight(segment.type);
    const int left = static_cast<int>(std::lround(x));
    const int right = static_cast<int>(std::lround(x + width));
    lv_obj_t* cell = lb::box(strip, left, 0, right - left, kChainStripHeight, fill);
    if (!live) lv_obj_set_style_opa(cell, kChainStripOpa, 0);
    lv_obj_t* code = lb::textLabel(cell, lb::type::code, segment.code, ink,
                                   kChainStripCodeInset, kChainStripCodeTop);
    lv_obj_set_width(code, right - left - kChainStripCodeInset);
    lv_label_set_long_mode(code, LV_LABEL_LONG_CLIP);
    x += width + kChainStripGap;
  }
}

// Returns the x after the last item. Widths are read from the style, which
// holds the value set at build, because coordinates lag until a layout pass.
int layoutRow(const std::vector<lv_obj_t*>& items, int x, int gap)
{
  for (lv_obj_t* item : items) {
    lv_obj_set_x(item, x);
    x += lv_obj_get_style_width(item, LV_PART_MAIN) + gap;
  }
  return x;
}

// Edit, Tuner, Looper, [- BANK +], Setup. The bank pair shares its borders,
// so only its outer edges take the rail gap.
void layoutPresetRail(const std::vector<lv_obj_t*>& items)
{
  if (items.size() != 7) return;
  const int pairX = layoutRow({items[0], items[1], items[2]}, lb::kGutter, lb::kGap);
  const int setupX = layoutRow({items[3], items[4], items[5]}, pairX, 0) + lb::kGap;
  lv_obj_set_x(items[6], setupX);
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
  presetChainStrips_.fill(nullptr);
  presetChainStripCache_ = {};
  presetChainStripLive_.fill(false);
  presetWarningLabels_.fill(nullptr);
  bankDownButton_ = nullptr;
  bankUpButton_ = nullptr;
  presetBankLabel_ = nullptr;
  presetBankTitleLabel_ = nullptr;
  presetRailItems_.clear();
  presetTelemetryLabel_ = nullptr;
  presetMasterValueLabel_ = nullptr;
  presetMasterMeter_ = nullptr;
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
    lv_label_set_text(presetTelemetryLabel_, presetTelemetryText(state).c_str());
    placeRightAligned(presetTelemetryLabel_, lb::type::headerRight, kDesignWidth - 28);
  }
  lb::syncMasterReadout(presetMasterValueLabel_, state.masterVolume);
  if (presetBankLabel_) {
    lv_label_set_text(presetBankLabel_, bankNumberText(state).c_str());
  }
  if (presetBankTitleLabel_ && presetBankLabel_) {
    lv_label_set_text(presetBankTitleLabel_, bankTitleText(state).c_str());
    lv_obj_set_x(presetBankTitleLabel_,
                 28 + lb::textWidth(lb::type::bank, lv_label_get_text(presetBankLabel_)) + 20);
  }
  if (presetLooperLabel_) {
    lv_obj_t* looper = lv_obj_get_parent(presetLooperLabel_);
    const std::string legend =
      state.looper.telemetry.sessionState == LooperSessionState::Inactive ? "LOOPER" : "RESUME LOOP";
    if (legend != lv_label_get_text(presetLooperLabel_)) {
      lv_obj_set_width(looper, lb::buttonWidth(legend));
      lb::setButtonText(looper, legend);
      layoutPresetRail(presetRailItems_);
    }
  }
  if (bankDownButton_) {
    lb::styleButton(bankDownButton_,
                    state.activeBank == kMinBank ? lb::ButtonKind::Off : lb::ButtonKind::Normal);
    if (state.activeBank == kMinBank) lv_obj_add_state(bankDownButton_, LV_STATE_DISABLED);
    else lv_obj_remove_state(bankDownButton_, LV_STATE_DISABLED);
  }
  if (bankUpButton_) {
    lb::styleButton(bankUpButton_,
                    state.activeBank == kMaxBank ? lb::ButtonKind::Off : lb::ButtonKind::Normal);
    if (state.activeBank == kMaxBank) lv_obj_add_state(bankUpButton_, LV_STATE_DISABLED);
    else lv_obj_remove_state(bankUpButton_, LV_STATE_DISABLED);
  }
  if (editPresetLabel_) {
    lv_label_set_text(editPresetLabel_, lb::editIdentityText(state).c_str());
    lb::placeModifiedTag(editPresetLabel_, editModifiedLabel_);
  }
  if (editModifiedLabel_) {
    if (state.dirty) lv_obj_remove_flag(editModifiedLabel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(editModifiedLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  if (editModuleCountLabel_) {
    const auto count = lb::editCountText(state);
    lv_label_set_text(editModuleCountLabel_, count.c_str());
    placeRightAligned(editModuleCountLabel_, lb::type::headerRight, kDesignWidth - 28);
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
    const bool sessionLocked = state.looper.telemetry.sessionState != LooperSessionState::Inactive;
    stylePresetCard(state, i);
    if (sessionLocked) lv_obj_add_state(presetCardButtons_[i], LV_STATE_DISABLED);
    else lv_obj_remove_state(presetCardButtons_[i], LV_STATE_DISABLED);
    lv_obj_remove_flag(presetCardButtons_[i], LV_OBJ_FLAG_HIDDEN);
  }
}

void LvglUi::stylePresetCard(const UiState& state, std::size_t index)
{
  lv_obj_t* card = presetCardButtons_[index];
  if (!card || index >= state.bank.presets.size()) return;
  const bool isActive = index == state.activePreset;
  const bool unavailable = presetHasUnavailableAssets(state, index);
  // A faulted LIVE preset keeps the fault border and legend on a plain plate:
  // lamp-on-lamp would hide the fault, so only healthy LIVE tiles flood.
  const bool floods = isActive && !unavailable;
  lv_obj_set_style_bg_color(card, lv_color_hex(floods ? lamp : panel), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(
    unavailable ? palette().faultLine : (floods ? lamp : rule)), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_COVER, 0);

  lv_obj_t* name = presetCardLabels_[index];
  const lb::Type& nameType = floods ? lb::type::livePresetName : lb::type::presetName;
  lb::applyType(name, nameType, unavailable ? danger : (floods ? lampInk : text));
  lv_obj_set_y(name, lb::textTop(nameType, floods ? kLiveNameTop : kNameTop) - 1);

  lv_label_set_text(presetHeaderLabels_[index], ("FS " + std::to_string(index + 1)).c_str());
  lv_obj_set_style_text_color(presetHeaderLabels_[index],
                              lv_color_hex(floods ? lampInk : disabled), 0);
  if (lv_obj_t* liveTag = presetHeaderStrips_[index]) {
    if (isActive) lv_obj_remove_flag(liveTag, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(liveTag, LV_OBJ_FLAG_HIDDEN);
    // On a faulted LIVE tile the tag prints lamp-on-plate so it still reads.
    lv_obj_set_style_bg_color(liveTag, lv_color_hex(floods ? lampInk : lamp), 0);
    lv_obj_set_style_text_color(lv_obj_get_child(liveTag, 0),
                                lv_color_hex(floods ? lamp : lampInk), 0);
  }
  if (presetWarningLabels_[index]) {
    if (unavailable) lv_obj_remove_flag(presetWarningLabels_[index], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(presetWarningLabels_[index], LV_OBJ_FLAG_HIDDEN);
  }
  if (lv_obj_t* strip = presetChainStrips_[index]) {
    auto segments = presetChainStrip(state.bank.presets[index]);
    if (segments != presetChainStripCache_[index] || floods != presetChainStripLive_[index]) {
      renderChainStrip(strip, segments, floods);
      presetChainStripCache_[index] = std::move(segments);
      presetChainStripLive_[index] = floods;
    }
  }
}

void LvglUi::renderPresetMode(lv_obj_t* root, UiState& state)
{
  // ---- header: bank number and name on the left, latency on the right ----
  lb::header(root);
  presetBankLabel_ = lb::textLabel(root, lb::type::bank, bankNumberText(state), text, 28, 7);
  presetBankTitleLabel_ = lb::textLabel(root, lb::type::bankName, bankTitleText(state), muted,
    28 + lb::textWidth(lb::type::bank, bankNumberText(state)) + 20, 13);
  // The engine publishes buffer telemetry once per second. It is secondary,
  // so it sits small in bone-3 beside the configured block latency.
  presetTelemetryLabel_ = lb::textLabel(root, lb::type::headerRight, presetTelemetryText(state),
                                   disabled, 0, 18);
  placeRightAligned(presetTelemetryLabel_, lb::type::headerRight, kDesignWidth - 28);

  // ---- preset map: 1 / 3 over 2 / 4 mirrors the footswitch corners ----
  for (std::size_t i = 0; i < presetCardButtons_.size(); ++i) {
    const bool populated = i < state.bank.presets.size();
    // Column-major maps each slot to its physical footswitch corner: FS1/FS2
    // on the left, FS3/FS4 on the right. Do not change this to reading order.
    const int column = static_cast<int>(i / 2);
    const int row = static_cast<int>(i % 2);
    lv_obj_t* preset = lv_button_create(root);
    lv_obj_remove_style_all(preset);
    lv_obj_remove_flag(preset, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(preset, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_pos(preset, kTileX + column * (kTileWidth + kTileGap),
                   kTileY + row * (kTileHeight + kTileGap));
    lv_obj_set_size(preset, kTileWidth, kTileHeight);
    lv_obj_set_style_bg_opa(preset, LV_OPA_COVER, 0);
    lb::setBorder(preset, rule, 1);
    lv_obj_set_style_outline_color(preset, lv_color_hex(text), LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(preset, 2, LV_STATE_PRESSED);
    lv_obj_set_style_opa(preset, LV_OPA_40, LV_STATE_DISABLED);
    presetCardButtons_[i] = preset;
    // Children sit inside the 1 px border, so tile offsets lose one pixel.
    // The name is the first child: tests and retained syncs read it there.
    lv_obj_t* presetName = lb::textLabel(preset, lb::type::presetName,
                                    populated ? uppercase(state.bank.presets[i].name) : "",
                                    text, kTilePadX - 1, kNameTop);
    lv_obj_set_width(presetName, kTileInnerWidth);
    lv_label_set_long_mode(presetName, LV_LABEL_LONG_MODE_DOTS);
    presetCardLabels_[i] = presetName;
    presetHeaderLabels_[i] = lb::textLabel(preset, lb::type::footswitch, "FS " + std::to_string(i + 1),
                                      disabled, kTilePadX - 1, kFootswitchTop - 1);
    // LIVE tag: lamp-ink plate with lamp lettering, top right.
    const int tagWidth = lb::textWidth(lb::type::liveTag, "LIVE") + 2 * kLiveTagPadX;
    lv_obj_t* liveTag = lb::box(preset, kTileWidth - kTilePadX - tagWidth - 1, kLiveTagY - 1,
                                tagWidth, kLiveTagHeight, lampInk);
    lb::textLabel(liveTag, lb::type::liveTag, "LIVE", lamp, kLiveTagPadX, 2);
    presetHeaderStrips_[i] = liveTag;
    // The chain strip names every block in its family colour.
    lv_obj_t* strip = lv_obj_create(preset);
    lv_obj_remove_style_all(strip);
    lv_obj_set_pos(strip, kTilePadX - 1, kChainStripY - 1);
    lv_obj_set_size(strip, kTileInnerWidth, kChainStripHeight);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    presetChainStrips_[i] = strip;
    // Fault legend: a bordered tag above the strip, part of the plate's own
    // nomenclature rather than a tooltip.
    const std::string fault = "ASSET NOT FOUND";
    const int faultWidth = lb::textWidth(lb::type::tag, fault) + 20;
    lv_obj_t* unavailable = lb::box(preset, kTilePadX - 1, kChainStripY - 1 - 12 - 33,
                                    faultWidth, 33, panel, palette().faultLine, 1);
    lb::textLabel(unavailable, lb::type::tag, fault, danger, 9, 3);
    presetWarningLabels_[i] = unavailable;
    lv_obj_add_flag(unavailable, LV_OBJ_FLAG_HIDDEN);
    stylePresetCard(state, i);
    if (state.looper.telemetry.sessionState != LooperSessionState::Inactive) {
      lv_obj_add_state(preset, LV_STATE_DISABLED);
    }
    if (!populated) lv_obj_add_flag(preset, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(preset, onPresetClicked, LV_EVENT_CLICKED, remember(state, i));
  }

  // ---- bottom rail: transport, the bank pair and the master meter ----
  lb::rail(root);
  presetRailItems_.clear();
  lv_obj_t* edit = lb::button(root, "EDIT", lb::ButtonKind::Primary, 0, lb::kRailButtonY);
  lv_obj_add_event_cb(edit, onEditModeClicked, LV_EVENT_PRESSED, remember(state));
  lv_obj_t* tuner = lb::button(root, "TUNER", lb::ButtonKind::Normal, 0, lb::kRailButtonY);
  lv_obj_add_event_cb(tuner, onTunerModeClicked, LV_EVENT_PRESSED, remember(state));
  const std::string looperLegend =
    state.looper.telemetry.sessionState == LooperSessionState::Inactive ? "LOOPER" : "RESUME LOOP";
  lv_obj_t* looper = lb::button(root, looperLegend, lb::ButtonKind::Normal, 0, lb::kRailButtonY);
  presetLooperLabel_ = lb::buttonLabel(looper);
  lv_obj_add_event_cb(looper, onLooperClicked, LV_EVENT_PRESSED, remember(state));
  // Bank pair: two step buttons around a recessed legend, borders shared.
  lv_obj_t* bankDown = lb::button(root, "-", lb::ButtonKind::Normal, 0, lb::kRailButtonY,
                                  kBankPairStepWidth);
  bankDownButton_ = bankDown;
  lv_obj_add_event_cb(bankDown, onBankDownClicked, LV_EVENT_CLICKED, remember(state));
  const int legendWidth = lb::textWidth(lb::type::pairLegend, "BANK") + 2 * kBankPairLegendPad;
  lv_obj_t* bankLegend = lb::box(root, 0, lb::kRailButtonY, legendWidth, lb::kButtonHeight, bg);
  lb::setBorder(bankLegend, rule, 1,
                static_cast<lv_border_side_t>(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM));
  lb::centeredText(bankLegend, lb::type::pairLegend, "BANK", muted, 0, -1, legendWidth,
                   lb::kButtonHeight);
  lv_obj_t* bankUp = lb::button(root, "+", lb::ButtonKind::Normal, 0, lb::kRailButtonY,
                                kBankPairStepWidth);
  bankUpButton_ = bankUp;
  lv_obj_add_event_cb(bankUp, onBankUpClicked, LV_EVENT_CLICKED, remember(state));
  if (state.activeBank == kMinBank) {
    lb::styleButton(bankDown, lb::ButtonKind::Off);
    lv_obj_add_state(bankDown, LV_STATE_DISABLED);
  }
  if (state.activeBank == kMaxBank) {
    lb::styleButton(bankUp, lb::ButtonKind::Off);
    lv_obj_add_state(bankUp, LV_STATE_DISABLED);
  }
  lv_obj_t* setup = lb::button(root, "SETUP", lb::ButtonKind::Normal, 0, lb::kRailButtonY);
  lv_obj_add_event_cb(setup, onSettingsClicked, LV_EVENT_PRESSED, remember(state));
  presetRailItems_ = {edit, tuner, looper, bankDown, bankLegend, bankUp, setup};
  layoutPresetRail(presetRailItems_);

  // ---- master readout, right-aligned: legend, value, then segments ----
  presetMasterValueLabel_ = lb::masterReadout(root, state.masterVolume);
  presetMasterMeter_ = nullptr;
}

void LvglUi::openPresetNameEditor(UiState& state)
{
  if (!presetNameOverlay_ || !presetNameField_
      || state.activePreset >= state.bank.presets.size()) {
    return;
  }
  presetNameEditorOpen_ = true;
  editingSceneName_ = false;
  lv_textarea_set_text(presetNameField_, state.bank.presets[state.activePreset].name.c_str());
  lv_label_set_text(presetNameMessageLabel_, "");
  lv_keyboard_set_textarea(presetNameKeyboard_, presetNameField_);
  lv_obj_remove_flag(presetNameOverlay_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(presetNameOverlay_);
}

void LvglUi::openSceneNameEditor(UiState& state)
{
  const auto& set = state.bank.presets[state.activePreset].sceneSet;
  if (!presetNameOverlay_ || !presetNameField_ || !set
      || state.editingScene >= set->scenes.size()) return;
  presetNameEditorOpen_ = true;
  editingSceneName_ = true;
  sceneNameTarget_ = state.editingScene;
  lv_textarea_set_text(presetNameField_, set->scenes[sceneNameTarget_].name.c_str());
  lv_label_set_text(presetNameMessageLabel_, "");
  lv_keyboard_set_textarea(presetNameKeyboard_, presetNameField_);
  lv_obj_remove_flag(presetNameOverlay_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(presetNameOverlay_);
}

void LvglUi::cancelPresetNameEditor()
{
  presetNameEditorOpen_ = false;
  editingSceneName_ = false;
  if (presetNameOverlay_) lv_obj_add_flag(presetNameOverlay_, LV_OBJ_FLAG_HIDDEN);
}

void LvglUi::savePresetName(UiState& state)
{
  if (!presetNameField_ || state.activePreset >= state.bank.presets.size()) return;
  const std::string name = trimmed(lv_textarea_get_text(presetNameField_));
  if (name.empty()) {
    lv_label_set_text(presetNameMessageLabel_, editingSceneName_
      ? "Enter a scene name" : "Enter a preset name");
    return;
  }

  if (editingSceneName_) {
    if (name.size() > 24) {
      lv_label_set_text(presetNameMessageLabel_, "Scene names use at most 24 characters");
      return;
    }
    const auto currentEditingScene = state.editingScene;
    state.editingScene = sceneNameTarget_;
    const bool changed = renameEditingScene(state, name);
    state.editingScene = currentEditingScene;
    cancelPresetNameEditor();
    if (changed) invalidate(UiChange::Header | UiChange::Presets | UiChange::Chain);
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
  constexpr int kSheetWidth = 1232;
  constexpr int kSheetHeight = 672;
  constexpr int kInset = lb::kDialogInset;
  presetNameOverlay_ = lb::createOverlay(root);
  lv_obj_t* sheet = lb::createDialog(presetNameOverlay_, kSheetWidth, kSheetHeight, "RENAME");
  lv_obj_t* hint = lb::textLabel(sheet, lb::type::itemSubtitle,
                                 "The new name is saved to the active slot.", muted, kInset - 1, 70);
  (void) hint;

  const int buttonsWidth = 2 * lb::kButtonMinWidth + lb::kGap;
  const int fieldWidth = kSheetWidth - 2 - 2 * kInset - buttonsWidth - lb::kGap;
  presetNameField_ = lv_textarea_create(sheet);
  lv_obj_set_pos(presetNameField_, kInset - 1, 104);
  lv_obj_set_size(presetNameField_, fieldWidth, lb::kButtonHeight);
  lv_textarea_set_one_line(presetNameField_, true);
  lv_textarea_set_max_length(presetNameField_, kPresetNameMaxLength);
  lv_textarea_set_placeholder_text(presetNameField_, "Preset name");
  lv_textarea_set_text(presetNameField_,
    state.activePreset < state.bank.presets.size()
      ? state.bank.presets[state.activePreset].name.c_str() : "");
  lb::styleField(presetNameField_);
  lv_obj_set_style_text_font(presetNameField_, &ardor_lb_cond600_22, 0);
  lv_obj_set_style_pad_ver(presetNameField_, 17, 0);

  const int cancelX = kInset - 1 + fieldWidth + lb::kGap;
  lv_obj_t* cancel = lb::button(sheet, "CANCEL", lb::ButtonKind::Normal, cancelX, 104);
  lv_obj_add_event_cb(cancel, onPresetNameCancelClicked, LV_EVENT_CLICKED, remember(state));
  lv_obj_t* save = lb::button(sheet, "SAVE", lb::ButtonKind::Primary,
                              cancelX + lb::kButtonMinWidth + lb::kGap, 104);
  lv_obj_add_event_cb(save, onPresetNameSaveClicked, LV_EVENT_CLICKED, remember(state));

  presetNameMessageLabel_ = lb::textLabel(sheet, lb::type::legend, "", dangerText, kInset - 1, 176);

  presetNameKeyboard_ = lv_keyboard_create(sheet);
  lv_obj_set_size(presetNameKeyboard_, kSheetWidth - 2 - 2 * kInset, kSheetHeight - 2 - 208 - kInset);
  // Keyboard widgets default to a bottom alignment. Pin this one explicitly so
  // all rows stay inside the sheet instead of inheriting the default offset.
  lv_obj_align(presetNameKeyboard_, LV_ALIGN_TOP_LEFT, kInset - 1, 208);
  lb::styleKeyboard(presetNameKeyboard_);
  lv_keyboard_set_textarea(presetNameKeyboard_, presetNameField_);
  lv_obj_add_event_cb(presetNameKeyboard_, onPresetNameSaveClicked,
                      LV_EVENT_READY, remember(state));
  lv_obj_add_event_cb(presetNameKeyboard_, onPresetNameCancelClicked,
                      LV_EVENT_CANCEL, remember(state));

  if (!presetNameEditorOpen_) lv_obj_add_flag(presetNameOverlay_, LV_OBJ_FLAG_HIDDEN);
}

} // namespace ardor
