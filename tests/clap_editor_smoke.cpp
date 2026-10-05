#include "ClapEditor.h"
#include "audio/EngineLoader.h"
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Session {
  ardor::PedalEngine engine;
  ardor::Preset preset;
  std::array<ardor::Preset, 4> library;
  std::array<double, 6> controls{};
  bool waiting = false;
  double rate = 44100;
  int dirty = 0, stages = 0;
  std::filesystem::path root;
  ardor::clap_editor::Canvas* canvas = nullptr;
  explicit Session(std::filesystem::path path) : root(std::move(path)) {
    preset.name = "Tremolo";
    preset.blocks.push_back({"fx", "mod", true, "", {{"mode", "vintage_trem"}, {"mix", .5}, {"depth", .2}}});
    library.fill(preset);
    std::string error;
    ardor::EngineLoadOptions options; options.blockSize = 64;
    require(ardor::applyPreset(engine, preset, root, options, error), error.c_str());
  }
  ardor::clap_editor::Callbacks callbacks() {
    ardor::clap_editor::Callbacks result;
    result.root = root;
    result.engine = [this] { return &engine; };
    result.waiting = [this] { return waiting; };
    result.sampleRate = [this] { return rate; };
    result.stage = [this](ardor::Preset next) {
      ++stages;
      if (next.name == "Reject") return false;
      preset = std::move(next); waiting = true; return true;
    };
    result.edited = [this](ardor::Preset next) { preset = std::move(next); ++dirty; };
    result.selectPreset = [this](int slot) { controls[4] = slot; canvas->synchronize(library[slot], slot); return true; };
    result.selectScene = [this](int scene) { controls[5] = scene; };
    result.control = [this](unsigned id) { return controls[id]; };
    result.setControl = [this](unsigned id, double value) { controls[id] = value; ++dirty; };
    return result;
  }
};
void capture(const ardor::clap_editor::Canvas& canvas, const char* name) {
  const char* directory = std::getenv("ARDOR_CLAP_SCREENSHOTS");
  if (!directory || !*directory) return;
  std::filesystem::create_directories(directory);
  std::ofstream out(std::filesystem::path(directory) / (std::string(name) + ".ppm"), std::ios::binary);
  out << "P6\n" << canvas.pixelWidth() << ' ' << canvas.pixelHeight() << "\n255\n";
  const auto& pixels = canvas.pixels();
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    const std::array<char, 3> rgb{static_cast<char>(pixels[i + 2]), static_cast<char>(pixels[i + 1]), static_cast<char>(pixels[i])};
    out.write(rgb.data(), 3);
  }
}
bool hasLabel(lv_obj_t* object, const char* text) {
  if (lv_obj_check_type(object, &lv_label_class) && std::strcmp(lv_label_get_text(object), text) == 0) return true;
  for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
    if (hasLabel(lv_obj_get_child(object, i), text)) return true;
  return false;
}
}
int main() {
  const auto root = std::filesystem::temp_directory_path() / ("ardor-editor-" + std::to_string(
    std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    Session a(root / "a"), b(root / "b");
    ardor::clap_editor::Canvas first(a.callbacks(), a.library, 0, a.preset);
    a.canvas = &first;
    const auto firstDisplay = first.display();
    {
      ardor::clap_editor::Canvas second(b.callbacks(), b.library, 0, b.preset);
      b.canvas = &second;
      require(second.display() != firstDisplay, "Editors share their display");
      first.tick(); second.tick();
      require(hasLabel(lv_display_get_screen_active(first.display()), "44.1 kHz"), "Editor did not show actual DAW rate");
      a.rate = 176400; first.tick();
      require(hasLabel(lv_display_get_screen_active(first.display()), "176.4 kHz"), "Editor did not refresh after host rate change");
      a.rate = 44100; first.tick();
      a.controls[0] = .05; b.controls[1] = .123;
      first.tick(); second.tick();
      require(a.controls[0] == .05 && b.controls[1] == .123 && !a.dirty && !b.dirty,
        "Refreshing toolbar quantized host automation or dirtied project");
      require(first.frameRevision() && second.frameRevision(), "Editor did not render");
      require(!std::filesystem::exists(root), "Opening the editor rewrote the library");
      capture(first, "clap-presets");
      // Real pointer input enters EDIT on the first canvas while the second
      // display exists; events must not use another instance's default display.
      first.pointer(40, 615, true); first.pointer(40, 615, false); first.tick();
      require(first.state().mode == ardor::UiMode::Edit, "Native pointer did not enter Edit");
      require(second.state().mode == ardor::UiMode::Preset, "Pointer edited another instance");
      first.ui().selectBlock(first.state(), 0);
      first.ui().focusParameter("depth");
      require(first.ui().applyFocusedParameterDelta(first.state(), 5), "Effect parameter was not editable");
      first.tick();
      require(a.dirty && !b.dirty, "Editor changes did not dirty only their project");
      require(a.preset.blocks[0].params.at("depth").get<float>() > .2f, "Parameter missing from persisted DAW draft");
      require(a.stages == 0, "Normal parameter unnecessarily restarted the engine");
      capture(first, "clap-parameters");
      first.resize(960, 584, 2); first.tick();
      require(first.pixelWidth() == 1920 && first.pixelHeight() == 1168, "Retina backing size incorrect");
      capture(first, "clap-small-retina");
      ardor::enterEditMode(first.state());
      ardor::openBlockDrawer(first.state());
      const auto asset = std::find_if(first.state().assets.begin(), first.state().assets.end(), [](const auto& item) {
        return item.blockType == "delay" && item.mode == "digital";
      });
      require(asset != first.state().assets.end(), "Built-in effect browser missing");
      ardor::appendAssetBlock(first.state(), static_cast<std::size_t>(asset - first.state().assets.begin()));
      require(ardor::pendingStructuralPreview(first.state()), "Chain change did not queue a preview");
      first.tick();
      require(a.stages == 1 && a.waiting && a.preset.blocks.size() == 2, "Chain change did not request preparation/restart");
      first.tick();
      require(a.stages == 1, "Waiting preview was repeatedly prepared");
      a.waiting = false; first.tick();
      require(ardor::previewIsSynchronized(first.state()), "Host restart did not complete preview");
      first.synchronize(a.library[1], 1); first.tick();
      require(first.state().activePreset == 1 && !first.state().dirty, "DAW state/preset load did not refresh editor");
      first.state().dirty = true;
      first.ui().actions().selectPreset(2);
      require(first.state().navigationPrompt && first.state().activePreset == 1, "Dirty preset switched without a decision");
      first.ui().actions().resolveNavigation(ardor::UiNavigationDecision::Cancel);
      require(!first.state().navigationPrompt && first.state().activePreset == 1, "Cancel lost the current preset");
      first.ui().actions().selectPreset(2);
      first.ui().actions().resolveNavigation(ardor::UiNavigationDecision::Discard);
      require(first.state().activePreset == 2 && !first.state().dirty && !first.state().navigationPrompt,
        "Discard re-opened the unsaved-changes prompt instead of switching");
      require(!first.ui().actions().showTuner && !first.ui().actions().showLooper, "Unavailable device controls advertised");
    }
    first.tick(); require(first.frameRevision() > 1, "Destroying one editor broke the other");
    std::cout << "CLAP editor rendering, pointer input, live effects, DAW draft, restart and independent instances passed\n";
    std::filesystem::remove_all(root);
    return 0;
  } catch (const std::exception& error) { std::filesystem::remove_all(root); std::cerr << error.what() << '\n'; return 1; }
}
