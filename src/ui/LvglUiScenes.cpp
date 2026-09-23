#include "ui/LvglUi.h"

#include "ui/LvglUiStyle.h"
#include "ui/fonts/SairaCondSemibold52.h"
#include "ui/fonts/SairaCondSemibold72.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ardor {
namespace {

using namespace lvgl_ui;

constexpr int kTopRailHeight = 52;
constexpr int kBottomRailHeight = 88;
constexpr int kBottomRailY = kDesignHeight - kBottomRailHeight;
constexpr int kEdge = 28;

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
  lv_obj_t* top = lv_obj_create(root);
  lv_obj_set_size(top, kDesignWidth, kTopRailHeight);
  lv_obj_set_pos(top, 0, 0);
  styleSurface(top, panel);
  lv_obj_set_style_radius(top, 0, 0);
  lv_obj_set_style_pad_all(top, 0, 0);
  lv_obj_set_style_border_side(top, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
  label(top, "SCENES", LV_ALIGN_LEFT_MID, kEdge, 0,
        &ardor_font_saira_cond_semibold_22, lamp);
  scenesPresetLabel_ = label(top, uppercase(preset.name), LV_ALIGN_CENTER, 0, 0,
                             &ardor_font_saira_cond_semibold_28);
  label(top, "BANK " + std::to_string(state.activeBank + 1) + "  /  PRESET "
             + std::to_string(state.activePreset + 1),
        LV_ALIGN_RIGHT_MID, -kEdge, 0, &ardor_font_saira_cond_medium_18, muted);
  scenesUnsavedLabel_ = label(top, "UNSAVED", LV_ALIGN_RIGHT_MID, -300, 0,
                              &ardor_font_saira_cond_medium_18, warning);
  if (!state.dirty) lv_obj_add_flag(scenesUnsavedLabel_, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* grid = lv_obj_create(root);
  lv_obj_set_size(grid, kDesignWidth - 2 * kEdge,
                  kDesignHeight - kTopRailHeight - kBottomRailHeight - 36);
  lv_obj_set_pos(grid, kEdge, kTopRailHeight + 18);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_style_pad_column(grid, 14, 0);
  lv_obj_set_style_pad_row(grid, 14, 0);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  static int32_t cols[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static int32_t rows[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  lv_obj_set_grid_dsc_array(grid, cols, rows);

  for (std::size_t index = 0; index < 4; ++index) {
    const PresetScene fallback{"", "Scene " + std::to_string(index + 1)};
    const auto& scene = preset.sceneSet ? preset.sceneSet->scenes[index] : fallback;
    lv_obj_t* card = button(grid, uppercase(scene.name));
    sceneCardButtons_[index] = card;
    lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(index / 2), 1,
                         LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(index % 2), 1);
    styleSurface(card, panel);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_t* name = lv_obj_get_child(card, 0);
    sceneNameLabels_[index] = name;
    lv_obj_set_style_text_font(name,
      scene.name.size() <= 14 ? &ardor_font_saira_cond_semibold_72
                              : &ardor_font_saira_cond_semibold_52, 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_size(name, LV_PCT(86), 146);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(name, 24, 62);

    lv_obj_t* header = lv_obj_create(card);
    sceneHeaderStrips_[index] = header;
    lv_obj_set_size(header, LV_PCT(100), 44);
    lv_obj_set_pos(header, 0, 0);
    styleSurface(header, panelAlt);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    sceneHeaderLabels_[index] = label(header, "FS " + std::to_string(index + 1),
      LV_ALIGN_LEFT_MID, 16, 0, &ardor_font_saira_cond_semibold_28);
    sceneDetailLabels_[index] = label(card, sceneDetail(scene),
      LV_ALIGN_BOTTOM_LEFT, 22, -16, &ardor_font_saira_cond_semibold_22, muted);
    lv_obj_t* progress = lv_obj_create(card);
    sceneProgressFills_[index] = progress;
    lv_obj_remove_style_all(progress);
    lv_obj_set_size(progress, 0, 3);
    lv_obj_align(progress, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(progress, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(progress, lv_color_hex(lamp), 0);
    lv_obj_add_flag(progress, LV_OBJ_FLAG_HIDDEN);
    if (!preset.sceneSet) lv_obj_add_state(card, LV_STATE_DISABLED);
    lv_obj_add_event_cb(card, onSceneClicked, LV_EVENT_CLICKED, remember(state, index));
  }

  lv_obj_t* bottom = lv_obj_create(root);
  lv_obj_set_size(bottom, kDesignWidth, kBottomRailHeight);
  lv_obj_set_pos(bottom, 0, kBottomRailY);
  styleSurface(bottom, bg);
  lv_obj_set_style_radius(bottom, 0, 0);
  lv_obj_set_style_pad_all(bottom, 0, 0);
  lv_obj_set_style_border_side(bottom, LV_BORDER_SIDE_TOP, 0);
  lv_obj_remove_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
  int x = kEdge;
  const auto railButton = [&](const char* value, int width, lv_event_cb_t callback) {
    lv_obj_t* control = button(bottom, value);
    lv_obj_set_size(control, width, 60);
    lv_obj_align(control, LV_ALIGN_LEFT_MID, x, 0);
    lv_obj_add_event_cb(control, callback, LV_EVENT_CLICKED, remember(state));
    x += width + 12;
  };
  railButton("Presets", 132, onPresetsClicked);
  railButton("Tuner", 112, onSceneTunerClicked);
  railButton("Looper", 124, onSceneLooperClicked);
  railButton("Edit", 112, onSceneEditClicked);
  scenesFaultLabel_ = label(bottom, "", LV_ALIGN_CENTER, 80, 0,
                            &ardor_font_saira_cond_semibold_22, danger);
  label(bottom, "BUFFER  " + std::to_string(static_cast<int>(std::clamp(
          100.0 - state.telemetry.bufferFreePercent, 0.0, 100.0))) + "% USED",
        LV_ALIGN_RIGHT_MID, -190, -16, &ardor_font_saira_cond_medium_18, muted);
  label(bottom, "MASTER  " + std::to_string(state.masterVolume),
        LV_ALIGN_RIGHT_MID, -kEdge, 16, &ardor_font_saira_cond_semibold_28);
  syncScenesView(state);
}

void LvglUi::syncScenesView(const UiState& state)
{
  if (!viewsInitialized_ && !scenesLayer_) return;
  const auto& preset = state.bank.presets[state.activePreset];
  if (scenesPresetLabel_) lv_label_set_text(scenesPresetLabel_, uppercase(preset.name).c_str());
  if (scenesUnsavedLabel_) {
    if (state.dirty) lv_obj_remove_flag(scenesUnsavedLabel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(scenesUnsavedLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  if (scenesFaultLabel_) {
    lv_label_set_text(scenesFaultLabel_, state.scenes.rejection.c_str());
    if (state.scenes.rejection.empty()) lv_obj_add_flag(scenesFaultLabel_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(scenesFaultLabel_, LV_OBJ_FLAG_HIDDEN);
  }
  for (std::size_t index = 0; index < sceneCardButtons_.size(); ++index) {
    if (!sceneCardButtons_[index]) continue;
    if (preset.sceneSet) {
      const auto& scene = preset.sceneSet->scenes[index];
      lv_label_set_text(sceneNameLabels_[index], uppercase(scene.name).c_str());
      lv_label_set_text(sceneDetailLabels_[index], sceneDetail(scene).c_str());
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
    lv_obj_set_style_bg_color(sceneHeaderStrips_[index],
                              lv_color_hex(live ? lamp : panelAlt), 0);
    lv_obj_set_style_text_color(sceneHeaderLabels_[index], lv_color_hex(live ? bg : text), 0);
    lv_obj_set_style_border_color(sceneCardButtons_[index],
                                  lv_color_hex((live || goingTo) ? lamp : rule), 0);
    lv_obj_set_style_border_width(sceneCardButtons_[index], (live || goingTo) ? 3 : 1, 0);
    if (goingTo) {
      lv_obj_remove_flag(sceneProgressFills_[index], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_width(sceneProgressFills_[index],
        LV_PCT(static_cast<int>(std::lround(state.scenes.progress * 100.0f))));
    } else {
      lv_obj_add_flag(sceneProgressFills_[index], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

} // namespace ardor
