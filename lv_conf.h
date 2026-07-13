#pragma once

#define LV_CONF_H

// System malloc: the builtin 64K pool cannot fit the fbdev draw buffer (86KB)
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN
#define LV_USE_FONT_COMPRESSED 1

#ifdef ARDOR_UI_BACKEND_FBDEV
// Pi DSI framebuffer is RGB565; LVGL must render in the same format
#define LV_COLOR_DEPTH 16
#else
#define LV_COLOR_DEPTH 32
#endif

#ifdef ARDOR_UI_BACKEND_FBDEV
#define LV_USE_LINUX_FBDEV 1
// Software rotation (portrait panel -> landscape UI) must rotate the WHOLE
// frame at once. The default PARTIAL mode rotates each dirty stripe on its own
// and blits it back, leaving horizontal seams; single-buffering also tears on
// the DSI panel. FULL + 2 buffers renders off-screen, rotates once, one write.
#define LV_LINUX_FBDEV_RENDER_MODE LV_DISPLAY_RENDER_MODE_FULL
#define LV_LINUX_FBDEV_BUFFER_COUNT 2
#define LV_USE_EVDEV 1
#define LV_USE_SDL 0
#else
#define LV_USE_SDL 1
#define LV_USE_LINUX_FBDEV 0
#define LV_USE_EVDEV 0
#endif

#define LV_USE_OS LV_OS_NONE

#ifdef ARDOR_UI_BACKEND_FBDEV
// Warnings/errors to stdout: a silent lv_malloc failure cost a debugging day
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1
#else
#define LV_USE_LOG 0
#endif
