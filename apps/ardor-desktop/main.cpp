#define SDL_MAIN_HANDLED

#include "DesktopPlatform.h"
#include "desktop/DesktopSession.h"
#include "ui/LampBlack.h"
#include "ui/LvglUi.h"

#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

namespace style = ardor::lvgl_ui;

struct Events {
  std::atomic<bool> closeRequested{false};
};

struct EventFilterGuard {
  ~EventFilterGuard() { SDL_SetEventFilter(nullptr, nullptr); }
};

int filterEvent(void* userdata, SDL_Event* event)
{
  auto& events = *static_cast<Events*>(userdata);
  if (event->type == SDL_QUIT || (event->type == SDL_WINDOWEVENT
      && event->window.event == SDL_WINDOWEVENT_CLOSE)) {
    // LVGL's SDL driver normally deletes the display or exits here. Retain it
    // until the main thread has handled unsaved edits and stopped audio.
    events.closeRequested.store(true);
    return 0;
  }
  return 1;
}

class DesktopWindow {
public:
  DesktopWindow(ardor::DesktopSession& session, lv_display_t* display)
    : session_(session), display_(display), ui_(actions())
  {
    group_ = lv_group_create();
    lv_group_set_default(group_);
    keyboard_ = lv_sdl_keyboard_create();
    lv_indev_set_group(keyboard_, group_);
    auto* screen = lv_screen_active();
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(screen, lv_color_hex(style::bg), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    toolbar_ = lv_obj_create(screen);
    style::styleSurface(toolbar_);
    lv_obj_set_style_radius(toolbar_, 0, 0);
    lv_obj_set_style_border_width(toolbar_, 0, 0);
    lv_obj_set_style_pad_all(toolbar_, 0, 0);
    lv_obj_remove_flag(toolbar_, LV_OBJ_FLAG_SCROLLABLE);
    message_ = lv_label_create(toolbar_);
    style::setText(message_);
    lv_label_set_long_mode(message_, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(message_, 16, 19);
    setup_ = button(toolbar_, "Audio setup", onSetup);
    start_ = button(toolbar_, "Start audio", onStart);
    content_ = lv_obj_create(screen);
    lv_obj_remove_style_all(content_);
    lv_obj_remove_flag(content_, LV_OBJ_FLAG_SCROLLABLE);
    resize();
    ui_.build(content_, session_.state());
    blocker_ = lv_obj_create(screen);
    lv_obj_remove_style_all(blocker_);
    lv_obj_set_style_bg_opa(blocker_, LV_OPA_30, 0);
    lv_obj_set_style_bg_color(blocker_, lv_color_black(), 0);
    lv_obj_add_flag(blocker_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(blocker_, LV_OBJ_FLAG_SCROLLABLE);
    resizeBlocker();
    update();
  }

  ~DesktopWindow()
  {
    closeDialog();
    lv_indev_set_group(keyboard_, nullptr);
    lv_group_set_default(nullptr);
    lv_group_delete(group_);
  }

  bool running() const { return running_; }

  void update()
  {
    const int width = lv_display_get_horizontal_resolution(display_);
    const int height = lv_display_get_vertical_resolution(display_);
    if (width != width_ || height != height_) {
      resize();
      ui_.build(content_, session_.state());
      resizeBlocker();
    }
    if (permissionPending_) {
      const auto permission = ardor::requestAudioInputPermission();
      if (permission != ardor::AudioInputPermission::Pending) {
        permissionPending_ = false;
        if (permission == ardor::AudioInputPermission::Available) session_.startAudio();
        else permissionError();
      }
    }
    ui_.refresh(content_, session_.state());
    const auto message = permissionPending_ ? "Waiting for audio-input permission..."
      : session_.busy() ? "Preparing preset..." : session_.audioMessage();
    if (message != lastMessage_) {
      lv_label_set_text(message_, message.c_str());
      lastMessage_ = message;
    }
    lv_label_set_text(lv_obj_get_child(start_, 0), session_.playing() ? "Stop audio" : "Start audio");
    if (session_.busy()) lv_obj_remove_flag(blocker_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(blocker_, LV_OBJ_FLAG_HIDDEN);
    if (session_.busy() || permissionPending_) lv_obj_add_state(start_, LV_STATE_DISABLED);
    else lv_obj_remove_state(start_, LV_STATE_DISABLED);
    lv_indev_set_group(keyboard_, dialogGroup_ ? dialogGroup_ : session_.busy() ? nullptr : group_);
  }

  // Exercise native window behavior without an audio device or microphone
  // permission. Optional captures are for bounded visual inspection.
  void smokeStep(int step, const std::filesystem::path& screenshots)
  {
    const auto require = [](bool condition, const char* error) {
      if (!condition) throw std::runtime_error(error);
    };
    if (step == 0) {
      require(!session_.busy() && session_.engine(), "Desktop did not prepare its factory preset");
      require(!session_.playing(), "Desktop opened audio without user action");
      const auto point = ui_.toCanvas({40, 104});
      require(point.x == 40 && point.y == 40, "Toolbar offset broke chain hit-testing");
      capture(screenshots, "desktop");
      openSetup();
    } else if (step == 1) {
      require(dialog_ && input_ && output_, "Audio setup did not open");
      applySetup();
      require(dialog_ && std::string(lv_label_get_text(setupError_)).find("Choose both") != std::string::npos,
              "Audio setup allowed unspecified default devices");
      capture(screenshots, "audio-setup");
      closeDialog();
      lv_sdl_window_set_size(display_, 960, 604);
      // Programmatic SDL_SetWindowSize emits SIZE_CHANGED; the LVGL driver
      // handles user resize through RESIZED. Inject that same event in the
      // headless driver so the test follows the real resize path.
      SDL_Event resized{};
      resized.type = SDL_WINDOWEVENT;
      resized.window.windowID = SDL_GetWindowID(lv_sdl_window_get_window(display_));
      resized.window.event = SDL_WINDOWEVENT_RESIZED;
      resized.window.data1 = 960;
      resized.window.data2 = 604;
      SDL_PushEvent(&resized);
    } else if (step == 2) {
      require(width_ == 960 && height_ == 604, "Desktop did not handle window resizing");
      const auto point = ui_.toCanvas({30, 94});
      require(point.x == 40 && point.y == 40, "Resizing broke chain hit-testing");
      capture(screenshots, "desktop-small");
      session_.state().dirty = true;
      requestClose();
      require(dialog_ && running_, "Close did not protect unsaved edits");
    } else if (step == 3) {
      capture(screenshots, "unsaved-close");
      closeDialog();
      require(running_, "Cancel closed the desktop window");
      session_.state().dirty = false;
      SDL_Event quit{};
      quit.type = SDL_QUIT;
      SDL_PushEvent(&quit);
    }
  }

  void requestClose()
  {
    if (dialog_) return;
    if (!session_.state().dirty && !session_.busy()) { running_ = false; return; }
    auto* panel = dialog("Close Ardor");
    paragraph(panel, session_.busy() ? "A preset is loading. Wait for it to finish, or close without saving."
      : "Save your preset changes before closing?");
    button(panel, "Keep playing", onCancel);
    if (!session_.busy()) button(panel, "Save and close", onSaveClose);
    button(panel, "Close without saving", onDiscardClose);
  }

private:
  void capture(const std::filesystem::path& directory, const char* name)
  {
    if (directory.empty()) return;
    std::filesystem::create_directories(directory);
    lv_refr_now(display_);
    auto* renderer = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(display_));
    auto* surface = SDL_CreateRGBSurfaceWithFormat(0, width_, height_, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) throw std::runtime_error(SDL_GetError());
    const auto path = directory / (std::string(name) + ".bmp");
    const bool failed = SDL_RenderReadPixels(renderer, nullptr, surface->format->format,
      surface->pixels, surface->pitch) != 0 || SDL_SaveBMP(surface, path.string().c_str()) != 0;
    SDL_FreeSurface(surface);
    if (failed) throw std::runtime_error(SDL_GetError());
  }

  ardor::UiActions actions()
  {
    ardor::UiActions result;
    result.selectPreset = [this](std::size_t index) { session_.selectPreset({session_.state().activeBank, index}); };
    result.savePreset = [this] { session_.savePreset(); };
    result.changeBank = [this](int delta) { session_.selectPreset({std::clamp(session_.state().activeBank + delta, 0, 99), session_.state().activePreset}); };
    result.resolveNavigation = [this](auto decision) { session_.resolveNavigation(decision); };
    result.setTunerMode = [this](bool enabled) { session_.setTuner(enabled); };
    const auto parameter = [this](const std::string& id, const std::string& key, float value) {
      return session_.updateParameter(id, key, value);
    };
    result.updateDaisyParameter = parameter;
    result.updateCompressorParameter = parameter;
    result.updateNoiseGateParameter = parameter;
    result.updateWahParameter = parameter;
    result.updateBlockParameter = parameter;
    result.updateEqBand = [this](const auto& id, std::size_t band, const auto& params) {
      return !session_.busy() && session_.engine() && session_.engine()->setParametricEqBand(id, band, params);
    };
    result.updateEqPassFilter = [this](const auto& id, auto kind, const auto& params) {
      return !session_.busy() && session_.engine() && session_.engine()->setParametricEqPassFilter(id, kind, params);
    };
    result.updateBlockEnabled = [this](const auto& id, bool enabled) {
      return !session_.busy() && session_.engine() && session_.engine()->setBlockEnabled(id, enabled);
    };
    result.updateGlobalGains = [this](float input, float output) {
      if (auto* engine = session_.engine()) {
        engine->setInputGain(ardor::dbToGain(input));
        engine->setOutputGain(ardor::dbToGain(output));
      }
    };
    result.updateCabParameters = [this](float level, float mix) {
      if (auto* engine = session_.engine()) { engine->setCabLevel(ardor::dbToGain(level)); engine->setCabMix(mix); }
    };
    result.selectScene = [this](auto index) { session_.selectScene(index); };
    result.setSceneLayer = [this](bool active) {
      if (active) ardor::enterScenesMode(session_.state());
      else ardor::enterPresetMode(session_.state());
    };
    result.updateSceneTarget = [this](std::size_t index, float value) {
      return !session_.busy() && session_.engine() && session_.engine()->tryOverrideSceneTarget(index, value);
    };
    result.requestSceneCapture = [this] {
      if (session_.engine() && session_.playing()) session_.engine()->requestSceneValueSnapshot();
      else ardor::failCurrentSoundCapture(session_.state(), "Start audio before capturing a scene");
    };
    result.openHostSettings = [this] { openSetup(); };
    result.openLooper = [this] { ardor::setUiStatus(session_.state(), "Desktop looper controls are not available in this prototype"); };
    return result;
  }

  lv_obj_t* button(lv_obj_t* parent, const char* text, lv_event_cb_t callback)
  {
    auto* object = style::button(parent, text);
    lv_obj_set_size(object, 164, 42);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, this);
    lv_group_add_obj(dialogGroup_ ? dialogGroup_ : group_, object);
    return object;
  }

  void resize()
  {
    width_ = lv_display_get_horizontal_resolution(display_);
    height_ = lv_display_get_vertical_resolution(display_);
    lv_obj_set_size(toolbar_, width_, 64);
    lv_obj_set_pos(setup_, width_ - 352, 11);
    lv_obj_set_pos(start_, width_ - 180, 11);
    lv_obj_set_width(message_, std::max(120, width_ - 400));
    lv_obj_set_pos(content_, 0, 64);
    lv_obj_set_size(content_, width_, height_ - 64);
  }

  void resizeBlocker()
  {
    lv_obj_set_pos(blocker_, 0, 64);
    lv_obj_set_size(blocker_, width_, height_ - 64);
  }

  void paragraph(lv_obj_t* parent, const std::string& text)
  {
    auto* label = lv_label_create(parent);
    style::setText(label);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_text(label, text.c_str());
  }

  lv_obj_t* dialog(const char* title)
  {
    dialog_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(dialog_);
    lv_obj_set_size(dialog_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(dialog_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dialog_, LV_OPA_70, 0);
    lv_obj_remove_flag(dialog_, LV_OBJ_FLAG_SCROLLABLE);
    dialogGroup_ = lv_group_create();
    lv_indev_set_group(keyboard_, dialogGroup_);
    auto* panel = lv_obj_create(dialog_);
    style::styleSurface(panel);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_size(panel, 680, std::min(height_ - 48, 660));
    lv_obj_center(panel);
    lv_obj_set_style_pad_all(panel, 24, 0);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    auto* heading = lv_label_create(panel);
    style::setText(heading, style::text, &ardor_font_saira_cond_semibold_28);
    lv_label_set_text(heading, title);
    return panel;
  }

  void closeDialog()
  {
    if (!dialog_) return;
    lv_indev_set_group(keyboard_, group_);
    // Delete after the current event dispatch has finished; callbacks retain
    // their widget pointers until LVGL returns from the dispatch.
    lv_obj_delete_async(dialog_);
    dialog_ = nullptr;
    lv_group_delete(dialogGroup_);
    dialogGroup_ = nullptr;
    input_ = output_ = channel_ = buffer_ = setupError_ = nullptr;
  }

  lv_obj_t* field(lv_obj_t* parent, const char* name, const std::string& options, int selected)
  {
    paragraph(parent, name);
    auto* object = lv_dropdown_create(parent);
    lv_obj_set_width(object, LV_PCT(100));
    lv_obj_set_height(object, 42);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(style::panelAlt), 0);
    style::setText(object);
    lv_obj_set_style_text_font(object, LV_FONT_DEFAULT, LV_PART_INDICATOR);
    style::setText(lv_dropdown_get_list(object));
    lv_obj_set_style_bg_color(lv_dropdown_get_list(object), lv_color_hex(style::panelAlt), 0);
    lv_dropdown_set_options(object, options.c_str());
    lv_dropdown_set_selected(object, selected);
    lv_group_add_obj(dialogGroup_, object);
    return object;
  }

  void openSetup()
  {
    if (dialog_) return;
    std::string error;
    ardor::enumerateAudioDevices(devices_, error);
    auto* panel = dialog("Audio setup");
    paragraph(panel, "Connect your guitar to an audio interface. Use headphones or the interface outputs. Ardor processes at 48 kHz.");
    auto deviceField = [&](const char* label, const auto& devices, const auto& selectedId) {
      std::string options = "Select an interface";
      int selected = 0;
      for (std::size_t index = 0; index < devices.size(); ++index) {
        auto name = devices[index].name;
        std::replace(name.begin(), name.end(), '\n', ' ');
        options += "\n" + name;
        if (devices[index].id == selectedId) selected = static_cast<int>(index + 1);
      }
      if (!selectedId.empty() && selected == 0 && error.empty()) error = "A saved interface is unavailable. Reconnect it and reopen Audio setup, or choose another.";
      return field(panel, label, options, selected);
    };
    const auto& settings = session_.settings();
    input_ = deviceField("Input interface", devices_.capture, settings.captureDeviceId);
    output_ = deviceField("Output interface (stereo)", devices_.playback, settings.playbackDeviceId);
    std::string channels = "1";
    for (int index = 2; index <= 32; ++index) channels += "\n" + std::to_string(index);
    channel_ = field(panel, "Guitar input channel", channels, settings.inputChannel);
    const std::uint32_t sizes[] = {32, 64, 128, 256};
    int selected = 2;
    for (int index = 0; index < 4; ++index) if (sizes[index] == settings.blockSize) selected = index;
    buffer_ = field(panel, "Buffer (frames)", "32\n64\n128\n256", selected);
    setupError_ = lv_label_create(panel);
    style::setText(setupError_, style::warning);
    lv_obj_set_width(setupError_, LV_PCT(100));
    if (devices_.capture.empty() || devices_.playback.empty()) error = "No usable input/output interface found. Connect one and reopen Audio setup.";
    lv_label_set_text(setupError_, error.c_str());
    auto* row = lv_obj_create(panel);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 42);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 12, 0);
    button(row, "Save settings", onApply);
    button(row, "Cancel", onCancel);
    lv_group_focus_obj(input_);
  }

  void applySetup()
  {
    const auto inputIndex = lv_dropdown_get_selected(input_);
    const auto outputIndex = lv_dropdown_get_selected(output_);
    if (!inputIndex || !outputIndex || inputIndex > devices_.capture.size() || outputIndex > devices_.playback.size()) {
      lv_label_set_text(setupError_, "Choose both an input and an output interface.");
      return;
    }
    auto settings = session_.settings();
    settings.captureDeviceId = devices_.capture[inputIndex - 1].id;
    settings.playbackDeviceId = devices_.playback[outputIndex - 1].id;
    settings.inputChannel = lv_dropdown_get_selected(channel_);
    const std::uint32_t sizes[] = {32, 64, 128, 256};
    settings.blockSize = sizes[lv_dropdown_get_selected(buffer_)];
    std::string error;
    if (!session_.configureAudio(std::move(settings), error)) { lv_label_set_text(setupError_, error.c_str()); return; }
    closeDialog();
  }

  void permissionError()
  {
    ardor::setUiStatus(session_.state(), "Allow Ardor in System Settings > Privacy & Security > Microphone, then press Start audio.", true);
  }

  void toggleAudio()
  {
    if (session_.playing()) { session_.stopAudio(); return; }
    if (session_.settings().captureDeviceId.empty() || session_.settings().playbackDeviceId.empty()) { openSetup(); return; }
    const auto permission = ardor::requestAudioInputPermission();
    if (permission == ardor::AudioInputPermission::Denied) permissionError();
    else if (permission == ardor::AudioInputPermission::Pending) permissionPending_ = true;
    else session_.startAudio();
  }

  static DesktopWindow& self(lv_event_t* event) { return *static_cast<DesktopWindow*>(lv_event_get_user_data(event)); }
  static void onSetup(lv_event_t* event) { self(event).openSetup(); }
  static void onStart(lv_event_t* event) { self(event).toggleAudio(); }
  static void onApply(lv_event_t* event) { self(event).applySetup(); }
  static void onCancel(lv_event_t* event) { self(event).closeDialog(); }
  static void onSaveClose(lv_event_t* event) { auto& window = self(event); if (window.session_.savePreset()) window.running_ = false; }
  static void onDiscardClose(lv_event_t* event) { self(event).running_ = false; }

  ardor::DesktopSession& session_;
  lv_display_t* display_;
  ardor::LvglUi ui_;
  lv_group_t* group_ = nullptr;
  lv_group_t* dialogGroup_ = nullptr;
  lv_indev_t* keyboard_ = nullptr;
  lv_obj_t *toolbar_ = nullptr, *content_ = nullptr, *blocker_ = nullptr;
  lv_obj_t *message_ = nullptr, *setup_ = nullptr, *start_ = nullptr;
  lv_obj_t *dialog_ = nullptr, *input_ = nullptr, *output_ = nullptr;
  lv_obj_t *channel_ = nullptr, *buffer_ = nullptr, *setupError_ = nullptr;
  ardor::AudioDeviceList devices_;
  int width_ = 0, height_ = 0;
  bool running_ = true, permissionPending_ = false;
  std::string lastMessage_;
};

} // namespace

int main(int argc, char** argv)
{
  bool smoke = false;
  std::filesystem::path root;
  std::filesystem::path screenshots;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--smoke-test") smoke = true;
    else if (argument == "--smoke-screenshots" && index + 1 < argc) screenshots = argv[++index];
    else if (argument == "--data-root" && index + 1 < argc) root = argv[++index];
    else if (argument == "--help") { std::cout << "Ardor desktop [--data-root DIR] [--smoke-test [--smoke-screenshots DIR]]\n"; return 0; }
    else { std::cerr << "Unknown desktop argument: " << argument << '\n'; return 2; }
  }
  if (smoke && !root.empty()) { std::cerr << "Smoke tests require their own temporary library.\n"; return 2; }
  if (!smoke && !screenshots.empty()) { std::cerr << "Screenshots require --smoke-test.\n"; return 2; }
  std::filesystem::path smokeRoot;
  try {
    if (smoke) {
      smokeRoot = std::filesystem::temp_directory_path() / ("ardor-desktop-window-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
      root = smokeRoot;
    } else if (root.empty()) root = ardor::desktopDataRoot();
    auto session = std::make_unique<ardor::DesktopSession>(root);
    SDL_SetMainReady();
    lv_init();
    auto* display = lv_sdl_window_create(1280, 784);
    if (!display || !lv_sdl_window_get_window(display)) {
      throw std::runtime_error(std::string("Cannot create the desktop window: ") + SDL_GetError());
    }
    lv_sdl_window_set_title(display, "Ardor");
    lv_sdl_window_set_resizeable(display, true);
    SDL_SetWindowMinimumSize(lv_sdl_window_get_window(display), 960, 604);
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
    Events events;
    SDL_SetEventFilter(filterEvent, &events);
    EventFilterGuard filterGuard;
    {
      DesktopWindow window(*session, display);
      const auto started = std::chrono::steady_clock::now();
      int smokeStep = 0;
      while (window.running()) {
        lv_timer_handler();
        session->tick();
        window.update();
        if (events.closeRequested.exchange(false)) window.requestClose();
        const auto elapsed = std::chrono::steady_clock::now() - started;
        if (smoke && elapsed >= std::chrono::milliseconds(500 * (smokeStep + 1)) && smokeStep < 4) {
          window.smokeStep(smokeStep++, screenshots);
        }
        if (smoke && elapsed > std::chrono::seconds(10)) throw std::runtime_error("Desktop window smoke test timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      session->stopAudio();
    }
    SDL_SetEventFilter(nullptr, nullptr);
    lv_deinit();
    session.reset();
    if (smoke) std::filesystem::remove_all(smokeRoot);
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "Ardor desktop: " << exception.what() << '\n';
    if (!smoke) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ardor could not start", exception.what(), nullptr);
    if (!smokeRoot.empty()) { std::error_code ignored; std::filesystem::remove_all(smokeRoot, ignored); }
    return 1;
  }
}
