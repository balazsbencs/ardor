import { expect, test } from "@playwright/test";

import { defaultState, mockApi, type MockState } from "./mockApi";

let pedal: { state(): MockState };

test.beforeEach(async ({ page }) => {
  pedal = await mockApi(page, defaultState());
  await page.goto("/#assets");
  await expect(page.getByRole("button", { name: "Glass Clean", exact: true })).toBeVisible();
});

test("uploads with a name conflict and rejects a wrong file type", async ({ page }) => {
  await page.locator('input[type="file"]').setInputFiles([
    { name: "Brown Sound.nam", mimeType: "application/octet-stream", buffer: Buffer.from("nam v2") },
    { name: "Liquid Lead.nam", mimeType: "application/octet-stream", buffer: Buffer.from("nam") },
    { name: "notes.txt", mimeType: "text/plain", buffer: Buffer.from("hi") },
  ]);
  await expect(page.getByText("Brown Sound.nam is already on the pedal.", { exact: false })).toBeVisible();
  await expect(page.getByText("notes.txt is not a .nam or .wav file.", { exact: false })).toBeVisible();
  await expect(page.getByRole("button", { name: "Liquid Lead", exact: true })).toBeVisible();
  await expect(page.getByText("Liquid Lead.nam uploaded to NAM models.")).toBeVisible();
  await page.getByRole("button", { name: "Replace", exact: true }).click();
  await expect(page.getByText("Brown Sound.nam uploaded to NAM models.")).toBeVisible();
  await expect(page.getByText("Brown Sound.nam is already on the pedal.", { exact: false })).toBeHidden();
  const models = pedal.state().assets.models;
  expect(models.map(({ filename }) => filename)).toEqual(["Brown Sound.nam", "Glass Clean.nam", "Liquid Lead.nam", "Plexi Lead.nam"]);
  expect(models.find(({ filename }) => filename === "Brown Sound.nam")?.sizeBytes).toBe("nam v2".length);
});

test("renames a file and reports the presets", async ({ page }) => {
  await page.getByRole("button", { name: "Glass Clean", exact: true }).click();
  await page.getByRole("region", { name: "Glass Clean.nam" }).getByRole("button", { name: "Rename", exact: true }).click();
  await expect(page.getByText("1 saved preset will use the new name.", { exact: false })).toBeVisible();
  await page.getByRole("textbox", { name: "New file name" }).fill("Glass Clean v2.nam");
  await page.keyboard.press("Enter");
  await expect(page.getByText("Renamed to Glass Clean v2.nam. 1 saved preset uses the new name.")).toBeVisible();
  expect(pedal.state().presets["0:0"].blocks.find(({ id }) => id === "nam-1")?.asset).toBe("models/Glass Clean v2.nam");
  // The open preset follows the new name on the stage.
  await page.getByRole("button", { name: "Edit", exact: true }).click();
  await expect(page.getByRole("group", { name: /^Glass Clean v2,/ })).toBeVisible();
});

test("asks before it deletes, in the rail", async ({ page }) => {
  await page.getByRole("button", { name: "Delete Glass Clean.nam" }).click();
  const confirm = page.getByRole("alertdialog", { name: "Confirm delete" });
  await expect(confirm).toContainText("Glass Cathedral uses it.");
  await confirm.getByRole("button", { name: "Cancel" }).click();
  await expect(confirm).toBeHidden();

  await page.getByRole("button", { name: "Delete Plexi Lead.nam" }).click();
  await expect(confirm).toContainText("No preset uses it.");
  await expect(confirm).toContainText("You cannot undo this.");
  await confirm.getByRole("button", { name: "Delete file" }).click();
  await expect(page.getByRole("button", { name: "Plexi Lead", exact: true })).toBeHidden();
  await expect(page.getByText("Plexi Lead.nam deleted from the pedal.")).toBeVisible();
  expect(pedal.state().assets.models.map(({ filename }) => filename)).not.toContain("Plexi Lead.nam");
});

test("shows a missing file and opens the preset from it", async ({ page }) => {
  await expect(page.getByText("Doom Fuzz needs Fuzz Stack.nam, which is not on the pedal.")).toBeVisible();
  await page.getByRole("button", { name: "Pick another" }).click();
  await expect(page.getByRole("textbox", { name: "Preset name" })).toHaveValue("Doom Fuzz");
  await expect(page.getByRole("group", { name: /^Fuzz Stack,/ })).toContainText("FILE MISSING");
});
