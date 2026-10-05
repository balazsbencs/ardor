#include "desktop/DesktopLibrary.h"
#include "desktop/DesktopSession.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

void settle(ardor::DesktopSession& session)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do {
    session.tick();
    if (!session.busy() && !ardor::pendingStructuralPreview(session.state())) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  } while (std::chrono::steady_clock::now() < deadline);
  throw std::runtime_error("Desktop engine preparation timed out");
}

} // namespace

int main()
{
  const auto root = std::filesystem::temp_directory_path() / ("ardor-desktop-session-" + std::to_string(
    std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    // First-run initialization must never replace a player's saved preset.
    ardor::PresetStore store(root);
    ardor::Preset custom;
    custom.name = "My clean preset";
    store.save({0, 0}, custom);
    ardor::initializeDesktopLibrary(root);
    ardor::initializeDesktopLibrary(root);
    require(store.load({0, 0}).name == custom.name, "First-run initialization overwrote a user preset");
    ardor::EngineLoadOptions options;
    for (int slot = 0; slot < 4; ++slot) {
      ardor::PedalEngine engine;
      std::string error;
      require(ardor::applyPreset(engine, store.load({0, slot}), root, options, error), error);
    }

    ardor::DesktopSettings settings;
    settings.captureDeviceId = "coreaudio:my-interface-input";
    settings.playbackDeviceId = "coreaudio:my-interface-output";
    settings.inputChannel = 1;
    settings.blockSize = 128;
    ardor::saveDesktopSettings(root, settings);
    auto restored = ardor::loadDesktopSettings(root);
    require(restored.captureDeviceId == settings.captureDeviceId && restored.playbackDeviceId == settings.playbackDeviceId
            && restored.inputChannel == 1 && restored.blockSize == 128, "Audio settings did not survive restart");
    settings.blockSize = 0;
    bool rejected = false;
    try { ardor::saveDesktopSettings(root, settings); } catch (const std::exception&) { rejected = true; }
    require(rejected && ardor::loadDesktopSettings(root).blockSize == 128, "Invalid settings damaged the saved configuration");

    {
      ardor::DesktopSession session(root);
      settle(session);
      require(session.engine() && !session.playing(), "Startup must prepare the preset without opening an audio device");
      require(session.state().bank.presets[0].name == custom.name, "Startup loaded the wrong preset");
      // The desktop policy must also prepare WDW without Linux worker setup.
      ardor::Preset wdw;
      wdw.version = 3;
      wdw.routing = "wdw";
      wdw.name = "Portable WDW";
      wdw.wdw = ardor::WdwRouting{};
      std::filesystem::copy_file(ARDOR_NAM_EXAMPLE_MODEL, root / "models/example.nam");
      wdw.wdw->dry.blocks.push_back({"dry-nam", "nam", true, "models/example.nam", nlohmann::json::object()});
      wdw.wdw->wet.blocks.push_back({"wet-nam", "nam", true, "models/example.nam", nlohmann::json::object()});
      wdw.wdw->wet.blocks.push_back({"wet-delay", "delay", true, "", {{"mode", "digital"}}});
      store.save({1, 0}, wdw);
      session.selectPreset({1, 0});
      settle(session);
      require(session.state().activeBank == 1 && session.engine()->wdwRoutingEnabled(), "Desktop WDW was rejected: " + session.state().statusMessage);
      float input[128]{}, left[128]{}, right[128]{};
      input[0] = 0.25f;
      session.engine()->processBlock(input, left, right, 128);
      for (int frame = 0; frame < 128; ++frame) require(std::isfinite(left[frame]) && std::isfinite(right[frame]), "Desktop WDW output was not finite");
      session.selectPreset({0, 1});
      settle(session);
      require(session.state().activePreset == 1 && session.settings().slot == 1, "Preset navigation was not committed");

      // A broken target must preserve both the visible and prepared selection.
      auto invalid = store.load({0, 2});
      invalid.blocks.front().params["mode"] = "unknown-desktop-mode";
      store.save({0, 2}, invalid);
      auto* previous = session.engine();
      session.selectPreset({0, 2});
      settle(session);
      require(session.state().activePreset == 1 && session.engine() == previous, "Rejected preset changed the active selection");
      require(session.state().statusIsError, "Rejected preset did not report an error");

      // Structural editing is prepared while stopped and is not marked saved.
      auto rollback = ardor::captureUiPreviewSnapshot(session.state());
      session.state().bank.presets[1].blocks.front().params["mode"] = "bogus";
      session.state().dirty = true;
      require(ardor::queuePreview(session.state(), std::move(rollback), "invalid desktop edit"), "Could not queue a draft");
      settle(session);
      require(session.engine() == previous && session.state().bank.presets[1].blocks.front().params["mode"] == "vintage_trem",
              "Rejected structural edit did not restore the draft");

      ardor::setActiveOutputGainDb(session.state(), -6.0f);
      auto next = session.settings();
      next.blockSize = 256;
      std::string error;
      require(session.configureAudio(next, error), error);
      settle(session);
      require(session.state().dirty && ardor::activePresetToPreset(session.state()).global.outputGainDb == -6.0f,
              "Audio configuration discarded unsaved edits");
      require(session.savePreset(), "Could not save desktop preset");
      require(store.load({0, 1}).global.outputGainDb == -6.0f, "Save did not persist the audible draft");
      next = session.settings();
      next.captureDeviceId.clear();
      next.playbackDeviceId.clear();
      require(session.configureAudio(next, error), error);
      require(!session.startAudio() && !session.playing(), "Missing selection must not open default devices");
    }

    // A corrupt settings file leaves the application recoverable and preserves
    // the file for diagnosis instead of silently rewriting it at startup.
    {
      std::ofstream file(root / "settings/desktop.json");
      file << R"({"version":1,"inputChannel":-1})";
    }
    {
      ardor::DesktopSession session(root);
      settle(session);
      require(session.engine() && session.state().statusIsError, "Corrupt settings prevented recovery");
      require(!session.playing(), "Corrupt settings enabled audio");
    }
    std::filesystem::remove_all(root);
    std::cout << "desktop session smoke passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    return 1;
  }
}
