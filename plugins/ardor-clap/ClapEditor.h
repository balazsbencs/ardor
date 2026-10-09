#pragma once

#include "dsp/PedalEngine.h"
#include "ui/LvglUi.h"
#include <functional>
#include <memory>

namespace ardor::clap_editor {

struct Callbacks {
  std::filesystem::path root;
  std::function<PedalEngine*()> engine;
  std::function<bool()> waiting;
  std::function<double()> sampleRate;
  std::function<bool(Preset)> stage;
  std::function<void(Preset)> edited;
  std::function<bool(int)> selectPreset;
  std::function<void(int)> selectScene;
  std::function<double(unsigned)> control;
  std::function<void(unsigned, double)> setControl;
};

// Main-thread only. Owns an LVGL display and input devices, never an audio device.
class Canvas {
public:
  Canvas(Callbacks callbacks, const std::array<Preset, 4>& presets, int slot, const Preset& current);
  ~Canvas();
  Canvas(const Canvas&) = delete;
  Canvas& operator=(const Canvas&) = delete;
  void synchronize(const Preset& preset, int slot);
  void tick();
  bool flushEdits();
  void resize(int width, int height, double backingScale = 1);
  void pointer(double x, double y, bool pressed);
  void key(uint32_t key, bool pressed);
  const std::vector<uint8_t>& pixels() const { return pixels_; }
  int pixelWidth() const { return pixelWidth_; }
  int pixelHeight() const { return pixelHeight_; }
  uint64_t frameRevision() const { return frameRevision_; }
  UiState& state() { return state_; }
  LvglUi& ui() { return ui_; }
  lv_display_t* display() const { return display_; }

private:
  UiActions actions();
  void choosePreset(std::size_t slot);
  bool savePreset();
  void refresh();
  void buildToolbar();
  static void draw(lv_display_t*, const lv_area_t*, uint8_t*);
  static void readPointer(lv_indev_t*, lv_indev_data_t*);
  static void readKey(lv_indev_t*, lv_indev_data_t*);
  static void toolbarChanged(lv_event_t*);
  Callbacks callbacks_;
  UiState state_;
  LvglUi ui_;
  lv_display_t* display_ = nullptr;
  lv_group_t* group_ = nullptr;
  lv_indev_t* pointer_ = nullptr;
  lv_indev_t* keyboard_ = nullptr;
  lv_obj_t* content_ = nullptr;
  lv_obj_t* toolbar_ = nullptr;
  lv_obj_t* channel_ = nullptr;
  lv_obj_t* inputTrim_ = nullptr;
  lv_obj_t* outputTrim_ = nullptr;
  lv_obj_t* bypass_ = nullptr;
  lv_obj_t* rate_ = nullptr;
  lv_obj_t* waiting_ = nullptr;
  std::vector<uint8_t> drawBuffer_, pixels_;
  lv_point_t point_{};
  bool pointerPressed_ = false, keyPressed_ = false, stagedPreview_ = false;
  bool syncingToolbar_ = false;
  uint32_t key_ = 0;
  uint64_t frameRevision_ = 0, snapshotSerial_ = 0;
  int pixelWidth_ = 0, pixelHeight_ = 0;
  double backingScale_ = 1;
  std::string serialized_;
  nlohmann::json compiledScenes_;
};

class NativeEditor {
public:
  virtual ~NativeEditor() = default;
  virtual bool setParent(void* parent) = 0;
  virtual bool setSize(uint32_t width, uint32_t height) = 0;
  virtual bool show() = 0;
  virtual bool hide() = 0;
  virtual bool setScale(double) { return false; }
  virtual double scale() const { return 1; }
  virtual uint32_t width() const = 0;
  virtual uint32_t height() const = 0;
};
std::unique_ptr<NativeEditor> createMacEditor(Canvas& canvas);
std::unique_ptr<NativeEditor> createWindowsEditor(Canvas& canvas);

} // namespace ardor::clap_editor
