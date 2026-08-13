#include "ui/LvglUi.h"

#include "ui/LvglUiParameterView.h"
#include "ui/LvglUiStyle.h"

#include <algorithm>
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
      if (state.paramTarget == UiParamTarget::Block) {
        if (const auto* selected = selectedUiBlock(state)) {
          const auto& block = *selected;
          if (block.type == "cab") {
            if (selectedBlockIsLaneChild(state)) {
              liveUpdateSucceeded = false;
            } else if (actions_.updateCabParameters) {
              actions_.updateCabParameters(block.params.value("levelDb", 0.0f),
                                           block.params.value("mix", 1.0f));
            }
          }
        }
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
  const std::string signature = editingEq
    ? "eq:parametric"
    : (state.paramTarget == UiParamTarget::Globals
        ? "globals:" + std::to_string(parameterPage_)
        : (selected
            ? "block:" + selected->type + ":"
                + selected->params.value("mode", std::string{}) + ":"
                + std::to_string(parameterPage_)
            : "none"));

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
  const std::string signature = editingEq
    ? "eq:parametric"
    : (state.paramTarget == UiParamTarget::Globals
        ? "globals:" + std::to_string(parameterPage_)
        : (selected
            ? "block:" + selected->type + ":"
                + selected->params.value("mode", std::string{}) + ":"
                + std::to_string(parameterPage_)
            : "none"));
  if (signature != renderedParameterSignature_) {
    rebuildParameterView(state);
    return;
  }

  if (parameterBypassControl_ && state.paramTarget == UiParamTarget::Block && selected) {
    parameter_view::syncBypass(parameterBypassControl_, !selected->enabled);
  }

  if (!editingEq) {
    if (parameterTitleLabel_) {
      if (state.paramTarget == UiParamTarget::Globals) {
        lv_label_set_text(parameterTitleLabel_, "Global");
      } else if (selected) {
        const auto title = selected->label + "  /  " + selected->assetName;
        lv_label_set_text(parameterTitleLabel_, title.c_str());
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
    lv_label_set_text(lv_obj_get_child(eqEnabledButton_, 0),
                      enabled ? (passStage ? "Filter On" : "Band On")
                              : (passStage ? "Filter Off" : "Band Off"));
    styleSurface(eqEnabledButton_, enabled ? panel : panelAlt);
    lv_obj_set_style_text_color(lv_obj_get_child(eqEnabledButton_, 0),
                                lv_color_hex(enabled ? lamp : danger), 0);
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
