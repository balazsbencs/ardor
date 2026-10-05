#include <clap/clap.h>
#include "audio/EngineLoader.h"
#include "desktop/DesktopLibrary.h"
#include "preset/PresetStore.h"
#include "RateAdapter.h"
#include "dsp/DenormalGuard.h"
#ifdef ARDOR_CLAP_HAS_EDITOR
#include "ClapEditor.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
#ifdef ARDOR_CLAP_HAS_EDITOR
#ifdef _WIN32
constexpr const char* windowApi = CLAP_WINDOW_API_WIN32;
#else
constexpr const char* windowApi = CLAP_WINDOW_API_COCOA;
#endif
#endif
constexpr std::size_t quantum = 64;
constexpr std::size_t stateLimit = 512 * 1024;
constexpr const char* pluginId = "org.ardor.guitar";
const char* const features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_STEREO, nullptr};
const clap_plugin_descriptor_t descriptor = {CLAP_VERSION, pluginId, "Ardor", "Ardor",
  "https://github.com/balazsbencs/ardor", "", "https://github.com/balazsbencs/ardor/issues",
  "0.1.0 Beta 4", "Shared Ardor guitar effects engine with host sample-rate conversion", features};

enum Param : clap_id { InputTrim, OutputTrim, Bypass, InputChannel, LibrarySlot, Scene, Count };
struct ParamSpec { const char* name; double low, high, initial; bool stepped; };
constexpr std::array<ParamSpec, Count> specs{{
  {"Input trim", -24, 24, 0, false}, {"Output trim", -24, 12, 0, false},
  {"Bypass", 0, 1, 0, true}, {"Guitar input (Left / Right / Average)", 0, 2, 0, true},
  {"Library preset (bank 1)", 0, 3, 0, true}, {"Scene", 0, 3, 0, true}
}};
static_assert(std::atomic<double>::is_always_lock_free);

class Instance {
public:
  explicit Instance(const clap_host_t* host) : host_(host)
  {
    for (std::size_t i = 0; i < Count; ++i) values_[i].store(specs[i].initial);
    preset_.name = "Clean";
    api = {&descriptor, this, init, destroy, activate, deactivate, start, stop, reset,
           process, extension, onMain};
  }
  clap_plugin_t api{};
  ~Instance() {
#ifdef ARDOR_CLAP_HAS_EDITOR
    nativeEditor_.reset();
    editor_.reset();
#endif
  }

private:
  static Instance& self(const clap_plugin_t* p) { return *static_cast<Instance*>(p->plugin_data); }
  const clap_host_t* host_;
  const clap_host_log_t* log_ = nullptr;
  const clap_host_params_t* hostParams_ = nullptr;
  const clap_host_state_t* hostState_ = nullptr;
  std::filesystem::path root_;
  ardor::Preset preset_, pendingPreset_;
  std::optional<ardor::Preset> editorDraft_;
#ifdef ARDOR_CLAP_HAS_EDITOR
  std::unique_ptr<ardor::clap_editor::Canvas> editor_;
  std::unique_ptr<ardor::clap_editor::NativeEditor> nativeEditor_;
#endif
  std::unique_ptr<ardor::PedalEngine> engine_, pendingEngine_;
  bool pending_ = false, active_ = false;
  std::array<std::atomic<double>, Count> values_{};
  std::array<std::atomic<double>, Count> uiValues_{};
  std::atomic<uint32_t> uiEvents_{0};
  std::atomic<int> requestedSlot_{-1};
  std::atomic<bool> callbackQueued_{false};
  ardor::clap_audio::RateAdapter rateAdapter_;
  double hostRate_ = 48000;
  float smoothing_ = .004157998f;
  std::uint32_t latency_ = quantum, maxFrames_ = 0;
  float inputGain_ = 1, outputGain_ = 1, wetMix_ = 1;
  int appliedScene_ = -1;
  std::uint64_t sceneRequest_ = 0;

  void report(const char* error) const
  { if (log_) log_->log(host_, CLAP_LOG_ERROR, error); }
  void rescan() const
  { if (hostParams_) hostParams_->rescan(host_, CLAP_PARAM_RESCAN_VALUES); }

  static int defaultScene(const ardor::Preset& preset)
  {
    if (preset.sceneSet)
      for (int i = 0; i < 4; ++i)
        if (preset.sceneSet->scenes[i].id == preset.sceneSet->defaultSceneId) return i;
    return 0;
  }

  ardor::Preset libraryPreset(int slot) const
  {
    ardor::PresetStore store(root_);
    if (std::filesystem::exists(store.pathFor({0, slot}))) return store.load({0, slot});
    ardor::Preset p;
    const char* names[] = {"Clean", "Tremolo", "Chorus", "Delay"};
    p.name = names[slot];
    if (slot) {
      const char* modes[] = {"", "vintage_trem", "chorus", "digital"};
      p.blocks.push_back({"factory-fx", slot == 3 ? "delay" : "mod", true, "",
                         {{"mode", modes[slot]}, {"mix", slot == 3 ? .25 : .5}}});
    }
    return p;
  }

  std::unique_ptr<ardor::PedalEngine> prepare(const ardor::Preset& preset)
  {
    auto engine = std::make_unique<ardor::PedalEngine>();
    ardor::EngineLoadOptions options;
    options.blockSize = quantum;
    options.parallelRigs = false;
    std::string error;
    if (!ardor::applyPreset(*engine, preset, root_, options, error)) throw std::runtime_error(error);
    if (engine->flexibleRoutingEnabled())
      throw std::runtime_error("Structural scene graphs are not supported by this CLAP beta's latency contract.");
    return engine;
  }

  // All file reads, parsing, preparation and ownership changes happen on main.
  // While active, old audio keeps running until the host grants a restart.
  bool stage(ardor::Preset preset)
  {
    try {
      auto prepared = prepare(preset);
      pendingPreset_ = std::move(preset);
      pendingEngine_ = std::move(prepared);
      pending_ = true;
      if (active_) host_->request_restart(host_);
      return true;
    } catch (const std::exception& error) { report(error.what()); return false; }
  }

  static bool CLAP_ABI init(const clap_plugin_t* p) noexcept
  {
    auto& s = self(p);
    try {
      s.log_ = static_cast<const clap_host_log_t*>(s.host_->get_extension(s.host_, CLAP_EXT_LOG));
      s.hostParams_ = static_cast<const clap_host_params_t*>(s.host_->get_extension(s.host_, CLAP_EXT_PARAMS));
      s.hostState_ = static_cast<const clap_host_state_t*>(s.host_->get_extension(s.host_, CLAP_EXT_STATE));
      s.root_ = ardor::desktopDataRoot();
      // Reading a library never creates or rewrites desktop presets/settings.
      s.preset_ = s.libraryPreset(0);
      s.values_[Scene].store(defaultScene(s.preset_));
      return true;
    } catch (const std::exception& error) { s.report(error.what()); return false; }
  }
  static void CLAP_ABI destroy(const clap_plugin_t* p) noexcept { delete &self(p); }
  static bool CLAP_ABI activate(const clap_plugin_t* p, double rate, uint32_t minimum, uint32_t maximum) noexcept
  {
    auto& s = self(p);
    if (!ardor::clap_audio::RateAdapter::supports(rate) || !minimum || maximum < minimum || maximum > INT32_MAX) {
      s.report("Ardor CLAP requires a DAW rate from 1 to 768 kHz and a valid host block range.");
      return false;
    }
    try {
      if (s.pending_) {
        s.engine_ = std::move(s.pendingEngine_);
        s.preset_ = std::move(s.pendingPreset_);
        s.pending_ = false;
      } else {
        if (s.editorDraft_) s.preset_ = *s.editorDraft_;
        s.engine_ = s.prepare(s.preset_);
      }
      const auto engineLatency = s.engine_->latencyFrames();
      if (engineLatency > 48000 * 10) throw std::runtime_error("Preset latency exceeds the beta's supported bound.");
      s.rateAdapter_.prepare(rate, static_cast<unsigned>(engineLatency));
      s.hostRate_ = rate;
      s.smoothing_ = static_cast<float>(1. - std::exp(-1. / (.005 * rate)));
      const auto previousLatency = s.latency_;
      s.latency_ = s.rateAdapter_.latency();
      s.maxFrames_ = maximum;
      s.active_ = true;
      reset(p);
      if (s.latency_ != previousLatency) {
        if (const auto* ext = static_cast<const clap_host_latency_t*>(s.host_->get_extension(s.host_, CLAP_EXT_LATENCY)))
          ext->changed(s.host_);
      }
      return true;
    } catch (const std::exception& error) { s.report(error.what()); return false; }
  }
  static void CLAP_ABI deactivate(const clap_plugin_t* p) noexcept { self(p).active_ = false; }
  static bool CLAP_ABI start(const clap_plugin_t* p) noexcept { return bool(self(p).engine_); }
  static void CLAP_ABI stop(const clap_plugin_t*) noexcept {}
  static void CLAP_ABI reset(const clap_plugin_t* p) noexcept
  {
    auto& s = self(p);
    if (s.engine_) s.engine_->reset();
    s.rateAdapter_.reset(static_cast<int>(s.values_[Scene].load()));
    s.inputGain_ = std::pow(10.0, s.values_[InputTrim].load() / 20);
    s.outputGain_ = std::pow(10.0, s.values_[OutputTrim].load() / 20);
    s.wetMix_ = s.values_[Bypass].load() >= .5 ? 0 : 1;
    s.appliedScene_ = -1;
    // Keep request IDs monotonic: engine.reset() retains the scene controller,
    // whose mailbox rejects IDs already applied before the host reset.
  }

  void event(const clap_event_header_t* event) noexcept
  {
    if (!event || event->space_id != CLAP_CORE_EVENT_SPACE_ID || event->type != CLAP_EVENT_PARAM_VALUE
        || event->size < sizeof(clap_event_param_value_t)) return;
    const auto& value = *reinterpret_cast<const clap_event_param_value_t*>(event);
    if (value.param_id >= Count || !std::isfinite(value.value) || value.note_id != -1
        || value.port_index != -1 || value.channel != -1 || value.key != -1) return;
    const auto& spec = specs[value.param_id];
    double bounded = std::clamp(value.value, spec.low, spec.high);
    if (spec.stepped) bounded = std::round(bounded);
    // A slot selector schedules work; it never loads a model on the audio thread.
    if (value.param_id == LibrarySlot) {
      requestedSlot_.store(static_cast<int>(bounded), std::memory_order_release);
      if (!callbackQueued_.exchange(true)) host_->request_callback(host_);
    } else values_[value.param_id].store(bounded, std::memory_order_relaxed);
  }

  void emitUiEvents(const clap_output_events_t* out) noexcept {
    if (!out || !out->try_push) return;
    const auto events = uiEvents_.exchange(0, std::memory_order_acq_rel);
    for (unsigned i = 0; i < Count; ++i) if (events & (1u << i)) {
      clap_event_param_value_t event{};
      event.header = {sizeof(event), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, CLAP_EVENT_IS_LIVE};
      event.param_id = i;
      event.value = uiValues_[i].load(std::memory_order_acquire);
      event.note_id = event.port_index = event.channel = event.key = -1;
      if (!out->try_push(out, &event.header)) uiEvents_.fetch_or(1u << i, std::memory_order_release);
    }
  }

  static void engineBlock(void* context, const float* input, float* left, float* right, unsigned frames, int scene)
  {
    auto& s = *static_cast<Instance*>(context);
    if (s.preset_.sceneSet && scene != s.appliedScene_) {
      const auto duration = static_cast<uint32_t>(std::min<uint64_t>(
        uint64_t(s.preset_.sceneSet->scenes[scene].enterTimeMs) * 48, UINT32_MAX));
      s.engine_->tryRequestScene({s.engine_->scenePresetGeneration(), ++s.sceneRequest_, duration,
                                 static_cast<uint8_t>(scene)});
      s.appliedScene_ = scene;
    }
    s.engine_->processBlock(input, left, right, frames);
  }

  static clap_process_status CLAP_ABI process(const clap_plugin_t* p, const clap_process_t* block) noexcept
  {
    const ardor::ScopedDenormalGuard denormalGuard;
    auto& s = self(p);
    if (!block || !s.active_ || !s.engine_ || block->frames_count > s.maxFrames_
        || block->audio_inputs_count != 1 || block->audio_outputs_count != 1
        || !block->audio_inputs || !block->audio_outputs) return CLAP_PROCESS_ERROR;
    const auto& input = block->audio_inputs[0];
    auto& output = block->audio_outputs[0];
    if (input.channel_count != 2 || output.channel_count != 2 || !input.data32 || !output.data32
        || !input.data32[0] || !input.data32[1] || !output.data32[0] || !output.data32[1]) return CLAP_PROCESS_ERROR;
    output.constant_mask = 0;
    s.emitUiEvents(block->out_events);
    const auto* events = block->in_events;
    const auto count = events ? events->size(events) : 0;
    uint32_t nextEvent = 0;
    double inputTarget = s.values_[InputTrim].load();
    double outputTarget = s.values_[OutputTrim].load();
    float inputGain = std::pow(10.0, inputTarget / 20);
    float outputGain = std::pow(10.0, outputTarget / 20);
    for (uint32_t frame = 0; frame < block->frames_count; ++frame) {
      while (nextEvent < count) {
        const auto* e = events->get(events, nextEvent);
        if (e && e->time > frame) break;
        s.event(e); ++nextEvent;
      }
      const double newInputTarget = s.values_[InputTrim].load(std::memory_order_relaxed);
      const double newOutputTarget = s.values_[OutputTrim].load(std::memory_order_relaxed);
      if (newInputTarget != inputTarget) { inputTarget = newInputTarget; inputGain = std::pow(10.0, inputTarget / 20); }
      if (newOutputTarget != outputTarget) { outputTarget = newOutputTarget; outputGain = std::pow(10.0, outputTarget / 20); }
      const float coefficient = s.smoothing_; // 5 ms at the host rate.
      s.inputGain_ += coefficient * (inputGain - s.inputGain_);
      s.outputGain_ += coefficient * (outputGain - s.outputGain_);
      const int channel = static_cast<int>(s.values_[InputChannel].load(std::memory_order_relaxed));
      const float l = input.data32[0][frame], r = input.data32[1][frame];
      const float selected = channel == 0 ? l : channel == 1 ? r : .5f * (l + r);
      const float raw = std::isfinite(selected) ? selected : 0;
      ardor::clap_audio::RateAdapter::Output converted{};
      if (!s.rateAdapter_.tick(raw * s.inputGain_, raw, static_cast<int>(s.values_[Scene].load(std::memory_order_relaxed)),
                             engineBlock, &s, converted)) return CLAP_PROCESS_ERROR;
      const float dry = converted.dry;
      const float mix = s.values_[Bypass].load(std::memory_order_relaxed) >= .5 ? 0.f : 1.f;
      s.wetMix_ += coefficient * (mix - s.wetMix_);
      output.data32[0][frame] = dry + s.wetMix_ * (converted.left * s.outputGain_ - dry);
      output.data32[1][frame] = dry + s.wetMix_ * (converted.right * s.outputGain_ - dry);
    }
    return CLAP_PROCESS_CONTINUE;
  }

  static void CLAP_ABI onMain(const clap_plugin_t* p) noexcept
  {
    auto& s = self(p);
    s.callbackQueued_.store(false);
    const int slot = s.requestedSlot_.exchange(-1, std::memory_order_acq_rel);
    if (slot < 0) return;
    try {
      if (!s.selectLibrary(slot)) s.rescan();
    } catch (const std::exception& error) { s.report(error.what()); s.rescan(); }
  }

  void syncEditor() {
    editorDraft_.reset();
#ifdef ARDOR_CLAP_HAS_EDITOR
    if (editor_) editor_->synchronize(pending_ ? pendingPreset_ : preset_, static_cast<int>(values_[LibrarySlot].load()));
#endif
  }
  bool selectLibrary(int slot) {
    if (!stage(libraryPreset(slot))) return false;
    values_[LibrarySlot].store(slot);
    values_[Scene].store(defaultScene(pendingPreset_));
    uiEvents_.fetch_and(~(1u << Scene));
    syncEditor();
    rescan();
    if (hostState_) hostState_->mark_dirty(host_);
    return true;
  }

#ifdef ARDOR_CLAP_HAS_EDITOR
  void setEditorControl(unsigned id, double value) {
    if (id >= Count || id == LibrarySlot || !std::isfinite(value)) return;
    value = std::clamp(value, specs[id].low, specs[id].high);
    if (specs[id].stepped) value = std::round(value);
    values_[id].store(value);
    uiValues_[id].store(value, std::memory_order_release);
    uiEvents_.fetch_or(1u << id, std::memory_order_release);
    if (hostParams_ && hostParams_->request_flush) hostParams_->request_flush(host_);
    if (hostState_) hostState_->mark_dirty(host_);
  }
  static bool CLAP_ABI guiSupported(const clap_plugin_t*, const char* api, bool floating) noexcept {
    return api && !floating && std::strcmp(api, windowApi) == 0;
  }
  static bool CLAP_ABI guiPreferred(const clap_plugin_t*, const char** api, bool* floating) noexcept {
    if (!api || !floating) return false;
    *api = windowApi; *floating = false; return true;
  }
  static bool CLAP_ABI guiCreate(const clap_plugin_t* p, const char* api, bool floating) noexcept {
    auto& s = self(p);
    if (!guiSupported(p, api, floating) || s.editor_) return false;
    try {
      if (!s.engine_ && !s.pendingEngine_ && !s.stage(s.editorDraft_ ? *s.editorDraft_ : s.preset_)) return false;
      ardor::clap_editor::Callbacks callbacks;
      callbacks.root = s.root_;
      callbacks.engine = [&s] { return s.pending_ ? s.pendingEngine_.get() : s.engine_.get(); };
      callbacks.waiting = [&s] { return s.active_ && s.pending_; };
      callbacks.sampleRate = [&s] { return s.active_ ? s.hostRate_ : 0.; };
      callbacks.stage = [&s](ardor::Preset preset) { return s.stage(std::move(preset)); };
      callbacks.edited = [&s](ardor::Preset preset) {
        // The live audio callback reads preset_, never this main-thread draft.
        if (s.pending_) s.pendingPreset_ = preset;
        s.editorDraft_ = std::move(preset);
        if (s.hostState_) s.hostState_->mark_dirty(s.host_);
      };
      callbacks.selectPreset = [&s](int slot) { return s.selectLibrary(slot); };
      callbacks.selectScene = [&s](int scene) { s.setEditorControl(Scene, scene); };
      callbacks.control = [&s](unsigned id) { return id < Count ? s.values_[id].load() : 0.; };
      callbacks.setControl = [&s](unsigned id, double value) { s.setEditorControl(id, value); };
      std::array<ardor::Preset, 4> library;
      for (int i = 0; i < 4; ++i) {
        try { library[i] = s.libraryPreset(i); }
        catch (...) { library[i].name = "Unavailable preset"; }
      }
      s.editor_ = std::make_unique<ardor::clap_editor::Canvas>(std::move(callbacks), library,
        static_cast<int>(s.values_[LibrarySlot].load()), s.editorDraft_ ? *s.editorDraft_ : s.pending_ ? s.pendingPreset_ : s.preset_);
#ifdef _WIN32
      s.nativeEditor_ = ardor::clap_editor::createWindowsEditor(*s.editor_);
#else
      s.nativeEditor_ = ardor::clap_editor::createMacEditor(*s.editor_);
#endif
      return true;
    } catch (const std::exception& error) {
      s.nativeEditor_.reset(); s.editor_.reset(); s.report(error.what()); return false;
    }
  }
  static void CLAP_ABI guiDestroy(const clap_plugin_t* p) noexcept {
    auto& s = self(p);
    try { if (s.editor_) s.editor_->flushEdits(); } catch (...) {}
    s.nativeEditor_.reset(); s.editor_.reset();
  }
  static bool CLAP_ABI guiScale(const clap_plugin_t* p, double scale) noexcept {
    try { return self(p).nativeEditor_ && self(p).nativeEditor_->setScale(scale); }
    catch (...) { return false; }
  }
  static bool CLAP_ABI guiSize(const clap_plugin_t* p, uint32_t* width, uint32_t* height) noexcept {
    auto& s = self(p);
    if (!s.nativeEditor_ || !width || !height) return false;
    *width = s.nativeEditor_->width(); *height = s.nativeEditor_->height(); return true;
  }
  static bool CLAP_ABI guiResizable(const clap_plugin_t*) noexcept { return true; }
  static bool CLAP_ABI guiHints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) noexcept {
    if (!hints) return false;
    *hints = {true, true, false, 0, 0}; return true;
  }
  static bool CLAP_ABI guiAdjust(const clap_plugin_t* p, uint32_t* width, uint32_t* height) noexcept {
    if (!width || !height) return false;
    const auto scale = self(p).nativeEditor_ ? self(p).nativeEditor_->scale() : 1.;
    *width = std::clamp(*width, uint32_t(std::lround(960 * scale)), uint32_t(std::lround(1920 * scale)));
    *height = std::clamp(*height, uint32_t(std::lround(584 * scale)), uint32_t(std::lround(1124 * scale))); return true;
  }
  static bool CLAP_ABI guiResize(const clap_plugin_t* p, uint32_t width, uint32_t height) noexcept {
    try { return self(p).nativeEditor_ && self(p).nativeEditor_->setSize(width, height); }
    catch (...) { return false; }
  }
  static bool CLAP_ABI guiParent(const clap_plugin_t* p, const clap_window_t* window) noexcept {
    if (!window || !guiSupported(p, window->api, false)) return false;
#ifdef _WIN32
    auto* parent = window->win32;
#else
    auto* parent = window->cocoa;
#endif
    if (!parent) return false;
    try { return self(p).nativeEditor_ && self(p).nativeEditor_->setParent(parent); }
    catch (...) { return false; }
  }
  static bool CLAP_ABI guiTransient(const clap_plugin_t*, const clap_window_t*) noexcept { return false; }
  static void CLAP_ABI guiTitle(const clap_plugin_t*, const char*) noexcept {}
  static bool CLAP_ABI guiShow(const clap_plugin_t* p) noexcept {
    try { return self(p).nativeEditor_ && self(p).nativeEditor_->show(); } catch (...) { return false; }
  }
  static bool CLAP_ABI guiHide(const clap_plugin_t* p) noexcept {
    try { return self(p).nativeEditor_ && self(p).nativeEditor_->hide(); } catch (...) { return false; }
  }
  static const clap_plugin_gui_t& guiExtension() {
    static const clap_plugin_gui_t gui{guiSupported, guiPreferred, guiCreate, guiDestroy, guiScale,
      guiSize, guiResizable, guiHints, guiAdjust, guiResize, guiParent, guiTransient, guiTitle, guiShow, guiHide};
    return gui;
  }
#endif

  static uint32_t CLAP_ABI paramCount(const clap_plugin_t*) noexcept { return Count; }
  static bool CLAP_ABI paramInfo(const clap_plugin_t*, uint32_t index, clap_param_info_t* info) noexcept
  {
    if (index >= Count || !info) return false;
    *info = {};
    info->id = index;
    info->flags = index == LibrarySlot ? 0 : CLAP_PARAM_IS_AUTOMATABLE;
    if (specs[index].stepped) info->flags |= CLAP_PARAM_IS_STEPPED;
    if (index == Bypass) info->flags |= CLAP_PARAM_IS_BYPASS;
    if (index == InputChannel || index == LibrarySlot || index == Scene) info->flags |= CLAP_PARAM_IS_ENUM;
    std::snprintf(info->name, sizeof(info->name), "%s", specs[index].name);
    info->min_value = specs[index].low; info->max_value = specs[index].high;
    info->default_value = specs[index].initial;
    return true;
  }
  static bool CLAP_ABI paramValue(const clap_plugin_t* p, clap_id id, double* value) noexcept
  { if (id >= Count || !value) return false; *value = self(p).values_[id].load(); return true; }
  static bool CLAP_ABI toText(const clap_plugin_t*, clap_id id, double value, char* text, uint32_t size) noexcept
  {
    if (id >= Count || !text || !size || !std::isfinite(value)) return false;
    const int integer = static_cast<int>(std::clamp(value, specs[id].low, specs[id].high));
    if (id == InputChannel) { const char* names[] = {"Left", "Right", "Average"}; std::snprintf(text, size, "%s", names[integer]); }
    else if (id == Bypass) std::snprintf(text, size, "%s", integer ? "Bypassed" : "Active");
    else if (id == LibrarySlot || id == Scene) std::snprintf(text, size, "%d", integer + 1);
    else std::snprintf(text, size, "%.2f dB", value);
    return true;
  }
  static bool CLAP_ABI fromText(const clap_plugin_t*, clap_id id, const char* text, double* value) noexcept
  {
    if (id >= Count || !text || !value) return false;
    if (id == InputChannel || id == Bypass) {
      const char* names[] = {"Left", "Right", "Average", "Active", "Bypassed"};
      for (int i = id == InputChannel ? 0 : 3; i < (id == InputChannel ? 3 : 5); ++i)
        if (std::strcmp(text, names[i]) == 0) { *value = id == InputChannel ? i : i - 3; return true; }
    }
    char* end = nullptr;
    double parsed = std::strtod(text, &end);
    if (end == text || !std::isfinite(parsed)) return false;
    while (*end == ' ') ++end;
    if (*end && (id >= Bypass || std::strcmp(end, "dB") != 0)) return false;
    if (id == LibrarySlot || id == Scene) parsed -= 1;
    if (parsed < specs[id].low || parsed > specs[id].high) return false;
    *value = specs[id].stepped ? std::round(parsed) : parsed;
    return true;
  }
  static void CLAP_ABI flush(const clap_plugin_t* p, const clap_input_events_t* in, const clap_output_events_t* out) noexcept
  {
    auto& s = self(p);
    if (in) for (uint32_t i = 0, n = in->size(in); i < n; ++i) s.event(in->get(in, i));
    s.emitUiEvents(out);
  }

  static bool CLAP_ABI save(const clap_plugin_t* p, const clap_ostream_t* stream) noexcept
  {
    try {
      auto& s = self(p);
#ifdef ARDOR_CLAP_HAS_EDITOR
      if (s.editor_ && !s.editor_->flushEdits()) return false;
#endif
      nlohmann::json values = nlohmann::json::array();
      for (const auto& value : s.values_) values.push_back(value.load());
      const auto data = nlohmann::json({{"version", 1}, {"preset", ardor::toJson(s.editorDraft_ ? *s.editorDraft_ : s.pending_ ? s.pendingPreset_ : s.preset_)},
                                      {"parameters", values}}).dump();
      if (!stream || !stream->write || data.size() > stateLimit) return false;
      std::size_t offset = 0;
      while (offset < data.size()) {
        const auto written = stream->write(stream, data.data() + offset, data.size() - offset);
        if (written <= 0 || static_cast<uint64_t>(written) > data.size() - offset) return false;
        offset += static_cast<std::size_t>(written);
      }
      return true;
    } catch (...) { return false; }
  }
  static bool CLAP_ABI load(const clap_plugin_t* p, const clap_istream_t* stream) noexcept
  {
    auto& s = self(p);
    try {
      if (!stream || !stream->read) return false;
      std::string data;
      std::array<char, 4096> buffer{};
      for (;;) {
        const auto read = stream->read(stream, buffer.data(), buffer.size());
        if (read < 0 || read > static_cast<int64_t>(buffer.size())) return false;
        if (!read) break;
        if (data.size() + read > stateLimit) return false;
        data.append(buffer.data(), static_cast<std::size_t>(read));
      }
      const auto json = nlohmann::json::parse(data);
      if (json.at("version") != 1 || json.at("parameters").size() != Count) return false;
      std::array<double, Count> values;
      for (std::size_t i = 0; i < Count; ++i) {
        values[i] = json.at("parameters").at(i).get<double>();
        if (!std::isfinite(values[i]) || values[i] < specs[i].low || values[i] > specs[i].high
            || (specs[i].stepped && values[i] != std::round(values[i]))) return false;
      }
      if (!s.stage(ardor::presetFromJson(json.at("preset")))) return false;
      for (std::size_t i = 0; i < Count; ++i) s.values_[i].store(values[i]);
      s.uiEvents_.store(0);
      s.syncEditor();
      s.rescan();
      return true;
    } catch (const std::exception& error) { s.report(error.what()); return false; }
  }
  static bool CLAP_ABI loadPreset(const clap_plugin_t* p, uint32_t kind, const char* path, const char* key) noexcept
  {
    if (kind != CLAP_PRESET_DISCOVERY_LOCATION_FILE || !path || (key && *key)) return false;
    auto& s = self(p);
    try {
      const std::filesystem::path location(reinterpret_cast<const char8_t*>(path));
      if (std::filesystem::file_size(location) > stateLimit) return false;
      std::ifstream file(location);
      nlohmann::json json;
      file >> json;
      if (!s.stage(ardor::presetFromJson(json))) return false;
      s.values_[Scene].store(defaultScene(s.pendingPreset_));
      s.uiEvents_.fetch_and(~(1u << Scene));
      s.syncEditor();
      s.rescan();
      if (s.hostState_) s.hostState_->mark_dirty(s.host_);
      if (const auto* host = static_cast<const clap_host_preset_load_t*>(s.host_->get_extension(s.host_, CLAP_EXT_PRESET_LOAD)))
        host->loaded(s.host_, kind, path, nullptr);
      return true;
    } catch (const std::exception& error) { s.report(error.what()); return false; }
  }
  static uint32_t CLAP_ABI portCount(const clap_plugin_t*, bool) noexcept { return 1; }
  static bool CLAP_ABI portInfo(const clap_plugin_t*, uint32_t index, bool input, clap_audio_port_info_t* info) noexcept
  {
    if (index || !info) return false;
    *info = {};
    info->id = 0; info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2; info->port_type = CLAP_PORT_STEREO; info->in_place_pair = 0;
    std::snprintf(info->name, sizeof(info->name), "%s", input ? "Guitar" : "Effects");
    return true;
  }
  static bool CLAP_ABI hardRealtime(const clap_plugin_t*) noexcept { return false; }
  static bool CLAP_ABI renderMode(const clap_plugin_t*, clap_plugin_render_mode mode) noexcept
  { return mode == CLAP_RENDER_REALTIME || mode == CLAP_RENDER_OFFLINE; }
  static uint32_t CLAP_ABI latency(const clap_plugin_t* p) noexcept { return self(p).latency_; }
  static uint32_t CLAP_ABI tail(const clap_plugin_t* p) noexcept
  {
    // Conservative until every feedback algorithm has a verified finite bound.
    const auto& s = self(p);
    return s.preset_.blocks.empty() && !s.preset_.wdw ? s.rateAdapter_.filterTail() : INT32_MAX;
  }
  static const void* CLAP_ABI extension(const clap_plugin_t*, const char* id) noexcept
  {
    static const clap_plugin_render_t render{hardRealtime, renderMode};
    static const clap_plugin_params_t params{paramCount, paramInfo, paramValue, toText, fromText, flush};
    static const clap_plugin_audio_ports_t ports{portCount, portInfo};
    static const clap_plugin_state_t state{save, load};
    static const clap_plugin_latency_t latencyExtension{latency};
    static const clap_plugin_tail_t tailExtension{tail};
    static const clap_plugin_preset_load_t presetLoad{loadPreset};
    if (!id) return nullptr;
#ifdef ARDOR_CLAP_HAS_EDITOR
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &guiExtension();
#endif
    if (std::strcmp(id, CLAP_EXT_RENDER) == 0) return &render;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &ports;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &state;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latencyExtension;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tailExtension;
    if (std::strcmp(id, CLAP_EXT_PRESET_LOAD) == 0) return &presetLoad;
    return nullptr;
  }
};

uint32_t CLAP_ABI count(const clap_plugin_factory_t*) noexcept { return 1; }
const clap_plugin_descriptor_t* CLAP_ABI describe(const clap_plugin_factory_t*, uint32_t index) noexcept
{ return index == 0 ? &descriptor : nullptr; }
const clap_plugin_t* CLAP_ABI create(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) noexcept
{
  if (!host || !id || !clap_version_is_compatible(host->clap_version) || std::strcmp(id, pluginId) != 0) return nullptr;
  try { return &(new Instance(host))->api; } catch (...) { return nullptr; }
}
const void* CLAP_ABI factory(const char* id) noexcept
{
  static const clap_plugin_factory_t pluginFactory{count, describe, create};
  return id && std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &pluginFactory : nullptr;
}
bool CLAP_ABI entryInit(const char*) noexcept { return true; }
void CLAP_ABI entryDeinit() noexcept {}
} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = {CLAP_VERSION, entryInit, entryDeinit, factory};
