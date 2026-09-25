#include "ui/LvglUi.h"

#include "ui/LampBlack.h"
#include "ui/LvglChainLayout.h"
#include "ui/LvglUiDrag.h"
#include "ui/LvglUiNavigation.h"
#include "ui/LvglUiStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace ardor {
namespace {

using namespace chain_layout;
using namespace lvgl_drag;
using namespace lvgl_navigation;
using namespace lvgl_ui;
using lb::editCountText;
using lb::editIdentityText;
using lb::placeModifiedTag;

// Chain card body (mockups/lvgl-taste/1-lamp-black.html): up to two value
// rows sit 16 px above the card's foot. Each row is a 24 px legend line, a
// 10 px gap, a 4 px family-coloured bar and a 10 px gap.
constexpr std::size_t kSummaryRows = 2;
constexpr int kSummaryRowPitch = 48;
constexpr int kSummaryBarTop = 34;
constexpr int kSummaryBarHeight = 4;
constexpr int kSummaryBottom = 16;
constexpr int kSummaryHeight = static_cast<int>(kSummaryRows) * kSummaryRowPitch - 10;
constexpr int kCardCaptionTop = 10;
constexpr int kCardNameTop = 69;
constexpr int kCardNameLineHeight = 30;
// The OFF legend on a bypassed card: 12 px from the right, 76 px down.
constexpr int kOffTagRight = 12;
constexpr int kOffTagTop = 76;
constexpr int kOffTagWidth = 44;
constexpr int kOffTagHeight = 28;
// The selected card lifts 4 px up and left and rests on a hard plate that
// sits 8 px below and right of it.
constexpr int kLiftShift = 4;
constexpr int kLiftOffset = 8;
constexpr int kSelectedBorder = 3;
// A bypassed card's cap prints at 45 %.
constexpr lv_opa_t kBypassedCapOpa = 115;
// Bypass hatch: CSS repeating-linear-gradient(135deg, plate 0 10px, stripe
// 10px 13px), rendered once per palette into a shared image.
constexpr int kHatchPeriod = 13;
constexpr int kHatchStripe = 3;
// Wide enough for a lane card, tall enough for a module card.
constexpr int kHatchWidth = std::max(kChainTileWidth, kLaneTileWidth);
// Scene tabs share the header with the preset identity, right-aligned.
constexpr int kSceneTabWidth = 132;
constexpr int kSceneTabHeight = 44;
constexpr int kSceneTabGap = 8;
constexpr int kSceneTabY = 10;
// Scene settings cards share the parameter drawer's 403 x 166 control card.
// They live at file scope because GCC rejects locals as lambda defaults.
constexpr int kSceneCardWidth = 403;
constexpr int kSceneCardHeight = 166;

struct HatchImage {
  std::vector<std::uint16_t> pixels;
  lv_image_dsc_t descriptor{};
  std::uint32_t plate = 0;
  std::uint32_t stripe = 0;
};

// One hatch bitmap serves every bypassed card; it is rebuilt only when the
// palette changes.
const lv_image_dsc_t* bypassHatch()
{
  static HatchImage image;
  const std::uint32_t stripe = lv_color_to_u32(
    lv_color_mix(lv_color_hex(text), lv_color_hex(panel), 17)) & 0xffffff;
  if (!image.pixels.empty() && image.plate == panel && image.stripe == stripe) {
    return &image.descriptor;
  }
  image.plate = panel;
  image.stripe = stripe;
  const auto plate565 = lv_color_to_u16(lv_color_hex(panel));
  const auto stripe565 = lv_color_to_u16(lv_color_hex(stripe));
  image.pixels.assign(static_cast<std::size_t>(kHatchWidth) * kChainTileHeight, plate565);
  for (int y = 0; y < kChainTileHeight; ++y) {
    for (int x = 0; x < kHatchWidth; ++x) {
      // Distance along the 135-degree gradient line, sampled at the pixel
      // centre, from the card's top-left corner.
      const double along = (x + y + 1.0) / std::sqrt(2.0);
      if (std::fmod(along, kHatchPeriod) >= kHatchPeriod - kHatchStripe) {
        image.pixels[static_cast<std::size_t>(y) * kHatchWidth + x] = stripe565;
      }
    }
  }
  image.descriptor = {};
  image.descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
  image.descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
  image.descriptor.header.w = kHatchWidth;
  image.descriptor.header.h = kChainTileHeight;
  image.descriptor.header.stride = kHatchWidth * 2;
  image.descriptor.data_size = static_cast<uint32_t>(image.pixels.size() * 2);
  image.descriptor.data = reinterpret_cast<const uint8_t*>(image.pixels.data());
  return &image.descriptor;
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
  // ---- header: EDIT, the preset or scene being edited, the MODIFIED tag ----
  lb::header(root);
  const auto& editedPreset = state.bank.presets[state.activePreset];
  const auto* sceneSet = editedPreset.sceneSet ? &*editedPreset.sceneSet : nullptr;
  lb::textLabel(root, lb::type::headerTitle, "EDIT", text, 28, 9);
  const int identityX = 28 + lb::textWidth(lb::type::headerTitle, "EDIT") + 20;
  editPresetLabel_ = lb::textLabel(root, lb::type::headerSub, editIdentityText(state), muted,
                                   identityX, 13);
  editModifiedLabel_ = lb::box(root, 0, 15, lb::textWidth(lb::type::tag, "MODIFIED") + 20, 33,
                               warning);
  lb::textLabel(editModifiedLabel_, lb::type::tag, "MODIFIED", warnInk, 10, 3);
  placeModifiedTag(editPresetLabel_, editModifiedLabel_);
  if (!state.dirty) lv_obj_add_flag(editModifiedLabel_, LV_OBJ_FLAG_HIDDEN);
  const bool hasWdwRoute = state.bank.presets[state.activePreset].routing == "wdw";
  if (sceneSet) {
    // Scene tabs replace the block count on the right of the header.
    const int tabsX = kDesignWidth - lb::kGutter
      - static_cast<int>(sceneSet->scenes.size()) * (kSceneTabWidth + kSceneTabGap) + kSceneTabGap;
    for (std::size_t index = 0; index < sceneSet->scenes.size(); ++index) {
      const auto& scene = sceneSet->scenes[index];
      lv_obj_t* tab = lb::button(root, "FS" + std::to_string(index + 1) + "  " + uppercase(scene.name),
        index == state.editingScene ? lb::ButtonKind::Primary : lb::ButtonKind::Normal,
        tabsX + static_cast<int>(index) * (kSceneTabWidth + kSceneTabGap), kSceneTabY,
        kSceneTabWidth, kSceneTabHeight, lb::type::page);
      lv_label_set_long_mode(lb::buttonLabel(tab), LV_LABEL_LONG_CLIP);
      lv_obj_add_event_cb(tab, onSceneTabClicked, LV_EVENT_CLICKED, remember(state, index));
    }
  } else {
    editModuleCountLabel_ = lb::textLabel(root, lb::type::headerRight, editCountText(state),
                                          disabled, 0, 18);
    lv_obj_set_x(editModuleCountLabel_, kDesignWidth - 28
                 - lb::textWidth(lb::type::headerRight, editCountText(state)));
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
  lv_obj_remove_style_all(chainViewport_);
  lv_obj_set_size(chainViewport_, kChainWidth, kChainHeight);
  lv_obj_set_pos(chainViewport_, kChainLeft, kChainTop);
  // Transparent over the ground; the colour only identifies the stage.
  lv_obj_set_style_bg_color(chainViewport_, lv_color_hex(bg), 0);
  lv_obj_set_scroll_dir(chainViewport_, LV_DIR_HOR);
  // The stage shows no scrollbar at rest; it appears only while scrolling.
  lv_obj_set_scrollbar_mode(chainViewport_, LV_SCROLLBAR_MODE_ACTIVE);
  lv_obj_set_style_bg_color(chainViewport_, lv_color_hex(disabled), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(chainViewport_, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(chainViewport_, 4, LV_PART_SCROLLBAR);
  lv_obj_set_style_height(chainViewport_, 4, LV_PART_SCROLLBAR);
  lv_obj_remove_flag(chainViewport_, LV_OBJ_FLAG_SCROLL_ELASTIC);

  chainWorld_ = lv_obj_create(chainViewport_);
  lv_obj_remove_style_all(chainWorld_);
  lv_obj_set_size(chainWorld_, kChainWidth, kChainWorldHeight);
  lv_obj_set_pos(chainWorld_, 0, 0);
  lv_obj_remove_flag(chainWorld_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(chainWorld_, LV_OBJ_FLAG_CLICKABLE);

  auto* scrollContext = remember(state);
  scrollContext->controlledObject = chainViewport_;
  lv_obj_add_event_cb(chainViewport_, onChainScroll, LV_EVENT_SCROLL, scrollContext);
  lv_obj_add_event_cb(chainViewport_, onChainScrollEnd, LV_EVENT_SCROLL_END, scrollContext);

  // The signal wire: 4 px of bone-3 through every gap in the chain.
  const auto rail = [&](int x, int y, int width, int color = disabled) {
    if (width <= 0) return static_cast<lv_obj_t*>(nullptr);
    return lb::box(chainWorld_, x, y - kChainWireHeight / 2, width, kChainWireHeight,
                   static_cast<std::uint32_t>(color));
  };
  // Jack: a 2 px bone-3 frame on the ground with its legend and signal.
  const auto terminal = [&](int x, const char* title, const char* detail) {
    lv_obj_t* object = lb::box(chainWorld_, x, kChainRailY - kChainTerminalHeight / 2,
                               kChainTerminalWidth, kChainTerminalHeight, bg, disabled, 2);
    const int inner = kChainTerminalWidth - 4;
    lb::textLabel(object, lb::type::jack, title, text,
                  (inner - lb::textWidth(lb::type::jack, title)) / 2, 2);
    lb::textLabel(object, lb::type::jackSmall, detail, disabled,
                  (inner - lb::textWidth(lb::type::jackSmall, detail)) / 2, 30);
    return object;
  };
  const auto topInsert = [&](int x, std::size_t index, bool disabledInsert = false) {
    rail(x - kChainGap, kChainRailY, kChainInsertWidth + 2 * kChainGap);
    lv_obj_t* add = lv_button_create(chainWorld_);
    lv_obj_remove_style_all(add);
    lv_obj_remove_flag(add, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_pos(add, x, kChainRailY - kChainInsertWidth / 2);
    lv_obj_set_size(add, kChainInsertWidth, kChainInsertWidth);
    lv_obj_set_style_radius(add, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(add, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(add, lv_color_hex(bg), 0);
    lb::setBorder(add, disabled, 2);
    lv_obj_set_style_border_color(add, lv_color_hex(text), LV_STATE_PRESSED);
    lv_obj_set_style_opa(add, LV_OPA_40, LV_STATE_DISABLED);
    // A larger invisible hit area keeps the 36 px circle finger-sized.
    lv_obj_set_ext_click_area(add, 6);
    lb::centeredText(add, lb::type::plus, "+", muted, -2, -2, kChainInsertWidth,
                     kChainInsertWidth);
    if (disabledInsert) lv_obj_add_state(add, LV_STATE_DISABLED);
    auto* context = remember(state, index);
    lv_obj_add_event_cb(add, onOpenBlockDrawerAt, LV_EVENT_CLICKED, context);
    chainInsertionXs_.push_back(x + kChainInsertWidth / 2);
  };
  const auto laneInsert = [&](int x, int y, std::size_t rigIndex,
                              std::size_t laneIndex, std::size_t index, int color, bool disabled) {
    lv_obj_t* add = lb::button(chainWorld_, "", lb::ButtonKind::Normal, x + 3, y - 18, 36, 36);
    lv_obj_set_style_radius(add, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(add, lv_color_hex(bg), 0);
    lb::setBorder(add, static_cast<std::uint32_t>(color), 2);
    lv_obj_set_ext_click_area(add, 6);
    lv_obj_delete(lb::buttonLabel(add));
    lb::centeredText(add, lb::type::plus, "+", static_cast<std::uint32_t>(color), -2, -2, 36, 36);
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
    styleSurface(handle, bg);
    lv_obj_set_style_pad_all(handle, 2, 0);
    setText(lv_obj_get_child(handle, 0), disabled, font);
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
  // Created before every card so it always draws behind the selected one.
  chainLiftPlate_ = lb::box(chainWorld_, 0, 0, kChainTileWidth, kChainTileHeight, liftShadow);
  lv_obj_add_flag(chainLiftPlate_, LV_OBJ_FLAG_HIDDEN);
  int x = kChainStartX;
  terminal(x, "IN", "MONO");
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
      rail(x - kChainGap, kChainRailY, kChainTileWidth + 2 * kChainGap);
      lv_obj_t* object = lv_button_create(chainWorld_);
      lv_obj_remove_style_all(object);
      lv_obj_remove_flag(object, LV_OBJ_FLAG_GESTURE_BUBBLE);
      lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_size(object, kChainTileWidth, kChainTileHeight);
      lv_obj_set_pos(object, x, kChainTileTop);
      lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(object, lv_color_hex(panel), 0);
      lb::setBorder(object, rule, 1);

      // The hatch sits behind everything and shows only while bypassed. It
      // ignores the border so its stripes line up with the card's corner.
      lv_obj_t* hatch = lv_image_create(object);
      lv_image_set_src(hatch, bypassHatch());
      lv_obj_add_flag(hatch, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_add_flag(hatch, LV_OBJ_FLAG_FLOATING);
      lv_obj_remove_flag(hatch, LV_OBJ_FLAG_CLICKABLE);

      // Family identity lives in the printed cap. The whole cap is the drag
      // surface; the body below is a tap target.
      lv_obj_t* categoryHeader = lv_obj_create(object);
      lv_obj_remove_style_all(categoryHeader);
      lv_obj_set_size(categoryHeader, LV_PCT(100), kChainHeaderHeight);
      lv_obj_set_pos(categoryHeader, 0, 0);
      lv_obj_set_style_bg_opa(categoryHeader, LV_OPA_COVER, 0);
      lv_obj_remove_flag(categoryHeader, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(categoryHeader, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(categoryHeader, LV_OBJ_FLAG_GESTURE_BUBBLE);
      lv_obj_t* categoryLabel = lb::textLabel(categoryHeader, lb::type::cap,
                                              uppercase(block.label), bg, kChainTextX,
                                              kCardCaptionTop);
      lv_obj_set_width(categoryLabel, kChainTextWidth);
      lv_label_set_long_mode(categoryLabel, LV_LABEL_LONG_CLIP);
      lv_obj_remove_flag(categoryLabel, LV_OBJ_FLAG_CLICKABLE);
      bindBlockDragSurface(categoryHeader, object, i);

      lv_obj_t* assetName = lb::textLabel(object, lb::type::blockName, uppercase(block.assetName),
                                          text, kChainTextX, kCardNameTop);
      // CSS lets one long word run into the right padding instead of
      // breaking it, so the label may use that padding too.
      lv_obj_set_width(assetName, kChainTextWidth + kChainTextX);
      lv_obj_set_style_text_line_space(assetName,
        kCardNameLineHeight - lv_font_get_line_height(lb::type::blockName.font), 0);
      lv_label_set_long_mode(assetName, LV_LABEL_LONG_WRAP);

      lv_obj_t* bypassed = lb::box(object, 0, kOffTagTop, kOffTagWidth, kOffTagHeight,
                                   panel, disabled, 1);
      lv_obj_set_style_bg_opa(bypassed, LV_OPA_TRANSP, 0);
      lb::centeredText(bypassed, lb::type::offTag, "OFF", muted, -1, -1, kOffTagWidth,
                       kOffTagHeight);
      lv_obj_add_flag(bypassed, LV_OBJ_FLAG_HIDDEN);

      lv_obj_t* summary = lv_obj_create(object);
      lv_obj_remove_style_all(summary);
      lv_obj_set_size(summary, kChainTextWidth, kSummaryHeight);
      lv_obj_remove_flag(summary, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_remove_flag(summary, LV_OBJ_FLAG_SCROLLABLE);

      auto* clickContext = remember(state, i);
      lv_obj_add_event_cb(object, onBlockClicked, LV_EVENT_CLICKED, clickContext);

      chainCards_[i] = object;
      chainCategoryLabels_[i] = categoryLabel;
      chainAssetLabels_[i] = assetName;
      chainBypassLabels_[i] = bypassed;
      chainSummaries_[i] = summary;
      chainClickContexts_[i] = clickContext;
      styleChainCard(state, i);
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
      styleSurface(splitStem, disabled);
      lv_obj_set_style_border_width(splitStem, 0, 0);
      lv_obj_remove_flag(splitStem, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_t* joinStem = lv_obj_create(chainWorld_);
      lv_obj_set_size(joinStem, 3, kChainRightRailY - kChainLeftRailY);
      lv_obj_set_pos(joinStem, joinX + kChainJunctionWidth / 2 - 1, kChainLeftRailY);
      styleSurface(joinStem, disabled);
      lv_obj_set_style_border_width(joinStem, 0, 0);
      lv_obj_remove_flag(joinStem, LV_OBJ_FLAG_CLICKABLE);

      lv_obj_t* split = button(chainWorld_, wdw ? "WDW" : "SPLIT");
      lv_obj_set_size(split, kChainJunctionWidth, 82);
      lv_obj_set_pos(split, splitX, kChainRailY - 41);
      styleSurface(split, bg);
      lv_obj_set_style_pad_all(split, 0, 0);
      lv_obj_set_style_border_color(split, lv_color_hex(selected ? text : disabled), 0);
      lv_obj_set_style_border_width(split, selected ? 3 : 2, 0);
      lb::applyType(lv_obj_get_child(split, 0), lb::type::jack, text);
      lv_obj_t* splitLabel = lv_obj_get_child(split, 0);
      lv_obj_set_width(splitLabel, kChainJunctionWidth - kChainHandleWidth - 24);
      lv_obj_align(splitLabel, LV_ALIGN_LEFT_MID, 12, 0);
      lv_obj_set_style_text_align(splitLabel, LV_TEXT_ALIGN_LEFT, 0);
      if (wdw) {
        // The note sits under the junction, left of the wet lane's stem;
        // inside the junction the drag grip clips it.
        lv_obj_t* note = lb::textLabel(chainWorld_, lb::type::jackSmall, "NO DIRECT INPUT",
                                       disabled, splitX, kChainRailY + 41 + 6);
        lv_obj_set_style_text_letter_space(note, 0, 0);
        lv_obj_set_width(note, kChainJunctionWidth / 2 - 6);
        lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
      }
      auto* clickContext = remember(state, i);
      lv_obj_add_event_cb(split, onBlockClicked, LV_EVENT_CLICKED, clickContext);
      dragHandle(split, split, i);
      chainCards_[i] = split;
      chainClickContexts_[i] = clickContext;

      lv_obj_t* join = lv_obj_create(chainWorld_);
      lv_obj_set_size(join, kChainJunctionWidth, 64);
      lv_obj_set_pos(join, joinX, kChainRailY - 32);
      styleSurface(join, bg);
      lv_obj_set_style_border_color(join, lv_color_hex(disabled), 0);
      lv_obj_set_style_border_width(join, 2, 0);
      lv_obj_set_style_pad_all(join, 0, 0);
      lv_obj_remove_flag(join, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_remove_flag(join, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_t* joinLabel = label(join, "JOIN", LV_ALIGN_CENTER, 0, 0, lb::type::jack.font, text);
      lv_obj_set_style_text_letter_space(joinLabel, lb::type::jack.letterSpace(), 0);

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
              lb::type::controlLabel.font, visibleLaneColor);
        if (wdw) {
          label(chainWorld_, uppercase(wdwLaneMixSummary(block, laneIndex)),
                LV_ALIGN_TOP_LEFT, splitX + kChainJunctionWidth / 2 + 12,
                laneIndex == 0 ? laneY - 54 : laneY + 78,
                lb::type::chipSmall.font, laneEnabled ? muted : disabled);
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
          styleSurface(childObject, panel);
          lv_obj_set_style_pad_all(childObject, 0, 0);
          const bool childSelected = state.paramTarget == UiParamTarget::Block
            && state.selectedBlockId == child.id;
          lv_obj_set_style_border_color(childObject,
                                        lv_color_hex(childSelected ? text : visibleLaneColor), 0);
          lv_obj_set_style_border_width(childObject,
                                        childSelected ? 3 : (child.enabled ? 1 : 0), 0);
          if (child.enabled) {
            lv_obj_remove_state(childObject, LV_STATE_USER_1);
          } else {
            lv_obj_add_state(childObject, LV_STATE_USER_1);
          }
          if (!child.enabled) {
            lv_obj_t* hatch = lv_image_create(childObject);
            lv_image_set_src(hatch, bypassHatch());
            lv_obj_add_flag(hatch, LV_OBJ_FLAG_FLOATING);
            lv_obj_remove_flag(hatch, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_pos(hatch, -1, -1);
          }
          if (!laneEnabled) lv_obj_set_style_opa(childObject, LV_OPA_70, 0);
          // Compact lane cards keep the same interaction grammar: the entire
          // title strip is a deliberate, finger-sized drag surface, while the
          // body remains a tap target for editing.
          lv_obj_t* childHeader = lv_obj_create(childObject);
          lv_obj_set_size(childHeader, kLaneTileWidth, kLaneHeaderHeight);
          lv_obj_set_pos(childHeader, 0, 0);
          styleSurface(childHeader, categoryColor(child.type));
          if (!child.enabled || !laneEnabled) lv_obj_set_style_opa(childHeader, kBypassedCapOpa, 0);
          lv_obj_set_style_border_width(childHeader, 0, 0);
          lv_obj_set_style_pad_all(childHeader, 0, 0);
          lv_obj_remove_flag(childHeader, LV_OBJ_FLAG_SCROLLABLE);
          lv_obj_add_flag(childHeader, LV_OBJ_FLAG_CLICKABLE);
          lv_obj_set_style_opa(childHeader, LV_OPA_70, LV_STATE_PRESSED);
          // Cap legends print in the module card's cap face on the family
          // colour; the asset reads in the chip face below.
          lv_obj_t* childTitle = label(childHeader, laneToken(child), LV_ALIGN_LEFT_MID, 14, 0,
                                       lb::type::cap.font, bg);
          lv_obj_set_style_text_letter_space(childTitle, lb::type::cap.letterSpace(), 0);
          lv_obj_t* childDragLabel = label(childHeader, "DRAG", LV_ALIGN_RIGHT_MID, -14, 0,
                                           lb::type::cap.font, bg);
          lv_obj_set_style_text_letter_space(childDragLabel, lb::type::cap.letterSpace(), 0);
          lv_obj_remove_flag(childTitle, LV_OBJ_FLAG_CLICKABLE);
          lv_obj_remove_flag(childDragLabel, LV_OBJ_FLAG_CLICKABLE);
          const std::string childReadout = child.enabled
            ? uppercase(child.assetName) : "OFF  /  " + uppercase(child.assetName);
          lv_obj_t* childAsset = label(childObject, childReadout, LV_ALIGN_BOTTOM_LEFT, 13, -10,
                                       lb::type::chip.font,
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

  rail(x - kChainGap, kChainRailY, kChainGap);
  terminal(x, "OUT", "STEREO");
  x += kChainTerminalWidth + kChainStartX;
  lv_obj_set_width(chainWorld_, std::max(x, kChainWidth));
  lv_obj_update_layout(chainViewport_);
  const int32_t savedScroll = state.activePreset < state.chainScrollOffsets.size()
    ? state.chainScrollOffsets[state.activePreset] : 0;
  lv_obj_scroll_to_x(chainViewport_, savedScroll, LV_ANIM_OFF);

  if (sceneSet && state.sceneSettingsOpen) {
    const auto& scene = sceneSet->scenes[state.editingScene];
    // The sheet covers the stage with the parameter drawer's card grid.
    lv_obj_t* sheet = lb::box(root, 0, lb::kHeaderHeight, kDesignWidth,
                              lb::kRailY - lb::kHeaderHeight, bg);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);
    constexpr int kRowA = 12;
    constexpr int kRowB = kRowA + kSceneCardHeight + lb::kGap;
    constexpr int kRowC = kRowB + kSceneCardHeight + lb::kGap;
    constexpr int kWideHeight = lb::kRailY - lb::kHeaderHeight - kRowC - lb::kGap;
    const auto columnX = [](int column) {
      return lb::kGutter + static_cast<int>(std::lround(column * ((1232.0 - 24.0) / 3.0 + 12.0) - 0.001));
    };
    const auto card = [&](int column, int y, const std::string& title, int width = kSceneCardWidth,
                          int height = kSceneCardHeight) {
      lv_obj_t* result = lb::box(sheet, columnX(column), y, width, height, panel, rule, 1);
      lb::textLabel(result, lb::type::controlLabel, title, muted, 20, 15);
      return result;
    };
    const auto action = [&](lv_obj_t* parent, const std::string& caption, int x, int y, int width,
                            int height, lv_event_cb_t callback, std::size_t index = 0,
                            lb::ButtonKind kind = lb::ButtonKind::Normal) {
      lv_obj_t* control = lb::button(parent, caption, kind, x, y, width, height);
      lv_obj_add_event_cb(control, callback, LV_EVENT_CLICKED, remember(state, index));
      return control;
    };
    // A chosen segment lifts to the raised plate with a bone foot.
    const auto markSelected = [](lv_obj_t* control) {
      lv_obj_set_style_bg_color(control, lv_color_hex(plateHi), 0);
      lb::box(control, 0, lv_obj_get_style_height(control, LV_PART_MAIN) - 2 - 5,
              lv_obj_get_style_width(control, LV_PART_MAIN) - 2, 5, text);
    };
    const auto value = [&](lv_obj_t* parent, const std::string& caption, std::uint32_t color) {
      return lb::textLabel(parent, lb::type::contextValue, caption, color, 20,
                           lb::centeredTextTop(lb::type::contextValue, 46, 50)
                             - lb::textTop(lb::type::contextValue, 0) + 0);
    };
    constexpr int kInner = kSceneCardWidth - 2 - 40;
    constexpr int kFootY = kSceneCardHeight - 2 - 16 - 56;

    lv_obj_t* nameCard = card(0, kRowA, "NAME");
    value(nameCard, uppercase(scene.name), text);
    action(nameCard, "RENAME", 20, kFootY, kInner, 56, onSceneNameEditClicked);

    lv_obj_t* enterCard = card(1, kRowA, "ENTER TIME");
    char enterText[32]{};
    if (scene.enterTimeMs == 0) std::snprintf(enterText, sizeof(enterText), "INSTANT");
    else std::snprintf(enterText, sizeof(enterText), "%.1f S", scene.enterTimeMs / 1000.0f);
    value(enterCard, enterText, scene.enterTimeMs == 0 ? muted : text);
    lv_obj_t* enterMinus = action(enterCard, "-", kInner + 20 - 2 * 60 - 8, 40, 60, 48,
                                  onSceneEnterDeltaClicked, 0);
    lv_obj_t* enterPlus = action(enterCard, "+", kInner + 20 - 60, 40, 60, 48,
                                 onSceneEnterDeltaClicked, 1);
    if (scene.enterTimeMs == 0) {
      lv_obj_add_state(enterMinus, LV_STATE_DISABLED);
      lv_obj_add_state(enterPlus, LV_STATE_DISABLED);
    }
    const int half = (kInner + 1) / 2;
    lv_obj_t* instant = action(enterCard, "INSTANT", 19, kFootY, half + 1, 56, onSceneInstantClicked);
    lv_obj_t* timed = action(enterCard, "TIMED", 19 + half, kFootY, kInner + 1 - half + 1, 56,
                             onSceneTimedClicked);
    markSelected(scene.enterTimeMs == 0 ? instant : timed);

    lv_obj_t* trimCard = card(2, kRowA, "SCENE TRIM");
    char trimText[32]{};
    std::snprintf(trimText, sizeof(trimText), "%+.1f DB", scene.outputTrimDb);
    value(trimCard, trimText, text);
    const std::string range = "-12 DB  \xC2\xB7  +6 DB";
    lb::textLabel(trimCard, lb::type::page, range, disabled,
                  kInner + 20 - lb::textWidth(lb::type::page, range), 16);
    action(trimCard, "-", 19, kFootY, half + 1, 56, onSceneTrimDeltaClicked, 0);
    action(trimCard, "+", 19 + half, kFootY, kInner + 1 - half + 1, 56, onSceneTrimDeltaClicked, 1);

    lv_obj_t* defaultCard = card(0, kRowB, "DEFAULT ON PRESET LOAD");
    const auto defaultScene = std::find_if(sceneSet->scenes.begin(), sceneSet->scenes.end(),
      [&](const PresetScene& candidate) { return candidate.id == sceneSet->defaultSceneId; });
    value(defaultCard, defaultScene == sceneSet->scenes.end() ? std::string{"UNKNOWN"}
                                                              : uppercase(defaultScene->name),
          text);
    const bool isDefault = sceneSet->defaultSceneId == scene.id;
    lv_obj_t* makeDefault = action(defaultCard, isDefault ? "CURRENT DEFAULT"
                                                          : "MAKE " + uppercase(scene.name) + " DEFAULT",
                                   20, kFootY, kInner, 56, onSceneDefaultClicked);
    if (isDefault) lv_obj_add_state(makeDefault, LV_STATE_DISABLED);

    lv_obj_t* presetSettings = card(1, kRowB, "PRESET SETTINGS  ·  OPEN IN");
    lv_obj_t* openPresets = action(presetSettings, "PRESETS", 19, kFootY, half + 1, 56,
                                   onSceneOpenModeClicked, 0);
    lv_obj_t* openScenes = action(presetSettings, "SCENES", 19 + half, kFootY,
                                  kInner + 1 - half + 1, 56, onSceneOpenModeClicked, 1);
    markSelected(sceneSet->openIn == PresetSceneOpenMode::Presets ? openPresets : openScenes);
    value(presetSettings, sceneSet->openIn == PresetSceneOpenMode::Presets ? "PRESETS" : "SCENES",
          text);

    lv_obj_t* moreCard = card(2, kRowB, "MORE");
    lv_obj_t* capture = action(moreCard, state.sceneCapturePending ? "CAPTURING..." : "MORE  ·  CAPTURE",
                               20, 44, kInner, 52, onCaptureCurrentSoundClicked);
    if (state.sceneCapturePending || state.scenes.transitioning || state.scenes.pending)
      lv_obj_add_state(capture, LV_STATE_DISABLED);
    action(moreCard, "MORE  ·  DISABLE", 20, 44 + 52 + 8, kInner, 52, onDisableScenesClicked, 0,
           lb::ButtonKind::Danger);

    lv_obj_t* operations = card(0, kRowC, "COPY SOUND TO / SWAP PHYSICAL SLOT",
                                kDesignWidth - 2 * lb::kGutter, kWideHeight);
    constexpr int kLegendWidth = 120;
    const int slotWidth = (kDesignWidth - 2 * lb::kGutter - 2 - 40 - kLegendWidth
                           - 3 * lb::kGap) / 4;
    const int copyY = 44;
    const int swapY = copyY + 48 + 8;
    lb::textLabel(operations, lb::type::controlLabel, "COPY TO", muted, 20, copyY + 10);
    lb::textLabel(operations, lb::type::controlLabel, "SWAP", muted, 20, swapY + 10);
    for (std::size_t index = 0; index < sceneSet->scenes.size(); ++index) {
      const int bx = 20 + kLegendWidth + static_cast<int>(index) * (slotWidth + lb::kGap);
      auto* copy = action(operations, std::to_string(index + 1) + " "
        + uppercase(sceneSet->scenes[index].name), bx, copyY, slotWidth, 48, onSceneCopyClicked,
        index);
      auto* swap = action(operations, "SLOT " + std::to_string(index + 1), bx, swapY, slotWidth, 48,
                          onSceneSwapClicked, index);
      if (index == state.editingScene) {
        lv_obj_add_state(copy, LV_STATE_DISABLED);
        lv_obj_add_state(swap, LV_STATE_DISABLED);
      }
    }

    if (state.sceneOperation.operation != UiSceneOperation::None) {
      const auto prompt = state.sceneOperation;
      lv_obj_t* veil = lb::createOverlay(root);
      const auto& source = sceneSet->scenes[prompt.source];
      const auto& destination = sceneSet->scenes[prompt.destination];
      const bool copying = prompt.operation == UiSceneOperation::Copy;
      const bool disabling = prompt.operation == UiSceneOperation::Disable;
      constexpr int kDialogWidth = 700;
      constexpr int kDialogHeight = 270;
      lv_obj_t* dialog = lb::createDialog(veil, kDialogWidth, kDialogHeight,
        copying ? "REPLACE SCENE SOUND?" : disabling ? "DISABLE ALL SCENES?" : "SWAP PHYSICAL SLOTS?");
      const std::string explanation = copying
        ? "Copy " + source.name + " into " + destination.name
            + ". The destination name and ID stay in place."
        : disabling
        ? "Keep " + source.name
            + " as the ordinary preset sound and remove scene data and scene MIDI links."
        : source.name + " and " + destination.name
            + " move with their IDs, defaults, and MIDI links.";
      lb::dialogBody(dialog, uppercase(explanation), kDialogWidth);
      auto actions = lb::dialogActions(dialog, kDialogWidth, kDialogHeight,
        {{"CANCEL", lb::ButtonKind::Normal},
         {copying ? "REPLACE" : disabling ? "DISABLE" : "SWAP SLOTS",
          disabling ? lb::ButtonKind::Danger : lb::ButtonKind::Primary}});
      lv_obj_add_event_cb(actions[0], onSceneOperationCancelClicked, LV_EVENT_CLICKED, remember(state));
      lv_obj_add_event_cb(actions[1], onSceneOperationConfirmClicked, LV_EVENT_CLICKED, remember(state));
    }
  }

  // ---- bottom rail: Save is primary, Modules opens the drawer, Global
  // reaches the input/output gain page, Done returns to Preset. Rename and
  // the scene entry point take the free rail space after the mockup's four.
  // There is no Redo: only a single-level undo snapshot exists today, and a
  // Redo control with nothing behind it would be a dead button.
  lb::rail(root);
  int railX = lb::kGutter;
  const auto railButton = [&](const std::string& legend, lb::ButtonKind kind) {
    lv_obj_t* btn = lb::button(root, legend, kind, railX, lb::kRailButtonY);
    railX += lv_obj_get_style_width(btn, LV_PART_MAIN) + lb::kGap;
    return btn;
  };
  lv_obj_t* save = railButton("SAVE", lb::ButtonKind::Primary);
  saveButtonLabel_ = lb::buttonLabel(save);
  lv_obj_add_event_cb(save, onSaveClicked, LV_EVENT_CLICKED, remember(state));
  lv_obj_t* undo = railButton("UNDO", lb::ButtonKind::Normal);
  lv_obj_add_event_cb(undo, onEditRailUndoClicked, LV_EVENT_CLICKED, remember(state));
  if (!state.blockEditUndo.has_value()) lv_obj_add_state(undo, LV_STATE_DISABLED);
  lv_obj_t* modulesButton = railButton("MODULES", lb::ButtonKind::Normal);
  lv_obj_add_event_cb(modulesButton, onOpenBlockDrawer, LV_EVENT_PRESSED, remember(state));
  lv_obj_t* globalButton = railButton("GLOBAL", lb::ButtonKind::Normal);
  lv_obj_add_event_cb(globalButton, onGlobalParamsClicked, LV_EVENT_CLICKED, remember(state));
  lv_obj_t* rename = railButton(sceneSet ? "RENAME SCENE" : "RENAME", lb::ButtonKind::Normal);
  lv_obj_add_event_cb(rename, sceneSet ? onSceneNameEditClicked : onPresetNameEditClicked,
                      LV_EVENT_CLICKED, remember(state));
  if (sceneSet) {
    lv_obj_t* sceneSettings = railButton(
      state.sceneSettingsOpen ? "CLOSE SETTINGS" : "SCENE SETTINGS", lb::ButtonKind::Normal);
    lv_obj_add_event_cb(sceneSettings,
      state.sceneSettingsOpen ? onCloseSceneSettingsClicked : onOpenSceneSettingsClicked,
      LV_EVENT_CLICKED, remember(state));
  } else {
    lv_obj_t* createScenes = railButton("CREATE FOUR SCENES", lb::ButtonKind::Normal);
    lv_obj_add_event_cb(createScenes, onCreateScenesClicked, LV_EVENT_CLICKED, remember(state));
  }
  lv_obj_t* done = lb::button(root, "DONE", lb::ButtonKind::Normal, 0, lb::kRailButtonY);
  lv_obj_set_x(done, kDesignWidth - lb::kGutter - lv_obj_get_style_width(done, LV_PART_MAIN));
  lv_obj_add_event_cb(done, onPresetModeClicked, LV_EVENT_PRESSED, remember(state));
}

void LvglUi::styleChainCard(const UiState& state, std::size_t index)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  lv_obj_t* card = index < chainCards_.size() ? chainCards_[index] : nullptr;
  if (!card || index >= blocks.size()) return;
  const auto& block = blocks[index];
  const bool selected = state.paramTarget == UiParamTarget::Block
    && state.selectedBlock == index && !selectedBlockIsLaneChild(state);
  const bool highlighted = isBlockHighlighted(block.id);
  const int border = selected || highlighted ? kSelectedBorder : 1;
  lv_obj_set_style_border_color(card, lv_color_hex(selected || highlighted ? text : rule), 0);
  lv_obj_set_style_border_width(card, border, 0);
  lv_obj_set_style_translate_x(card, selected ? -kLiftShift : 0, 0);
  lv_obj_set_style_translate_y(card, selected ? -kLiftShift : 0, 0);
  if (block.enabled) lv_obj_remove_state(card, LV_STATE_USER_1);
  else lv_obj_add_state(card, LV_STATE_USER_1);

  // Children sit inside the border, so a 3 px border shifts them by two.
  const int inset = border - 1;
  lv_obj_t* hatch = lv_obj_get_child(card, 0);
  lv_obj_set_pos(hatch, -border, -border);
  if (block.enabled) lv_obj_add_flag(hatch, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(hatch, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* categoryLabel = chainCategoryLabels_[index];
  lv_obj_t* categoryHeader = lv_obj_get_parent(categoryLabel);
  lv_label_set_text(categoryLabel, uppercase(block.label).c_str());
  lv_obj_set_style_bg_color(categoryHeader, lv_color_hex(categoryColor(block.type)), 0);
  lv_obj_set_style_opa(categoryHeader, block.enabled ? LV_OPA_COVER : kBypassedCapOpa, 0);

  lv_obj_t* assetName = chainAssetLabels_[index];
  lv_label_set_text(assetName, uppercase(block.assetName).c_str());
  lv_obj_set_style_text_color(assetName, lv_color_hex(block.enabled ? text : disabled), 0);
  lv_obj_set_y(assetName, lb::textTop(lb::type::blockName, kCardNameTop) - inset);

  lv_obj_t* offTag = chainBypassLabels_[index];
  lv_obj_set_pos(offTag, kChainTileWidth - 2 * border - kOffTagRight - kOffTagWidth,
                 kOffTagTop - inset);
  if (block.enabled) lv_obj_add_flag(offTag, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(offTag, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* summary = chainSummaries_[index];
  lv_obj_set_pos(summary, kChainTextX,
                 kChainTileHeight - 2 * border - kSummaryBottom - kSummaryHeight);
  renderChainSummary(summary, state, block);
}

void LvglUi::renderChainSummary(lv_obj_t* container, const UiState& state, const UiBlock& block)
{
  if (!container) return;
  lv_obj_clean(container);
  // A bypassed card shows only its OFF legend; values would suggest it runs.
  if (!block.enabled) return;
  const auto family = static_cast<std::uint32_t>(categoryColor(block.type));
  const auto items = blockSummaryControls(state, block, kSummaryRows);
  // Rows stack from the card's foot: one row sits where the second would.
  int y = static_cast<int>(kSummaryRows - std::min(items.size(), kSummaryRows)) * kSummaryRowPitch;
  for (const auto& item : items) {
    lv_obj_t* value = lb::textLabel(container, lb::type::paramValue, item.formatted, text, 0, y);
    const int valueWidth = lb::textWidth(lb::type::paramValue, item.formatted);
    lv_obj_set_x(value, kChainTextWidth - valueWidth);
    lv_obj_t* legend = lb::textLabel(container, lb::type::paramName, item.label, muted, 0, y);
    lv_obj_set_width(legend, std::max(0, kChainTextWidth - valueWidth - 6));
    lv_label_set_long_mode(legend, LV_LABEL_LONG_CLIP);
    if (item.kind == ParameterControlKind::Continuous && item.maximum > item.minimum) {
      const float fraction = std::clamp((item.value - item.minimum) / (item.maximum - item.minimum),
                                        0.0f, 1.0f);
      lb::box(container, 0, y + kSummaryBarTop,
              std::max(2, static_cast<int>(std::lround(fraction * kChainTextWidth))),
              kSummaryBarHeight, family);
    }
    y += kSummaryRowPitch;
  }
}

void LvglUi::syncChainLiftPlate(const UiState& state)
{
  if (!chainLiftPlate_) return;
  const bool topLevelSelected = state.paramTarget == UiParamTarget::Block
    && !selectedBlockIsLaneChild(state) && state.selectedBlock < chainCards_.size();
  lv_obj_t* card = topLevelSelected ? chainCards_[state.selectedBlock] : nullptr;
  // Only full-height module cards lift; split/join junctions stay flat.
  if (!card || lv_obj_get_style_height(card, LV_PART_MAIN) != kChainTileHeight) {
    lv_obj_add_flag(chainLiftPlate_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  // The card itself moves up-left by kLiftShift; its plate lands 8 px below
  // and right of the lifted card.
  lv_obj_set_pos(chainLiftPlate_,
                 lv_obj_get_style_x(card, LV_PART_MAIN) - kLiftShift + kLiftOffset,
                 lv_obj_get_style_y(card, LV_PART_MAIN) - kLiftShift + kLiftOffset);
  lv_obj_remove_flag(chainLiftPlate_, LV_OBJ_FLAG_HIDDEN);
}

} // namespace ardor
