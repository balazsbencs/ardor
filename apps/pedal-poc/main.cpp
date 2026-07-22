#if defined(ARDOR_HAS_UI) && !defined(ARDOR_UI_BACKEND_FBDEV)
#define SDL_MAIN_HANDLED
#endif

#include "miniaudio.h"

#include "audio/EngineLoader.h"
#include "audio/MiniaudioBackend.h"
#include "audio/PresetActivation.h"
#include "control/ControlEvents.h"
#if defined(__linux__)
#include "control/LinuxInput.h"
#endif
#include "audio/WavIo.h"
#include "dsp/PedalEngine.h"
#include "dsp/Tuner.h"
#include "preset/ChainPlan.h"
#include "preset/Preset.h"
#include "preset/PresetStore.h"
#include "preset/RuntimeCommands.h"
#include "preset/RuntimeState.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#if defined(__linux__)
#include <cerrno>
#include <cstring>
#include <sys/mman.h>
#endif
#include <chrono>
#include <csignal>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(ARDOR_HAS_UI)
#include "ui/LvglUi.h"
#include "ui/UiModel.h"
#include <lvgl.h>
#endif

namespace {

volatile std::sig_atomic_t running = 1;
// The stdin helper is detached because formatted stdin reads are not
// cancellation-friendly. These requests therefore need process lifetime rather
// than stack lifetime.
std::atomic<int> requestedSlot{-1};
std::atomic<int> requestedBank{-1};

#if defined(ARDOR_HAS_UI)
ardor::UiClipDebugTelemetry makeUiClipDebugTelemetry(const ardor::ClipDiagnosticsSnapshot& diagnostics)
{
  ardor::UiClipDebugTelemetry telemetry;
  telemetry.enabled = true;
  telemetry.limiterFrames = diagnostics.limiterFrames;
  if (!diagnostics.stages.empty()) {
    const float peak = diagnostics.stages.back().peak;
    telemetry.peakDb = peak > 0.0f ? 20.0f * std::log10(peak) : -120.0f;
  }
  for (const auto& stage : diagnostics.stages) {
    if (stage.overloadFrames == 0) continue;
    telemetry.overloaded = true;
    telemetry.firstStage = ardor::signalStageKindName(stage.kind);
    if (!stage.id.empty()) telemetry.firstStage += ":" + stage.id;
    telemetry.peakDb = stage.peak > 0.0f ? 20.0f * std::log10(stage.peak) : -120.0f;
    telemetry.overloadFrames = stage.overloadFrames;
    break;
  }
  return telemetry;
}
#endif

void handleSignal(int)
{
  running = 0;
}

const char* replaceResultName(ardor::EngineReplaceResult result)
{
  switch (result) {
  case ardor::EngineReplaceResult::Activated:
    return "activated";
  case ardor::EngineReplaceResult::Busy:
    return "another replacement is already pending";
  case ardor::EngineReplaceResult::DeviceStopped:
    return "the audio device stopped";
  case ardor::EngineReplaceResult::TimedOut:
    return "the audio callback did not acknowledge the replacement within 2 seconds";
  }
  return "unknown replacement result";
}

enum class DeviceRecoveryResult {
  NotNeeded,
  Waiting,
  Restarted,
  Failed,
};

class DeviceRecoveryController {
public:
  bool pending() const { return pending_; }

  DeviceRecoveryResult service(ardor::MiniaudioBackend& backend,
                               ardor::PedalEngine& engine,
                               const ardor::RealtimeOptions& options)
  {
    if (!backend.deviceStopped()) {
      return DeviceRecoveryResult::NotNeeded;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!pending_) {
      pending_ = true;
      attempts_ = 0;
      nextAttempt_ = now;
    }
    if (now < nextAttempt_) {
      return DeviceRecoveryResult::Waiting;
    }

    backend.stop();
    ++attempts_;
    std::cerr << "Attempting audio-device restart " << attempts_ << "/" << kMaxAttempts << "\n";
    if (backend.start(engine, options)) {
      pending_ = false;
      return DeviceRecoveryResult::Restarted;
    }
    if (attempts_ == kMaxAttempts) {
      pending_ = false;
      return DeviceRecoveryResult::Failed;
    }

    const auto delay = std::chrono::milliseconds(100 * (1 << (attempts_ - 1)));
    nextAttempt_ = now + delay;
    return DeviceRecoveryResult::Waiting;
  }

private:
  static constexpr int kMaxAttempts = 3;
  bool pending_ = false;
  int attempts_ = 0;
  std::chrono::steady_clock::time_point nextAttempt_{};
};

#if defined(ARDOR_HAS_UI) && defined(ARDOR_UI_BACKEND_FBDEV)
// Locate the touchscreen evdev node by device name. On the Touch Display 2 the
// controller enumerates as "Goodix Capacitive TouchScreen"; the event number is
// not stable across boots, so match on name rather than hardcoding event0.
std::string findTouchDevice()
{
  namespace fs = std::filesystem;
  std::error_code ec;
  for (fs::directory_iterator it{"/sys/class/input", ec}, end; !ec && it != end; it.increment(ec)) {
    const auto& entry = *it;
    const std::string node = entry.path().filename().string();
    if (node.rfind("event", 0) != 0) continue;
    std::ifstream nameFile{entry.path() / "device" / "name"};
    std::string name;
    std::getline(nameFile, name);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name.find("goodix") != std::string::npos ||
        name.find("touch") != std::string::npos) {
      return "/dev/input/" + node;
    }
  }
  return {};
}

// Live touch-coordinate overlay for calibration (ARDOR_TOUCH_DEBUG=1).
struct TouchDebug {
  lv_obj_t* label;
  lv_obj_t* dot;
  lv_indev_t* indev;
};

void touchDebugTick(lv_timer_t* timer)
{
  auto* d = static_cast<TouchDebug*>(lv_timer_get_user_data(timer));
  lv_point_t p;
  lv_indev_get_point(d->indev, &p);
  const bool down = lv_indev_get_state(d->indev) == LV_INDEV_STATE_PRESSED;
  lv_label_set_text_fmt(d->label, "touch %s  x:%d  y:%d", down ? "DOWN" : "up",
                        static_cast<int>(p.x), static_cast<int>(p.y));
  if (down) {
    lv_obj_remove_flag(d->dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(d->dot, static_cast<int32_t>(p.x) - 12, static_cast<int32_t>(p.y) - 12);
  } else {
    lv_obj_add_flag(d->dot, LV_OBJ_FLAG_HIDDEN);
  }
}

// Overlay lives on the top layer (above the UI, survives screen rebuilds).
void installTouchDebug(lv_indev_t* touch)
{
  lv_obj_t* top = lv_layer_top();

  lv_obj_t* label = lv_label_create(top);
  lv_obj_set_style_bg_opa(label, LV_OPA_70, 0);
  lv_obj_set_style_bg_color(label, lv_color_black(), 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_pad_all(label, 6, 0);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 4, 4);
  lv_label_set_text(label, "touch: --");

  lv_obj_t* dot = lv_obj_create(top);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 24, 24);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(0xff3b30), 0);
  lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);

  // Lives for the process lifetime; freeing is not worth the bookkeeping.
  auto* dbg = new TouchDebug{label, dot, touch};
  lv_timer_create(touchDebugTick, 30, dbg);
}
#endif

struct Args {
  bool offline = false;
  bool realtime = false;
  bool allowNonRealtime = false;
  bool allowDeviceResampling = false;
  bool bypassNam = false;
  bool devices = false;
  uint32_t sampleRate = 48000;
  uint32_t blockSize = 64;
  size_t irSamples = 0;
  float tailSeconds = -1.0f;
  bool noTail = false;
  float inputGainDb = 0.0f;
  float outputGainDb = 0.0f;
  float safetyLimitDb = -1.0f;
  bool safetyLimiter = true;
  bool clipDebug = false;
  int captureDeviceIndex = -1;
  int playbackDeviceIndex = -1;
  uint32_t inputChannel = 0;
  ardor::OutputChannel outputChannel = ardor::OutputChannel::Both;
  std::filesystem::path preset;
  std::filesystem::path dataRoot = ".";
  bool presetSlotMode = false;
  int bank = 0;
  int slot = 0;
  std::vector<std::filesystem::path> controlDevices;
  bool enableUi = false;
  std::filesystem::path model;
  std::filesystem::path ir;
  std::filesystem::path input;
  std::filesystem::path output;
};

bool parse(int argc, char** argv, Args& args)
{
  auto parseChannel = [](const std::string& value, uint32_t& channel) {
    if (value == "left" || value == "0") {
      channel = 0;
      return true;
    }
    if (value == "right" || value == "1") {
      channel = 1;
      return true;
    }
    return false;
  };

  try {
    for (int i = 1; i < argc; ++i) {
      const std::string a = argv[i];
      auto value = [&]() -> const char* {
        if (i + 1 >= argc) return nullptr;
        return argv[++i];
      };

      if (a == "--version") {
        std::cout << "ardor pedal poc\n";
        std::exit(0);
      } else if (a == "--offline") {
        args.offline = true;
      } else if (a == "--realtime") {
        args.realtime = true;
      } else if (a == "--allow-non-realtime") {
        args.allowNonRealtime = true;
      } else if (a == "--allow-device-resampling") {
        args.allowDeviceResampling = true;
      } else if (a == "--devices") {
        args.devices = true;
      } else if (a == "--bypass-nam") {
        args.bypassNam = true;
      } else if (a == "--preset") {
        const char* v = value();
        if (!v) return false;
        args.preset = v;
      } else if (a == "--data-root") {
        const char* v = value();
        if (!v) return false;
        args.dataRoot = v;
      } else if (a == "--bank") {
        const char* v = value();
        if (!v) return false;
        args.bank = std::stoi(v);
        args.presetSlotMode = true;
      } else if (a == "--slot") {
        const char* v = value();
        if (!v) return false;
        args.slot = std::stoi(v);
        args.presetSlotMode = true;
      } else if (a == "--sample-rate") {
        const char* v = value();
        if (!v) return false;
        args.sampleRate = static_cast<uint32_t>(std::stoul(v));
      } else if (a == "--block-size") {
        const char* v = value();
        if (!v) return false;
        args.blockSize = static_cast<uint32_t>(std::stoul(v));
      } else if (a == "--ir-samples") {
        const char* v = value();
        if (!v) return false;
        args.irSamples = static_cast<size_t>(std::stoull(v));
      } else if (a == "--tail-seconds") {
        const char* v = value();
        if (!v) return false;
        args.tailSeconds = std::stof(v);
        if (!std::isfinite(args.tailSeconds) || args.tailSeconds < 0.0f) return false;
      } else if (a == "--no-tail") {
        args.noTail = true;
      } else if (a == "--input-gain-db") {
        const char* v = value();
        if (!v) return false;
        args.inputGainDb = std::stof(v);
      } else if (a == "--output-gain-db") {
        const char* v = value();
        if (!v) return false;
        args.outputGainDb = std::stof(v);
      } else if (a == "--safety-limit-db") {
        const char* v = value();
        if (!v) return false;
        args.safetyLimitDb = std::stof(v);
      } else if (a == "--no-safety-limit") {
        args.safetyLimiter = false;
      } else if (a == "--clip-debug") {
        args.clipDebug = true;
      } else if (a == "--capture-device") {
        const char* v = value();
        if (!v) return false;
        args.captureDeviceIndex = std::stoi(v);
      } else if (a == "--playback-device") {
        const char* v = value();
        if (!v) return false;
        args.playbackDeviceIndex = std::stoi(v);
      } else if (a == "--input-channel") {
        const char* v = value();
        if (!v || !parseChannel(v, args.inputChannel)) return false;
      } else if (a == "--output-channel") {
        const char* v = value();
        if (!v) return false;
        const std::string channel = v;
        if (channel == "both") args.outputChannel = ardor::OutputChannel::Both;
        else if (channel == "left") args.outputChannel = ardor::OutputChannel::Left;
        else if (channel == "right") args.outputChannel = ardor::OutputChannel::Right;
        else return false;
      } else if (a == "--control-device") {
        const char* v = value();
        if (!v) return false;
        args.controlDevices.emplace_back(v);
#if defined(ARDOR_HAS_UI)
      } else if (a == "--ui") {
        args.enableUi = true;
#endif
      } else if (a == "--model") {
        const char* v = value();
        if (!v) return false;
        args.model = v;
      } else if (a == "--ir") {
        const char* v = value();
        if (!v) return false;
        args.ir = v;
      } else if (a == "--input") {
        const char* v = value();
        if (!v) return false;
        args.input = v;
      } else if (a == "--output") {
        const char* v = value();
        if (!v) return false;
        args.output = v;
      } else {
        return false;
      }
    }
  } catch (const std::exception&) {
    return false;
  }

  if (!args.preset.empty() && args.bypassNam) {
    return false;
  }

  if (args.presetSlotMode && (args.bank < 0 || args.bank >= 100 || args.slot < 0 || args.slot >= 4)) {
    return false;
  }

  if (args.sampleRate != 48000) {
    return false;
  }
  if (args.blockSize == 0) {
    return false;
  }

  if (args.devices) return true;
  if (args.offline) {
    if (!args.preset.empty() || args.presetSlotMode) {
      return !args.input.empty() && !args.output.empty();
    }
    return !args.ir.empty() && !args.input.empty() && !args.output.empty() && (args.bypassNam || !args.model.empty());
  }
  if (args.realtime) {
    if (!args.preset.empty() || args.presetSlotMode) {
      return true;
    }
    return !args.ir.empty() && !args.model.empty();
  }
  return false;
}

float dbToGain(float db)
{
  return std::pow(10.0f, db / 20.0f);
}

void writeStereo(const std::filesystem::path& path, const std::vector<float>& interleaved, ma_uint32 sampleRate)
{
  ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 2, sampleRate);
  ma_encoder encoder;
  if (ma_encoder_init_file(path.string().c_str(), &cfg, &encoder) != MA_SUCCESS) {
    throw std::runtime_error("failed to create wav: " + path.string());
  }
  ma_uint64 framesWritten = 0;
  if (ma_encoder_write_pcm_frames(&encoder, interleaved.data(), interleaved.size() / 2, &framesWritten) != MA_SUCCESS
      || framesWritten != interleaved.size() / 2) {
    ma_encoder_uninit(&encoder);
    throw std::runtime_error("failed to write wav: " + path.string());
  }
  ma_encoder_uninit(&encoder);
}

int printDevices()
{
  ma_context context;
  if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
    std::cerr << "Failed to initialize audio context\n";
    return 1;
  }

  ma_device_info* playback = nullptr;
  ma_uint32 playbackCount = 0;
  ma_device_info* capture = nullptr;
  ma_uint32 captureCount = 0;
  if (ma_context_get_devices(&context, &playback, &playbackCount, &capture, &captureCount) != MA_SUCCESS) {
    ma_context_uninit(&context);
    std::cerr << "Failed to enumerate audio devices\n";
    return 1;
  }

  std::cout << "Playback devices:\n";
  for (ma_uint32 i = 0; i < playbackCount; ++i) {
    std::cout << "  [" << i << "] " << playback[i].name << "\n";
  }
  std::cout << "Capture devices:\n";
  for (ma_uint32 i = 0; i < captureCount; ++i) {
    std::cout << "  [" << i << "] " << capture[i].name << "\n";
  }

  ma_context_uninit(&context);
  return 0;
}

} // namespace

int main(int argc, char** argv)
{
  try {
    Args args;
    if (!parse(argc, argv, args)) {
      std::cerr << "Usage:\n"
                << "  pedal-poc --devices\n"
                << "  pedal-poc --offline --preset preset.json --data-root data --input dry.wav --output wet.wav\n"
                << "  pedal-poc --offline --data-root data --bank 0 --slot 0 --input dry.wav --output wet.wav\n"
                << "  pedal-poc --offline --ir cab.wav --input dry.wav --output wet.wav (--model amp.nam | --bypass-nam)\n"
                << "            [--tail-seconds N | --no-tail]\n"
                << "  pedal-poc --realtime --preset preset.json --data-root data [--sample-rate 48000] [--block-size 64]\n"
                << "  pedal-poc --realtime --data-root data --bank 0 --slot 0 [--sample-rate 48000] [--block-size 64]\n"
                << "  pedal-poc --realtime --model amp.nam --ir cab.wav [--sample-rate 48000] [--block-size 64]\n"
                << "            [--allow-non-realtime] (development only)\n"
                << "            [--allow-device-resampling] (development only)\n"
                << "            [--capture-device N] [--playback-device N] [--input-channel left|right]\n"
                << "            [--output-channel both|left|right] [--ir-samples N]\n"
                << "            [--input-gain-db DB] [--output-gain-db DB]\n"
                << "            [--safety-limit-db DB] [--no-safety-limit]\n"
                << "            [--clip-debug] (per-stage peak/overload diagnostics)\n"
                << "            [--control-device /dev/input/eventX]...\n"
                << "            [--ui]\n";
      return 2;
    }

    if (args.devices) {
      return printDevices();
    }

    if (args.realtime) {
      std::signal(SIGINT, handleSignal);
      std::signal(SIGTERM, handleSignal);
    }

    const ardor::EngineLoadOptions loadOptions{
      args.sampleRate,
      args.blockSize,
      args.irSamples == 0 ? size_t{8192} : args.irSamples,
    };

    // Realtime slot mode: unique_ptr engine enables stop/swap/restart switching
    if (args.realtime && args.presetSlotMode) {
      auto liveEngine = std::make_unique<ardor::PedalEngine>();
      ardor::PresetStore store(args.dataRoot);
      std::string loadError;
      if (!ardor::applyPresetSlot(*liveEngine, store, {args.bank, args.slot}, args.dataRoot, loadOptions, loadError)) {
        std::cerr << "Warning: preset " << args.bank << ":" << args.slot << " failed (" << loadError
                  << "), using pass-through\n";
        liveEngine->clearEffects();
        liveEngine->prepareBlockSize(loadOptions.blockSize);
      }

      requestedSlot.store(-1, std::memory_order_relaxed);
      requestedBank.store(-1, std::memory_order_relaxed);
      std::thread controlThread([]() {
        char c = 0;
        while (std::cin >> c) {
          if (c >= '0' && c <= '3') {
            requestedSlot.store(c - '0', std::memory_order_relaxed);
          }
        }
      });
      controlThread.detach();

      ardor::RealtimeOptions options;
      options.sampleRate = args.sampleRate;
      options.blockSize = args.blockSize;
      options.captureDeviceIndex = args.captureDeviceIndex;
      options.playbackDeviceIndex = args.playbackDeviceIndex;
      options.inputChannel = args.inputChannel;
      options.outputChannel = args.outputChannel;
#if defined(__linux__)
      options.requireRealtimeScheduler = !args.allowNonRealtime;
      options.requireNativeSampleRate = !args.allowDeviceResampling;
#endif

#if defined(__linux__)
      if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "mlockall failed (running without locked memory): " << std::strerror(errno) << "\n";
      }
#endif
      ardor::MiniaudioBackend backend;
      if (!backend.start(*liveEngine, options)) {
        std::cerr << "Failed to start realtime audio\n";
        return 1;
      }
      DeviceRecoveryController deviceRecovery;

      ardor::ControlState controls{args.slot, 80};
      ardor::FootswitchGesture footswitchGesture;
      ardor::TunerAnalyzer tuner(static_cast<float>(args.sampleRate));
      bool tunerMode = false;
      uint64_t lastTunerRevision = 0;
      std::array<float, 2048> tunerInput{};
      int deferredTunerBank = -1;
      int deferredTunerSlot = -1;
      ardor::ActivePresetSelection activeSelection{args.bank, args.slot};
      liveEngine->setMasterVolume(static_cast<float>(controls.masterVolume) / 100.0f);

#if defined(ARDOR_HAS_UI)
      std::unique_ptr<ardor::LvglUi> ui;
      ardor::UiState uiState;
      bool previewOverlayPresented = false;
      std::optional<std::chrono::steady_clock::time_point> previewQueuedAt;
      if (args.enableUi) {
        lv_init();
#if defined(ARDOR_UI_BACKEND_FBDEV)
        {
          lv_display_t* disp = lv_linux_fbdev_create();
          lv_linux_fbdev_set_file(disp, "/dev/fb0");
          // Panel is 720x1280 portrait; present the UI as 1280x720 landscape.
          // The display is mounted upside down, so use the opposite landscape
          // rotation from the usual 90° orientation.
          lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);

          // Touch: Goodix reports raw coords matching the panel's PHYSICAL
          // portrait orientation (x:0..719, y:0..1279). LVGL itself rotates the
          // pointer to the logical landscape frame (lv_display_rotate_point,
          // driven by the display rotation set above), so evdev must emit
          // *unrotated physical* coords — do NOT swap or pre-rotate here, or the
          // rotation is applied twice. Raw already equals physical 1:1, verified
          // by capturing swipes: horizontal motion drives raw Y, vertical drives
          // raw X, both straight through. This remains true at 270°: LVGL
          // applies the inverse transform for the selected display rotation.
          // Calibration just pins the ranges.
          // Set ARDOR_TOUCH_DEBUG=1 to overlay the live coordinate for tuning.
          const std::string touchDev = findTouchDevice();
          if (!touchDev.empty()) {
            lv_indev_t* touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, touchDev.c_str());
            lv_indev_set_display(touch, disp);
            lv_evdev_set_swap_axes(touch, false);
            lv_evdev_set_calibration(touch, /*min_x*/ 0, /*min_y*/ 0,
                                            /*max_x*/ 719, /*max_y*/ 1279);
            if (std::getenv("ARDOR_TOUCH_DEBUG") != nullptr) {
              installTouchDebug(touch);
            }
          } else {
            std::cerr << "Warning: no touchscreen (Goodix) found under /dev/input\n";
          }
        }
#else
        lv_sdl_window_create(800, 480);
        lv_sdl_mouse_create();
        lv_sdl_mousewheel_create();
        lv_sdl_keyboard_create();
#endif
        uiState = ardor::makeDemoUiState();
        ardor::loadAssetsFromDataRoot(uiState, args.dataRoot);
        ardor::loadBankFromStore(uiState, store, args.bank);
        // The engine was loaded immediately above. Reflect that state in the
        // UI without queuing the same preset for another audio-engine swap.
        ardor::synchronizePresetSelection(uiState, static_cast<std::size_t>(args.slot));
        ui = std::make_unique<ardor::LvglUi>(ardor::UiActions{
          [&](std::size_t index) {
            if (ardor::requestPresetNavigation(uiState, {args.bank, index})) {
              requestedSlot.store(static_cast<int>(index), std::memory_order_relaxed);
            }
          },
          [&]() {
            std::string saveError;
            if (!ardor::previewIsSynchronized(uiState)) {
              ardor::setUiStatus(uiState, "Effect chain is still applying", true);
              return;
            }
            if (!ardor::saveActivePresetToStore(uiState, store, args.bank, saveError)) {
              std::cerr << saveError << "\n";
              ardor::setUiStatus(uiState, "Save failed: " + saveError, true);
            } else {
              ardor::setUiStatus(uiState, "Preset saved");
            }
          },
          [&](const std::string& blockId, std::size_t bandIndex, const ardor::EqBandParams& params) {
            if (!liveEngine->setParametricEqBand(blockId, bandIndex, params)) {
              std::cerr << "Unable to update EQ band for block " << blockId << "\n";
              return false;
            }
            return true;
          },
          [&](const std::string& blockId, const std::string& key, float normalized) {
            if (!liveEngine->setDaisyParameter(blockId, key, normalized)) {
              std::cerr << "Unable to update Daisy parameter " << blockId << ":" << key << "\n";
              return false;
            }
            return true;
          },
          [&](const std::string& blockId, const std::string& key, float value) {
            if (!liveEngine->setCompressorParameter(blockId, key, value)) {
              std::cerr << "Unable to update compressor parameter " << blockId << ":" << key << "\n";
              return false;
            }
            return true;
          },
          [&](float inputGainDb, float outputGainDb) {
            liveEngine->setInputGain(ardor::dbToGain(inputGainDb));
            liveEngine->setOutputGain(ardor::dbToGain(outputGainDb));
          },
          [&](float levelDb, float mix) {
            liveEngine->setCabLevel(ardor::dbToGain(levelDb));
            liveEngine->setCabMix(mix);
          },
          [&](int delta) {
            const int pending = requestedBank.load(std::memory_order_relaxed);
            const int current = pending >= 0 ? pending : args.bank;
            const int target = std::clamp(current + delta, 0, 99);
            if (target != current
                && ardor::requestPresetNavigation(uiState,
                                                   {target, static_cast<std::size_t>(args.slot)})) {
              requestedBank.store(target, std::memory_order_relaxed);
              requestedSlot.store(args.slot, std::memory_order_relaxed);
            }
          },
          [&](ardor::UiNavigationDecision decision) {
            if (decision == ardor::UiNavigationDecision::Cancel) {
              ardor::confirmNavigation(uiState, decision);
              return;
            }
            if (decision == ardor::UiNavigationDecision::Save) {
              std::string saveError;
              if (!ardor::saveActivePresetToStore(uiState, store, args.bank, saveError)) {
                ardor::setUiStatus(uiState, "Save failed: " + saveError, true);
                return;
              }
            }
            const auto target = ardor::confirmNavigation(uiState, decision);
            if (!target.has_value()) return;
            requestedBank.store(target->bank, std::memory_order_relaxed);
            requestedSlot.store(static_cast<int>(target->preset), std::memory_order_relaxed);
          },
        });
        ui->build(lv_screen_active(), uiState);
      }
#endif

      ardor::RuntimeState runtime;
      uint64_t previousCallbacks = 0;
      uint64_t previousOverBudget = 0;
      uint64_t previousNonFiniteBlocks = 0;
#if defined(__linux__)
      std::vector<ardor::LinuxInputDevice> inputDevices;
      inputDevices.resize(args.controlDevices.size());
      for (std::size_t i = 0; i < args.controlDevices.size(); ++i) {
        std::string error;
        if (!inputDevices[i].open(args.controlDevices[i], error)) {
          std::cerr << "Failed to open control device " << args.controlDevices[i] << ": " << error << "\n";
          return 1;
        }
      }
#else
      if (!args.controlDevices.empty()) {
        std::cerr << "--control-device is only supported on Linux\n";
        return 1;
      }
#endif
      auto nextRuntimeCommandPoll = std::chrono::steady_clock::now();
      auto nextTelemetry = nextRuntimeCommandPoll;
      const auto applyFootswitchAction = [&](const ardor::FootswitchAction& action) {
#if defined(ARDOR_HAS_UI)
        if (args.enableUi && ui && !ardor::previewIsSynchronized(uiState)) {
          // The physical master-volume path remains independent below, but a
          // local chain transaction must not be superseded by a footswitch
          // preset change or tuner entry.
          return;
        }
#endif
        if (action.type == ardor::FootswitchActionType::ToggleTuner) {
          tunerMode = !tunerMode;
          backend.setOutputMuted(tunerMode);
          backend.discardCapturedInput();
          tuner.reset();
          lastTunerRevision = 0;
#if defined(ARDOR_HAS_UI)
          if (args.enableUi && ui) {
            if (tunerMode) ardor::enterTunerMode(uiState);
            else ardor::enterPresetMode(uiState);
          }
#endif
          std::cerr << (tunerMode ? "Tuner active; output muted\n" : "Tuner closed; output restored\n");
          if (!tunerMode && deferredTunerSlot >= 0) {
            if (deferredTunerBank >= 0) {
              requestedBank.store(deferredTunerBank, std::memory_order_relaxed);
            }
            requestedSlot.store(deferredTunerSlot, std::memory_order_relaxed);
            deferredTunerBank = -1;
            deferredTunerSlot = -1;
          }
          return;
        }
        if (tunerMode) return;
        const int previousSlot = controls.activeSlot;
        if (ardor::applyControlEvent(
              controls, {ardor::ControlEventType::FootswitchPressed, action.index, 0})
            && controls.activeSlot != previousSlot) {
#if defined(ARDOR_HAS_UI)
          if (args.enableUi && ui) {
            if (ardor::requestPresetNavigation(
                  uiState, {args.bank, static_cast<std::size_t>(controls.activeSlot)})) {
              requestedSlot.store(controls.activeSlot, std::memory_order_relaxed);
            } else {
              // A dirty draft opened the confirmation prompt (or navigation
              // was otherwise unavailable). Keep the hardware selection in
              // sync with the still-audible preset until a decision commits.
              controls.activeSlot = previousSlot;
            }
          } else {
            requestedSlot.store(controls.activeSlot, std::memory_order_relaxed);
          }
#else
          requestedSlot.store(controls.activeSlot, std::memory_order_relaxed);
#endif
        }
      };
      while (running) {
#if defined(ARDOR_HAS_UI)
        if (args.enableUi && ui) {
          lv_timer_handler();
          ui->refresh(lv_screen_active(), uiState);
          lv_delay_ms(5);
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
#endif
        if (backend.deviceStopped()) {
          const bool startingRecovery = !deviceRecovery.pending();
          const auto recoveryResult = deviceRecovery.service(backend, *liveEngine, options);
#if defined(ARDOR_HAS_UI)
          if (args.enableUi && ui && startingRecovery) {
            ardor::setUiStatus(uiState, "Audio device lost; reconnecting", true);
          }
#endif
          if (recoveryResult == DeviceRecoveryResult::Failed) {
            std::cerr << "Audio-device restart failed after bounded retries\n";
#if defined(ARDOR_HAS_UI)
            if (args.enableUi && ui) {
              ardor::setUiStatus(uiState, "Audio restart failed", true);
            }
#endif
            return 1;
          }
          if (recoveryResult == DeviceRecoveryResult::Restarted) {
            previousCallbacks = 0;
            previousOverBudget = 0;
            backend.setOutputMuted(tunerMode);
            backend.discardCapturedInput();
            tuner.reset();
            lastTunerRevision = 0;
#if defined(ARDOR_HAS_UI)
            if (args.enableUi && ui) {
              ardor::setUiStatus(uiState, "Audio device reconnected");
            }
#endif
          } else {
            continue;
          }
        }
#if defined(__linux__)
        for (auto& inputDevice : inputDevices) {
          ardor::ControlEvent controlEvent;
          while (inputDevice.poll(controlEvent)) {
            if (controlEvent.type == ardor::ControlEventType::FootswitchPressed
                || controlEvent.type == ardor::ControlEventType::FootswitchReleased) {
              if (tunerMode && controlEvent.type == ardor::ControlEventType::FootswitchPressed) {
                applyFootswitchAction({ardor::FootswitchActionType::ToggleTuner, 0});
                // The release belonging to this exit press must not turn into
                // a delayed preset selection.
                footswitchGesture.reset();
                continue;
              }
              if (const auto action = footswitchGesture.handle(
                    controlEvent, ardor::FootswitchGesture::Clock::now())) {
                applyFootswitchAction(*action);
              }
              continue;
            }
#if defined(ARDOR_HAS_UI)
            if (args.enableUi && ui && controlEvent.type == ardor::ControlEventType::EncoderTurned
                && ardor::previewIsSynchronized(uiState)
                && ui->applyFocusedParameterDelta(uiState, controlEvent.delta)) {
              const auto& global = uiState.bank.presets[uiState.activePreset].global;
              liveEngine->setInputGain(ardor::dbToGain(global.inputGainDb));
              liveEngine->setOutputGain(ardor::dbToGain(global.outputGainDb));
              continue;
            }
#endif
            const int previousSlot = controls.activeSlot;
            const int previousVolume = controls.masterVolume;
            if (!ardor::applyControlEvent(controls, controlEvent)) {
              continue;
            }
            if (controls.activeSlot != previousSlot) {
              requestedSlot.store(controls.activeSlot, std::memory_order_relaxed);
            }
            if (controls.masterVolume != previousVolume) {
              liveEngine->setMasterVolume(static_cast<float>(controls.masterVolume) / 100.0f);
              std::cerr << "Master volume " << controls.masterVolume << "%\n";
            }
          }
        }
        if (const auto action = footswitchGesture.poll(ardor::FootswitchGesture::Clock::now())) {
          applyFootswitchAction(*action);
        }
#endif
        if (tunerMode) {
          for (;;) {
            const std::size_t captured = backend.readCapturedInput(tunerInput.data(), tunerInput.size());
            if (captured == 0) break;
            tuner.process(tunerInput.data(), captured);
            if (captured < tunerInput.size()) break;
          }
          const auto& reading = tuner.reading();
          if (reading.revision != lastTunerRevision) {
            lastTunerRevision = reading.revision;
#if defined(ARDOR_HAS_UI)
            if (args.enableUi && ui) {
              ardor::updateTunerTelemetry(uiState, {
                reading.signalDetected, reading.frequencyHz, reading.cents,
                reading.confidence, reading.note, reading.octave,
              });
            }
#endif
          }
        } else {
          backend.discardCapturedInput();
        }
#if defined(ARDOR_HAS_UI)
        if (args.enableUi && ui) {
          const int uiSlot = ardor::consumePendingSlotRequest(uiState);
          if (uiSlot >= 0) {
            requestedSlot.store(uiSlot, std::memory_order_relaxed);
          }
          if (uiState.masterVolume != controls.masterVolume) {
            ardor::setMasterVolume(uiState, controls.masterVolume);
          }
          // Queuing happens in the LVGL handler. This separate control-loop
          // phase guarantees the loading overlay gets a refresh before the
          // synchronous engine preparation starts.
          if (uiState.previewState == ardor::UiPreviewState::Queued && !previewOverlayPresented) {
            // ui->refresh() above has made the overlay part of the retained
            // scene. Force that state through the display driver before the
            // synchronous preparation can freeze LVGL's timer loop.
            lv_refr_now(nullptr);
            previewOverlayPresented = true;
            previewQueuedAt = std::chrono::steady_clock::now();
          } else if (ardor::beginApplyingPreview(uiState)) {
            previewOverlayPresented = false;
            const auto operation = uiState.previewTransaction->operation;
            const auto blockCount = uiState.bank.presets[uiState.activePreset].blocks.size();
            const auto preparationStarted = std::chrono::steady_clock::now();
            std::optional<std::chrono::steady_clock::time_point> activationStarted;
            const auto previewPreset = ardor::activePresetToPreset(uiState);
            const auto activation = ardor::prepareAndActivateDraft(
              liveEngine, previewPreset, args.dataRoot,
              loadOptions, static_cast<float>(controls.masterVolume) / 100.0f,
              [&](ardor::PedalEngine& prepared) {
                activationStarted = std::chrono::steady_clock::now();
                return backend.replaceEngine(prepared);
              });
            const auto completedAt = std::chrono::steady_clock::now();
            const auto preparationMs = std::chrono::duration<double, std::milli>(
              (activationStarted.has_value() ? *activationStarted : completedAt) - preparationStarted).count();
            const auto activationMs = activationStarted.has_value()
              ? std::chrono::duration<double, std::milli>(completedAt - *activationStarted).count() : 0.0;
            const auto totalMs = previewQueuedAt.has_value()
              ? std::chrono::duration<double, std::milli>(completedAt - *previewQueuedAt).count()
              : preparationMs + activationMs;
            std::cerr << std::fixed << std::setprecision(1)
                      << "Live chain preview operation='" << operation << "' bank=" << activeSelection.bank
                      << " slot=" << activeSelection.slot << " blocks=" << blockCount
                      << " prepare_ms=" << preparationMs << " activate_ms=" << activationMs
                      << " total_ms=" << totalMs << " result="
                      << (activation.activated() ? "activated" : "rejected") << "\n" << std::defaultfloat;
            if (totalMs > 500.0) {
              std::cerr << "Slow live chain preview: " << totalMs << "ms operation='" << operation << "'\n";
            }
            previewQueuedAt.reset();
            if (activation.activated()) {
              runtime.changePreset();
              liveEngine->setEffectsBypassed(runtime.effectsBypassed());
              auto telemetry = uiState.telemetry;
              telemetry.bypassed = runtime.effectsBypassed();
              ardor::updateRealtimeTelemetry(uiState, telemetry);
              ardor::completeStructuralPreview(uiState);
            } else {
              const std::string reason = activation.error.empty()
                ? replaceResultName(activation.replacementResult) : activation.error;
              std::cerr << "Live chain preview failed: " << reason << "\n";
              ardor::failStructuralPreview(uiState, reason);
            }
          } else if (uiState.previewState == ardor::UiPreviewState::Synchronized) {
            previewOverlayPresented = false;
            previewQueuedAt.reset();
          }
        }
#endif
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextRuntimeCommandPoll) {
          nextRuntimeCommandPoll = now + std::chrono::milliseconds(100);
          bool reloadAssets = false;
          for (const auto& command : ardor::consumeRuntimeCommands(args.dataRoot)) {
            if (command.type == ardor::RuntimeCommandType::ReloadAssets) {
              reloadAssets = true;
            } else if (command.type == ardor::RuntimeCommandType::ApplyPreset) {
              requestedBank.store(command.bank, std::memory_order_relaxed);
              requestedSlot.store(command.slot, std::memory_order_relaxed);
            }
          }
#if defined(ARDOR_HAS_UI)
          if (reloadAssets && args.enableUi && ui) {
            ardor::loadAssetsFromDataRoot(uiState, args.dataRoot);
            ardor::setUiStatus(uiState, "Assets reloaded");
          }
#endif
        }
        const int nextBank = requestedBank.exchange(-1, std::memory_order_relaxed);
        const int nextSlot = requestedSlot.exchange(-1, std::memory_order_relaxed);
        if (nextSlot >= 0) {
          const int targetBank = nextBank >= 0 ? nextBank : args.bank;
          if (tunerMode) {
            deferredTunerBank = targetBank;
            deferredTunerSlot = nextSlot;
            continue;
          }
          ardor::Preset targetPreset;
          try {
            targetPreset = store.loadOrEmpty({targetBank, nextSlot});
          } catch (const std::exception& e) {
            std::cerr << "Preset switch rejected before activation: " << e.what() << "\n";
#if defined(ARDOR_HAS_UI)
            if (args.enableUi && ui) {
              if (uiState.dirty) {
                ardor::setUiStatus(uiState, "Could not switch preset; current edits retained.", true);
              } else {
                ardor::loadBankFromStore(uiState, store, args.bank);
                ardor::synchronizePresetSelection(uiState, static_cast<std::size_t>(args.slot));
                ardor::setUiStatus(uiState, "Preset load failed: " + std::string{e.what()}, true);
              }
            }
#endif
            continue;
          }
          std::string preflightError;
          if (!ardor::preflightPreset(targetPreset, args.dataRoot, loadOptions, preflightError)) {
            std::cerr << "Preset switch rejected before activation: " << preflightError << "\n";
#if defined(ARDOR_HAS_UI)
            if (args.enableUi && ui) {
              if (uiState.dirty) {
                ardor::setUiStatus(uiState, "Could not switch preset; current edits retained.", true);
              } else {
                ardor::loadBankFromStore(uiState, store, args.bank);
                ardor::synchronizePresetSelection(uiState, static_cast<std::size_t>(args.slot));
                ardor::setUiStatus(uiState, "Preset load failed: " + preflightError, true);
              }
            }
#endif
            continue;
          }
          const auto activation = ardor::prepareAndActivatePreset(
            liveEngine, activeSelection, targetPreset, {targetBank, nextSlot}, args.dataRoot,
            loadOptions, static_cast<float>(controls.masterVolume) / 100.0f,
            [&](ardor::PedalEngine& prepared) { return backend.replaceEngine(prepared); });
          if (!activation.activated()) {
            if (activation.status == ardor::PresetActivationStatus::PreparationFailed) {
              std::cerr << "Preset switch failed: " << activation.error << "\n";
#if defined(ARDOR_HAS_UI)
              if (args.enableUi && ui) {
                if (uiState.dirty) {
                  ardor::setUiStatus(uiState, "Could not switch preset; current edits retained.", true);
                } else {
                  ardor::loadBankFromStore(uiState, store, args.bank);
                  ardor::synchronizePresetSelection(uiState, static_cast<std::size_t>(args.slot));
                  ardor::setUiStatus(uiState, "Preset load failed: " + activation.error, true);
                }
              }
#endif
              continue;
            }
            std::cerr << "Failed to activate prepared preset: "
                      << replaceResultName(activation.replacementResult) << "\n";
            if (activation.replacementResult == ardor::EngineReplaceResult::DeviceStopped) {
              requestedBank.store(targetBank, std::memory_order_relaxed);
              requestedSlot.store(nextSlot, std::memory_order_relaxed);
              continue;
            }
            return 1;
          }
          runtime.changePreset();
          liveEngine->setEffectsBypassed(runtime.effectsBypassed());
          args.bank = activeSelection.bank;
          args.slot = activeSelection.slot;
          controls.activeSlot = activeSelection.slot;
          std::cerr << "Switched to preset " << args.bank << ":" << args.slot << "\n";
#if defined(ARDOR_HAS_UI)
          if (args.enableUi && ui) {
            ardor::loadBankFromStore(uiState, store, args.bank);
            ardor::synchronizePresetSelection(uiState, static_cast<std::size_t>(args.slot));
            auto telemetry = uiState.telemetry;
            telemetry.bypassed = runtime.effectsBypassed();
            ardor::updateRealtimeTelemetry(uiState, telemetry);
            ardor::setUiStatus(uiState, "Preset " + std::to_string(args.bank) + ":"
                                      + std::to_string(args.slot) + " active");
          }
#endif
          continue;
        }
        if (now >= nextTelemetry) {
          nextTelemetry = now + std::chrono::seconds(1);
          const auto stats = backend.stats();
          const auto nonFiniteBlocks = liveEngine->nonFiniteBlockCount();
          if (nonFiniteBlocks != previousNonFiniteBlocks) {
            previousNonFiniteBlocks = nonFiniteBlocks;
            const auto blockId = liveEngine->firstNonFiniteBlockId();
            const std::string message = "DSP fault: " + (blockId.empty() ? std::string{"unnamed block"} : blockId);
            std::cerr << message << "\n";
            // Prepare and swap a clean program from the control thread. Never
            // reset a potentially large effect state from the audio callback.
            requestedBank.store(args.bank, std::memory_order_relaxed);
            requestedSlot.store(args.slot, std::memory_order_relaxed);
#if defined(ARDOR_HAS_UI)
            if (args.enableUi && ui) {
              ardor::setUiStatus(uiState, message, true);
            }
#endif
          }
          runtime.observeRealtimeStats(previousCallbacks, stats.callbacks, previousOverBudget, stats.overBudget);
          previousCallbacks = stats.callbacks;
          previousOverBudget = stats.overBudget;
          liveEngine->setEffectsBypassed(runtime.effectsBypassed());
          const auto telemetry = ardor::makeRuntimeTelemetry(stats.callbacks, stats.overBudget, stats.callbackGaps,
                                                             stats.maxMs, stats.averageMs, stats.budgetMs,
                                                             runtime.effectsBypassed());
          std::cerr << ardor::formatRuntimeTelemetry(telemetry) << "\n";
          if (args.clipDebug) {
            const auto diagnostics = liveEngine->takeClipDiagnostics();
            std::cerr << ardor::formatClipDiagnostics(diagnostics) << "\n";
#if defined(ARDOR_HAS_UI)
            if (args.enableUi && ui) {
              ardor::updateClipDebugTelemetry(uiState, makeUiClipDebugTelemetry(diagnostics));
            }
#endif
          }
#if defined(ARDOR_HAS_UI)
          if (args.enableUi && ui) {
            ardor::updateRealtimeTelemetry(uiState, telemetry);
          }
#endif
        }
      }
      backend.stop();
      return 0;
    }

    // All other paths use a stack engine
    ardor::PedalEngine engine;
    if (args.presetSlotMode) {
      std::string error;
      ardor::PresetStore store(args.dataRoot);
      if (!ardor::applyPresetSlot(engine, store, {args.bank, args.slot}, args.dataRoot, loadOptions, error)) {
        std::cerr << error << "\n";
        return 1;
      }
    } else if (!args.preset.empty()) {
      std::string error;
      std::ifstream in(args.preset);
      if (!in) {
        std::cerr << "failed to open preset: " << args.preset << "\n";
        return 1;
      }
      nlohmann::json json;
      in >> json;
      if (!ardor::applyPreset(engine, ardor::presetFromJson(json), args.dataRoot, loadOptions, error)) {
        std::cerr << error << "\n";
        return 1;
      }
    } else {
      auto irWav = ardor::readMonoWav(args.ir);
      const ma_uint32 irRate = irWav.sampleRate;
      if (irRate != args.sampleRate) {
        std::cerr << "Expected " << args.sampleRate << " Hz IR\n";
        return 1;
      }

      const size_t originalIrFrames = irWav.samples.size();
      std::string irError;
      if (!ardor::prepareMonoIr(irWav, args.irSamples, irError)) {
        std::cerr << "Invalid IR: " << irError << "\n";
        return 1;
      }
      if (irWav.samples.size() != originalIrFrames) {
        std::cerr << "Trimmed IR from " << originalIrFrames << " to " << irWav.samples.size() << " samples\n";
      }
      // RuntimeChain is serial, so preserve the conventional guitar path:
      // input -> NAM amp model -> cabinet IR. The older fixed pipeline did not
      // depend on load order, but the serial runner does.
      if (!args.bypassNam
          && !engine.loadNam(args.model, args.sampleRate, static_cast<int>(args.blockSize))) {
        std::cerr << "Failed to load NAM model\n";
        return 1;
      }
      engine.loadIr(std::move(irWav.samples));
      engine.setInputGain(dbToGain(args.inputGainDb));
      engine.setOutputGain(dbToGain(args.outputGainDb));
      engine.setSafetyLimiterEnabled(args.safetyLimiter);
      engine.setSafetyLimit(dbToGain(args.safetyLimitDb));
      std::cerr << "Gains: input " << args.inputGainDb << " dB, output " << args.outputGainDb << " dB"
                << ", safety ";
      if (args.safetyLimiter) {
        std::cerr << args.safetyLimitDb << " dB\n";
      } else {
        std::cerr << "off\n";
      }
    }

    if (args.realtime) {
      ardor::RealtimeOptions options;
      options.sampleRate = args.sampleRate;
      options.blockSize = args.blockSize;
      options.captureDeviceIndex = args.captureDeviceIndex;
      options.playbackDeviceIndex = args.playbackDeviceIndex;
      options.inputChannel = args.inputChannel;
      options.outputChannel = args.outputChannel;
#if defined(__linux__)
      options.requireRealtimeScheduler = !args.allowNonRealtime;
      options.requireNativeSampleRate = !args.allowDeviceResampling;
#endif

#if defined(__linux__)
      if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "mlockall failed (running without locked memory): " << std::strerror(errno) << "\n";
      }
#endif
      ardor::MiniaudioBackend backend;
      if (!backend.start(engine, options)) {
        std::cerr << "Failed to start realtime audio\n";
        return 1;
      }

      DeviceRecoveryController deviceRecovery;
      ardor::RuntimeState runtime;
      uint64_t previousCallbacks = 0;
      uint64_t previousOverBudget = 0;
      uint64_t previousNonFiniteBlocks = 0;
      auto nextTelemetry = std::chrono::steady_clock::now();
      while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (backend.deviceStopped()) {
          const auto recoveryResult = deviceRecovery.service(backend, engine, options);
          if (recoveryResult == DeviceRecoveryResult::Failed) {
            std::cerr << "Audio-device restart failed after bounded retries\n";
            return 1;
          }
          if (recoveryResult == DeviceRecoveryResult::Restarted) {
            previousCallbacks = 0;
            previousOverBudget = 0;
          } else {
            continue;
          }
        }
        const auto now = std::chrono::steady_clock::now();
        if (now < nextTelemetry) continue;
        nextTelemetry = now + std::chrono::seconds(1);
        const auto stats = backend.stats();
        const auto nonFiniteBlocks = engine.nonFiniteBlockCount();
        if (nonFiniteBlocks != previousNonFiniteBlocks) {
          previousNonFiniteBlocks = nonFiniteBlocks;
          const auto blockId = engine.firstNonFiniteBlockId();
          std::cerr << "DSP fault: " << (blockId.empty() ? "unnamed block" : blockId) << "\n";
        }
        runtime.observeRealtimeStats(previousCallbacks, stats.callbacks, previousOverBudget, stats.overBudget);
        previousCallbacks = stats.callbacks;
        previousOverBudget = stats.overBudget;
        engine.setEffectsBypassed(runtime.effectsBypassed());
        const auto telemetry = ardor::makeRuntimeTelemetry(stats.callbacks, stats.overBudget, stats.callbackGaps,
                                                           stats.maxMs, stats.averageMs, stats.budgetMs,
                                                           runtime.effectsBypassed());
        std::cerr << ardor::formatRuntimeTelemetry(telemetry) << "\n";
        if (args.clipDebug) {
          std::cerr << ardor::formatClipDiagnostics(engine.takeClipDiagnostics()) << "\n";
        }
      }
      backend.stop();
      return 0;
    }

    auto inputWav = ardor::readMonoWav(args.input);
    auto input = std::move(inputWav.samples);
    const ma_uint32 inputRate = inputWav.sampleRate;
    if (inputRate != args.sampleRate) {
      std::cerr << "Expected " << args.sampleRate << " Hz input\n";
      return 1;
    }

    const size_t requestedTailFrames = args.noTail ? 0
      : args.tailSeconds >= 0.0f ? static_cast<size_t>(std::llround(args.tailSeconds * args.sampleRate))
                                 : engine.tailFrames();
    std::vector<float> out;
    out.reserve((input.size() + requestedTailFrames) * 2);
    std::vector<float> inBlock(args.blockSize, 0.0f);
    std::vector<float> leftBlock(args.blockSize, 0.0f);
    std::vector<float> rightBlock(args.blockSize, 0.0f);
    size_t remainingTailFrames = requestedTailFrames;
    auto appendOutput = [&](size_t frames) {
      for (size_t i = 0; i < frames; ++i) {
        out.push_back(leftBlock[i]);
        out.push_back(rightBlock[i]);
      }
    };
    for (size_t offset = 0; offset < input.size(); offset += args.blockSize) {
      const size_t frames = std::min<size_t>(args.blockSize, input.size() - offset);
      std::fill(inBlock.begin(), inBlock.end(), 0.0f);
      std::copy(input.begin() + static_cast<std::ptrdiff_t>(offset),
                input.begin() + static_cast<std::ptrdiff_t>(offset + frames),
                inBlock.begin());
      engine.processBlock(inBlock.data(), leftBlock.data(), rightBlock.data(), args.blockSize);
      appendOutput(frames);
      if (offset + frames == input.size() && remainingTailFrames > 0) {
        const size_t paddedFrames = args.blockSize - frames;
        const size_t retainedPadding = std::min(paddedFrames, remainingTailFrames);
        if (retainedPadding > 0) {
          const size_t start = frames;
          for (size_t i = 0; i < retainedPadding; ++i) {
            out.push_back(leftBlock[start + i]);
            out.push_back(rightBlock[start + i]);
          }
          remainingTailFrames -= retainedPadding;
        }
      }
    }
    std::fill(inBlock.begin(), inBlock.end(), 0.0f);
    while (remainingTailFrames > 0) {
      engine.processBlock(inBlock.data(), leftBlock.data(), rightBlock.data(), args.blockSize);
      const size_t frames = std::min<size_t>(args.blockSize, remainingTailFrames);
      appendOutput(frames);
      remainingTailFrames -= frames;
    }

    if (args.clipDebug) {
      std::cerr << ardor::formatClipDiagnostics(engine.takeClipDiagnostics()) << "\n";
    }
    writeStereo(args.output, out, inputRate);
    std::cerr << "Wrote " << args.output << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
