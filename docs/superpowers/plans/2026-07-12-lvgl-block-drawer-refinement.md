# LVGL Block Drawer Refinement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the block browser a full-height black floating drawer with a charcoal left divider, 5px charcoal asset tiles, and one swipe-scrollable category row.

**Architecture:** Modify only `LvglUi::renderBlockDrawer`; existing category filtering and asset drag/append callbacks remain unchanged. Use LVGL's horizontal scrolling container for categories and the existing `styleSurface` helper for tiles.

**Tech Stack:** C++20, LVGL 9.5, CMake/CTest.

## Global Constraints

- Drawer aligns to the display top and bottom edge on the right, with its left edge acting as the divider.
- Drawer background is black with only a 1px charcoal left border.
- Asset tiles are charcoal, borderless, and 5px radius.
- Category filters are one horizontal, touch-swipable row; they do not wrap.
- Preserve close, filtering, append, drag-to-chain, and list scrolling behavior.

---

### Task 1: Render and verify the refined drawer

**Files:**
- Modify: `src/ui/LvglUi.cpp:862-934`
- Modify: `tests/lvgl_ui_smoke.cpp`

**Interfaces:**
- Consumes: existing `setCategoryFilter`, `appendAssetBlock`, `insertAssetBlock`, and drag callbacks.
- Produces: an unchanged block-drawer interaction model with refined LVGL geometry/styles.

- [ ] **Step 1: Write a failing smoke assertion**

Expose a renderer test helper or inspect the drawer children after opening it to assert one scrollable horizontal category row and a full-height drawer. The assertion must fail while the row uses `LV_FLEX_FLOW_ROW_WRAP`.

- [ ] **Step 2: Run the focused test**

Run: `cmake --build build --target pedal-lvgl-ui-smoke && ctest --test-dir build -R pedal-lvgl-ui-smoke --output-on-failure`

Expected: FAIL because filters wrap.

- [ ] **Step 3: Implement the minimal LVGL changes**

Set the drawer to 360×720 at the right edge, black, radius 0, no border except `LV_BORDER_SIDE_LEFT` in `0x242424`. Change `filterRow` to a single `LV_FLEX_FLOW_ROW`, enable horizontal scrolling, set its width to 324 and height to one button row, and set every filter button to fixed width. Style asset rows with `styleSurface(item, 0x242424)` and keep the existing callbacks.

- [ ] **Step 4: Verify and commit**

Run: `cmake --build build && ctest --test-dir build --output-on-failure && git diff --check`

Expected: 13/13 tests pass and no whitespace errors.

Commit: `git add src/ui/LvglUi.cpp tests/lvgl_ui_smoke.cpp docs/superpowers/plans/2026-07-12-lvgl-block-drawer-refinement.md && git commit -m "feat: refine full-height LVGL block drawer"`
