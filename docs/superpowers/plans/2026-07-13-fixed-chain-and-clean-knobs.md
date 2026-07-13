# Fixed Chain and Clean Knobs Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render five fixed-width effect tiles in a horizontally scrollable chain and remove the duplicate/clipped LVGL knob artifacts.

**Architecture:** Keep all layout in `LvglUi::renderEditMode` and reuse LVGL's native horizontal scrolling. Store the currently rendered chain object on `LvglUi` so drag handlers can translate canvas coordinates by its scroll position. Keep the existing custom knob graphics, but hide LVGL's native arc handle and make the custom needle a non-clipped sibling of the dial rim.

**Tech Stack:** C++20, LVGL 9.5, existing `pedal-lvgl-ui-smoke` executable.

## Global Constraints

- Keep the 1280 x 720 design grid, 1240 x 126 chain viewport, black background, charcoal `0x242424` tiles, and 5 px tile radius.
- Show exactly five 232 px wide, 92 px high tile bodies in the initial chain viewport; preserve the existing 10 px gaps.
- Use LVGL horizontal touch scrolling; do not add dependencies or new UI state to `UiState`.
- Preserve tap selection, drag reorder, asset insertion, Open Sans, the 270-degree acid-green knob arc, and the white radial needle.
- Do not stage `.codex/`, `.superpowers/brain/`, or `sdcard.img`.

---

### Task 1: Render the chain as five fixed scrolling slots

**Files:**
- Modify: `src/ui/LvglUi.h:54-79`
- Modify: `src/ui/LvglUi.cpp:214-253, 617-655, 818-858`
- Test: `tests/lvgl_ui_smoke.cpp:62-112, 270-330, 366-375`

**Interfaces:**
- Consumes: `LvglUi::canvas()`, `lv_obj_get_scroll_x(lv_obj_t*)`, existing block event callbacks.
- Produces: `LvglUi::chain()` returning the active chain viewport for drag handlers; fixed five-slot tile geometry and scroll-adjusted insertion indicators.

- [ ] **Step 1: Write the failing chain-layout tests**

  Add a recursive `findObjectWithSize` helper and test the rendered edit-mode chain using the current one-block state. Assert the block button width is 232, the chain has `LV_OBJ_FLAG_SCROLLABLE`, and its scroll direction is horizontal. Append five more demo-compatible blocks, rebuild, then assert the sixth block has an x-coordinate greater than the chain's right edge.

  ```cpp
  lv_obj_t* chain = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x000000), 1240, 126);
  lv_obj_t* firstBlock = findLabel(lv_screen_active(), state.bank.presets[state.activePreset].blocks.front().label.c_str());
  if (require(chain && firstBlock, "chain and first block should render")) return 1;
  if (require(lv_obj_get_width(lv_obj_get_parent(firstBlock)) == 232,
              "single-block chain should use the fixed five-slot tile width")) return 1;
  if (require(lv_obj_has_flag(chain, LV_OBJ_FLAG_SCROLLABLE)
              && lv_obj_get_scroll_dir(chain) == LV_DIR_HOR,
              "long chains should use horizontal scrolling")) return 1;
  ```

- [ ] **Step 2: Run the focused test to verify it fails**

  Run: `cmake --build build --target pedal-lvgl-ui-smoke && ./build/pedal-lvgl-ui-smoke`

  Expected: FAIL with `single-block chain should use the fixed five-slot tile width` because the old layout derives tile width from block count.

- [ ] **Step 3: Implement the fixed scrolling viewport**

  Add a `chain_` member and public accessor to `LvglUi`:

  ```cpp
  lv_obj_t* chain() const { return chain_; }
  // private:
  lv_obj_t* chain_ = nullptr;
  ```

  Reset `chain_` to `nullptr` at the start of `build`, assign it after creating the edit-mode chain, and replace per-block width calculation with fixed constants:

  ```cpp
  constexpr int kVisibleChainBlocks = 5;
  constexpr int kChainSlotWidth = kChainWidth / kVisibleChainBlocks;
  constexpr int kChainTileWidth = kChainSlotWidth - 10;

  lv_obj_add_flag(chain, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(chain, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(chain, LV_SCROLLBAR_MODE_OFF);

  lv_obj_set_size(object, kChainTileWidth, 92);
  lv_obj_set_pos(object, 14 + static_cast<int>(i) * kChainSlotWidth, 17);
  ```

  Pass `context->ui->chain()` into the chain point helpers. Convert a canvas x to content x by adding `lv_obj_get_scroll_x(chain)`, use `contentX / kChainSlotWidth` clamped to the block count, and subtract that same scroll amount from the insertion indicator's canvas x. Use the chain getter for asset drops as well. Update the static mapping tests to assert fixed-slot positions (`x=34` maps to 0; `x=34 + kChainSlotWidth * 5` maps to 5) rather than testing that the viewport's right edge maps to the final block of a 10,000-block chain.

- [ ] **Step 4: Run the focused test to verify it passes**

  Run: `cmake --build build --target pedal-lvgl-ui-smoke && ./build/pedal-lvgl-ui-smoke`

  Expected: exit 0.

- [ ] **Step 5: Commit the chain viewport change**

  ```bash
  git add src/ui/LvglUi.cpp src/ui/LvglUi.h tests/lvgl_ui_smoke.cpp
  git commit -m "feat: show fixed scrolling LVGL chain tiles"
  ```

### Task 2: Remove native arc and dial overflow artifacts

**Files:**
- Modify: `src/ui/LvglUi.cpp:459-516`
- Test: `tests/lvgl_ui_smoke.cpp:40-60, 300-345`

**Interfaces:**
- Consumes: `createKnob`, `lv_obj_set_style_bg_opa`, `lv_obj_remove_flag`.
- Produces: one visible custom white needle per knob, no native LVGL arc handle, and no scrollable rim/centre objects.

- [ ] **Step 1: Write the failing knob-rendering tests**

  Extend the existing pointer helpers to find the `lv_arc_class` object and the 56 x 56 rim. After rendering a parameter panel, assert the arc knob background opacity is transparent, the rim is not scrollable, and the pointer parent is the 154 x 184 knob container rather than the rim.

  ```cpp
  lv_obj_t* arc = findObjectOfClass(lv_screen_active(), &lv_arc_class);
  lv_obj_t* rim = findObjectWithSizeAndBgColor(lv_screen_active(), lv_color_hex(0x000000), 56, 56);
  if (require(arc && rim && pointer, "arc, dial rim, and pointer should render")) return 1;
  if (require(lv_obj_get_style_bg_opa(arc, LV_PART_KNOB) == LV_OPA_TRANSP,
              "native arc knob should be transparent")) return 1;
  if (require(!lv_obj_has_flag(rim, LV_OBJ_FLAG_SCROLLABLE),
              "dial rim should not create scrollbars")) return 1;
  if (require(lv_obj_get_width(lv_obj_get_parent(pointer)) == 154,
              "custom pointer should be a sibling above the dial rim")) return 1;
  ```

- [ ] **Step 2: Run the focused test to verify it fails**

  Run: `cmake --build build --target pedal-lvgl-ui-smoke && ./build/pedal-lvgl-ui-smoke`

  Expected: FAIL with `native arc knob should be transparent` because the old code sets only the generic part opacity.

- [ ] **Step 3: Implement the clean custom needle**

  In `createKnob`, replace the native knob style with its background opacity and make generic dial objects non-scrollable:

  ```cpp
  lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_remove_flag(rim, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(centre, LV_OBJ_FLAG_SCROLLABLE);
  ```

  Create the pointer on `knob`, after the rim and centre, preserving its 3 x 20 body and `1, 21` pivot. Place its pivot at the dial centre in knob coordinates:

  ```cpp
  lv_obj_t* pointer = lv_obj_create(knob);
  lv_obj_set_size(pointer, 3, 20);
  lv_obj_set_pos(pointer, 76, 27);
  lv_obj_set_style_bg_color(pointer, lv_color_hex(text), 0);
  lv_obj_set_style_border_width(pointer, 0, 0);
  lv_obj_set_style_radius(pointer, 2, 0);
  lv_obj_set_style_transform_pivot_x(pointer, 1, LV_PART_MAIN);
  lv_obj_set_style_transform_pivot_y(pointer, 21, LV_PART_MAIN);
  lv_obj_set_style_transform_rotation(pointer, static_cast<int32_t>((45.0f + ratio * 270.0f) * 10.0f), 0);
  lv_obj_remove_flag(pointer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(pointer, LV_OBJ_FLAG_CLICKABLE);
  ```

- [ ] **Step 4: Run the focused test to verify it passes**

  Run: `cmake --build build --target pedal-lvgl-ui-smoke && ./build/pedal-lvgl-ui-smoke`

  Expected: exit 0.

- [ ] **Step 5: Run full verification and commit**

  Run: `cmake --build build && ctest --test-dir build --output-on-failure && git diff --check`

  Expected: build succeeds, 13/13 tests pass, and `git diff --check` emits no output.

  ```bash
  git add src/ui/LvglUi.cpp tests/lvgl_ui_smoke.cpp
  git commit -m "fix: render clean LVGL knob needles"
  ```
