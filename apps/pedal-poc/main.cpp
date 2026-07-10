#if defined(ARDOR_HAS_UI) && !defined(ARDOR_UI_BACKEND_FBDEV)
#define SDL_MAIN_HANDLED
#endif

#include "miniaudio.h"

#include "audio/EngineLoader.h"
#include "audio/MiniaudioBackend.h"
#include "control/ControlEvents.h"
#if defined(__linux__)
#include "control/LinuxInput.h"
#endif
#include "audio/WavIo.h"
#include "dsp/PedalEngine.h"
#include "preset/ChainPlan.h"
#include "preset/Preset.h"
#include "preset/PresetStore.h"
#include "preset/RuntimeState.h"

#include <algorithm>
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

void handleSignal(int)
{
  running = 0;
}

#if defined(ARDOR_HAS_UI) && defined(ARDOR_UI_BACKEND_FBDEV)
// Locate the touchscreen evdev node by device name. On the Touch Display 2 the
// controller enumerates as "Goodix Capacitive TouchScreen"; the event number is
// not stable across boots, so match on name rather than hardcoding event0.
std::string findTouchDevice()
{
  namespace fs = std::filesystem;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator{"/sys/class/input", ec}) {
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
  bool bypassNam = false;
  bool devices = false;
  uint32_t sampleRate = 48000;
  uint32_t blockSize = 64;
  size_t irSamples = 0;
  float inputGainDb = 0.0f;
  float outputGainDb = 0.0f;
  float safetyLimitDb = -1.0f;
  bool safetyLimiter = true;
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
                << "  pedal-poc --realtime --preset preset.json --data-root data [--sample-rate 48000] [--block-size 64]\n"
                << "  pedal-poc --realtime --data-root data --bank 0 --slot 0 [--sample-rate 48000] [--block-size 64]\n"
                << "  pedal-poc --realtime --model amp.nam --ir cab.wav [--sample-rate 48000] [--block-size 64]\n"
                << "            [--capture-device N] [--playback-device N] [--input-channel left|right]\n"
                << "            [--output-channel both|left|right] [--ir-samples N]\n"
                << "            [--input-gain-db DB] [--output-gain-db DB]\n"
                << "            [--safety-limit-db DB] [--no-safety-limit]\n"
                << "            [--control-device /dev/input/eventX]...\n"
                << "            [--ui]\n";
      return 2;
    }

    if (args.devices) {
      return printDevices();
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

      std::atomic<int> requestedSlot{-1};
      std::thread controlThread([&]() {
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
      if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "mlockall failed (running without locked memory): " << std::strerror(errno) << "\n";
      }
#endif
      ardor::MiniaudioBackend backend;
      if (!backend.start(*liveEngine, options)) {
        std::cerr << "Failed to start realtime audio\n";
        return 1;
      }

      ardor::ControlState controls{args.slot, 80};
      liveEngine->setMasterVolume(static_cast<float>(controls.masterVolume) / 100.0f);

#if defined(ARDOR_HAS_UI)
      std::unique_ptr<ardor::LvglUi> ui;
      ardor::UiState uiState;
      if (args.enableUi) {
        lv_init();
#if defined(ARDOR_UI_BACKEND_FBDEV)
        {
          lv_display_t* disp = lv_linux_fbdev_create();
          lv_linux_fbdev_set_file(disp, "/dev/fb0");
          // Panel is 720x1280 portrait; present the UI as 1280x720 landscape
          lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_90);

          // Touch: Goodix reports raw coords matching the panel's PHYSICAL
          // portrait orientation (x:0..719, y:0..1279). LVGL itself rotates the
          // pointer to the logical landscape frame (lv_display_rotate_point,
          // driven by the display rotation set above), so evdev must emit
          // *unrotated physical* coords — do NOT swap or pre-rotate here, or the
          // rotation is applied twice. Raw already equals physical 1:1, verified
          // by capturing swipes: horizontal motion drives raw Y, vertical drives
          // raw X, both straight through. Calibration just pins the ranges.
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
        ardor::selectPreset(uiState, static_cast<std::size_t>(args.slot));
        ui = std::make_unique<ardor::LvglUi>(ardor::UiActions{
          [&](std::size_t index) {
            requestedSlot.store(static_cast<int>(index), std::memory_order_relaxed);
            ardor::selectPreset(uiState, index);
          },
          [&]() {
            std::string saveError;
            if (!ardor::saveActivePresetToStore(uiState, store, args.bank, saveError)) {
              std::cerr << saveError << "\n";
            } else {
              requestedSlot.store(static_cast<int>(uiState.activePreset), std::memory_order_relaxed);
            }
          },
        });
        ui->build(lv_screen_active(), uiState);
      }
#endif

      ardor::RuntimeState runtime;
      uint64_t previousCallbacks = 0;
      uint64_t previousOverBudget = 0;
      std::signal(SIGINT, handleSignal);
      std::signal(SIGTERM, handleSignal);
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
      int tickCount = 0;
      while (running) {
#if defined(ARDOR_HAS_UI)
        if (args.enableUi && ui) {
          lv_timer_handler();
          ui->refresh(lv_screen_active(), uiState);
          lv_delay_ms(5);
          if (++tickCount < 200) continue;
          tickCount = 0;
        } else {
          std::this_thread::sleep_for(std::chrono::seconds(1));
        }
#else
        std::this_thread::sleep_for(std::chrono::seconds(1));
#endif
#if defined(__linux__)
        for (auto& inputDevice : inputDevices) {
          ardor::ControlEvent controlEvent;
          while (inputDevice.poll(controlEvent)) {
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
#endif
#if defined(ARDOR_HAS_UI)
        if (args.enableUi && ui) {
          const int uiSlot = ardor::consumePendingSlotRequest(uiState);
          if (uiSlot >= 0) {
            requestedSlot.store(uiSlot, std::memory_order_relaxed);
          }
          uiState.masterVolume = controls.masterVolume;
        }
#endif
        const int nextSlot = requestedSlot.exchange(-1, std::memory_order_relaxed);
        if (nextSlot >= 0 && nextSlot != args.slot) {
          auto nextEngine = std::make_unique<ardor::PedalEngine>();
          std::string swapError;
          if (!ardor::applyPresetSlot(*nextEngine, store, {args.bank, nextSlot}, args.dataRoot, loadOptions, swapError)) {
            std::cerr << "Preset switch failed: " << swapError << "\n";
          } else {
            backend.stop();
            liveEngine = std::move(nextEngine);
            liveEngine->reset();
            liveEngine->setMasterVolume(static_cast<float>(controls.masterVolume) / 100.0f);
            if (!backend.start(*liveEngine, options)) {
              std::cerr << "Failed to restart realtime audio\n";
              return 1;
            }
            args.slot = nextSlot;
            previousOverBudget = 0;
            std::cerr << "Switched to preset " << args.bank << ":" << args.slot << "\n";
#if defined(ARDOR_HAS_UI)
            if (args.enableUi && ui) {
              std::string uiLoadError;
              ardor::loadPresetSlotFromStore(uiState, store, {args.bank, args.slot}, uiLoadError);
            }
#endif
          }
        }
        const auto stats = backend.stats();
        runtime.observeRealtimeStats(previousCallbacks, stats.callbacks, previousOverBudget, stats.overBudget);
        previousCallbacks = stats.callbacks;
        previousOverBudget = stats.overBudget;
        liveEngine->setEffectsBypassed(runtime.effectsBypassed());
        const auto telemetry = ardor::makeRuntimeTelemetry(stats.callbacks, stats.overBudget, stats.maxMs,
                                                           stats.averageMs, stats.budgetMs,
                                                           runtime.effectsBypassed());
        std::cerr << ardor::formatRuntimeTelemetry(telemetry) << "\n";
#if defined(ARDOR_HAS_UI)
        if (args.enableUi && ui) {
          ardor::updateRealtimeTelemetry(uiState, telemetry);
        }
#endif
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
      auto impulse = std::move(irWav.samples);
      const ma_uint32 irRate = irWav.sampleRate;
      if (irRate != args.sampleRate) {
        std::cerr << "Expected " << args.sampleRate << " Hz IR\n";
        return 1;
      }

      if (args.realtime) {
        if (args.irSamples > 0 && impulse.size() > args.irSamples) {
          impulse.resize(args.irSamples);
          std::cerr << "Trimmed realtime IR to " << args.irSamples << " samples\n";
        }
      } else if (args.irSamples > 0 && impulse.size() > args.irSamples) {
        impulse.resize(args.irSamples);
        std::cerr << "Trimmed IR to " << args.irSamples << " samples\n";
      }
      engine.loadIr(std::move(impulse));
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
      if (args.preset.empty() && !engine.loadNam(args.model, args.sampleRate, static_cast<int>(args.blockSize))) {
        std::cerr << "Failed to load NAM model\n";
        return 1;
      }

      ardor::RealtimeOptions options;
      options.sampleRate = args.sampleRate;
      options.blockSize = args.blockSize;
      options.captureDeviceIndex = args.captureDeviceIndex;
      options.playbackDeviceIndex = args.playbackDeviceIndex;
      options.inputChannel = args.inputChannel;
      options.outputChannel = args.outputChannel;

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

      ardor::RuntimeState runtime;
      uint64_t previousCallbacks = 0;
      uint64_t previousOverBudget = 0;
      std::signal(SIGINT, handleSignal);
      std::signal(SIGTERM, handleSignal);
      while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto stats = backend.stats();
        runtime.observeRealtimeStats(previousCallbacks, stats.callbacks, previousOverBudget, stats.overBudget);
        previousCallbacks = stats.callbacks;
        previousOverBudget = stats.overBudget;
        engine.setEffectsBypassed(runtime.effectsBypassed());
        const auto telemetry = ardor::makeRuntimeTelemetry(stats.callbacks, stats.overBudget, stats.maxMs,
                                                           stats.averageMs, stats.budgetMs,
                                                           runtime.effectsBypassed());
        std::cerr << ardor::formatRuntimeTelemetry(telemetry) << "\n";
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

    if (args.preset.empty() && !args.presetSlotMode && !args.bypassNam && !engine.loadNam(args.model, args.sampleRate, 128)) {
      std::cerr << "Failed to load NAM model\n";
      return 1;
    }

    std::vector<float> out;
    out.reserve(input.size() * 2);
    std::vector<float> inBlock(args.blockSize, 0.0f);
    std::vector<float> leftBlock(args.blockSize, 0.0f);
    std::vector<float> rightBlock(args.blockSize, 0.0f);
    for (size_t offset = 0; offset < input.size(); offset += args.blockSize) {
      const size_t frames = std::min<size_t>(args.blockSize, input.size() - offset);
      std::fill(inBlock.begin(), inBlock.end(), 0.0f);
      std::copy(input.begin() + static_cast<std::ptrdiff_t>(offset),
                input.begin() + static_cast<std::ptrdiff_t>(offset + frames),
                inBlock.begin());
      engine.processBlock(inBlock.data(), leftBlock.data(), rightBlock.data(), args.blockSize);
      for (size_t i = 0; i < frames; ++i) {
        out.push_back(leftBlock[i]);
        out.push_back(rightBlock[i]);
      }
    }

    writeStereo(args.output, out, inputRate);
    std::cerr << "Wrote " << args.output << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
