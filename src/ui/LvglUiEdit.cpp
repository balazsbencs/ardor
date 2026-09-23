#include "ui/LvglUi.h"

#include "ui/LvglChainLayout.h"
#include "ui/LvglUiDrag.h"
#include "ui/LvglUiNavigation.h"
#include "ui/LvglUiStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace ardor {
namespace {

using namespace chain_layout;
using namespace lvgl_drag;
using namespace lvgl_navigation;
using namespace lvgl_ui;

// Edit's own top+bottom rails, matching Preset's (docs/lvgl-ui-redesign-spec.md
// §4f). The chain viewport (kChainTop=96, kChainHeight=492 in
// LvglChainLayout.h, shared with drag/hit-testing math) already fits above
// kEditBottomRailY without adjustment.
constexpr int kEditRailEdgeInset = 28;
constexpr int kEditTopRailHeight = 52;
constexpr int kEditBottomRailHeight = 88;
constexpr int kEditBottomRailY = kDesignHeight - kEditBottomRailHeight;

void drawBypassedOutline(lv_event_t* event)
{
  auto* object = lv_event_get_target_obj(event);
  if (!lv_obj_has_state(object, LV_STATE_USER_1)) return;

  lv_area_t area{};
  lv_obj_get_coords(object, &area);
  area.x1 += 1;
  area.y1 += 1;
  area.x2 -= 1;
  area.y2 -= 1;

  lv_draw_line_dsc_t line{};
  lv_draw_line_dsc_init(&line);
  line.color = lv_color_hex(rule);
  line.width = 2;
  line.dash_width = 9;
  line.dash_gap = 6;
  line.opa = LV_OPA_COVER;
  auto* layer = lv_event_get_layer(event);
  const auto draw = [&](int x1, int y1, int x2, int y2) {
    line.p1.x = x1;
    line.p1.y = y1;
    line.p2.x = x2;
    line.p2.y = y2;
    lv_draw_line(layer, &line);
  };
  draw(area.x1, area.y1, area.x2, area.y1);
  draw(area.x2, area.y1, area.x2, area.y2);
  draw(area.x2, area.y2, area.x1, area.y2);
  draw(area.x1, area.y2, area.x1, area.y1);
}

bool isWdwRoutingBlock(const UiBlock& block)
{
  return block.type == "dualRig"
    && block.params.value("routing", std::string{}) == "wdw";
}

std::string wdwLaneMixSummary(const UiBlock& block, std::size_t lane)
{
  char buffer[64]{};
  if (lane == 0) {
    const float level = block.params.value("dryLevelDb", 0.0f);
    const float pan = block.params.value("dryPan", 0.0f);
    const int panPercent = static_cast<int>(std::lround(std::fabs(pan) * 100.0f));
    if (panPercent == 0) {
      std::snprintf(buffer, sizeof(buffer), "LEVEL %+.0f DB / PAN C", level);
    } else {
      std::snprintf(buffer, sizeof(buffer), "LEVEL %+.0f DB / PAN %c%d%%", level,
                    pan < 0.0f ? 'L' : 'R', panPercent);
    }
  } else {
    const float level = block.params.value("wetLevelDb", 0.0f);
    const int width = static_cast<int>(std::lround(
      std::clamp(block.params.value("wetWidth", 1.0f), 0.0f, 1.0f) * 100.0f));
    std::snprintf(buffer, sizeof(buffer), "LEVEL %+.0f DB / WIDTH %d%%", level, width);
  }
  return buffer;
}

void redraw(UiEventContext* context)
{
  context->ui->invalidate(UiChange::None);
}

void onEditRailUndoClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (undoLastBlockEdit(*context->state)) {
    context->ui->resetParameterPage();
    redraw(context);
  }
}

void onPresetNameEditClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->openPresetNameEditor(*context->state);
}

void onSceneNameEditClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->openSceneNameEditor(*context->state);
}

void onSceneTabClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (!selectEditingScene(*context->state, context->index)) return;
  if (context->ui->actions().selectScene) context->ui->actions().selectScene(context->index);
  context->ui->invalidate(UiChange::Presets);
}

void onCreateScenesClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  enableScenes(*context->state);
  context->ui->invalidate(UiChange::Presets);
}

void onOpenSceneSettingsClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  openSceneSettings(*context->state);
  context->ui->invalidate(UiChange::Presets);
}

void onCloseSceneSettingsClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  closeSceneSettings(*context->state);
  context->ui->invalidate(UiChange::Presets);
}

void onSceneInstantClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  setEditingSceneEnterTime(*context->state, 0);
}

void onSceneTimedClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto& scene = context->state->bank.presets[context->state->activePreset]
    .sceneSet->scenes[context->state->editingScene];
  setEditingSceneEnterTime(*context->state, scene.enterTimeMs == 0 ? 500 : scene.enterTimeMs);
}

void onSceneEnterDeltaClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto& scene = context->state->bank.presets[context->state->activePreset]
    .sceneSet->scenes[context->state->editingScene];
  const int current = scene.enterTimeMs == 0 ? 500 : static_cast<int>(scene.enterTimeMs);
  setEditingSceneEnterTime(*context->state,
    static_cast<std::uint32_t>(std::clamp(current + (context->index == 0 ? -100 : 100), 100, 10000)));
}

void onSceneTrimDeltaClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const auto& scene = context->state->bank.presets[context->state->activePreset]
    .sceneSet->scenes[context->state->editingScene];
  setEditingSceneTrim(*context->state,
    std::clamp(scene.outputTrimDb + (context->index == 0 ? -0.5f : 0.5f), -12.0f, 6.0f));
}

void onSceneDefaultClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  makeEditingSceneDefault(*context->state);
}

void onSceneCopyClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  requestSceneOperation(*context->state, UiSceneOperation::Copy,
                        context->state->editingScene, context->index);
}

void onSceneSwapClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  requestSceneOperation(*context->state, UiSceneOperation::Swap,
                        context->state->editingScene, context->index);
}

void onSceneOperationCancelClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  cancelSceneOperation(*context->state);
}

void onSceneOperationConfirmClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  confirmSceneOperation(*context->state);
}

void onSceneOpenModeClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  setSceneOpenMode(*context->state, context->index == 0
    ? PresetSceneOpenMode::Presets : PresetSceneOpenMode::Scenes);
}

void onDisableScenesClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  requestSceneOperation(*context->state, UiSceneOperation::Disable,
                        context->state->editingScene, context->state->editingScene);
}

void onCaptureCurrentSoundClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (!requestCurrentSoundCapture(*context->state)) return;
  if (context->ui->actions().requestSceneCapture)
    context->ui->actions().requestSceneCapture();
  else
    failCurrentSoundCapture(*context->state, "Current sound capture is unavailable");
}

void onOpenBlockDrawer(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  openBlockDrawer(*context->state);
  redraw(context);
}

void onOpenBlockDrawerAt(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  openBlockDrawerAt(*context->state, context->index);
  redraw(context);
}

void onOpenLaneBlockDrawer(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  openLaneBlockDrawer(*context->state, context->parentIndex, context->laneIndex, context->index);
  redraw(context);
}

void onChainScroll(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->state->activePreset < context->state->chainScrollOffsets.size()) {
    context->state->chainScrollOffsets[context->state->activePreset]
      = lv_obj_get_scroll_x(lv_event_get_target_obj(event));
  }
}

void onChainScrollEnd(lv_event_t* event)
{
  onChainScroll(event);
}

void onChainStartClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->scrollChainToStart(*context->state);
}

void onChainEndClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->scrollChainToEnd(*context->state);
}

void onGlobalParamsClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->ui->selectGlobalParams(*context->state);
  redraw(context);
}

void onBlockClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->suppressClick) {
    context->suppressClick = false;
    return;
  }
  context->ui->scrollChainBlockIntoView(context->index);
  context->ui->selectBlock(*context->state, context->index);
  redraw(context);
}

void onLaneBlockClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->suppressClick) {
    context->suppressClick = false;
    return;
  }
  context->ui->scrollChainBlockIntoView(context->parentIndex);
  context->ui->selectLaneBlock(*context->state, context->parentIndex,
                               context->laneIndex, context->index);
  redraw(context);
}


void onBlockPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->dragging = false;
  context->suppressClick = false;
  context->ui->setChainDragActive(true);
  lv_indev_t* input = lv_event_get_indev(event);
  if (input) {
    lv_indev_get_point(input, &context->pressPoint);
    context->pressPoint = context->ui->toCanvas(context->pressPoint);
  }
}

void onBlockPressing(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    return;
  }
  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  const int dx = point.x - context->pressPoint.x;
  const int dy = point.y - context->pressPoint.y;
  if (!context->dragging && dx * dx + dy * dy < 64) {
    return;
  }
  if (!context->dragging) {
    context->dragging = true;
    context->ui->beginInteraction();
    lv_obj_set_style_opa(context->controlledObject ? context->controlledObject
                                                   : lv_event_get_target_obj(event), LV_OPA_TRANSP, 0);
  }
  updateDragVisuals(context, event);
}

void onBlockReleased(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const lv_opa_t restingOpacity = LV_OPA_COVER;
  lv_obj_set_style_opa(context->controlledObject ? context->controlledObject
                                                 : lv_event_get_target_obj(event), restingOpacity, 0);
  context->ui->setChainDragActive(false);
  if (!context->dragging) {
    return;
  }
  context->suppressClick = true;
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) {
    clearDragVisuals(context);
    context->ui->endInteraction();
    return;
  }

  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  const bool droppedOnChain = pointInVisibleChain(*context->state, point);
  const auto target = context->ui->chainSlotAtPoint(point);
  clearDragVisuals(context);
  if (!droppedOnChain || target == context->index) {
    context->ui->endInteraction();
    return;
  }

  moveBlock(*context->state, context->index, target);
  redraw(context);
  context->ui->endInteraction();
}

void onBlockPressLost(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  const lv_opa_t restingOpacity = LV_OPA_COVER;
  lv_obj_set_style_opa(context->controlledObject ? context->controlledObject
                                                 : lv_event_get_target_obj(event), restingOpacity, 0);
  context->ui->setChainDragActive(false);
  context->suppressClick = context->dragging;
  const bool wasDragging = context->dragging;
  clearDragVisuals(context);
  if (wasDragging) {
    context->ui->endInteraction();
  }
}

void placeLaneDragIndicator(UiEventContext* context, const UiLaneDropTarget& target)
{
  if (!context->indicator) {
    context->indicator = lv_obj_create(context->ui->canvas());
    lv_obj_set_size(context->indicator, 5, kLaneTileHeight);
    lv_obj_set_style_border_width(context->indicator, 0, 0);
    lv_obj_set_style_radius(context->indicator, 0, 0);
  }
  const auto position = context->ui->laneIndicatorForTarget(target);
  lv_obj_set_pos(context->indicator, position.x, position.y);
  lv_obj_set_style_bg_color(context->indicator,
                            lv_color_hex(target.laneIndex == 0 ? laneL : laneR), 0);
  moveToFront(context->indicator);
}

void onLaneBlockPressed(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  context->dragging = false;
  context->suppressClick = false;
  context->ui->setChainDragActive(true);
  const auto& blocks = context->state->bank.presets[context->state->activePreset].blocks;
  if (context->parentIndex < blocks.size()
      && context->laneIndex < blocks[context->parentIndex].lanes.size()
      && context->index < blocks[context->parentIndex].lanes[context->laneIndex].size()) {
    const auto& child = blocks[context->parentIndex].lanes[context->laneIndex][context->index];
    context->dragText = laneToken(child) + "\n" + child.assetName;
  }
  lv_indev_t* input = lv_event_get_indev(event);
  if (input) {
    lv_indev_get_point(input, &context->pressPoint);
    context->pressPoint = context->ui->toCanvas(context->pressPoint);
  }
}

void onLaneBlockPressing(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_indev_t* input = lv_event_get_indev(event);
  if (!input) return;
  lv_point_t point{};
  lv_indev_get_point(input, &point);
  point = context->ui->toCanvas(point);
  const int dx = point.x - context->pressPoint.x;
  const int dy = point.y - context->pressPoint.y;
  if (!context->dragging && dx * dx + dy * dy < 64) return;
  if (!context->dragging) {
    context->dragging = true;
    context->ui->beginInteraction();
    if (context->controlledObject) lv_obj_set_style_opa(context->controlledObject, LV_OPA_TRANSP, 0);
  }
  context->ui->autoScrollChainForDrag(*context->state, point);
  placeDragGhost(context, point);
  if (const auto target = context->ui->laneDropTargetAtPoint(point)) {
    placeLaneDragIndicator(context, *target);
  } else if (context->indicator) {
    lv_obj_delete(context->indicator);
    context->indicator = nullptr;
  }
}

void onLaneBlockReleased(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->controlledObject) {
    lv_obj_set_style_opa(context->controlledObject, LV_OPA_COVER, 0);
  }
  context->ui->setChainDragActive(false);
  if (!context->dragging) return;
  context->suppressClick = true;
  lv_indev_t* input = lv_event_get_indev(event);
  std::optional<UiLaneDropTarget> target;
  if (input) {
    lv_point_t point{};
    lv_indev_get_point(input, &point);
    point = context->ui->toCanvas(point);
    target = context->ui->laneDropTargetAtPoint(point);
  }
  clearDragVisuals(context);
  if (target) {
    moveLaneBlock(*context->state, context->parentIndex, context->laneIndex,
                  context->index, target->laneIndex, target->blockIndex);
    redraw(context);
  }
  context->ui->endInteraction();
}

void onLaneBlockPressLost(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  if (context->controlledObject) {
    lv_obj_set_style_opa(context->controlledObject, LV_OPA_COVER, 0);
  }
  context->ui->setChainDragActive(false);
  context->suppressClick = context->dragging;
  const bool wasDragging = context->dragging;
  clearDragVisuals(context);
  if (wasDragging) context->ui->endInteraction();
}

} // namespace

void LvglUi::renderEditMode(lv_obj_t* root, UiState& state)
{
  // ---- top legend rail: mirrors Preset's, naming this plate as an editor ----
  lv_obj_t* topRail = lv_obj_create(root);
  lv_obj_set_size(topRail, kDesignWidth, kEditTopRailHeight);
  lv_obj_set_pos(topRail, 0, 0);
  lv_obj_set_style_bg_color(topRail, lv_color_hex(panel), 0);
  lv_obj_set_style_bg_opa(topRail, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(topRail, 1, 0);
  lv_obj_set_style_border_side(topRail, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(topRail, lv_color_hex(rule), 0);
  lv_obj_set_style_radius(topRail, 0, 0);
  lv_obj_set_style_pad_all(topRail, 0, 0);
  lv_obj_remove_flag(topRail, LV_OBJ_FLAG_SCROLLABLE);

  const auto& editedPreset = state.bank.presets[state.activePreset];
  const auto* sceneSet = editedPreset.sceneSet ? &*editedPreset.sceneSet : nullptr;
  label(topRail, sceneSet ? "EDITING" : "PRESET " + std::to_string(state.activePreset + 1), LV_ALIGN_LEFT_MID,
        kEditRailEdgeInset, 0, &ardor_font_saira_cond_semibold_22, text);
  const std::string editorIdentity = sceneSet
    ? "SCENE " + std::to_string(state.editingScene + 1) + "  ·  "
        + uppercase(sceneSet->scenes[state.editingScene].name)
    : editedPreset.name;
  editPresetLabel_ = label(topRail, editorIdentity,
                           LV_ALIGN_LEFT_MID, 154, 0, &ardor_font_saira_cond_medium_18, muted);
  lv_obj_set_width(editPresetLabel_, 236);
  lv_label_set_long_mode(editPresetLabel_, LV_LABEL_LONG_CLIP);
  lv_obj_t* rename = button(topRail, "RENAME");
  lv_obj_set_size(rename, 96, 40);
  lv_obj_align(rename, LV_ALIGN_LEFT_MID, 404, 0);
  styleSurface(rename, panelAlt);
  lv_obj_add_event_cb(rename, onPresetNameEditClicked, LV_EVENT_CLICKED, remember(state));
  editModifiedLabel_ = label(topRail, "\xC2\xB7 MODIFIED", LV_ALIGN_LEFT_MID, 520, 0,
                             &ardor_font_saira_cond_medium_18, lamp);
  if (!state.dirty) lv_obj_add_flag(editModifiedLabel_, LV_OBJ_FLAG_HIDDEN);
  const bool hasWdwRoute = state.bank.presets[state.activePreset].routing == "wdw";
  if (hasWdwRoute) {
    label(topRail, "WET / DRY / WET", LV_ALIGN_LEFT_MID, 640, 0,
          &ardor_font_saira_cond_medium_18, categoryColor("delay"));
  }
  const auto moduleCount = state.bank.presets[state.activePreset].blocks.size();
  editModuleCountLabel_ = label(topRail,
    std::to_string(moduleCount) + (moduleCount == 1 ? " MODULE" : " MODULES"),
    LV_ALIGN_RIGHT_MID, -kEditRailEdgeInset, 0, &ardor_font_saira_cond_medium_18, muted);

  if (sceneSet) {
    constexpr int stripY = 54;
    constexpr int stripWidth = 202;
    for (std::size_t index = 0; index < sceneSet->scenes.size(); ++index) {
      const auto& scene = sceneSet->scenes[index];
      lv_obj_t* tab = button(root, "FS" + std::to_string(index + 1) + "  " + uppercase(scene.name));
      lv_obj_set_size(tab, stripWidth, 38);
      lv_obj_set_pos(tab, 28 + static_cast<int>(index) * (stripWidth + 8), stripY);
      styleSurface(tab, index == state.editingScene ? text : panelAlt);
      lv_obj_set_style_text_color(lv_obj_get_child(tab, 0),
                                  lv_color_hex(index == state.editingScene ? bg : text), 0);
      lv_obj_add_event_cb(tab, onSceneTabClicked, LV_EVENT_CLICKED, remember(state, index));
    }
    lv_obj_t* sceneSettings = button(root, state.sceneSettingsOpen ? "CLOSE SETTINGS" : "SCENE SETTINGS");
    lv_obj_set_size(sceneSettings, 190, 38);
    lv_obj_set_pos(sceneSettings, 1062, stripY);
    styleSurface(sceneSettings, panelAlt);
    lv_obj_add_event_cb(sceneSettings,
      state.sceneSettingsOpen ? onCloseSceneSettingsClicked : onOpenSceneSettingsClicked,
      LV_EVENT_CLICKED, remember(state));
  } else {
    label(root, "SCENES  ·  FOUR SOUNDS USING THIS RIG", LV_ALIGN_TOP_LEFT, 28, 61,
          &ardor_font_saira_cond_medium_18, muted);
    lv_obj_t* createScenes = button(root, "CREATE FOUR SCENES");
    lv_obj_set_size(createScenes, 226, 38);
    lv_obj_set_pos(createScenes, 1026, 54);
    styleSurface(createScenes, panelAlt);
    lv_obj_add_event_cb(createScenes, onCreateScenesClicked, LV_EVENT_CLICKED, remember(state));
  }

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  const auto* selectedEffect = selectedUiBlock(state);
  const bool editingEq = state.paramDrawerOpen && state.paramTarget == UiParamTarget::Block
    && selectedEffect && selectedEffect->type == "eq"
    && isParametricEqMode(selectedEffect->params);
  if (editingEq) {
    // The retained parameter layer owns the EQ editor.
  }

  chainViewport_ = lv_obj_create(root);
  lv_obj_set_size(chainViewport_, kChainWidth, kChainHeight);
  lv_obj_set_pos(chainViewport_, kChainLeft, kChainTop);
  styleSurface(chainViewport_, bg);
  lv_obj_set_style_pad_all(chainViewport_, 0, 0);
  lv_obj_set_style_radius(chainViewport_, 0, 0);
  lv_obj_set_scroll_dir(chainViewport_, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(chainViewport_, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_remove_flag(chainViewport_, LV_OBJ_FLAG_SCROLL_ELASTIC);

  chainWorld_ = lv_obj_create(chainViewport_);
  lv_obj_set_size(chainWorld_, kChainWidth, kChainWorldHeight);
  lv_obj_set_pos(chainWorld_, 0, 0);
  lv_obj_set_style_bg_opa(chainWorld_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chainWorld_, 0, 0);
  lv_obj_set_style_pad_all(chainWorld_, 0, 0);
  lv_obj_set_style_radius(chainWorld_, 0, 0);
  lv_obj_remove_flag(chainWorld_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(chainWorld_, LV_OBJ_FLAG_CLICKABLE);

  auto* scrollContext = remember(state);
  scrollContext->controlledObject = chainViewport_;
  lv_obj_add_event_cb(chainViewport_, onChainScroll, LV_EVENT_SCROLL, scrollContext);
  lv_obj_add_event_cb(chainViewport_, onChainScrollEnd, LV_EVENT_SCROLL_END, scrollContext);

  const auto rail = [&](int x, int y, int width, int color = muted) {
    if (width <= 0) return static_cast<lv_obj_t*>(nullptr);
    lv_obj_t* line = lv_obj_create(chainWorld_);
    lv_obj_set_size(line, width, 3);
    lv_obj_set_pos(line, x, y - 1);
    styleSurface(line, color);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_set_style_opa(line, LV_OPA_70, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    return line;
  };
  const auto terminal = [&](int x, const char* title, const char* detail) {
    lv_obj_t* object = lv_obj_create(chainWorld_);
    lv_obj_set_size(object, kChainTerminalWidth, 64);
    lv_obj_set_pos(object, x, kChainRailY - 32);
    styleSurface(object, panelAlt);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    label(object, title, LV_ALIGN_TOP_MID, 0, 8, &ardor_font_saira_cond_semibold_22, text);
    label(object, detail, LV_ALIGN_BOTTOM_MID, 0, -8, &ardor_font_saira_cond_medium_18, muted);
    return object;
  };
  const auto topInsert = [&](int x, std::size_t index, bool disabled = false) {
    rail(x - kChainGap, kChainRailY, kChainInsertWidth + 2 * kChainGap, muted);
    lv_obj_t* add = button(chainWorld_, "+");
    lv_obj_set_size(add, 46, 46);
    lv_obj_set_pos(add, x + (kChainInsertWidth - 46) / 2, kChainRailY - 23);
    styleSurface(add, panelAlt);
    lv_obj_set_style_radius(add, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(add, lv_color_hex(rule), 0);
    lv_obj_set_style_border_width(add, 1, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(add, 0), lv_color_hex(text), 0);
    if (disabled) lv_obj_add_state(add, LV_STATE_DISABLED);
    auto* context = remember(state, index);
    lv_obj_add_event_cb(add, onOpenBlockDrawerAt, LV_EVENT_CLICKED, context);
    chainInsertionXs_.push_back(x + kChainInsertWidth / 2);
  };
  const auto laneInsert = [&](int x, int y, std::size_t rigIndex,
                              std::size_t laneIndex, std::size_t index, int color, bool disabled) {
    lv_obj_t* add = button(chainWorld_, "+");
    lv_obj_set_size(add, 42, 42);
    lv_obj_set_pos(add, x, y - 21);
    styleSurface(add, panelAlt);
    lv_obj_set_style_radius(add, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(add, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(add, 1, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(add, 0), lv_color_hex(color), 0);
    if (disabled) lv_obj_add_state(add, LV_STATE_DISABLED);
    auto* context = remember(state, index);
    context->parentIndex = rigIndex;
    context->laneIndex = laneIndex;
    lv_obj_add_event_cb(add, onOpenLaneBlockDrawer, LV_EVENT_CLICKED, context);
    laneInsertionXs_[laneIndex].push_back(x + 21);
  };
  const auto bindBlockDragSurface = [&](lv_obj_t* surface, lv_obj_t* controlled,
                                        std::size_t index) {
    auto* context = remember(state, index);
    context->controlledObject = controlled;
    lv_obj_add_event_cb(surface, onBlockClicked, LV_EVENT_CLICKED, context);
    lv_obj_add_event_cb(surface, onBlockPressed, LV_EVENT_PRESSED, context);
    lv_obj_add_event_cb(surface, onBlockPressing, LV_EVENT_PRESSING, context);
    lv_obj_add_event_cb(surface, onBlockReleased, LV_EVENT_RELEASED, context);
    lv_obj_add_event_cb(surface, onBlockPressLost, LV_EVENT_PRESS_LOST, context);
    chainDragContexts_[index] = context;
  };
  const auto dragHandle = [&](lv_obj_t* parent, lv_obj_t* controlled, std::size_t index,
                              int width = kChainHandleWidth, int height = 52,
                              const lv_font_t* font = &ardor_font_saira_cond_semibold_22) {
    lv_obj_t* handle = button(parent, "|||");
    lv_obj_set_size(handle, width, height);
    lv_obj_align(handle, LV_ALIGN_RIGHT_MID, -6, 0);
    styleSurface(handle, panelAlt);
    lv_obj_set_style_pad_all(handle, 2, 0);
    setText(lv_obj_get_child(handle, 0), muted, font);
    bindBlockDragSurface(handle, controlled, index);
  };
  const auto laneEnd = [](std::size_t count) {
    return static_cast<int>(count + 1) * (kLaneInsertWidth + 8)
      + static_cast<int>(count) * (kLaneTileWidth + 8);
  };

  chainItemStarts_.clear();
  chainItemEnds_.clear();
  chainInsertionXs_.clear();
  renderedBlockIds_.clear();
  int x = kChainStartX;
  terminal(x, "INPUT", "MONO");
  x += kChainTerminalWidth + kChainGap;
  topInsert(x, 0, hasWdwRoute);
  x += kChainInsertWidth + kChainGap;

  for (std::size_t i = 0; i < blocks.size() && i < kMaxEffectBlocks; ++i) {
    const auto& block = blocks[i];
    const bool selected = state.paramTarget == UiParamTarget::Block
      && state.selectedBlock == i && !selectedBlockIsLaneChild(state);
    const int itemStart = x;
    chainItemStarts_.push_back(itemStart);
    renderedBlockIds_.push_back(block.id);

    if (block.type != "dualRig") {
      rail(x - kChainGap, kChainRailY, kChainTileWidth + 2 * kChainGap, muted);
      lv_obj_t* object = button(chainWorld_, "");
      lv_obj_set_size(object, kChainTileWidth, kChainTileHeight);
      lv_obj_set_pos(object, x, kChainTileTop);
      styleSurface(object, block.enabled ? panel : panelAlt);
      lv_obj_set_style_pad_all(object, 0, 0);
      lv_obj_set_style_bg_opa(object, block.enabled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
      if (block.enabled) {
        lv_obj_remove_state(object, LV_STATE_USER_1);
      } else {
        lv_obj_add_state(object, LV_STATE_USER_1);
      }
      lv_obj_add_event_cb(object, drawBypassedOutline, LV_EVENT_DRAW_POST, nullptr);
      // Selection reads as a lighter border, matching the mockup's .sel state
      // (panel.html line 148) rather than a separate indicator bar competing
      // with the family ticks for the card's bottom edge.
      lv_obj_set_style_border_color(object, lv_color_hex(selected ? text : rule), 0);
      lv_obj_set_style_border_width(object, selected ? 3 : (block.enabled ? 1 : 0), 0);
      if (isBlockHighlighted(block.id)) {
        lv_obj_set_style_border_color(object, lv_color_hex(text), 0);
        lv_obj_set_style_border_width(object, 3, 0);
      }
      const int catColor = categoryColor(block.type);
      // Family identity lives in the printed header strip, never as a wide
      // coloured side border. That keeps the card legible in every palette.
      // Bypassed blocks leave only a dashed footprint: the face clears to the
      // signal canvas and the header loses its family colour.
      lv_obj_t* categoryHeader = lv_obj_create(object);
      lv_obj_set_size(categoryHeader, kChainTileWidth, kChainHeaderHeight);
      lv_obj_set_pos(categoryHeader, 0, 0);
      styleSurface(categoryHeader, block.enabled ? catColor : rule);
      lv_obj_set_style_border_width(categoryHeader, 0, 0);
      lv_obj_set_style_pad_all(categoryHeader, 0, 0);
      lv_obj_remove_flag(categoryHeader, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(categoryHeader, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_opa(categoryHeader, LV_OPA_70, LV_STATE_PRESSED);

      lv_obj_t* categoryLabel = label(categoryHeader, uppercase(block.label),
                                      LV_ALIGN_LEFT_MID, 12, -10,
                                      &ardor_font_saira_cond_semibold_22,
                                      block.enabled ? bg : muted);
      lv_obj_set_width(categoryLabel, kChainTileWidth - 24);
      lv_label_set_long_mode(categoryLabel, LV_LABEL_LONG_CLIP);
      lv_obj_t* dragLabel = label(categoryHeader, "DRAG", LV_ALIGN_LEFT_MID, 12, 15,
                                  &ardor_font_saira_cond_semibold_22,
                                  block.enabled ? bg : muted);
      lv_obj_set_style_text_letter_space(dragLabel, 3, 0);
      lv_obj_remove_flag(categoryLabel, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(dragLabel, LV_OBJ_FLAG_CLICKABLE);
      bindBlockDragSurface(categoryHeader, object, i);

      lv_obj_t* assetName = label(object, uppercase(block.assetName), LV_ALIGN_TOP_LEFT,
                                  kChainTextX, kChainHeaderHeight + 18,
                                  &ardor_font_saira_cond_semibold_28,
                                  block.enabled ? text : disabled);
      lv_obj_set_width(assetName, kChainTextWidth);
      lv_label_set_long_mode(assetName, LV_LABEL_LONG_WRAP);
      lv_obj_t* bypassed = label(object, "OFF", LV_ALIGN_TOP_LEFT, kChainTextX,
                                 kChainHeaderHeight + 18 + 58, &ardor_font_saira_cond_medium_18, disabled);
      if (block.enabled) lv_obj_add_flag(bypassed, LV_OBJ_FLAG_HIDDEN);

      // Family swatch ticks at the card's foot, first tinted by category,
      // mirroring the mockup's .grp strip (panel.html lines 146-147).
      constexpr int kFamilyBarGap = 3;
      const int familyBarWidth = (kChainTextWidth - 2 * kFamilyBarGap) / 3;
      lv_obj_t* familyTick = nullptr;
      for (int bar = 0; bar < 3; ++bar) {
        lv_obj_t* tick = lv_obj_create(object);
        lv_obj_set_size(tick, familyBarWidth, 3);
        lv_obj_set_pos(tick, kChainTextX + bar * (familyBarWidth + kFamilyBarGap),
                       kChainTileHeight - 16 - 3);
        styleSurface(tick, block.enabled && bar == 0 ? catColor : rule);
        lv_obj_set_style_border_width(tick, 0, 0);
        lv_obj_remove_flag(tick, LV_OBJ_FLAG_CLICKABLE);
        if (bar == 0) familyTick = tick;
      }

      auto* clickContext = remember(state, i);
      lv_obj_add_event_cb(object, onBlockClicked, LV_EVENT_CLICKED, clickContext);

      chainCards_[i] = object;
      chainCategoryLabels_[i] = categoryLabel;
      chainAssetLabels_[i] = assetName;
      chainBypassLabels_[i] = bypassed;
      chainFamilyTicks_[i] = familyTick;
      chainClickContexts_[i] = clickContext;
      x += kChainTileWidth;
    } else {
      renderedRigIndex_ = i;
      const bool wdw = isWdwRoutingBlock(block);
      const std::size_t longest = std::max(block.lanes[0].size(), block.lanes[1].size());
      const int laneWidth = std::max(laneEnd(longest), laneEnd(1));
      const int splitX = x;
      const int laneStart = splitX + kChainJunctionWidth + 26;
      const int joinX = laneStart + laneWidth + 22;
      const bool dryLaneEnabled = !wdw || block.params.value("dryEnabled", true);
      const bool wetLaneEnabled = !wdw || block.params.value("wetEnabled", true);
      const int dryRailColor = dryLaneEnabled
        ? (wdw ? categoryColor("amp") : laneL) : muted;
      const int wetRailColor = wetLaneEnabled
        ? (wdw ? categoryColor("delay") : laneR) : muted;
      rail(splitX + kChainJunctionWidth / 2, kChainLeftRailY,
           joinX - splitX, dryRailColor);
      rail(splitX + kChainJunctionWidth / 2, kChainRightRailY,
           joinX - splitX, wetRailColor);
      lv_obj_t* splitStem = lv_obj_create(chainWorld_);
      lv_obj_set_size(splitStem, 3, kChainRightRailY - kChainLeftRailY);
      lv_obj_set_pos(splitStem, splitX + kChainJunctionWidth / 2 - 1, kChainLeftRailY);
      styleSurface(splitStem, muted);
      lv_obj_remove_flag(splitStem, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_t* joinStem = lv_obj_create(chainWorld_);
      lv_obj_set_size(joinStem, 3, kChainRightRailY - kChainLeftRailY);
      lv_obj_set_pos(joinStem, joinX + kChainJunctionWidth / 2 - 1, kChainLeftRailY);
      styleSurface(joinStem, muted);
      lv_obj_remove_flag(joinStem, LV_OBJ_FLAG_CLICKABLE);

      lv_obj_t* split = button(chainWorld_, wdw ? "WDW" : "SPLIT");
      lv_obj_set_size(split, kChainJunctionWidth, 82);
      lv_obj_set_pos(split, splitX, kChainRailY - 41);
      styleSurface(split, panelAlt);
      lv_obj_set_style_pad_all(split, 0, 0);
      lv_obj_set_style_border_color(split, lv_color_hex(selected ? text : rule), 0);
      lv_obj_set_style_border_width(split, selected ? 3 : 1, 0);
      lv_obj_t* splitLabel = lv_obj_get_child(split, 0);
      lv_obj_set_width(splitLabel, kChainJunctionWidth - kChainHandleWidth - 24);
      lv_obj_align(splitLabel, LV_ALIGN_LEFT_MID, 12, 0);
      lv_obj_set_style_text_align(splitLabel, LV_TEXT_ALIGN_LEFT, 0);
      if (wdw) {
        label(split, "NO DIRECT INPUT", LV_ALIGN_BOTTOM_LEFT, 12, -8,
              &ardor_font_saira_cond_semibold_11, muted);
      }
      auto* clickContext = remember(state, i);
      lv_obj_add_event_cb(split, onBlockClicked, LV_EVENT_CLICKED, clickContext);
      dragHandle(split, split, i);
      chainCards_[i] = split;
      chainClickContexts_[i] = clickContext;

      lv_obj_t* join = lv_obj_create(chainWorld_);
      lv_obj_set_size(join, kChainJunctionWidth, 64);
      lv_obj_set_pos(join, joinX, kChainRailY - 32);
      styleSurface(join, panelAlt);
      lv_obj_set_style_border_color(join, lv_color_hex(rule), 0);
      lv_obj_set_style_border_width(join, 1, 0);
      lv_obj_set_style_pad_all(join, 0, 0);
      lv_obj_remove_flag(join, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_remove_flag(join, LV_OBJ_FLAG_CLICKABLE);
      label(join, "JOIN", LV_ALIGN_CENTER, 0, 0, &ardor_font_saira_cond_semibold_22, text);

      for (std::size_t laneIndex = 0; laneIndex < block.lanes.size(); ++laneIndex) {
        const int laneY = laneIndex == 0 ? kChainLeftRailY : kChainRightRailY;
        const bool laneEnabled = !wdw || block.params.value(
          laneIndex == 0 ? "dryEnabled" : "wetEnabled", true);
        const int laneColor = wdw
          ? (laneIndex == 0 ? categoryColor("amp") : categoryColor("delay"))
          : (laneIndex == 0 ? laneL : laneR);
        const int visibleLaneColor = laneEnabled ? laneColor : muted;
        // Keep the lane captions outside the WDW junction: Dry sits above its
        // cards, while Wet mirrors that spacing below its cards.
        const int laneHeadingY = !wdw ? laneY - 54
          : laneIndex == 0 ? laneY - 78 : laneY + 54;
        label(chainWorld_, wdw ? (laneIndex == 0 ? "DRY" : "WET")
                              : (laneIndex == 0 ? "LEFT" : "RIGHT"),
              LV_ALIGN_TOP_LEFT, splitX + kChainJunctionWidth / 2 + 12, laneHeadingY,
              &ardor_font_saira_cond_semibold_22, visibleLaneColor);
        if (wdw) {
          label(chainWorld_, uppercase(wdwLaneMixSummary(block, laneIndex)),
                LV_ALIGN_TOP_LEFT, splitX + kChainJunctionWidth / 2 + 12,
                laneIndex == 0 ? laneY - 54 : laneY + 78,
                &ardor_font_saira_cond_semibold_11, laneEnabled ? muted : disabled);
        }
        int laneX = laneStart;
        laneInsert(laneX, laneY, i, laneIndex, 0, visibleLaneColor,
                   block.lanes[laneIndex].size() >= kMaxEffectBlocks);
        laneX += kLaneInsertWidth + 8;
        for (std::size_t childIndex = 0; childIndex < block.lanes[laneIndex].size(); ++childIndex) {
          const auto& child = block.lanes[laneIndex][childIndex];
          lv_obj_t* childObject = button(chainWorld_, "");
          lv_obj_set_size(childObject, kLaneTileWidth, kLaneTileHeight);
          lv_obj_set_pos(childObject, laneX, laneY - kLaneTileHeight / 2);
          styleSurface(childObject, child.enabled ? panel : panelAlt);
          lv_obj_set_style_pad_all(childObject, 0, 0);
          const bool childSelected = state.paramTarget == UiParamTarget::Block
            && state.selectedBlockId == child.id;
          lv_obj_set_style_bg_opa(childObject, child.enabled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
          lv_obj_set_style_border_color(childObject,
                                        lv_color_hex(childSelected ? text : visibleLaneColor), 0);
          lv_obj_set_style_border_width(childObject,
                                        childSelected ? 3 : (child.enabled ? 1 : 0), 0);
          if (child.enabled) {
            lv_obj_remove_state(childObject, LV_STATE_USER_1);
          } else {
            lv_obj_add_state(childObject, LV_STATE_USER_1);
          }
          lv_obj_add_event_cb(childObject, drawBypassedOutline, LV_EVENT_DRAW_POST, nullptr);
          if (!laneEnabled) lv_obj_set_style_opa(childObject, LV_OPA_70, 0);
          // Compact lane cards keep the same interaction grammar: the entire
          // title strip is a deliberate, finger-sized drag surface, while the
          // body remains a tap target for editing.
          lv_obj_t* childHeader = lv_obj_create(childObject);
          lv_obj_set_size(childHeader, kLaneTileWidth, kLaneHeaderHeight);
          lv_obj_set_pos(childHeader, 0, 0);
          styleSurface(childHeader, child.enabled && laneEnabled ? categoryColor(child.type) : rule);
          lv_obj_set_style_border_width(childHeader, 0, 0);
          lv_obj_set_style_pad_all(childHeader, 0, 0);
          lv_obj_remove_flag(childHeader, LV_OBJ_FLAG_SCROLLABLE);
          lv_obj_add_flag(childHeader, LV_OBJ_FLAG_CLICKABLE);
          lv_obj_set_style_opa(childHeader, LV_OPA_70, LV_STATE_PRESSED);
          lv_obj_t* childTitle = label(childHeader, laneToken(child), LV_ALIGN_LEFT_MID, 12, 0,
                                       &ardor_font_saira_cond_semibold_22,
                                       child.enabled && laneEnabled ? bg : muted);
          lv_obj_t* childDragLabel = label(childHeader, "DRAG", LV_ALIGN_RIGHT_MID, -12, 0,
                                           &ardor_font_saira_cond_semibold_22,
                                           child.enabled && laneEnabled ? bg : muted);
          lv_obj_set_style_text_letter_space(childDragLabel, 2, 0);
          lv_obj_remove_flag(childTitle, LV_OBJ_FLAG_CLICKABLE);
          lv_obj_remove_flag(childDragLabel, LV_OBJ_FLAG_CLICKABLE);
          const std::string childReadout = child.enabled
            ? uppercase(child.assetName) : "OFF  /  " + uppercase(child.assetName);
          lv_obj_t* childAsset = label(childObject, childReadout, LV_ALIGN_BOTTOM_LEFT, 10, -9,
                                       &ardor_font_saira_cond_semibold_22,
                                       child.enabled && laneEnabled ? text : disabled);
          lv_obj_set_width(childAsset, kLaneTileWidth - 20);
          lv_label_set_long_mode(childAsset, LV_LABEL_LONG_CLIP);
          auto* childClickContext = remember(state, childIndex);
          childClickContext->parentIndex = i;
          childClickContext->laneIndex = laneIndex;
          lv_obj_add_event_cb(childObject, onLaneBlockClicked, LV_EVENT_CLICKED, childClickContext);
          auto* childDragContext = remember(state, childIndex);
          childDragContext->parentIndex = i;
          childDragContext->laneIndex = laneIndex;
          childDragContext->controlledObject = childObject;
          childDragContext->dragText = laneToken(child) + "\n" + child.assetName;
          lv_obj_add_event_cb(childHeader, onLaneBlockClicked, LV_EVENT_CLICKED, childDragContext);
          lv_obj_add_event_cb(childHeader, onLaneBlockPressed, LV_EVENT_PRESSED, childDragContext);
          lv_obj_add_event_cb(childHeader, onLaneBlockPressing, LV_EVENT_PRESSING, childDragContext);
          lv_obj_add_event_cb(childHeader, onLaneBlockReleased, LV_EVENT_RELEASED, childDragContext);
          lv_obj_add_event_cb(childHeader, onLaneBlockPressLost, LV_EVENT_PRESS_LOST, childDragContext);
          laneX += kLaneTileWidth + 8;
          laneInsert(laneX, laneY, i, laneIndex, childIndex + 1, visibleLaneColor,
                     block.lanes[laneIndex].size() >= kMaxEffectBlocks);
          laneX += kLaneInsertWidth + 8;
        }
      }
      x = joinX + kChainJunctionWidth;
    }

    chainItemEnds_.push_back(x);
    x += kChainGap;
    topInsert(x, i + 1, hasWdwRoute);
    x += kChainInsertWidth + kChainGap;
  }

  rail(x - kChainGap, kChainRailY, kChainGap, muted);
  terminal(x, "OUTPUT", "STEREO");
  x += kChainTerminalWidth + kChainStartX;
  lv_obj_set_width(chainWorld_, std::max(x, kChainWidth));
  lv_obj_update_layout(chainViewport_);
  const int32_t savedScroll = state.activePreset < state.chainScrollOffsets.size()
    ? state.chainScrollOffsets[state.activePreset] : 0;
  lv_obj_scroll_to_x(chainViewport_, savedScroll, LV_ANIM_OFF);

  // The chain viewport ends at kChainTop + kChainHeight = 588; the jump row
  // fits the 44 px gap above the bottom rail (588-632) at a reduced height.
  lv_obj_t* inputJump = button(root, "<  INPUT");
  lv_obj_set_size(inputJump, 144, 40);
  lv_obj_set_pos(inputJump, 28, 592);
  styleSurface(inputJump, panelAlt);
  lv_obj_add_event_cb(inputJump, onChainStartClicked, LV_EVENT_CLICKED, remember(state));
  lv_obj_t* outputJump = button(root, "OUTPUT  >");
  lv_obj_set_size(outputJump, 144, 40);
  lv_obj_set_pos(outputJump, 1108, 592);
  styleSurface(outputJump, panelAlt);
  lv_obj_add_event_cb(outputJump, onChainEndClicked, LV_EVENT_CLICKED, remember(state));

  if (sceneSet && state.sceneSettingsOpen) {
    const auto& scene = sceneSet->scenes[state.editingScene];
    lv_obj_t* sheet = lv_obj_create(root);
    lv_obj_set_size(sheet, kDesignWidth, 536);
    lv_obj_set_pos(sheet, 0, 96);
    styleSurface(sheet, bg);
    lv_obj_set_style_pad_all(sheet, 0, 0);
    lv_obj_set_style_radius(sheet, 0, 0);
    lv_obj_remove_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back = button(sheet, "<  BACK TO CHAIN");
    lv_obj_set_size(back, 184, 44);
    lv_obj_set_pos(back, 28, 18);
    styleSurface(back, panelAlt);
    lv_obj_add_event_cb(back, onCloseSceneSettingsClicked, LV_EVENT_CLICKED, remember(state));
    label(sheet, "SCENE " + std::to_string(state.editingScene + 1) + "  ·  " + uppercase(scene.name),
          LV_ALIGN_TOP_LEFT, 238, 25, &ardor_font_saira_cond_semibold_22, text);

    const auto card = [&](int x, int y, int width, int height, const std::string& title) {
      lv_obj_t* result = lv_obj_create(sheet);
      lv_obj_set_size(result, width, height);
      lv_obj_set_pos(result, x, y);
      styleSurface(result, panel);
      lv_obj_set_style_pad_all(result, 0, 0);
      lv_obj_remove_flag(result, LV_OBJ_FLAG_SCROLLABLE);
      label(result, title, LV_ALIGN_TOP_LEFT, 20, 14,
            &ardor_font_saira_cond_semibold_11, muted);
      return result;
    };
    const auto smallAction = [&](lv_obj_t* parent, const std::string& caption,
                                 int x, int y, int width, lv_event_cb_t callback,
                                 std::size_t index = 0) {
      lv_obj_t* action = button(parent, caption);
      lv_obj_set_size(action, width, 44);
      lv_obj_set_pos(action, x, y);
      styleSurface(action, panelAlt);
      lv_obj_add_event_cb(action, callback, LV_EVENT_CLICKED, remember(state, index));
      return action;
    };

    lv_obj_t* nameCard = card(28, 76, 594, 124, "NAME");
    label(nameCard, scene.name, LV_ALIGN_BOTTOM_LEFT, 20, -18,
          &ardor_font_saira_cond_semibold_28, text);
    smallAction(nameCard, "RENAME", 452, 60, 122, onSceneNameEditClicked);

    lv_obj_t* enterCard = card(638, 76, 614, 124, "ENTER TIME");
    lv_obj_t* instant = smallAction(enterCard, "INSTANT", 20, 58, 112, onSceneInstantClicked);
    lv_obj_t* timed = smallAction(enterCard, "TIMED", 142, 58, 102, onSceneTimedClicked);
    styleSurface(scene.enterTimeMs == 0 ? instant : timed, text);
    lv_obj_set_style_text_color(lv_obj_get_child(scene.enterTimeMs == 0 ? instant : timed, 0),
                                lv_color_hex(bg), 0);
    lv_obj_t* enterMinus = smallAction(enterCard, "-", 310, 58, 48, onSceneEnterDeltaClicked, 0);
    lv_obj_t* enterPlus = smallAction(enterCard, "+", 546, 58, 48, onSceneEnterDeltaClicked, 1);
    char enterText[32]{};
    if (scene.enterTimeMs == 0) std::snprintf(enterText, sizeof(enterText), "INSTANT");
    else std::snprintf(enterText, sizeof(enterText), "%.1f s", scene.enterTimeMs / 1000.0f);
    label(enterCard, enterText, LV_ALIGN_TOP_LEFT, 382, 68,
          &ardor_font_saira_cond_semibold_22, scene.enterTimeMs == 0 ? muted : text);
    if (scene.enterTimeMs == 0) {
      lv_obj_add_state(enterMinus, LV_STATE_DISABLED);
      lv_obj_add_state(enterPlus, LV_STATE_DISABLED);
    }

    lv_obj_t* trimCard = card(28, 216, 594, 128, "SCENE TRIM");
    smallAction(trimCard, "-", 20, 62, 54, onSceneTrimDeltaClicked, 0);
    smallAction(trimCard, "+", 520, 62, 54, onSceneTrimDeltaClicked, 1);
    char trimText[32]{};
    std::snprintf(trimText, sizeof(trimText), "%+.1f dB", scene.outputTrimDb);
    label(trimCard, "-12 dB", LV_ALIGN_BOTTOM_LEFT, 90, -22,
          &ardor_font_saira_cond_medium_18, muted);
    label(trimCard, trimText, LV_ALIGN_BOTTOM_MID, 0, -20,
          &ardor_font_saira_cond_semibold_28, text);
    label(trimCard, "+6 dB", LV_ALIGN_BOTTOM_RIGHT, -90, -22,
          &ardor_font_saira_cond_medium_18, muted);

    lv_obj_t* defaultCard = card(638, 216, 614, 128, "DEFAULT ON PRESET LOAD");
    const bool isDefault = sceneSet->defaultSceneId == scene.id;
    lv_obj_t* makeDefault = smallAction(defaultCard,
      isDefault ? "CURRENT DEFAULT" : "MAKE " + uppercase(scene.name) + " DEFAULT",
      20, 62, 286, onSceneDefaultClicked);
    if (isDefault) lv_obj_add_state(makeDefault, LV_STATE_DISABLED);
    const auto defaultScene = std::find_if(sceneSet->scenes.begin(), sceneSet->scenes.end(),
      [&](const PresetScene& candidate) { return candidate.id == sceneSet->defaultSceneId; });
    label(defaultCard, "Current: " + (defaultScene == sceneSet->scenes.end()
          ? std::string{"Unknown"} : defaultScene->name),
          LV_ALIGN_BOTTOM_RIGHT, -20, -24, &ardor_font_saira_cond_medium_18, muted);

    lv_obj_t* operations = card(28, 360, 824, 150, "COPY SOUND TO / SWAP PHYSICAL SLOT");
    label(operations, "COPY TO", LV_ALIGN_TOP_LEFT, 20, 55,
          &ardor_font_saira_cond_medium_18, muted);
    label(operations, "SWAP", LV_ALIGN_TOP_LEFT, 20, 106,
          &ardor_font_saira_cond_medium_18, muted);
    for (std::size_t index = 0; index < sceneSet->scenes.size(); ++index) {
      const int bx = 132 + static_cast<int>(index) * 164;
      auto* copy = smallAction(operations, std::to_string(index + 1) + " "
        + uppercase(sceneSet->scenes[index].name), bx, 45, 152, onSceneCopyClicked, index);
      auto* swap = smallAction(operations, "SLOT " + std::to_string(index + 1),
                               bx, 96, 152, onSceneSwapClicked, index);
      if (index == state.editingScene) {
        lv_obj_add_state(copy, LV_STATE_DISABLED);
        lv_obj_add_state(swap, LV_STATE_DISABLED);
      }
    }

    lv_obj_t* presetSettings = card(868, 360, 384, 150, "PRESET SETTINGS  ·  OPEN IN");
    auto* openPresets = smallAction(presetSettings, "PRESETS", 20, 42, 158,
                                    onSceneOpenModeClicked, 0);
    auto* openScenes = smallAction(presetSettings, "SCENES", 196, 42, 168,
                                   onSceneOpenModeClicked, 1);
    auto* selectedOpen = sceneSet->openIn == PresetSceneOpenMode::Presets ? openPresets : openScenes;
    styleSurface(selectedOpen, text);
    lv_obj_set_style_text_color(lv_obj_get_child(selectedOpen, 0), lv_color_hex(bg), 0);
    lv_obj_t* capture = smallAction(presetSettings,
      state.sceneCapturePending ? "CAPTURING..." : "MORE  ·  CAPTURE",
      20, 94, 166, onCaptureCurrentSoundClicked);
    if (state.sceneCapturePending || state.scenes.transitioning || state.scenes.pending)
      lv_obj_add_state(capture, LV_STATE_DISABLED);
    lv_obj_t* disable = smallAction(presetSettings, "MORE  ·  DISABLE", 198, 94, 166,
                                    onDisableScenesClicked);
    lv_obj_set_style_text_color(lv_obj_get_child(disable, 0), lv_color_hex(danger), 0);

    if (state.sceneOperation.operation != UiSceneOperation::None) {
      const auto prompt = state.sceneOperation;
      lv_obj_t* veil = lv_obj_create(sheet);
      lv_obj_set_size(veil, kDesignWidth, 536);
      lv_obj_set_pos(veil, 0, 0);
      lv_obj_set_style_bg_color(veil, lv_color_hex(0x000000), 0);
      lv_obj_set_style_bg_opa(veil, LV_OPA_70, 0);
      lv_obj_set_style_border_width(veil, 0, 0);
      lv_obj_set_style_pad_all(veil, 0, 0);
      lv_obj_remove_flag(veil, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_t* dialog = lv_obj_create(veil);
      lv_obj_set_size(dialog, 650, 244);
      lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);
      styleSurface(dialog, panelAlt);
      lv_obj_set_style_pad_all(dialog, 0, 0);
      lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
      const auto& source = sceneSet->scenes[prompt.source];
      const auto& destination = sceneSet->scenes[prompt.destination];
      const bool copying = prompt.operation == UiSceneOperation::Copy;
      const bool disabling = prompt.operation == UiSceneOperation::Disable;
      label(dialog, copying ? "REPLACE SCENE SOUND?"
                            : disabling ? "DISABLE ALL SCENES?" : "SWAP PHYSICAL SLOTS?",
            LV_ALIGN_TOP_LEFT, 28, 24, &ardor_font_saira_cond_semibold_28, text);
      const std::string explanation = copying
        ? "Copy " + source.name + " into " + destination.name
            + ". The destination name and ID stay in place."
        : disabling
        ? "Keep " + source.name
            + " as the ordinary preset sound and remove scene data and scene MIDI links."
        : source.name + " and " + destination.name
            + " move with their IDs, defaults, and MIDI links.";
      lv_obj_t* details = label(dialog, explanation, LV_ALIGN_TOP_LEFT, 28, 74,
                                &ardor_font_saira_cond_medium_18, muted);
      lv_obj_set_width(details, 594);
      lv_label_set_long_mode(details, LV_LABEL_LONG_WRAP);
      lv_obj_t* cancel = button(dialog, "CANCEL");
      lv_obj_set_size(cancel, 174, 54);
      lv_obj_set_pos(cancel, 254, 166);
      styleSurface(cancel, panel);
      lv_obj_add_event_cb(cancel, onSceneOperationCancelClicked, LV_EVENT_CLICKED, remember(state));
      lv_obj_t* confirm = button(dialog, copying ? "REPLACE"
                                                 : disabling ? "DISABLE" : "SWAP SLOTS");
      lv_obj_set_size(confirm, 184, 54);
      lv_obj_set_pos(confirm, 438, 166);
      styleSurface(confirm, text);
      lv_obj_set_style_text_color(lv_obj_get_child(confirm, 0), lv_color_hex(bg), 0);
      lv_obj_add_event_cb(confirm, onSceneOperationConfirmClicked, LV_EVENT_CLICKED, remember(state));
    }
  }

  // ---- bottom control rail: Save is primary, Modules opens the drawer,
  // Global reaches the input/output gain page, Done returns to Preset.
  // There is no Redo: only a single-level undo snapshot exists today, and a
  // Redo control with nothing behind it would be a dead button.
  lv_obj_t* bottomRail = lv_obj_create(root);
  lv_obj_set_size(bottomRail, kDesignWidth, kEditBottomRailHeight);
  lv_obj_set_pos(bottomRail, 0, kEditBottomRailY);
  lv_obj_set_style_bg_color(bottomRail, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(bottomRail, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(bottomRail, 1, 0);
  lv_obj_set_style_border_side(bottomRail, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(bottomRail, lv_color_hex(rule), 0);
  lv_obj_set_style_radius(bottomRail, 0, 0);
  lv_obj_set_style_pad_all(bottomRail, 0, 0);
  lv_obj_remove_flag(bottomRail, LV_OBJ_FLAG_SCROLLABLE);

  int railX = kEditRailEdgeInset;
  const auto railButton = [&](const std::string& label_, int width, bool primary) {
    lv_obj_t* btn = button(bottomRail, uppercase(label_));
    lv_obj_set_size(btn, width, 52);
    lv_obj_align(btn, LV_ALIGN_LEFT_MID, railX, 0);
    lv_obj_set_style_text_letter_space(lv_obj_get_child(btn, 0), 2, 0);
    if (primary) {
      styleSurface(btn, text);
      lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), lv_color_hex(bg), 0);
    }
    railX += width + 11;
    return btn;
  };
  lv_obj_t* save = railButton(state.dirty ? "Save*" : "Save", 112, true);
  saveButtonLabel_ = lv_obj_get_child(save, 0);
  lv_obj_add_event_cb(save, onSaveClicked, LV_EVENT_CLICKED, remember(state));
  lv_obj_t* undo = railButton("Undo", 96, false);
  lv_obj_add_event_cb(undo, onEditRailUndoClicked, LV_EVENT_CLICKED, remember(state));
  if (!state.blockEditUndo.has_value()) lv_obj_add_state(undo, LV_STATE_DISABLED);
  lv_obj_t* modulesButton = railButton("Modules", 112, false);
  lv_obj_add_event_cb(modulesButton, onOpenBlockDrawer, LV_EVENT_PRESSED, remember(state));
  lv_obj_t* globalButton = railButton("Global", 96, false);
  lv_obj_add_event_cb(globalButton, onGlobalParamsClicked, LV_EVENT_CLICKED, remember(state));

  lv_obj_t* done = button(bottomRail, "DONE");
  lv_obj_set_size(done, 112, 52);
  lv_obj_align(done, LV_ALIGN_RIGHT_MID, -kEditRailEdgeInset, 0);
  styleSurface(done, text);
  lv_obj_set_style_text_color(lv_obj_get_child(done, 0), lv_color_hex(bg), 0);
  lv_obj_set_style_text_letter_space(lv_obj_get_child(done, 0), 2, 0);
  lv_obj_add_event_cb(done, onPresetModeClicked, LV_EVENT_PRESSED, remember(state));
}

} // namespace ardor
