#define SDL_MAIN_HANDLED

#include "dynamics/CompressorProcessor.h"
#include "preset/PresetStore.h"
#include "ui/LvglUi.h"
#include "ui/UiModel.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#include <lvgl.h>

namespace {

struct Args {
  std::filesystem::path dataRoot = ".";
  int bank = 0;
};

bool parse(int argc, char** argv, Args& args)
{
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--data-root" && i + 1 < argc) {
      args.dataRoot = argv[++i];
    } else if (a == "--bank" && i + 1 < argc) {
      args.bank = std::stoi(argv[++i]);
    } else {
      return false;
    }
  }
  return args.bank >= 0 && args.bank < 100;
}

} // namespace

int main(int argc, char** argv)
{
  Args args;
  if (!parse(argc, argv, args)) {
    std::cerr << "Usage: pedal-ui-sim [--data-root DIR] [--bank 0]\n";
    return 2;
  }

  lv_init();
  lv_sdl_window_create(1280, 720);
  lv_sdl_mouse_create();
  lv_sdl_mousewheel_create();
  lv_sdl_keyboard_create();

  ardor::PresetStore store(args.dataRoot);
  ardor::GlobalSettingsStore globalSettings(args.dataRoot);
  ardor::UiState state = ardor::makeDemoUiState();
  state.settings = globalSettings.load();
  ardor::loadAssetsFromDataRoot(state, args.dataRoot);
  ardor::loadBankFromStore(state, store, args.bank);

  const auto applyTarget = [&](ardor::UiNavigationTarget target) {
    if (target.bank != args.bank) {
      args.bank = target.bank;
      ardor::loadBankFromStore(state, store, args.bank);
    }
    std::string error;
    if (!ardor::loadPresetSlotFromStore(state, store, {args.bank, static_cast<int>(target.preset)}, error)) {
      std::cerr << error << "\n";
    }
  };
  ardor::UiActions actions;
  actions.selectPreset = [&](std::size_t index) {
    if (ardor::requestPresetNavigation(state, {args.bank, index})) applyTarget({args.bank, index});
  };
  actions.savePreset = [&]() {
      std::string error;
      if (!ardor::saveActivePresetToStore(state, store, args.bank, error)) {
        std::cerr << error << "\n";
      }
  };
  actions.changeBank = [&](int delta) {
    const int nextBank = std::clamp(args.bank + delta, 0, 99);
    if (nextBank != args.bank && ardor::requestPresetNavigation(
          state, {nextBank, state.activePreset})) {
      applyTarget({nextBank, state.activePreset});
    }
  };
  actions.resolveNavigation = [&](ardor::UiNavigationDecision decision) {
    if (decision == ardor::UiNavigationDecision::Cancel) {
      ardor::confirmNavigation(state, decision);
      return;
    }
    if (decision == ardor::UiNavigationDecision::Save) {
      std::string error;
      if (!ardor::saveActivePresetToStore(state, store, args.bank, error)) {
        std::cerr << error << "\n";
        return;
      }
    }
    if (const auto target = ardor::confirmNavigation(state, decision)) applyTarget(*target);
  };
  actions.savePalette = [&](ardor::PaletteId palette, std::string& error) {
    return globalSettings.savePalette(palette, error);
  };
  actions.saveWifiSettings = [&](const std::string& ssid, const std::string& password,
                                 const std::string& country, std::string& error) {
    return globalSettings.saveWifi(ssid, password, country, error);
  };
  ardor::LvglUi ui(std::move(actions));
  ui.build(lv_screen_active(), state);

  while (true) {
    lv_timer_handler();
    // This simulator has no real engine and no live audio behind it. To
    // still let the gain-reduction meter demo something while turning the
    // Threshold slider, approximate it against a fixed, plausible input
    // level using the same static gain law the real compressor runs --
    // real hardware instead reports the actual live reduction (see
    // apps/pedal-poc/main.cpp's nextGainReductionPoll).
    if (state.paramDrawerOpen && state.paramTarget == ardor::UiParamTarget::Block) {
      if (const auto* selected = ardor::selectedUiBlock(state)) {
        if (selected->type == "dynamics" && selected->params.value("mode", std::string{}) == "compressor") {
          constexpr float kDemoInputLevelDb = -14.0f;
          const float thresholdDb = selected->params.value("threshold_db", -24.0f);
          const float ratio = selected->params.value("ratio", 4.0f);
          const float kneeDb = selected->params.value("knee_db", 6.0f);
          ardor::updateCompressorGainReduction(
            state, ardor::compressorStaticGainDb(kDemoInputLevelDb, thresholdDb, ratio, kneeDb));
        }
      }
    }
    ui.refresh(lv_screen_active(), state);
    if (ardor::pendingStructuralPreview(state)) {
      ardor::completeStructuralPreview(state);
    }
    // LVGL busy-waits in LV_OS_NONE builds; use a real host sleep so the
    // simulator does not consume an otherwise idle CPU core.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}
