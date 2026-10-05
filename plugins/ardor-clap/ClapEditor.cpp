#include "ClapEditor.h"
#include "audio/EngineLoader.h"
#include "ui/LvglUiStyle.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace ardor::clap_editor {
namespace {
namespace style = lvgl_ui;
constexpr int toolbarHeight = 44;
constexpr unsigned inputTrimId = 0, outputTrimId = 1, bypassId = 2, channelId = 3, sceneId = 5;
struct Context {
  lv_display_t* display = lv_display_get_default();
  lv_group_t* group = lv_group_get_default();
  Context(lv_display_t* next, lv_group_t* nextGroup) {
    lv_display_set_default(next);
    lv_group_set_default(nextGroup);
  }
  ~Context() { lv_display_set_default(display); lv_group_set_default(group); }
};
uint32_t clockTick() {
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::string serialize(const UiState& state) { return toJson(activePresetToPreset(state)).dump(); }
bool updateParameter(PedalEngine* engine, const std::string& id, const std::string& key, float value) {
  return engine && (engine->setDaisyParameter(id, key, value)
    || engine->setCompressorParameter(id, key, value) || engine->setNoiseGateParameter(id, key, value)
    || engine->setWahParameter(id, key, value) || engine->setConsoleEqParameter(id, key, value)
    || engine->setTransientShaperParameter(id, key, value) || engine->setDistortionParameter(id, key, value)
    || engine->setStereoWidenerParameter(id, key, value) || engine->setIrReverbParameter(id, key, value)
    || engine->setCabParameter(id, key, value));
}
}

Canvas::Canvas(Callbacks callbacks, const std::array<Preset, 4>& presets, int slot, const Preset& current)
  : callbacks_(std::move(callbacks)), state_(makeDemoUiState()), ui_(actions())
{
  if (!lv_is_initialized()) { lv_init(); lv_tick_set_cb(clockTick); }
  loadAssetsFromDataRoot(state_, callbacks_.root);
  state_.bank.name = "Ardor library";
  state_.activeBank = 0;
  state_.masterVolume = 100;
  for (std::size_t i = 0; i < presets.size(); ++i) {
    state_.activePreset = i;
    replaceActivePreset(state_, presets[i]);
  }
  state_.activePreset = static_cast<std::size_t>(slot);
  replaceActivePreset(state_, current);
  state_.dirty = false;
  serialized_ = serialize(state_);
  compiledScenes_ = toJson(current).value("sceneSet", nlohmann::json{});
  group_ = lv_group_create();
  display_ = lv_display_create(1100, 663);
  if (!display_ || !group_) throw std::runtime_error("Cannot create Ardor editor display");
  lv_display_set_user_data(display_, this);
  lv_display_set_color_format(display_, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_flush_cb(display_, draw);
  {
    Context context(display_, group_);
    pointer_ = lv_indev_create();
    keyboard_ = lv_indev_create();
    for (auto* input : {pointer_, keyboard_}) {
      lv_indev_set_display(input, display_);
      lv_indev_set_user_data(input, this);
      // Event mode prevents another instance's timer from dispatching our input
      // while LVGL's global default display belongs to that other instance.
      lv_indev_set_mode(input, LV_INDEV_MODE_EVENT);
    }
    lv_indev_set_type(pointer_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer_, readPointer);
    lv_indev_set_type(keyboard_, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(keyboard_, readKey);
    lv_indev_set_group(keyboard_, group_);
  }
  resize(1100, 663);
}
Canvas::~Canvas() {
  // Each canvas owns only its objects. LVGL remains initialized for other
  // instances and for editor reopen; no process-wide lv_deinit/SDL shutdown.
  lv_indev_delete(pointer_);
  lv_indev_delete(keyboard_);
  lv_display_delete(display_);
  lv_group_delete(group_);
}
void Canvas::synchronize(const Preset& preset, int slot) {
  synchronizePresetSelection(state_, static_cast<std::size_t>(slot));
  replaceActivePreset(state_, preset);
  state_.pendingPreview.reset();
  state_.navigationPrompt.reset();
  state_.dirty = false;
  stagedPreview_ = false;
  serialized_ = serialize(state_);
  compiledScenes_ = toJson(preset).value("sceneSet", nlohmann::json{});
  snapshotSerial_ = 0;
  markUiChanged(state_, UiChange::All);
}
UiActions Canvas::actions() {
  UiActions a;
  a.showTuner = a.showLooper = a.showDeviceStatus = a.allowBankNavigation = false;
  a.selectPreset = [this](std::size_t slot) { choosePreset(slot); };
  a.savePreset = [this] { savePreset(); };
  a.resolveNavigation = [this](UiNavigationDecision decision) {
    if (decision == UiNavigationDecision::Save && !savePreset()) return;
    if (const auto target = confirmNavigation(state_, decision)) {
      if (!callbacks_.selectPreset(static_cast<int>(target->preset)))
        setUiStatus(state_, "Preset unavailable; check its assets", true);
    }
  };
  const auto parameter = [this](const std::string& id, const std::string& key, float value) {
    return !callbacks_.waiting() && updateParameter(callbacks_.engine(), id, key, value);
  };
  a.updateDaisyParameter = a.updateCompressorParameter = a.updateNoiseGateParameter = parameter;
  a.updateWahParameter = a.updateBlockParameter = parameter;
  a.updateBlockEnabled = [this](const std::string& id, bool enabled) {
    return !callbacks_.waiting() && callbacks_.engine() && callbacks_.engine()->setBlockEnabled(id, enabled);
  };
  a.updateEqBand = [this](const auto& id, std::size_t band, const auto& value) {
    return !callbacks_.waiting() && callbacks_.engine() && callbacks_.engine()->setParametricEqBand(id, band, value);
  };
  a.updateEqPassFilter = [this](const auto& id, auto kind, const auto& value) {
    return !callbacks_.waiting() && callbacks_.engine() && callbacks_.engine()->setParametricEqPassFilter(id, kind, value);
  };
  a.updateGlobalGains = [this](float input, float output) {
    if (auto* engine = callbacks_.engine()) { engine->setInputGain(dbToGain(input)); engine->setOutputGain(dbToGain(output)); }
  };
  a.updateCabParameters = [this](float level, float mix) {
    if (auto* engine = callbacks_.engine()) { engine->setCabLevel(dbToGain(level)); engine->setCabMix(mix); }
  };
  a.selectScene = [this](std::size_t scene) { if (!callbacks_.waiting()) callbacks_.selectScene(static_cast<int>(scene)); };
  a.setSceneLayer = [this](bool active) { if (active) enterScenesMode(state_); else enterPresetMode(state_); };
  a.updateSceneTarget = [this](std::size_t target, float value) {
    return !callbacks_.waiting() && callbacks_.engine() && callbacks_.engine()->tryOverrideSceneTarget(target, value);
  };
  a.requestSceneCapture = [this] {
    if (auto* engine = callbacks_.engine(); engine && !callbacks_.waiting()) engine->requestSceneValueSnapshot();
    else failCurrentSoundCapture(state_, "Wait for the DAW to apply the preset before capturing a scene");
  };
  a.openHostSettings = [this] { setUiStatus(state_, "Choose your audio interface and monitoring in the DAW"); };
  return a;
}
void Canvas::choosePreset(std::size_t slot) {
  if (slot >= 4 || callbacks_.waiting()) return;
  if (state_.dirty && !requestPresetNavigation(state_, {0, slot})) return;
  if (!callbacks_.selectPreset(static_cast<int>(slot))) setUiStatus(state_, "Preset unavailable; check its assets", true);
}
bool Canvas::savePreset() {
  if (!flushEdits() || callbacks_.waiting()) {
    setUiStatus(state_, "Wait for the DAW to apply the chain before saving", true); return false;
  }
  std::string error;
  const bool saved = saveActivePresetToStore(state_, PresetStore(callbacks_.root), 0, error);
  setUiStatus(state_, saved ? "Preset saved to bank 1" : "Could not save preset: " + error, !saved);
  return saved;
}
bool Canvas::flushEdits() {
  auto preset = activePresetToPreset(state_);
  const auto json = toJson(preset).dump();
  if (json == serialized_) return true;
  // Structural edits and scene definitions require a freshly prepared graph.
  // Parameter setters above publish directly to the existing realtime controls.
  const bool sceneChanged = toJson(preset).value("sceneSet", nlohmann::json{}) != compiledScenes_;
  if (pendingStructuralPreview(state_) || sceneChanged) {
    if (!callbacks_.stage(preset)) {
      if (pendingStructuralPreview(state_)) failStructuralPreview(state_, "Cannot apply chain; check its assets and routing");
      setUiStatus(state_, "Cannot apply chain; check its assets and routing", true);
      return false;
    }
    compiledScenes_ = toJson(preset).value("sceneSet", nlohmann::json{});
    stagedPreview_ = bool(pendingStructuralPreview(state_));
    setUiStatus(state_, callbacks_.waiting() ? "Waiting for the DAW to apply the chain" : "Chain ready");
  }
  callbacks_.edited(std::move(preset));
  serialized_ = json;
  return true;
}
void Canvas::tick() {
  Context context(display_, group_);
  style::setPalette(state_.settings.paletteId);
  if (!ui_.interacting()) flushEdits();
  if (stagedPreview_ && !callbacks_.waiting()) { completeStructuralPreview(state_); stagedPreview_ = false; }
  if (auto* engine = callbacks_.engine(); engine && !callbacks_.waiting() && engine->scenesPrepared()) {
    const auto scene = engine->sceneTransitionTelemetry();
    updateSceneTelemetry(state_, {scene.currentSceneIndex, scene.destinationSceneIndex,
      scene.totalFrames ? float(scene.elapsedFrames) / scene.totalFrames : 1.f, false,
      scene.transitioning, state_.scenes.altered, false, {}});
    if (state_.sceneCapturePending) {
      std::vector<float> values;
      if (engine->tryReadSceneValueSnapshot(snapshotSerial_, values)) completeCurrentSoundCapture(state_, values);
    }
  }
  const bool bypassed = callbacks_.control(bypassId) >= .5;
  if (state_.effectsBypassed != bypassed) {
    state_.effectsBypassed = bypassed;
    markUiChanged(state_, UiChange::Presets | UiChange::Header);
  }
  if (state_.paramDrawerOpen) if (const auto* block = selectedUiBlock(state_))
    updateCompressorGainReduction(state_, callbacks_.engine() ? callbacks_.engine()->compressorGainReductionDb(block->id) : 0);
  refresh();
  lv_timer_handler();
  lv_refr_now(display_);
}
void Canvas::refresh() {
  ui_.refresh(content_, state_);
  lv_dropdown_set_selected(channel_, static_cast<uint32_t>(callbacks_.control(channelId)));
  lv_spinbox_set_value(inputTrim_, static_cast<int32_t>(std::round(callbacks_.control(inputTrimId) * 10)));
  lv_spinbox_set_value(outputTrim_, static_cast<int32_t>(std::round(callbacks_.control(outputTrimId) * 10)));
  if (callbacks_.control(bypassId) >= .5) lv_obj_add_state(bypass_, LV_STATE_CHECKED);
  else lv_obj_remove_state(bypass_, LV_STATE_CHECKED);
  if (callbacks_.waiting()) lv_obj_remove_flag(waiting_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(waiting_, LV_OBJ_FLAG_HIDDEN);
}
void Canvas::resize(int width, int height, double scale) {
  if (width < 1 || height < toolbarHeight + 1 || !std::isfinite(scale) || scale < 1 || scale > 3)
    throw std::runtime_error("Invalid Ardor editor size");
  Context context(display_, group_);
  if (pointerPressed_) { pointerPressed_ = false; lv_indev_read(pointer_); }
  if (keyPressed_) { keyPressed_ = false; lv_indev_read(keyboard_); }
  backingScale_ = scale;
  pixelWidth_ = static_cast<int>(std::round(width * scale));
  pixelHeight_ = static_cast<int>(std::round(height * scale));
  drawBuffer_.assign(static_cast<std::size_t>(pixelWidth_) * pixelHeight_ * 4, 0);
  pixels_.assign(drawBuffer_.size(), 0);
  lv_display_set_resolution(display_, pixelWidth_, pixelHeight_);
  lv_display_set_buffers(display_, drawBuffer_.data(), nullptr, static_cast<uint32_t>(drawBuffer_.size()), LV_DISPLAY_RENDER_MODE_FULL);
  auto* screen = lv_display_get_screen_active(display_);
  lv_obj_clean(screen);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(screen, lv_color_hex(style::bg), 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  buildToolbar();
  content_ = lv_obj_create(screen);
  lv_obj_remove_style_all(content_);
  lv_obj_set_pos(content_, 0, static_cast<int32_t>(toolbarHeight * scale));
  lv_obj_set_size(content_, pixelWidth_, pixelHeight_ - static_cast<int32_t>(toolbarHeight * scale));
  ui_.build(content_, state_);
  waiting_ = lv_obj_create(screen);
  lv_obj_remove_style_all(waiting_);
  lv_obj_set_size(waiting_, pixelWidth_, pixelHeight_);
  lv_obj_set_style_bg_color(waiting_, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(waiting_, LV_OPA_60, 0);
  lv_obj_add_flag(waiting_, LV_OBJ_FLAG_CLICKABLE);
  auto* label = lv_label_create(waiting_);
  style::setText(label);
  lv_label_set_text(label, "Waiting for the DAW to apply the chain");
  lv_obj_center(label);
  refresh();
  lv_refr_now(display_);
}
void Canvas::buildToolbar() {
  auto* screen = lv_display_get_screen_active(display_);
  toolbar_ = lv_obj_create(screen);
  lv_obj_remove_style_all(toolbar_);
  lv_obj_remove_flag(toolbar_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(toolbar_, static_cast<int32_t>(pixelWidth_ / backingScale_), toolbarHeight);
  lv_obj_set_style_bg_opa(toolbar_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(toolbar_, lv_color_hex(style::bg), 0);
  lv_obj_set_style_transform_pivot_x(toolbar_, 0, 0);
  lv_obj_set_style_transform_pivot_y(toolbar_, 0, 0);
  lv_obj_set_style_transform_scale(toolbar_, static_cast<int32_t>(256 * backingScale_), 0);
  const auto label = [this](const char* text, int x) {
    auto* item = lv_label_create(toolbar_); style::setText(item); lv_label_set_text(item, text); lv_obj_set_pos(item, x, 12);
  };
  label("Guitar", 12);
  channel_ = lv_dropdown_create(toolbar_);
  lv_dropdown_set_options(channel_, "Left\nRight\nAverage");
  lv_obj_set_pos(channel_, 68, 4); lv_obj_set_size(channel_, 132, 36);
  style::styleSurface(channel_); style::setText(channel_);
  lv_obj_set_style_text_font(channel_, LV_FONT_DEFAULT, LV_PART_INDICATOR);
  style::styleSurface(lv_dropdown_get_list(channel_)); style::setText(lv_dropdown_get_list(channel_));
  lv_obj_add_event_cb(channel_, toolbarChanged, LV_EVENT_VALUE_CHANGED, this);
  const auto spinbox = [this](int x, int low, int high) {
    auto* item = lv_spinbox_create(toolbar_);
    lv_obj_set_pos(item, x, 4); lv_obj_set_size(item, 96, 36);
    lv_spinbox_set_range(item, low, high); lv_spinbox_set_digit_format(item, 3, 2);
    lv_spinbox_set_step(item, 1); style::styleSurface(item); style::setText(item);
    lv_obj_add_event_cb(item, toolbarChanged, LV_EVENT_VALUE_CHANGED, this);
    return item;
  };
  label("Input dB", 220); inputTrim_ = spinbox(296, -240, 240);
  label("Output dB", 412); outputTrim_ = spinbox(500, -240, 120);
  bypass_ = style::button(toolbar_, "Bypass");
  lv_obj_set_pos(bypass_, 616, 4); lv_obj_set_size(bypass_, 120, 36);
  lv_obj_add_flag(bypass_, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_add_event_cb(bypass_, toolbarChanged, LV_EVENT_VALUE_CHANGED, this);
  label("48 kHz", 760);
}
void Canvas::toolbarChanged(lv_event_t* event) {
  auto& canvas = *static_cast<Canvas*>(lv_event_get_user_data(event));
  auto* target = lv_event_get_target_obj(event);
  const unsigned id = target == canvas.channel_ ? channelId : target == canvas.inputTrim_ ? inputTrimId
    : target == canvas.outputTrim_ ? outputTrimId : bypassId;
  const double value = id == channelId ? lv_dropdown_get_selected(target)
    : id == bypassId ? (lv_obj_has_state(target, LV_STATE_CHECKED) ? 1. : 0.) : lv_spinbox_get_value(target) / 10.;
  if (value != canvas.callbacks_.control(id)) canvas.callbacks_.setControl(id, value);
}
void Canvas::draw(lv_display_t* display, const lv_area_t*, uint8_t* buffer) {
  auto& canvas = *static_cast<Canvas*>(lv_display_get_user_data(display));
  std::memcpy(canvas.pixels_.data(), buffer, canvas.pixels_.size());
  ++canvas.frameRevision_;
  lv_display_flush_ready(display);
}
void Canvas::readPointer(lv_indev_t* input, lv_indev_data_t* data) {
  auto& canvas = *static_cast<Canvas*>(lv_indev_get_user_data(input));
  data->point = canvas.point_;
  data->state = canvas.pointerPressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
void Canvas::readKey(lv_indev_t* input, lv_indev_data_t* data) {
  auto& canvas = *static_cast<Canvas*>(lv_indev_get_user_data(input));
  data->key = canvas.key_;
  data->state = canvas.keyPressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
void Canvas::pointer(double x, double y, bool pressed) {
  Context context(display_, group_); style::setPalette(state_.settings.paletteId);
  point_ = {static_cast<int32_t>(std::round(x * backingScale_)), static_cast<int32_t>(std::round(y * backingScale_))};
  pointerPressed_ = pressed;
  lv_indev_read(pointer_);
}
void Canvas::key(uint32_t key, bool pressed) {
  Context context(display_, group_); style::setPalette(state_.settings.paletteId);
  key_ = key; keyPressed_ = pressed;
  lv_indev_read(keyboard_);
}
} // namespace ardor::clap_editor
