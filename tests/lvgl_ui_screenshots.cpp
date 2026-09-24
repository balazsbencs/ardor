// Renders every touchscreen screen and overlay to a 1280 x 720 PPM file, so the
// UI can be compared with the Lamp Black mockups. Usage:
//   pedal-lvgl-ui-screenshots <output-directory>
#include "ui/LvglUi.h"
#include "ui/LvglUiStyle.h"

#include <array>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 1280;
constexpr int kHeight = 720;

void captureFlush(lv_display_t* display, const lv_area_t*, uint8_t*)
{
  lv_display_flush_ready(display);
}

struct Capture {
  lv_display_t* display = nullptr;
  uint8_t* pixels = nullptr;
  uint32_t stride = 0;
  std::string directory;

  bool save(const std::string& name) const
  {
    lv_obj_update_layout(lv_screen_active());
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(display);
    const auto path = directory + "/" + name + ".ppm";
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "P6\n" << kWidth << ' ' << kHeight << "\n255\n";
    for (int y = 0; y < kHeight; ++y) {
      const auto* row = pixels + static_cast<std::size_t>(y) * stride;
      for (int x = 0; x < kWidth; ++x) {
        const std::array<char, 3> rgb = {
          static_cast<char>(row[x * 3 + 2]),
          static_cast<char>(row[x * 3 + 1]),
          static_cast<char>(row[x * 3]),
        };
        output.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
      }
    }
    std::cout << "wrote " << path << "\n";
    return output.good();
  }
};

lv_obj_t* findLabel(lv_obj_t* parent, const char* text)
{
  if (lv_obj_check_type(parent, &lv_label_class)
      && std::strcmp(lv_label_get_text(parent), text) == 0) {
    return parent;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
    if (auto* found = findLabel(lv_obj_get_child(parent, static_cast<int32_t>(i)), text)) {
      return found;
    }
  }
  return nullptr;
}

void click(const char* text, lv_event_code_t code = LV_EVENT_CLICKED)
{
  lv_obj_t* label = findLabel(lv_screen_active(), text);
  if (!label) {
    std::cerr << "no label " << text << "\n";
    return;
  }
  lv_obj_send_event(lv_obj_get_parent(label), code, nullptr);
}

std::size_t assetIndex(const ardor::UiState& state, const std::string& name)
{
  for (std::size_t i = 0; i < state.assets.size(); ++i) {
    if (state.assets[i].name == name) return i;
  }
  std::cerr << "no asset " << name << "\n";
  return state.assets.size();
}

void insertAsset(ardor::UiState& state, const std::string& name, std::size_t at)
{
  const auto index = assetIndex(state, name);
  if (index >= state.assets.size()) return;
  ardor::insertAssetBlock(state, index, at);
  if (ardor::pendingStructuralPreview(state)) ardor::completeStructuralPreview(state);
}

// The mockup chain: Compressor, NAM, Cab, Chorus (off), Tape delay, Reverb.
ardor::UiState makeRichState()
{
  auto state = ardor::makeDemoUiState();
  state.bank.name = "Bank 001 - Sunday Set";
  state.activeBank = 1;
  insertAsset(state, "Compressor", 0);
  insertAsset(state, "Chorus", 3);
  insertAsset(state, "Tape Delay", 4);
  insertAsset(state, "Plate Reverb", 5);
  auto& blocks = state.bank.presets[state.activePreset].blocks;
  if (blocks.size() > 3) blocks[3].enabled = false;
  state.dirty = false;
  // Inserting modules leaves a transient "Chain updated" toast; clear it so
  // each shot shows the screen at rest.
  ardor::setUiStatus(state, "");
  ardor::enterPresetMode(state);
  ardor::updateRealtimeTelemetry(state, {});
  return state;
}

void addScenes(ardor::UiState& state)
{
  auto& preset = state.bank.presets[state.activePreset];
  preset.sceneSet = ardor::PresetSceneSet{};
  preset.sceneSet->defaultSceneId = "scene-1";
  const std::array names = {"Verse", "Chorus", "Solo", "Outro"};
  for (std::size_t index = 0; index < names.size(); ++index) {
    auto& scene = preset.sceneSet->scenes[index];
    scene.id = "scene-" + std::to_string(index + 1);
    scene.name = names[index];
    scene.enterTimeMs = index == 1 ? 500 : 0;
    scene.outputTrimDb = index == 2 ? 2.0f : 0.0f;
  }
}

std::size_t blockOfType(const ardor::UiState& state, const std::string& type)
{
  const auto& blocks = state.bank.presets[state.activePreset].blocks;
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    if (blocks[i].type == type) return i;
  }
  return blocks.size();
}

void drawerShot(const Capture& capture, const std::string& name, const std::string& asset,
                std::size_t page = 0)
{
  auto state = makeRichState();
  ardor::enterEditMode(state);
  std::size_t index = 0;
  if (!asset.empty()) {
    const auto& blocks = state.bank.presets[state.activePreset].blocks;
    index = blocks.size();
    for (std::size_t i = 0; i < blocks.size(); ++i) {
      if (blocks[i].assetName == asset) index = i;
    }
    if (index == blocks.size()) {
      insertAsset(state, asset, blocks.size());
      index = state.bank.presets[state.activePreset].blocks.size() - 1;
    }
  }
  state.dirty = true;
  ardor::LvglUi ui;
  ui.selectBlock(state, index);
  ui.setParameterPage(page);
  ui.build(lv_screen_active(), state);
  capture.save(name);
}

} // namespace

int main(int argc, char** argv)
{
  if (argc < 2) {
    std::cerr << "usage: pedal-lvgl-ui-screenshots <output-directory>\n";
    return 2;
  }
  lv_init();
  lv_display_t* display = lv_display_create(kWidth, kHeight);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
  const auto stride = lv_draw_buf_width_to_stride(kWidth, LV_COLOR_FORMAT_RGB888);
  std::vector<uint8_t> storage(stride * kHeight + LV_DRAW_BUF_ALIGN);
  auto* pixels = static_cast<uint8_t*>(lv_draw_buf_align(storage.data(), LV_COLOR_FORMAT_RGB888));
  lv_display_set_buffers(display, pixels, nullptr, stride * kHeight, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, captureFlush);
  const Capture capture{display, pixels, stride, argv[1]};

  // Preset screen, clean and with a modified preset.
  {
    auto state = makeRichState();
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    capture.save("01-preset");
  }
  // Edit chain with a selected block.
  {
    auto state = makeRichState();
    ardor::enterEditMode(state);
    state.dirty = true;
    ardor::LvglUi ui;
    ui.selectBlock(state, 1);
    state.paramDrawerOpen = false;
    ui.build(lv_screen_active(), state);
    capture.save("02-edit");
  }
  // Edit chain scrolled to the end.
  {
    auto state = makeRichState();
    ardor::enterEditMode(state);
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    ui.scrollChainToEnd(state);
    capture.save("03-edit-end");
  }
  // Parameter drawers, one per block family.
  drawerShot(capture, "10-param-delay", "Tape Delay");
  drawerShot(capture, "11-param-delay-p2", "Tape Delay", 1);
  drawerShot(capture, "12-param-nam", "Clean Twin");
  drawerShot(capture, "13-param-cab", "Open Back 2x12");
  drawerShot(capture, "14-param-comp", "Compressor");
  drawerShot(capture, "15-param-chorus-off", "Chorus");
  drawerShot(capture, "16-param-reverb", "Plate Reverb");
  drawerShot(capture, "17-param-eq", "Five Band EQ");
  drawerShot(capture, "18-param-gate", "Noise Gate");
  drawerShot(capture, "19-param-wah", "GCB-95 Wah");
  drawerShot(capture, "20-param-rat", "RAT Distortion");
  drawerShot(capture, "21-param-tape", "Tape Machine");
  drawerShot(capture, "22-param-widener", "Stereo Widener");
  drawerShot(capture, "23-param-fuzz", "Big Cheese Fuzz");
  drawerShot(capture, "24-param-trem", "Vintage Trem");
  {
    auto state = makeRichState();
    ardor::enterEditMode(state);
    ardor::LvglUi ui;
    ui.selectGlobalParams(state);
    ui.build(lv_screen_active(), state);
    capture.save("25-param-global");
  }
  // Module drawer.
  {
    auto state = makeRichState();
    ardor::enterEditMode(state);
    state.dirty = true;
    ardor::openBlockDrawerAt(state, 2);
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    capture.save("30-modules");
    click("DRIVE");
    ui.refresh(lv_screen_active(), state);
    capture.save("31-modules-drive");
  }
  // Tuner.
  {
    auto state = makeRichState();
    ardor::enterTunerMode(state);
    ardor::updateTunerTelemetry(state, {true, 82.4f, -2.0f, 0.96f, "E", 2});
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    capture.save("40-tuner");
    ardor::updateTunerTelemetry(state, {true, 110.9f, 18.0f, 0.96f, "A", 2});
    ui.refresh(lv_screen_active(), state);
    capture.save("41-tuner-sharp");
    ardor::updateTunerTelemetry(state, {});
    ui.refresh(lv_screen_active(), state);
    capture.save("42-tuner-nosignal");
  }
  // Looper.
  {
    auto state = makeRichState();
    ardor::LooperTelemetry telemetry;
    telemetry.sessionState = ardor::LooperSessionState::Running;
    telemetry.masterFrames = 480000;
    telemetry.maximumFrames = 1920000;
    telemetry.playheadFrame = 240000;
    telemetry.tracks[0].state = ardor::LooperTrackState::Playing;
    telemetry.tracks[0].audible = true;
    telemetry.tracks[0].undoAvailable = true;
    telemetry.tracks[1].state = ardor::LooperTrackState::ArmedOverdub;
    telemetry.tracks[1].audible = true;
    telemetry.tracks[2].state = ardor::LooperTrackState::Recording;
    telemetry.tracks[2].audible = true;
    telemetry.tracks[3].state = ardor::LooperTrackState::Muted;
    ardor::enterLooperMode(state, "Clean Lead", 128ULL * 1024ULL * 1024ULL);
    ardor::updateLooperUi(state, telemetry, 0);
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    capture.save("50-looper");
    ardor::LooperTelemetry empty;
    ardor::updateLooperUi(state, empty, 0);
    ui.refresh(lv_screen_active(), state);
    capture.save("51-looper-empty");
    telemetry.revision = 3;
    telemetry.sessionState = ardor::LooperSessionState::Paused;
    ardor::updateLooperUi(state, telemetry, 0);
    ui.refresh(lv_screen_active(), state);
    click("PLAY");
    ui.refresh(lv_screen_active(), state);
    capture.save("52-looper-mixer");
    click("CLEAR TRACK", LV_EVENT_PRESSED);
    ui.refresh(lv_screen_active(), state);
    capture.save("53-looper-clear");
    state.looper.mixerOpen = false;
    state.looper.clearTrackConfirmationOpen = false;
    ardor::markUiChanged(state, ardor::UiChange::Looper);
    ardor::markLooperUnsaved(state);
    ui.refresh(lv_screen_active(), state);
    click("NEW", LV_EVENT_PRESSED);
    ui.refresh(lv_screen_active(), state);
    capture.save("54-looper-new");
    click("CANCEL");
    ui.refresh(lv_screen_active(), state);
    ardor::UiLooperState::LibraryEntry entry;
    entry.id = std::string(32, 'a');
    entry.name = "Night Sketch";
    entry.sourcePresetName = "Ambient Lead";
    entry.savedAt = "2026-08-31T21:00:00Z";
    entry.loopFrames = 480000;
    entry.populatedTracks = 3;
    entry.available = true;
    auto second = entry;
    second.id = std::string(32, 'b');
    second.name = "Sunday Groove";
    second.populatedTracks = 2;
    ardor::openLooperLibrary(state, {entry, second});
    ui.refresh(lv_screen_active(), state);
    capture.save("55-looper-library");
    ardor::closeLooperLibrary(state);
    ui.refresh(lv_screen_active(), state);
    click("CLOSE", LV_EVENT_PRESSED);
    ui.refresh(lv_screen_active(), state);
    capture.save("56-looper-close");
  }
  // Scenes.
  {
    auto state = makeRichState();
    addScenes(state);
    state.dirty = true;
    ardor::enterScenesMode(state);
    ardor::updateSceneTelemetry(state, {0, 2, 0.5f, false, true, false, false, {}});
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    capture.save("60-scenes");
    ardor::updateSceneTelemetry(state, {2, 2, 1.0f, false, false, true, true, {}});
    ui.refresh(lv_screen_active(), state);
    capture.save("61-scenes-live");
  }
  {
    auto state = makeRichState();
    addScenes(state);
    ardor::enterPresetMode(state);
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    capture.save("62-preset-with-scenes");
    ardor::enterEditMode(state);
    state.paramDrawerOpen = false;
    ui.build(lv_screen_active(), state);
    capture.save("63-scene-editor");
    click("SCENE SETTINGS");
    ui.refresh(lv_screen_active(), state);
    capture.save("64-scene-settings");
    click("2 CHORUS");
    ui.refresh(lv_screen_active(), state);
    capture.save("65-scene-copy-confirm");
    click("CANCEL");
    ardor::closeSceneSettings(state);
    ui.selectBlock(state, 1);
    ui.build(lv_screen_active(), state);
    capture.save("66-scene-param");
  }
  // Settings, each section.
  {
    auto state = makeRichState();
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    ui.openSettings(state);
    ui.refresh(lv_screen_active(), state);
    const std::array names = {"70-settings-appearance", "71-settings-wifi", "72-settings-audio",
                              "73-settings-control", "74-settings-updates"};
    for (std::size_t section = 0; section < names.size(); ++section) {
      ui.showSettingsSection(state, section);
      ui.refresh(lv_screen_active(), state);
      capture.save(names[section]);
    }
  }
  // Preset name editor and the unsaved-changes prompt.
  {
    auto state = makeRichState();
    ardor::enterEditMode(state);
    state.paramDrawerOpen = false;
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    ui.openPresetNameEditor(state);
    ui.refresh(lv_screen_active(), state);
    capture.save("80-preset-name");
  }
  {
    auto state = makeRichState();
    state.dirty = true;
    ardor::requestPresetNavigation(state, {0, 2});
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    capture.save("81-navigation-prompt");
  }
  {
    auto state = makeRichState();
    ardor::enterEditMode(state);
    ardor::LvglUi ui;
    ui.selectBlock(state, blockOfType(state, "delay"));
    const auto controls = ardor::parameterPage(state, 0);
    if (!controls.empty()) ardor::beginMidiLearn(state, controls.front());
    ui.build(lv_screen_active(), state);
    capture.save("82-midi-learn");
  }
  {
    auto state = makeRichState();
    ardor::enterEditMode(state);
    state.paramDrawerOpen = false;
    ardor::setUiStatus(state, "Preset saved");  // the toast, on purpose
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    capture.save("83-status-toast");
  }
  // Dual Rig and WDW chains.
  {
    ardor::Preset preset;
    preset.version = 2;
    preset.name = "Dual Rig";
    ardor::PresetBlock rig{"rig", "dualRig", true, "", {
      {"inputMode", "sum"}, {"leftLevelDb", 0.0f}, {"leftPolarityInvert", false},
      {"rightLevelDb", -3.0f}, {"rightPolarityInvert", true},
    }};
    rig.lanes[0].push_back({"left-nam", "nam", true, "models/clean.nam", nlohmann::json::object()});
    rig.lanes[0].push_back({"left-cab", "cab", true, "irs/open-back.wav", nlohmann::json::object()});
    rig.lanes[0].push_back({"left-chorus", "mod", false, "", {{"mode", "chorus"}}});
    rig.lanes[1].push_back({"right-nam", "nam", true, "models/crunch.nam", nlohmann::json::object()});
    rig.lanes[1].push_back({"right-delay", "delay", true, "", {{"mode", "digital"}}});
    preset.blocks.push_back(std::move(rig));
    auto state = ardor::makeDemoUiState();
    ardor::replaceActivePreset(state, preset);
    ardor::enterEditMode(state);
    state.paramDrawerOpen = false;
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    capture.save("90-dual-rig");
  }
  {
    ardor::Preset preset;
    preset.version = 3;
    preset.routing = "wdw";
    preset.name = "Wet Dry Wet";
    preset.wdw = ardor::WdwRouting{};
    preset.wdw->dry.levelDb = -2.0f;
    preset.wdw->wet.width = 0.8f;
    preset.wdw->dry.blocks.push_back({"dry-nam", "nam", true, "models/clean.nam", nlohmann::json::object()});
    preset.wdw->dry.blocks.push_back({"dry-cab", "cab", true, "irs/open-back.wav", nlohmann::json::object()});
    preset.wdw->wet.blocks.push_back({"wet-nam", "nam", true, "models/crunch.nam", nlohmann::json::object()});
    preset.wdw->wet.blocks.push_back({"wet-delay", "delay", true, "", {{"mode", "digital"}}});
    auto state = ardor::makeDemoUiState();
    ardor::replaceActivePreset(state, preset);
    ardor::enterEditMode(state);
    state.paramDrawerOpen = false;
    ardor::LvglUi ui;
    ui.build(lv_screen_active(), state);
    capture.save("91-wdw");
  }
  return 0;
}
