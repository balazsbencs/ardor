#pragma once

#include <lvgl.h>

// Subset to the tuner's note designator alphabet only (A-G, #, 0-9, space,
// hyphen for the muted "--" state) — a full Latin set at 110 px is several
// hundred KB. See docs/lvgl-ui-redesign-spec.md §5, "Font budget".
extern const lv_font_t ardor_font_saira_cond_semibold_tuner_110;
