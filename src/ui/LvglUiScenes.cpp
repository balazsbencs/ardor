#include "ui/LvglUi.h"

#include "ui/LampBlack.h"
#include "ui/LvglUiStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ardor {
namespace {

using namespace lvgl_ui;

// Scenes mirror the preset map: four 610 x 254 plates, the flooded plate is
// the live scene.
constexpr int kTileX = 24;
constexpr int kTileY = 80;
constexpr int kTileWidth = 610;
constexpr int kTileHeight = 254;
constexpr int kTileGap = 12;
constexpr int kTilePadX = 27;
constexpr int kFootswitchTop = 20;
constexpr int kNameTop = 49;
constexpr int kLiveNameTop = 35;
constexpr int kDetailTop = 196;
constexpr int kProgressHeight = 8;
constexpr int kProgressBottom = 22;

void onSceneClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().selectScene) {
    context->ui->actions().selectScene(context->index);
  }
}

void onPresetsClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().setSceneLayer) context->ui->actions().setSceneLayer(false);
  enterPresetMode(*context->state);
}

void onSceneEditClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  enterEditMode(*context->state);
}

void onSceneTunerClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().setTunerMode) context->ui->actions().setTunerMode(true);
  else enterTunerMode(*context->state);
}

void onSceneLooperClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->ui->actions().openLooper) context->ui->actions().openLooper();
  else enterLooperMode(*context->state);
}

std::string sceneDetail(const PresetScene& scene)
{
  std::string result;
  if (std::fabs(scene.outputTrimDb) >= 0.05f) {
    char trim[24]{};
    std::snprintf(trim, sizeof(trim), "%+.1f dB  ·  ", scene.outputTrimDb);
    result = trim;
  }
  if (scene.enterTimeMs == 0) return result + "Instant";
  char time[24]{};
  std::snprintf(time, sizeof(time), "%.1f s", scene.enterTimeMs / 1000.0f);
  return result + time;
}

} // namespace

void LvglUi::rebuildScenesView(UiState& state)
{
  if (!scenesLayer_) return;
  lv_obj_clean(scenesLayer_);
  contexts_.remove_if([](const UiEventContext& context) {
    return context.region == UiContextRegion::Scenes;
  });
  sceneCardButtons_.fill(nullptr);
  sceneHeaderStrips_.fill(nullptr);
  sceneHeaderLabels_.fill(nullptr);
  sceneNameLabels_.fill(nullptr);
  sceneDetailLabels_.fill(nullptr);
  sceneProgressFills_.fill(nullptr);
  scenesPresetLabel_ = nullptr;
  scenesUnsavedLabel_ = nullptr;
  scenesFaultLabel_ = nullptr;
  contextRegion_ = UiContextRegion::Scenes;
  renderScenesMode(scenesLayer_, state);
  contextRegion_ = UiContextRegion::None;
}

void LvglUi::renderScenesMode(lv_obj_t* root, UiState& state)
{
  const auto& preset = state.bank.presets[state.activePreset];
  lb::header(root);
  lb::textLabel(root, lb::type::headerTitle, "SCENES", text, 28, 9);
  const int nameX = 28 + lb::textWidth(lb::type::headerTitle, "SCENES") + 20;
  scenesPresetLabel_ = lb::textLabel(root, lb::type::headerSub, uppercase(preset.name), muted,
                                     nameX, 13);
  scenesUnsavedLabel_ = lb::box(root, 0, 15, lb::textWidth(lb::type::tag, "UNSAVED") + 20, 33,
                                warning);
  lb::textLabel(scenesUnsavedLabel_, lb::type::tag, "UNSAVED", warnInk, 10, 3);
  lb::placeModifiedTag(scenesPresetLabel_, scenesUnsavedLabel_);
  if (!state.dirty) lv_obj_add_flag(scenesUnsavedLabel_, LV_OBJ_FLAG_HIDDEN);
  char where[48]{};
  std::snprintf(where, sizeof(where), "BANK %02d  \xC2\xB7  PRESET %zu", state.activeBank,
                state.activePreset + 1);
  lb::textLabel(root, lb::type::headerRight, where, disabled,
                kDesignWidth - 28 - lb::textWidth(lb::type::headerRight, where), 18);

  for (std::size_t index = 0; index < 4; ++index) {
    const PresetScene fallback{"", "Scene " + std::to_string(index + 1)};
    const auto& scene = preset.sceneSet ? preset.sceneSet->scenes[index] : fallback;
    const int column = static_cast<int>(index / 2);
    const int row = static_cast<int>(index % 2);
    lv_obj_t* card = lv_button_create(root);
    lv_obj_remove_style_all(card);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_pos(card, kTileX + column * (kTileWidth + kTileGap),
                   kTileY + row * (kTileHeight + kTileGap));
    lv_obj_set_size(card, kTileWidth, kTileHeight);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(panel), 0);
    lb::setBorder(card, rule, 1);
    lv_obj_set_style_outline_color(card, lv_color_hex(text), LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(card, 2, LV_STATE_PRESSED);
    lv_obj_set_style_opa(card, LV_OPA_40, LV_STATE_DISABLED);
    sceneCardButtons_[index] = card;
    lv_obj_t* name = lb::textLabel(card, lb::type::presetName, uppercase(scene.name), text,
                                   kTilePadX - 1, kNameTop - 1);
    lv_obj_set_width(name, kTileWidth - 2 * kTilePadX);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    sceneNameLabels_[index] = name;
    // The header strip is kept as a transparent holder for the FS legend.
    lv_obj_t* header = lv_obj_create(card);
    lv_obj_remove_style_all(header);
    lv_obj_set_size(header, kTileWidth - 2, 60);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    sceneHeaderStrips_[index] = header;
    sceneHeaderLabels_[index] = lb::textLabel(header, lb::type::footswitch,
                                              "FS " + std::to_string(index + 1), disabled,
                                              kTilePadX - 1, kFootswitchTop - 1);
    sceneDetailLabels_[index] = lb::textLabel(card, lb::type::controlLabel, sceneDetail(scene),
                                              muted, kTilePadX - 1, kDetailTop - 1);
    lv_obj_t* progressTrack = lb::box(card, kTilePadX - 1,
      kTileHeight - 1 - kProgressBottom - kProgressHeight, kTileWidth - 2 * kTilePadX,
      kProgressHeight, plateHi);
    sceneProgressFills_[index] = lb::box(progressTrack, 0, 0, 0, kProgressHeight, text);
    lv_obj_add_flag(progressTrack, LV_OBJ_FLAG_HIDDEN);
    if (!preset.sceneSet) lv_obj_add_state(card, LV_STATE_DISABLED);
    lv_obj_add_event_cb(card, onSceneClicked, LV_EVENT_CLICKED, remember(state, index));
  }

  lb::rail(root);
  int x = lb::kGutter;
  const auto railButton = [&](const char* value, lb::ButtonKind kind, lv_event_cb_t callback) {
    lv_obj_t* control = lb::button(root, value, kind, x, lb::kRailButtonY);
    lv_obj_add_event_cb(control, callback, LV_EVENT_CLICKED, remember(state));
    x += lv_obj_get_style_width(control, LV_PART_MAIN) + lb::kGap;
  };
  railButton("EDIT", lb::ButtonKind::Primary, onSceneEditClicked);
  railButton("TUNER", lb::ButtonKind::Normal, onSceneTunerClicked);
  railButton("LOOPER", lb::ButtonKind::Normal, onSceneLooperClicked);
  railButton("PRESETS", lb::ButtonKind::Normal, onPresetsClicked);
  scenesFaultLabel_ = lb::textLabel(root, lb::type::legend, "", dangerText, x + 8, 653);
  lb::masterReadout(root, state.masterVolume);
  syncScenesView(state);
}

void LvglUi::syncScenesView(const UiState& state)
{
  if (!viewsInitialized_ && !scenesLayer_) return;
  const auto& preset = state.bank.presets[state.activePreset];
  if (scenesPresetLabel_) {
    lv_label_set_text(scenesPresetLabel_, uppercase(preset.name).c_str());
    lb::placeModifiedTag(scenesPresetLabel_, scenesUnsavedLabel_);
  }
  if (scenesUnsavedLabel_) {
    if (state.dirty) lv_obj_remove_flag(scenesUnsavedLabel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(scenesUnsavedLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  if (scenesFaultLabel_) {
    lv_label_set_text(scenesFaultLabel_, uppercase(state.scenes.rejection).c_str());
    if (state.scenes.rejection.empty()) lv_obj_add_flag(scenesFaultLabel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(scenesFaultLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  for (std::size_t index = 0; index < sceneCardButtons_.size(); ++index) {
    lv_obj_t* card = sceneCardButtons_[index];
    if (!card) continue;
    if (preset.sceneSet) {
      const auto& scene = preset.sceneSet->scenes[index];
      lv_label_set_text(sceneNameLabels_[index], uppercase(scene.name).c_str());
      lv_label_set_text(sceneDetailLabels_[index], uppercase(sceneDetail(scene)).c_str());
    }
    const bool goingTo = state.scenes.transitioning && state.scenes.destinationScene == index;
    const bool pending = state.scenes.pending && state.scenes.destinationScene == index;
    const bool live = !state.scenes.transitioning && !state.scenes.pending
      && state.scenes.currentScene == index;
    std::string header = "FS " + std::to_string(index + 1);
    if (live) header += "  ·  LIVE";
    else if (goingTo) header += "  ·  GOING TO";
    else if (pending) header += "  ·  PENDING";
    if (live && state.scenes.altered) {
      header += state.scenes.pedalOverride ? "  ·  PEDAL" : "  ·  ALTERED";
    }
    lv_label_set_text(sceneHeaderLabels_[index], header.c_str());
    // The live scene floods with the lamp; the destination of a timed
    // change lifts to a bone frame with its travel along the foot.
    lv_obj_set_style_bg_color(card, lv_color_hex(live ? lamp : panel), 0);
    const int border = goingTo ? 3 : 1;
    lv_obj_set_style_border_width(card, border, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(live ? lamp : goingTo ? text : rule), 0);
    lv_obj_set_style_text_color(sceneHeaderLabels_[index],
                                lv_color_hex(live ? lampInk : goingTo ? text : disabled), 0);
    lv_obj_set_pos(sceneHeaderStrips_[index], 1 - border, 1 - border);
    const lb::Type& nameType = live ? lb::type::livePresetName : lb::type::presetName;
    lb::applyType(sceneNameLabels_[index], nameType, live ? lampInk : text);
    lv_obj_set_pos(sceneNameLabels_[index], kTilePadX - border,
                   lb::textTop(nameType, live ? kLiveNameTop : kNameTop) - border);
    lv_obj_set_style_text_color(sceneDetailLabels_[index], lv_color_hex(live ? lampInk : muted), 0);
    lv_obj_set_pos(sceneDetailLabels_[index], kTilePadX - border,
                   lb::textTop(lb::type::controlLabel, kDetailTop) - border);
    lv_obj_t* progressTrack = lv_obj_get_parent(sceneProgressFills_[index]);
    lv_obj_set_pos(progressTrack, kTilePadX - border,
                   kTileHeight - border - kProgressBottom - kProgressHeight);
    if (goingTo) {
      lv_obj_remove_flag(progressTrack, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_width(sceneProgressFills_[index], static_cast<int>(std::lround(
        std::clamp(state.scenes.progress, 0.0f, 1.0f) * (kTileWidth - 2 * kTilePadX))));
    } else {
      lv_obj_add_flag(progressTrack, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

} // namespace ardor
