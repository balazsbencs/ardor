#define SDL_MAIN_HANDLED

#include "ui/LvglUi.h"
#include "ui/UiModel.h"

#include <lvgl.h>

int main()
{
  lv_init();
  lv_sdl_window_create(800, 480);
  lv_sdl_mouse_create();
  lv_sdl_mousewheel_create();
  lv_sdl_keyboard_create();

  ardor::UiState state = ardor::makeDemoUiState();
  ardor::LvglUi ui;
  ui.build(lv_screen_active(), state);

  while (true) {
    lv_timer_handler();
    ui.refresh(lv_screen_active(), state);
    lv_delay_ms(5);
  }
}
