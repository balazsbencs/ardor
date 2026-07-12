#define SDL_MAIN_HANDLED

#include "preset/PresetStore.h"
#include "ui/LvglUi.h"
#include "ui/UiModel.h"

#include <filesystem>
#include <iostream>
#include <string>

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
  ardor::UiState state = ardor::makeDemoUiState();
  ardor::loadAssetsFromDataRoot(state, args.dataRoot);
  ardor::loadBankFromStore(state, store, args.bank);

  ardor::LvglUi ui({
    [&](std::size_t index) {
      ardor::selectPreset(state, index);
      std::string error;
      if (!ardor::loadPresetSlotFromStore(state, store, {args.bank, static_cast<int>(index)}, error)) {
        std::cerr << error << "\n";
      }
    },
    [&]() {
      std::string error;
      if (!ardor::saveActivePresetToStore(state, store, args.bank, error)) {
        std::cerr << error << "\n";
      }
    },
  });
  ui.build(lv_screen_active(), state);

  while (true) {
    lv_timer_handler();
    ui.refresh(lv_screen_active(), state);
    lv_delay_ms(5);
  }
}
