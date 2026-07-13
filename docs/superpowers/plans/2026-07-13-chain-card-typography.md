# Chain Card Typography Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render signal-chain cards with an uppercase category and a single effect-name title.

**Architecture:** Keep the change inside `LvglUi::renderEditMode`, where the card labels are already assembled. The category is derived from the existing display label, transformed only for display, and the existing asset name remains the card title.

**Tech Stack:** C++20, LVGL 9.5, Open Sans font assets, existing `pedal-lvgl-ui-smoke` executable.

## Global Constraints

- Change only signal-chain cards; parameter titles, drag ghosts, and block-drawer entries retain their existing labels.
- Display the category in uppercase, light-grey Open Sans 18 at the card top-left.
- Display only the asset name as the centred main title in Open Sans 22.
- Do not display short internal types such as `mod`, `nam`, or `reverb`.
- Preserve the black chain background, charcoal cards, selected acid-green dot, touch selection, and drag behaviour.
- Do not stage `presets/bank-000/preset-0.json`, `.codex/`, `.superpowers/brainstorm/`, or `sdcard.img`.

---

### Task 1: Render compact chain-card labels

**Files:**
- Modify: `src/ui/LvglUi.cpp:841-858`
- Test: `tests/lvgl_ui_smoke.cpp:294-300`

**Interfaces:**
- Consumes: `UiBlock::label`, `UiBlock::assetName`, the `label(...)` LVGL helper, and `ardor_font_open_sans_regular_18` / `ardor_font_open_sans_semibold_22`.
- Produces: chain cards that include an uppercase category and asset name but omit the internal type.

- [ ] **Step 1: Write the failing renderer test**

  After building the edit-mode chain in `tests/lvgl_ui_smoke.cpp`, assert that the selected block's display label exists in uppercase, its asset name exists, and its internal type does not.

  ```cpp
  if (require(findLabel(lv_screen_active(), "REVERB"),
              "chain card should render an uppercase category")) return 1;
  if (require(findLabel(lv_screen_active(), "Room Reverb"),
              "chain card should render its asset name")) return 1;
  if (require(!findLabel(lv_screen_active(), "reverb"),
              "chain card should not render its short internal type")) return 1;
  ```

- [ ] **Step 2: Run the focused test to verify it fails**

  Run: `cmake --build build --target pedal-lvgl-ui-smoke && ./build/pedal-lvgl-ui-smoke`

  Expected: failure containing `chain card should render an uppercase category`, because cards currently show `block.type` and a combined label/asset-name button title.

- [ ] **Step 3: Implement the minimal renderer change**

  Include `<cctype>` and transform a copy of `block.label` with `std::toupper`. Create each tile with an empty button label, then add two explicit labels:

  ```cpp
  std::string category = block.label;
  std::transform(category.begin(), category.end(), category.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });

  lv_obj_t* object = button(chain, "");
  label(object, category, LV_ALIGN_TOP_LEFT, 10, 8, &ardor_font_open_sans_regular_18, muted);
  label(object, block.assetName, LV_ALIGN_CENTER, 0, 8, &ardor_font_open_sans_regular_22, text);
  ```

  Remove the existing `label(object, block.type, ...)` call. Do not alter card geometry, events, colours, or the selected indicator.

- [ ] **Step 4: Run the focused test to verify it passes**

  Run: `cmake --build build --target pedal-lvgl-ui-smoke && ./build/pedal-lvgl-ui-smoke`

  Expected: exit 0.

- [ ] **Step 5: Run the full regression suite**

  Run: `cmake --build build && ctest --test-dir build --output-on-failure && git diff --check`

  Expected: build exit 0, 13/13 CTest tests passing, and no diff-check output.

- [ ] **Step 6: Commit and push the renderer change**

  ```bash
  git add src/ui/LvglUi.cpp tests/lvgl_ui_smoke.cpp docs/superpowers/plans/2026-07-13-chain-card-typography.md
  git commit -m "fix: simplify LVGL chain card titles"
  git push origin feat/lvgl-flat-knob-redesign
  ```
