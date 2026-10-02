import { expect, test, type Page } from "@playwright/test";

import { defaultState, mockApi, type MockState } from "./mockApi";

/** Block titles in chain order, from the card labels ("Tape Delay, Delay, on"). */
const chainNames = (page: Page) => page.getByRole("region", { name: "Signal chain" }).getByRole("group")
  .evaluateAll((elements) => elements.map((element) => element.getAttribute("aria-label")!.split(",")[0]));

/** The app bar and the stage head both carry MODIFIED; the phone hides the app bar one. */
const modified = (page: Page) => page.getByText("MODIFIED", { exact: true }).filter({ visible: true }).first();

/** Raw mouse input during a view transition hits the transition overlay, so wait until the drawer has settled. */
const transitionDone = (page: Page) => page.waitForFunction(() =>
  !CSS.supports("selector(:active-view-transition)") || !document.documentElement.matches(":active-view-transition"));

const savedNames = (state: MockState) => state.presets["0:0"].blocks.map(({ id }) => id);

let pedal: { state(): MockState };

test.beforeEach(async ({ page }) => {
  pedal = await mockApi(page, defaultState());
  await page.goto("/");
  await expect(page.getByRole("button", { name: "Device: Ardor" })).toBeVisible();
  await expect(page.getByRole("textbox", { name: "Preset name" })).toHaveValue("Glass Cathedral");
});

test("reorders a block by dragging its cap and saves", async ({ page, isMobile }) => {
  test.skip(isMobile, "Mouse drag; the phone uses long-press, and the keyboard move is covered below");
  const rat = page.getByRole("group", { name: /^RAT Distortion,/ });
  const cab = page.getByRole("group", { name: /^Open Back 2x12,/ });
  const from = await rat.locator(".blk__cap").boundingBox();
  const to = await cab.boundingBox();
  // Grab the middle of the cap and drop with the card's centre just right of the cab's centre (closestCenter).
  await page.mouse.move(from!.x + from!.width / 2, from!.y + from!.height / 2);
  await page.mouse.down();
  await page.mouse.move(to!.x + to!.width / 2 + 12, to!.y + from!.height / 2, { steps: 12 });
  await page.mouse.up();
  await expect.poll(() => chainNames(page)).toEqual([
    "Noise Gate", "Compressor", "Glass Clean", "Open Back 2x12", "RAT Distortion",
    "Five Band Parametric EQ", "Chorus", "Tape Delay", "Shimmer Reverb",
  ]);
  await expect(modified(page)).toBeVisible();
  await page.getByRole("button", { name: "Save", exact: true }).click();
  await expect(modified(page)).toBeHidden();
  expect(savedNames(pedal.state())).toEqual(["gate-1", "cmp-1", "nam-1", "cab-1", "rat-1", "eq-1", "cho-1", "tape-1", "verb-1"]);
});

test("edits a value in the drawer and undoes the whole drag at once", async ({ page }) => {
  await page.getByRole("group", { name: /^Tape Delay,/ }).click();
  await expect(page.getByRole("region", { name: "Tape Delay parameters" })).toBeVisible();
  await transitionDone(page);
  const mix = page.getByRole("slider", { name: "Mix", exact: true });
  const before = await mix.getAttribute("aria-valuenow");
  await mix.scrollIntoViewIfNeeded();
  const box = await mix.boundingBox();
  await page.mouse.move(box!.x + 5, box!.y + box!.height / 2);
  await page.mouse.down();
  // Past the right end: pointer capture keeps the drag, and the value stops at the maximum.
  await page.mouse.move(box!.x + box!.width + 20, box!.y + box!.height / 2, { steps: 10 });
  await page.mouse.up();
  await expect(mix).toHaveAttribute("aria-valuenow", "1");
  await page.keyboard.press("ControlOrMeta+z");
  await expect(mix).toHaveAttribute("aria-valuenow", before!);
  // One undo step took back the whole drag, so nothing is left to undo.
  await expect(page.getByRole("button", { name: "Undo" })).toBeDisabled();
});

test("moves a block with the keyboard", async ({ page }) => {
  await page.getByRole("group", { name: /^Noise Gate,/ }).focus();
  await page.keyboard.press("Alt+ArrowRight");
  await expect.poll(async () => (await chainNames(page)).slice(0, 2)).toEqual(["Compressor", "Noise Gate"]);
  await expect(modified(page)).toBeVisible();
});

test("keeps the draft when the Assets view opens and closes", async ({ page }) => {
  await page.getByRole("group", { name: /^Tape Delay,/ }).click();
  const mix = page.getByRole("slider", { name: "Mix", exact: true });
  await mix.press("ArrowRight");
  await expect(mix).toHaveAttribute("aria-valuenow", "0.31");
  await page.getByRole("button", { name: "Assets", exact: true }).click();
  await expect(page.getByRole("region", { name: "File type" })).toBeVisible();
  await page.getByRole("button", { name: "Edit", exact: true }).click();
  await expect(modified(page)).toBeVisible();
  await page.getByRole("group", { name: /^Tape Delay,/ }).click();
  await expect(page.getByRole("slider", { name: "Mix", exact: true })).toHaveAttribute("aria-valuenow", "0.31");
});

test("saves and loads a changed draft on the pedal", async ({ page }) => {
  await page.getByRole("group", { name: /^Noise Gate,/ }).focus();
  await page.keyboard.press("Alt+ArrowRight");
  await page.getByRole("button", { name: "Save and load" }).click();
  await expect(page.getByText("LIVE ON PEDAL", { exact: true }).filter({ visible: true }).first()).toBeVisible();
  await expect(modified(page)).toBeHidden();
  expect(savedNames(pedal.state()).slice(0, 2)).toEqual(["cmp-1", "gate-1"]);
});
