#pragma once

#include "ui/EqEditorModel.h"
#include "ui/ParameterControls.h"

#include <array>
#include <vector>

#include <lvgl.h>

namespace ardor {

struct UiEventContext;
struct UiState;

// Internal boundary between retained parameter-view state and the LVGL
// renderer. Keeping renderer-only visual structs private avoids exposing them
// through LvglUi while allowing the controller to live in its own translation
// unit.
namespace parameter_view {

void syncSlider(lv_obj_t* slider, const ParameterControl& control, bool focused = true);
void syncMappingToolbar(lv_obj_t* toolbar, const ParameterControl& control,
                        std::size_t controlIndex, bool expressionSupported,
                        bool midiSupported, bool expressionAssigned, bool midiAssigned);
void syncBypass(lv_obj_t* control, bool bypassed);
// `reductionDb` is expected <= 0 (0 = no reduction). Cheap enough to call
// unconditionally on every refresh() tick -- see LvglUi::syncCompressorGainMeter.
void syncCompressorGainMeter(lv_obj_t* fill, lv_obj_t* label, float reductionDb);
ParameterControl eqControl(EqBandField field, const EqBandParams& band);
ParameterControl eqControl(EqBandField field, const EqPassFilterParams& filter);
void syncEqGraph(lv_obj_t* graph, const ParametricEqParams& params, bool throttle = false);
void syncEqBandSelection(
  lv_obj_t* graph,
  const std::array<lv_obj_t*, kEqStageCount>& bandButtons,
  const ParametricEqParams& params, std::size_t selectedStage);

void buildEqPanel(
  lv_obj_t* root, UiState& state, UiEventContext* context,
  lv_obj_t** graphOut,
  std::array<lv_obj_t*, kEqStageCount>* bandButtonsOut,
  lv_obj_t** enabledOut, UiEventContext** enabledContextOut,
  UiEventContext** resetContextOut,
  std::array<lv_obj_t*, 3>* slidersOut,
  std::array<UiEventContext*, 3>* sliderContextsOut,
  lv_obj_t** bypassOut);

void buildPanel(lv_obj_t* root, UiState& state, UiEventContext* context,
                std::vector<lv_obj_t*>* controlsOut,
                lv_obj_t** bypassOut, lv_obj_t** titleOut,
                lv_obj_t** mappingToolbarOut,
                lv_obj_t** gainMeterFillOut, lv_obj_t** gainMeterLabelOut);

} // namespace parameter_view
} // namespace ardor
