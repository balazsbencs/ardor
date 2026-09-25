#include "ui/LvglUi.h"

#include "ui/LvglUiParameterView.h"
#include "ui/LampBlack.h"
#include "ui/LvglUiStyle.h"
#include "ui/PresetChainStrip.h"

#include <algorithm>
#include <cstdint>
#include <array>
#include <string>
#include <utility>

namespace ardor {

namespace {

bool isPassEqStage(std::size_t stage)
{
  return stage == kEqHighPassStage || stage == kEqLowPassStage;
}

EqPassFilterKind passKindForStage(std::size_t stage)
{
  return stage == kEqHighPassStage ? EqPassFilterKind::HighPass : EqPassFilterKind::LowPass;
}

const EqPassFilterParams& passFilterForStage(const ParametricEqParams& params, std::size_t stage)
{
  return stage == kEqHighPassStage ? params.highPass : params.lowPass;
}

} // namespace

void LvglUi::toggleExpressionAssignment(UiState& state, const ParameterControl& control)
{
  if (!ardor::toggleExpressionAssignment(state, control)) return;
  if (actions_.updateExpressionAssignment) {
    actions_.updateExpressionAssignment(state.bank.presets[state.activePreset].expression);
  }
  invalidate(UiChange::Parameters | UiChange::Status | UiChange::Telemetry);
}

using namespace lvgl_ui;

bool LvglUi::updateSelectedEqBand(UiState& state, EqBandParams params, bool requestUiRebuild)
{
  if (isPassEqStage(selectedEqStage_)) return false;
  const auto* selected = selectedUiBlock(state);
  if (!selected) return false;
  const auto blockId = selected->id;
  const auto blockName = selected->assetName;
  const std::size_t bandIndex = selectedEqStage_ - kEqFirstBandStage;
  const auto before = selectedParametricEqParams(state).bands[bandIndex];
  const bool dirtyBefore = state.dirty;
  if (!setSelectedEqBand(state, bandIndex, params)) {
    return false;
  }
  const auto after = selectedParametricEqParams(state).bands[bandIndex];
  if (after == before) {
    return false;
  }
  if (actions_.updateEqBand && !actions_.updateEqBand(blockId, bandIndex, after)) {
    // A missing runtime ID is an unexpected draft/runtime divergence. Keep
    // the candidate and heal it through the complete-preview path.
    setSelectedEqBand(state, bandIndex, before);
    state.dirty = dirtyBefore;
    auto previewRollback = captureUiPreviewSnapshot(state);
    setSelectedEqBand(state, bandIndex, after);
    if (queuePreview(state, previewRollback, "update " + blockName + " EQ")) {
      invalidate(UiChange::Parameters | UiChange::Header);
      return true;
    }
    setSelectedEqBand(state, bandIndex, before);
    state.dirty = dirtyBefore;
    invalidate(UiChange::Parameters | UiChange::Header);
    return false;
  }
  if (requestUiRebuild) {
    invalidate(UiChange::Parameters);
  }
  return true;
}

bool LvglUi::updateSelectedEqPassFilter(UiState& state, EqPassFilterParams params,
                                        bool requestUiRebuild)
{
  if (!isPassEqStage(selectedEqStage_)) return false;
  const auto* selected = selectedUiBlock(state);
  if (!selected) return false;
  const auto blockId = selected->id;
  const auto blockName = selected->assetName;
  const auto kind = passKindForStage(selectedEqStage_);
  const auto before = passFilterForStage(selectedParametricEqParams(state), selectedEqStage_);
  const bool dirtyBefore = state.dirty;
  if (!setSelectedEqPassFilter(state, kind, params)) return false;
  const auto after = passFilterForStage(selectedParametricEqParams(state), selectedEqStage_);
  if (after == before) return false;
  if (actions_.updateEqPassFilter && !actions_.updateEqPassFilter(blockId, kind, after)) {
    setSelectedEqPassFilter(state, kind, before);
    state.dirty = dirtyBefore;
    auto previewRollback = captureUiPreviewSnapshot(state);
    setSelectedEqPassFilter(state, kind, after);
    if (queuePreview(state, previewRollback, "update " + blockName + " filter")) {
      invalidate(UiChange::Parameters | UiChange::Header);
      return true;
    }
    setSelectedEqPassFilter(state, kind, before);
    state.dirty = dirtyBefore;
    invalidate(UiChange::Parameters | UiChange::Header);
    return false;
  }
  if (requestUiRebuild) invalidate(UiChange::Parameters);
  return true;
}

bool LvglUi::applyFocusedParameterDelta(UiState& state, int delta, bool continuousTouch)
{
  if (focusedEqField_.has_value()) {
    const auto* selected = selectedUiBlock(state);
    if (state.paramTarget != UiParamTarget::Block || !selected
        || selected->type != "eq" || !isParametricEqMode(selected->params)) {
      return false;
    }
    auto params = selectedParametricEqParams(state);
    if (isPassEqStage(selectedEqStage_)) {
      auto filter = passFilterForStage(params, selectedEqStage_);
      adjustEqPassFilterField(filter, *focusedEqField_, delta);
      if (!updateSelectedEqPassFilter(state, filter, focusedEqGraph_ == nullptr)) return false;
    } else {
      const auto bandIndex = selectedEqStage_ - kEqFirstBandStage;
      adjustEqBandField(params.bands[bandIndex], *focusedEqField_, delta);
      if (!updateSelectedEqBand(state, params.bands[bandIndex], focusedEqGraph_ == nullptr)) return false;
    }
    const auto updated = selectedParametricEqParams(state);
    if (focusedControl_) {
      const auto control = isPassEqStage(selectedEqStage_)
        ? parameter_view::eqControl(*focusedEqField_, passFilterForStage(updated, selectedEqStage_))
        : parameter_view::eqControl(*focusedEqField_,
            updated.bands[selectedEqStage_ - kEqFirstBandStage]);
      parameter_view::syncSlider(focusedControl_, control);
    }
    parameter_view::syncEqGraph(focusedEqGraph_, updated, continuousTouch);
    renderedRevisions_.parameters = state.revisions.parameters;
    renderedRevisions_.header = state.revisions.header;
    syncHeaderView(state);
    return true;
  }
  if (focusedKey_.empty()) {
    return false;
  }
  for (const auto& control : ardor::parameterPage(state, parameterPage_)) {
    if (control.key != focusedKey_) {
      continue;
    }
    const bool dirtyBefore = state.dirty;
    nlohmann::json paramsBefore;
    bool hasBlockSnapshot = false;
    std::string selectedName = "effect";
    if (state.paramTarget == UiParamTarget::Block) {
      if (const auto* selected = selectedUiBlock(state)) {
        paramsBefore = selected->params;
        selectedName = selected->assetName;
        hasBlockSnapshot = true;
      }
    }
    if (applyParameterDelta(state, control, delta)) {
      bool liveUpdateSucceeded = true;
      const bool sceneOwned = control.sceneScope == UiSceneScope::ThisScene;
      if (sceneOwned) {
        const auto targetIndex = selectedParameterSceneTargetIndex(state, control.key);
        if (targetIndex) {
          const auto& target = state.bank.presets[state.activePreset].sceneSet
            ->scenes[state.editingScene].targets[*targetIndex];
          const float targetValue = target.value.is_boolean()
            ? (target.value.get<bool>() ? 1.0f : 0.0f) : target.value.get<float>();
          liveUpdateSucceeded = actions_.updateSceneTarget
            && actions_.updateSceneTarget(*targetIndex, targetValue);
        } else {
          liveUpdateSucceeded = false;
        }
      }
      if (!sceneOwned) {
      if (state.paramTarget == UiParamTarget::Globals && actions_.updateGlobalGains) {
        const auto& global = state.bank.presets[state.activePreset].global;
        actions_.updateGlobalGains(global.inputGainDb, global.outputGainDb);
      }
      if (actions_.updateDaisyParameter && state.paramTarget == UiParamTarget::Block) {
        if (const auto* selected = selectedUiBlock(state)) {
          const auto& block = *selected;
          if ((block.type == "mod" || block.type == "delay" || block.type == "reverb")
              && block.params.contains(control.key) && block.params[control.key].is_number()) {
            liveUpdateSucceeded = actions_.updateDaisyParameter(
              block.id, control.key, block.params[control.key].get<float>());
          }
        }
      }
      if (actions_.updateCompressorParameter && state.paramTarget == UiParamTarget::Block) {
        if (const auto* selected = selectedUiBlock(state)) {
          const auto& block = *selected;
          if (block.type == "dynamics" && block.params.value("mode", "") == "compressor"
              && block.params.contains(control.key) && block.params[control.key].is_number()) {
            liveUpdateSucceeded = actions_.updateCompressorParameter(
              block.id, control.key, block.params[control.key].get<float>());
          }
        }
      }
      if (actions_.updateNoiseGateParameter && state.paramTarget == UiParamTarget::Block) {
        if (const auto* selected = selectedUiBlock(state)) {
          const auto& block = *selected;
          if (block.type == "dynamics" && block.params.value("mode", "") == "noise_gate"
              && block.params.contains(control.key) && block.params[control.key].is_number()) {
            liveUpdateSucceeded = actions_.updateNoiseGateParameter(
              block.id, control.key, block.params[control.key].get<float>());
          }
        }
      }
      if (actions_.updateWahParameter && state.paramTarget == UiParamTarget::Block) {
        if (const auto* selected = selectedUiBlock(state)) {
          const auto& block = *selected;
          if (block.type == "wah" && block.params.contains(control.key)
              && block.params[control.key].is_number()) {
            liveUpdateSucceeded = actions_.updateWahParameter(
              block.id, control.key, block.params[control.key].get<float>());
          }
        }
      }
      if (actions_.updateBlockParameter && state.paramTarget == UiParamTarget::Block) {
        if (const auto* selected = selectedUiBlock(state)) {
          const auto& block = *selected;
          const auto mode = block.params.value("mode", std::string{});
          const bool wdwMixParameter = block.type == "dualRig"
            && block.params.value("routing", std::string{}) == "wdw"
            && (control.key == "dryLevelDb" || control.key == "dryPan"
                || control.key == "wetLevelDb" || control.key == "wetWidth");
          const bool coveredAbove = block.type == "mod" || block.type == "delay"
            || block.type == "reverb" || block.type == "wah" || block.type == "cab"
            || (block.type == "dynamics" && (mode == "compressor" || mode == "noise_gate"));
          if (!coveredAbove && !wdwMixParameter && block.params.contains(control.key)
              && block.params[control.key].is_number()) {
            liveUpdateSucceeded = actions_.updateBlockParameter(
              block.id, control.key, block.params[control.key].get<float>());
          }
        }
      }
      if (state.paramTarget == UiParamTarget::Block) {
        if (const auto* selected = selectedUiBlock(state)) {
          const auto& block = *selected;
          if (block.type == "cab") {
            if (selectedBlockIsLaneChild(state) && actions_.updateBlockParameter
                && block.params.contains(control.key) && block.params[control.key].is_number()) {
              liveUpdateSucceeded = actions_.updateBlockParameter(
                block.id, control.key, block.params[control.key].get<float>());
            } else if (actions_.updateCabParameters) {
              actions_.updateCabParameters(block.params.value("levelDb", 0.0f),
                                           block.params.value("mix", 1.0f));
            }
          }
        }
      }
      }
      if (!liveUpdateSucceeded && sceneOwned) {
        if (const auto targetIndex = selectedParameterSceneTargetIndex(state, control.key)) {
          auto& target = state.bank.presets[state.activePreset].sceneSet
            ->scenes[state.editingScene].targets[*targetIndex];
          if (target.value.is_boolean()) target.value = control.value != 0.0f;
          else if (control.kind == ParameterControlKind::NormalizedChoice)
            target.value = control.choiceValues[static_cast<std::size_t>(std::lround(control.value))];
          else target.value = control.value;
        }
        state.dirty = dirtyBefore;
        invalidate(UiChange::Parameters | UiChange::Header);
        return false;
      }
      if (!liveUpdateSucceeded && hasBlockSnapshot) {
        auto* selected = selectedUiBlock(state);
        if (!selected) {
          state.dirty = dirtyBefore;
          invalidate(UiChange::Parameters | UiChange::Header);
          return false;
        }
        auto paramsAfter = std::move(selected->params);
        selected->params = paramsBefore;
        state.dirty = dirtyBefore;
        auto previewRollback = captureUiPreviewSnapshot(state);
        selected->params = std::move(paramsAfter);
        state.dirty = true;
        if (queuePreview(state, previewRollback, "update " + selectedName)) {
          invalidate(UiChange::Parameters | UiChange::Header);
          return true;
        }
        selected->params = std::move(paramsBefore);
        state.dirty = dirtyBefore;
        invalidate(UiChange::Parameters | UiChange::Header);
        return true;
      }
      const auto updatedControls = ardor::parameterPage(state, parameterPage_);
      const auto updated = std::find_if(
        updatedControls.begin(), updatedControls.end(),
        [this](const auto& item) { return item.key == focusedKey_; });
      if (focusedControl_ && updated != updatedControls.end()) {
        parameter_view::syncSlider(focusedControl_, *updated);
        if (parameterMappingToolbar_) {
          const auto updatedIndex = static_cast<std::size_t>(
            std::distance(updatedControls.begin(), updated));
          const auto* selected = selectedUiBlock(state);
          const bool expressionAssigned = state.paramTarget == UiParamTarget::Block && selected
            && state.bank.presets[state.activePreset].expression.has_value()
            && state.bank.presets[state.activePreset].expression->blockId == selected->id
            && state.bank.presets[state.activePreset].expression->parameter == updated->key;
          const bool expressionSupported = parameterSupportsExpression(state, *updated);
          parameter_view::syncMappingToolbar(
            parameterMappingToolbar_, *updated, updatedIndex, expressionSupported,
            expressionSupported && !selectedBlockIsLaneChild(state), expressionAssigned,
            parameterHasMidiBinding(state, *updated));
        }
        renderedRevisions_.parameters = state.revisions.parameters;
        renderedRevisions_.header = state.revisions.header;
        syncHeaderView(state);
      } else {
        invalidate(UiChange::Parameters);
      }
    }
    return true;
  }
  return false;
}

namespace {

// The chip strip fills the 84 px band between the header and the drawer:
// IN, one chip per top-level block, OUT. Chips are 56 px tall with a 6 px
// family-colour top edge and at least 150 px wide; the strip scrolls
// sideways when a long chain does not fit.
constexpr int kChipStripY = lb::kHeaderHeight;
constexpr int kChipStripHeight = 84;
constexpr int kChipY = 14;
constexpr int kChipGap = 8;
constexpr int kChipHeight = 56;
constexpr int kChipMinWidth = 150;
constexpr int kChipIoWidth = 72;
constexpr int kChipTopEdge = 6;
constexpr int kChipTextInset = 15;
constexpr int kChipSmallTop = 9;
constexpr int kChipNameTop = 23;
constexpr int kSelectedChipBorder = 3;
constexpr std::uintptr_t kChipGlobals = static_cast<std::uintptr_t>(-1);

void onParameterChipClicked(lv_event_t* event)
{
  auto* context = static_cast<UiEventContext*>(lv_event_get_user_data(event));
  lv_obj_t* chip = lv_event_get_current_target_obj(event);
  const auto index = reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(chip));
  // IN and OUT open the global input and output gains.
  if (index == kChipGlobals) context->ui->selectGlobalParams(*context->state);
  else context->ui->selectBlock(*context->state, static_cast<std::size_t>(index));
  context->ui->invalidate(UiChange::Chain | UiChange::Parameters | UiChange::Header);
}

std::string chipKey(const UiBlock& block)
{
  return block.id + "|" + block.type + "|" + block.assetName + (block.enabled ? "|on" : "|off");
}

std::string chipSmallText(const UiBlock& block)
{
  return block.enabled ? uppercase(block.label) : blockTypeCode(block.type) + "  /  OFF";
}

lv_obj_t* createChip(lv_obj_t* strip, int x, int width, std::uintptr_t index,
                     UiEventContext* context)
{
  lv_obj_t* chip = lv_obj_create(strip);
  lv_obj_remove_style_all(chip);
  lv_obj_set_pos(chip, x, kChipY);
  lv_obj_set_size(chip, width, kChipHeight);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(chip, lv_color_hex(panel), 0);
  lb::setBorder(chip, rule, 1);
  lv_obj_add_flag(chip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_user_data(chip, reinterpret_cast<void*>(index));
  lv_obj_add_event_cb(chip, onParameterChipClicked, LV_EVENT_CLICKED, context);
  return chip;
}

// The IN / OUT end chips: ruled on three sides, the legend centred.
lv_obj_t* createIoChip(lv_obj_t* strip, int x, const char* legend, UiEventContext* context)
{
  lv_obj_t* chip = createChip(strip, x, kChipIoWidth, kChipGlobals, context);
  lb::setBorder(chip, rule, 1, static_cast<lv_border_side_t>(
    LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM));
  lb::centeredText(chip, lb::type::chipIo, legend, text, -1, 0, kChipIoWidth, kChipHeight - 1);
  return chip;
}

// Positions a block chip's children for its border: a selected chip has a
// 3 px bone border under the same 6 px family edge.
void styleBlockChip(lv_obj_t* chip, const UiBlock& block, bool selected)
{
  const int side = selected ? kSelectedChipBorder : 1;
  lv_obj_set_style_bg_color(chip, lv_color_hex(selected ? plateHi : panel), 0);
  lv_obj_set_style_border_color(chip, lv_color_hex(selected ? text : rule), 0);
  lv_obj_set_style_border_width(chip, side, 0);
  lv_obj_t* edge = lv_obj_get_child(chip, 0);
  lv_obj_set_pos(edge, -side, -side);
  lv_obj_set_width(edge, lv_obj_get_style_width(chip, LV_PART_MAIN));
  lv_obj_set_style_bg_color(edge, lv_color_hex(categoryColor(block.type)), 0);
  const int shift = selected ? 2 : 0;
  lv_obj_t* small = lv_obj_get_child(chip, 1);
  lv_obj_t* name = lv_obj_get_child(chip, 2);
  lv_obj_set_pos(small, kChipTextInset + shift - side,
                 lb::textTop(lb::type::chipSmall, kChipSmallTop - (selected ? 1 : 0)) - side);
  lv_obj_set_pos(name, kChipTextInset + shift - side,
                 lb::textTop(lb::type::chip, kChipNameTop - (selected ? 1 : 0)) - side);
  lv_obj_set_style_text_color(name, lv_color_hex(block.enabled ? text : disabled), 0);
}

} // namespace

void LvglUi::syncParameterChipStrip(UiState& state)
{
  if (!parameterLayer_) return;
  const auto* selected = selectedUiBlock(state);
  const bool editingEq = state.paramTarget == UiParamTarget::Block
    && selected && selected->type == "eq" && isParametricEqMode(selected->params);
  const bool show = state.mode == UiMode::Edit && state.paramDrawerOpen && !editingEq;
  if (!parameterChipStrip_) {
    if (!show) return;
    parameterChipStrip_ = lv_obj_create(parameterLayer_);
    lv_obj_remove_style_all(parameterChipStrip_);
    lv_obj_set_size(parameterChipStrip_, kDesignWidth, kChipStripHeight);
    lv_obj_set_pos(parameterChipStrip_, 0, kChipStripY);
    lv_obj_set_style_bg_opa(parameterChipStrip_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(parameterChipStrip_, lv_color_hex(bg), 0);
    lv_obj_set_scroll_dir(parameterChipStrip_, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(parameterChipStrip_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(parameterChipStrip_, LV_OBJ_FLAG_SCROLL_ELASTIC);
    contextRegion_ = UiContextRegion::Parameters;
    parameterChipContext_ = remember(state);
    contextRegion_ = UiContextRegion::None;
  }
  if (!show) {
    lv_obj_add_flag(parameterChipStrip_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_remove_flag(parameterChipStrip_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(parameterChipStrip_);

  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  std::vector<std::string> keys;
  keys.reserve(blocks.size());
  for (const auto& block : blocks) keys.push_back(chipKey(block));
  if (keys != renderedChipKeys_) {
    lv_obj_clean(parameterChipStrip_);
    int x = lb::kGutter;
    createIoChip(parameterChipStrip_, x, "IN", parameterChipContext_);
    x += kChipIoWidth + kChipGap;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
      const auto& block = blocks[i];
      const std::string name = uppercase(block.assetName);
      const std::string small = chipSmallText(block);
      const int width = std::max({kChipMinWidth,
        lb::textWidth(lb::type::chip, name) + 2 * kChipTextInset + 2,
        lb::textWidth(lb::type::chipSmall, small) + 2 * kChipTextInset + 2});
      lv_obj_t* chip = createChip(parameterChipStrip_, x, width,
                                  static_cast<std::uintptr_t>(i), parameterChipContext_);
      lb::box(chip, 0, 0, width, kChipTopEdge, categoryColor(block.type));
      lv_obj_add_flag(lv_obj_get_child(chip, 0), LV_OBJ_FLAG_FLOATING);
      lb::textLabel(chip, lb::type::chipSmall, small, disabled, 0, 0);
      lb::textLabel(chip, lb::type::chip, name, text, 0, 0);
      x += width + kChipGap;
    }
    createIoChip(parameterChipStrip_, x, "OUT", parameterChipContext_);
    renderedChipKeys_ = std::move(keys);
  }

  const bool laneChild = selectedBlockIsLaneChild(state);
  const bool globals = state.paramTarget == UiParamTarget::Globals;
  const auto chips = lv_obj_get_child_count(parameterChipStrip_);
  for (uint32_t i = 1; i + 1 < chips && i - 1 < blocks.size(); ++i) {
    lv_obj_t* chip = lv_obj_get_child(parameterChipStrip_, static_cast<int32_t>(i));
    styleBlockChip(chip, blocks[i - 1], !globals && !laneChild && i - 1 == state.selectedBlock);
  }
  // IN and OUT light together while the global gains are open.
  for (uint32_t i : {0u, chips - 1}) {
    lv_obj_t* chip = lv_obj_get_child(parameterChipStrip_, static_cast<int32_t>(i));
    lv_obj_set_style_bg_color(chip, lv_color_hex(globals ? plateHi : panel), 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(globals ? text : rule), 0);
  }
  syncHeaderView(state);
}

void LvglUi::rebuildParameterView(UiState& state)
{
  if (!parameterLayer_) return;
  focusedControl_ = nullptr;
  focusedEqGraph_ = nullptr;
  compressorGainMeterFill_ = nullptr;
  compressorGainMeterLabel_ = nullptr;
  if (state.mode != UiMode::Edit || !state.paramDrawerOpen) {
    resetParameterPage();
    return;
  }

  const auto* selected = selectedUiBlock(state);
  const bool editingEq = state.paramTarget == UiParamTarget::Block
    && selected && selected->type == "eq" && isParametricEqMode(selected->params);
  if (editingEq) {
    selectEqStage(selectedEqStage_);
  }
  const auto& sceneSet = state.bank.presets[state.activePreset].sceneSet;
  const std::string sceneSignature = sceneSet
    ? ":scenes:" + std::to_string(sceneSet->scenes[state.editingScene].targets.size()) : ":shared";
  const std::string signature = (editingEq
    ? "eq:parametric"
    : (state.paramTarget == UiParamTarget::Globals
        ? "globals:" + std::to_string(parameterPage_)
        : (selected
            ? "block:" + selected->type + ":"
                + selected->params.value("mode", std::string{}) + ":"
                + std::to_string(parameterPage_)
            : "none"))) + sceneSignature;

  for (auto& [key, view] : parameterViews_) {
    (void) key;
    if (view.layer) lv_obj_add_flag(view.layer, LV_OBJ_FLAG_HIDDEN);
  }

  const auto activate = [this](ParameterViewRefs& view) {
    activeParameterLayer_ = view.layer;
    parameterControls_ = view.controls;
    parameterTitleLabel_ = view.titleLabel;
    parameterBypassControl_ = view.bypassControl;
    parameterMappingToolbar_ = view.mappingToolbar;
    eqGraph_ = view.eqGraph;
    eqBandButtons_ = view.eqBandButtons;
    eqSliders_ = view.eqSliders;
    eqEnabledButton_ = view.eqEnabledButton;
    eqEnabledContext_ = view.eqEnabledContext;
    eqResetContext_ = view.eqResetContext;
    eqSliderContexts_ = view.eqSliderContexts;
    compressorGainMeterFill_ = view.gainMeterFill;
    compressorGainMeterLabel_ = view.gainMeterLabel;
    lv_obj_remove_flag(view.layer, LV_OBJ_FLAG_HIDDEN);
  };
  if (auto existing = parameterViews_.find(signature); existing != parameterViews_.end()) {
    activate(existing->second);
    renderedParameterSignature_ = signature;
    syncParameterView(state);
    return;
  }

  lv_obj_t* viewLayer = lv_obj_create(parameterLayer_);
  lv_obj_remove_style_all(viewLayer);
  lv_obj_set_size(viewLayer, kDesignWidth, kDesignHeight);
  lv_obj_set_pos(viewLayer, 0, 0);
  lv_obj_remove_flag(viewLayer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(viewLayer, LV_OBJ_FLAG_CLICKABLE);
  parameterControls_.clear();
  parameterTitleLabel_ = nullptr;
  parameterBypassControl_ = nullptr;
  parameterMappingToolbar_ = nullptr;
  eqGraph_ = nullptr;
  eqBandButtons_.fill(nullptr);
  eqSliders_.fill(nullptr);
  eqEnabledButton_ = nullptr;
  eqEnabledContext_ = nullptr;
  eqResetContext_ = nullptr;
  eqSliderContexts_.fill(nullptr);
  contextRegion_ = UiContextRegion::Parameters;
  if (editingEq) {
    parameter_view::buildEqPanel(
      viewLayer, state, remember(state), &eqGraph_, &eqBandButtons_,
      &eqEnabledButton_, &eqEnabledContext_, &eqResetContext_,
      &eqSliders_, &eqSliderContexts_, &parameterBypassControl_);
  } else {
    parameter_view::buildPanel(viewLayer, state, remember(state),
                               &parameterControls_, &parameterBypassControl_,
                               &parameterTitleLabel_, &parameterMappingToolbar_,
                               &compressorGainMeterFill_, &compressorGainMeterLabel_);
  }
  contextRegion_ = UiContextRegion::None;

  ParameterViewRefs refs;
  refs.layer = viewLayer;
  refs.controls = parameterControls_;
  refs.titleLabel = parameterTitleLabel_;
  refs.bypassControl = parameterBypassControl_;
  refs.mappingToolbar = parameterMappingToolbar_;
  refs.eqGraph = eqGraph_;
  refs.eqBandButtons = eqBandButtons_;
  refs.eqSliders = eqSliders_;
  refs.eqEnabledButton = eqEnabledButton_;
  refs.eqEnabledContext = eqEnabledContext_;
  refs.eqResetContext = eqResetContext_;
  refs.eqSliderContexts = eqSliderContexts_;
  refs.gainMeterFill = compressorGainMeterFill_;
  refs.gainMeterLabel = compressorGainMeterLabel_;
  auto [inserted, unused] = parameterViews_.emplace(signature, std::move(refs));
  (void) unused;
  activate(inserted->second);
  renderedParameterSignature_ = signature;
}

void LvglUi::syncParameterView(UiState& state)
{
  if (state.mode != UiMode::Edit || !state.paramDrawerOpen) {
    resetParameterPage();
    return;
  }

  const auto* selected = selectedUiBlock(state);
  const bool editingEq = state.paramTarget == UiParamTarget::Block
    && selected && selected->type == "eq" && isParametricEqMode(selected->params);
  const auto& sceneSet = state.bank.presets[state.activePreset].sceneSet;
  const std::string sceneSignature = sceneSet
    ? ":scenes:" + std::to_string(sceneSet->scenes[state.editingScene].targets.size()) : ":shared";
  const std::string signature = (editingEq
    ? "eq:parametric"
    : (state.paramTarget == UiParamTarget::Globals
        ? "globals:" + std::to_string(parameterPage_)
        : (selected
            ? "block:" + selected->type + ":"
                + selected->params.value("mode", std::string{}) + ":"
                + std::to_string(parameterPage_)
            : "none"))) + sceneSignature;
  if (signature != renderedParameterSignature_) {
    rebuildParameterView(state);
    return;
  }

  if (parameterBypassControl_ && state.paramTarget == UiParamTarget::Block && selected) {
    const auto sceneEnabled = selectedParameterSceneValue(state, "blockEnabled");
    const bool displayedEnabled = sceneEnabled && sceneEnabled->is_boolean()
      ? sceneEnabled->get<bool>() : selected->enabled;
    parameter_view::syncBypass(parameterBypassControl_, !displayedEnabled);
  }

  // Parameter edits publish Parameters, not Chain, so the chain would keep
  // showing stale values. Refresh only the selected card's summary rows.
  if (state.paramTarget == UiParamTarget::Block && selected && !selectedBlockIsLaneChild(state)
      && state.selectedBlock < chainSummaries_.size()) {
    renderChainSummary(chainSummaries_[state.selectedBlock], state, *selected);
  }

  if (!editingEq) {
    if (parameterTitleLabel_) {
      if (state.paramTarget == UiParamTarget::Globals) {
        lv_label_set_text(parameterTitleLabel_, "GLOBAL");
      } else if (selected) {
        lv_label_set_text(parameterTitleLabel_, uppercase(selected->assetName).c_str());
      }
    }
    const auto controls = ardor::parameterPage(state, parameterPage_);
    if (controls.size() != parameterControls_.size()) {
      rebuildParameterView(state);
      return;
    }
    if (!controls.empty()
        && std::none_of(controls.begin(), controls.end(), [this](const auto& control) {
             return isParameterFocused(control.key);
           })) {
      focusedKey_ = controls.front().key;
    }
    for (std::size_t i = 0; i < controls.size(); ++i) {
      parameter_view::syncSlider(parameterControls_[i], controls[i],
                                 isParameterFocused(controls[i].key));
    }
    if (parameterMappingToolbar_ && !controls.empty()) {
      const auto focused = std::find_if(controls.begin(), controls.end(), [this](const auto& control) {
        return isParameterFocused(control.key);
      });
      const auto selectedIndex = focused == controls.end()
        ? std::size_t{0}
        : static_cast<std::size_t>(std::distance(controls.begin(), focused));
      const auto& control = controls[selectedIndex];
      const bool expressionAssigned = state.paramTarget == UiParamTarget::Block && selected
        && state.bank.presets[state.activePreset].expression.has_value()
        && state.bank.presets[state.activePreset].expression->blockId == selected->id
        && state.bank.presets[state.activePreset].expression->parameter == control.key;
      const bool expressionSupported = parameterSupportsExpression(state, control);
      parameter_view::syncMappingToolbar(
        parameterMappingToolbar_, control, selectedIndex, expressionSupported,
        expressionSupported && !selectedBlockIsLaneChild(state), expressionAssigned,
        parameterHasMidiBinding(state, control));
    }
    return;
  }

  const auto params = selectedParametricEqParams(state);
  parameter_view::syncEqGraph(eqGraph_, params);
  parameter_view::syncEqBandSelection(eqGraph_, eqBandButtons_, params, selectedEqStage_);

  const bool passStage = isPassEqStage(selectedEqStage_);
  const bool enabled = passStage
    ? passFilterForStage(params, selectedEqStage_).enabled
    : params.bands[selectedEqStage_ - kEqFirstBandStage].enabled;
  if (eqEnabledButton_) {
    lb::setButtonText(eqEnabledButton_, enabled ? (passStage ? "Filter On" : "Band On")
                                                : (passStage ? "Filter Off" : "Band Off"));
    lb::styleButton(eqEnabledButton_, enabled ? lb::ButtonKind::Normal : lb::ButtonKind::Off);
  }
  if (eqEnabledContext_) eqEnabledContext_->index = selectedEqStage_;
  if (eqResetContext_) eqResetContext_->index = selectedEqStage_;
  const std::array<const char*, 3> sliderKeys = passStage
    ? std::array<const char*, 3>{"frequency", "q", "slope"}
    : std::array<const char*, 3>{"frequency", "q", "gain"};
  for (std::size_t i = 0; i < eqSliderContexts_.size(); ++i) {
    if (eqSliderContexts_[i]) {
      eqSliderContexts_[i]->index = selectedEqStage_;
      eqSliderContexts_[i]->filter = sliderKeys[i];
      eqSliderContexts_[i]->controlledObject = eqGraph_;
    }
  }
  syncEqSliders(state);
}

void LvglUi::syncEqSliders(const UiState& state)
{
  const auto params = selectedParametricEqParams(state);
  const bool passStage = isPassEqStage(selectedEqStage_);
  constexpr std::array<EqBandField, 3> bandFields = {
    EqBandField::Frequency, EqBandField::Q, EqBandField::Gain,
  };
  constexpr std::array<EqBandField, 3> passFields = {
    EqBandField::Frequency, EqBandField::Q, EqBandField::Slope,
  };
  const auto& fields = passStage ? passFields : bandFields;
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (eqSliders_[i]) {
      lv_obj_remove_flag(eqSliders_[i], LV_OBJ_FLAG_HIDDEN);
      const auto control = passStage
        ? parameter_view::eqControl(fields[i], passFilterForStage(params, selectedEqStage_))
        : parameter_view::eqControl(fields[i],
            params.bands[selectedEqStage_ - kEqFirstBandStage]);
      parameter_view::syncSlider(
        eqSliders_[i], control,
        isEqBandFieldFocused(fields[i]));
    }
  }
}

void LvglUi::syncCompressorGainMeter(UiState& state)
{
  if (!compressorGainMeterFill_ || !compressorGainMeterLabel_) return;
  if (state.mode != UiMode::Edit || !state.paramDrawerOpen
      || state.paramTarget != UiParamTarget::Block) {
    return;
  }
  parameter_view::syncCompressorGainMeter(
    compressorGainMeterFill_, compressorGainMeterLabel_, state.compressorGainReductionDb);
}

} // namespace ardor
