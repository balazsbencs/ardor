import { readFileSync } from "node:fs";
import { expect, test } from "@playwright/test";
import { defaultState, mockApi } from "./mockApi";

test("shows the trainer's combined export once and restores Nano with its controls", async ({ page }, testInfo) => {
  const fixture = JSON.parse(readFileSync(new URL("../../../tests/fixtures/parametric-trainer-a2-dual.json", import.meta.url), "utf8"));
  const model = fixture.models[0].model;
  const state = defaultState();
  state.presets["0:0"].blocks = state.presets["0:0"].blocks.filter((b) => b.type === "nam");
  state.assets.models.push({ id: "Trainer A2.nam", kind: "model", filename: "Trainer A2.nam",
    path: "models/Trainer A2.nam", sizeBytes: JSON.stringify(model).length,
    nam: { architecture: model.architecture, version: model.version, gearType: "pedal", supported: true,
      slimmable: true, parameters: model.config.submodels[0].model.config.params } });
  const pedal = await mockApi(page, state);
  await page.goto("/");
  await expect(page.getByRole("textbox", { name: "Preset name" })).toHaveValue("Glass Cathedral");
  await page.getByRole("button", { name: "Add block", exact: true }).filter({ visible: true }).first().click();
  await page.getByRole("searchbox", { name: "Search blocks" }).fill("Trainer A2");
  const module = page.getByRole("button", { name: /Trainer A2/ });
  await expect(module).toHaveCount(1);
  await module.click();
  await page.getByRole("group", { name: /^Trainer A2,/ }).click();
  const size = page.getByRole("radiogroup", { name: "Model size" });
  await expect(size.getByRole("radio", { name: "Standard", exact: true })).toHaveAttribute("aria-checked", "true");
  await page.getByRole("slider", { name: "Drive", exact: true }).press("End");
  await page.getByRole("radiogroup", { name: "Mode", exact: true }).getByRole("radio", { name: "Hard", exact: true }).click();
  await size.getByRole("radio", { name: "Nano", exact: true }).click();
  await expect(page.getByRole("slider", { name: "Drive", exact: true })).toHaveAttribute("aria-valuenow", "10");
  await page.getByRole("button", { name: "Save", exact: true }).filter({ visible: true }).click();
  await expect.poll(() => pedal.state().presets["0:0"].blocks.find((b) => b.type === "namPedal")?.params)
    .toMatchObject({ useNano: true, modelParameters: { Drive: 10, Mode: 1 } });
  await page.screenshot({ path: `/tmp/ardor-dual-model-manager-${testInfo.project.name}.png`, fullPage: true });
  expect(await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth)).toBe(false);
  await page.reload();
  await page.getByRole("group", { name: /^Trainer A2,/ }).click();
  await expect(page.getByRole("radiogroup", { name: "Model size" }).getByRole("radio", { name: "Nano", exact: true }))
    .toHaveAttribute("aria-checked", "true");
  await page.getByRole("radiogroup", { name: "Model size" }).getByRole("radio", { name: "Standard", exact: true }).click();
  await expect(page.getByRole("slider", { name: "Drive", exact: true })).toHaveAttribute("aria-valuenow", "10");
  await expect(page.getByRole("radiogroup", { name: "Mode", exact: true }).getByRole("radio", { name: "Hard", exact: true }))
    .toHaveAttribute("aria-checked", "true");
});

test("adds the open parametric pedal and restores its saved controls", async ({ page }, testInfo) => {
  const model = JSON.parse(readFileSync(new URL("../../../assets/nam/Ardor-Parametric-Overdrive-Demo.nam", import.meta.url), "utf8"));
  const state = defaultState();
  state.presets["0:0"].blocks = state.presets["0:0"].blocks.filter((b) => b.type === "nam");
  state.assets.models.push({ id: "Ardor Parametric Overdrive Demo.nam", kind: "model", filename: "Ardor Parametric Overdrive Demo.nam",
    path: "models/Ardor Parametric Overdrive Demo.nam", sizeBytes: 18000,
    nam: { architecture: model.architecture, version: model.version, gearType: "pedal", supported: true, parameters: model.config.params } });
  const pedal = await mockApi(page, state);
  await page.goto("/");
  await expect(page.getByRole("textbox", { name: "Preset name" })).toHaveValue("Glass Cathedral");
  await page.getByRole("button", { name: "Add block", exact: true }).filter({ visible: true }).first().click();
  await page.getByRole("searchbox", { name: "Search blocks" }).fill("Ardor Parametric Overdrive Demo");
  await page.getByRole("button", { name: /Ardor Parametric Overdrive Demo/ }).click();
  await page.getByRole("group", { name: /^Ardor Parametric Overdrive Demo,/ }).click();
  const drive = page.getByRole("slider", { name: "Drive", exact: true });
  await expect(drive).toHaveAttribute("aria-valuenow", "5");
  await drive.press("End");
  await page.getByRole("slider", { name: "Tone", exact: true }).press("Home");
  await page.getByRole("button", { name: "Save", exact: true }).filter({ visible: true }).click();
  await expect.poll(() => pedal.state().presets["0:0"].blocks.find((b) => b.type === "namPedal")?.params.modelParameters)
    .toEqual({ Drive: 10, Tone: 0 });
  await page.screenshot({ path: `/tmp/ardor-parametric-manager-${testInfo.project.name}.png`, fullPage: true });
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth);
  expect(overflow).toBe(false);
  await page.reload();
  await page.getByRole("group", { name: /^Ardor Parametric Overdrive Demo,/ }).click();
  await expect(page.getByRole("slider", { name: "Drive", exact: true })).toHaveAttribute("aria-valuenow", "10");
  await expect(page.getByRole("slider", { name: "Tone", exact: true })).toHaveAttribute("aria-valuenow", "0");
});

test("adds Clone Overdrive with FiLM controls and restores its compact tier", async ({ page }, testInfo) => {
  const model = JSON.parse(readFileSync(new URL("../../../assets/nam/clone-overdrive.nam", import.meta.url), "utf8"));
  const state = defaultState();
  state.presets["0:0"].blocks = state.presets["0:0"].blocks.filter((b) => b.type === "nam");
  state.assets.models.push({ id: "Clone Overdrive.nam", kind: "model", filename: "Clone Overdrive.nam",
    path: "models/Clone Overdrive.nam", sizeBytes: 18000,
    nam: { architecture: model.architecture, version: model.version, gearType: "pedal", supported: true, slimmable: true, parameters: model.config.submodels[0].model.config.parametric.parameters } });
  const pedal = await mockApi(page, state);
  await page.goto("/");
  await expect(page.getByRole("textbox", { name: "Preset name" })).toHaveValue("Glass Cathedral");
  await page.getByRole("button", { name: "Add block", exact: true }).filter({ visible: true }).first().click();
  await page.getByRole("searchbox", { name: "Search blocks" }).fill("Clone Overdrive");
  await page.getByRole("button", { name: /Clone Overdrive/ }).click();
  await page.getByRole("group", { name: /^Clone Overdrive,/ }).click();
  const drive = page.getByRole("slider", { name: "Gain", exact: true });
  await expect(drive).toHaveAttribute("aria-valuenow", "0.5");
  await drive.press("End");
  await page.getByRole("slider", { name: "Tone", exact: true }).press("Home");
  await page.getByRole("radiogroup", { name: "Model size" }).getByRole("radio", { name: "Nano", exact: true }).click();
  await page.getByRole("button", { name: "Save", exact: true }).filter({ visible: true }).click();
  await expect.poll(() => pedal.state().presets["0:0"].blocks.find((b) => b.type === "namPedal")?.params.modelParameters)
    .toEqual({ Gain: 1, Tone: 0 });
  expect(pedal.state().presets["0:0"].blocks.find((b) => b.type === "namPedal")?.params.useNano).toBe(true);
  await page.screenshot({ path: `/tmp/ardor-clone-overdrive-manager-${testInfo.project.name}.png`, fullPage: true });
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth);
  expect(overflow).toBe(false);
  await page.reload();
  await page.getByRole("group", { name: /^Clone Overdrive,/ }).click();
  await expect(page.getByRole("slider", { name: "Gain", exact: true })).toHaveAttribute("aria-valuenow", "1");
  await expect(page.getByRole("radiogroup", { name: "Model size" }).getByRole("radio", { name: "Nano", exact: true })).toHaveAttribute("aria-checked", "true");
  await expect(page.getByRole("slider", { name: "Tone", exact: true })).toHaveAttribute("aria-valuenow", "0");
});


test("adds a FiLM amp with learned controls and saves/loads the selected tier", async ({ page }) => {
  const fixture = JSON.parse(readFileSync(new URL("../../../tests/fixtures/parametric-trainer-film.json", import.meta.url), "utf8"));
  const model = fixture.models.find((entry: { label: string }) => entry.label === "film-dual").model;
  model.metadata = { ...model.metadata, gear_type: "amp" };
  const state = defaultState();
  state.presets["0:0"].blocks = [];
  state.assets.models.push({ id: "Trainer FiLM Amp.nam", kind: "model", filename: "Trainer FiLM Amp.nam",
    path: "models/Trainer FiLM Amp.nam", sizeBytes: 10000,
    nam: { architecture: model.architecture, version: model.version, gearType: "amp", supported: true, slimmable: true,
      parameters: model.config.submodels[0].model.config.parametric.parameters } });
  const pedal = await mockApi(page, state);
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  await expect(page.getByRole("textbox", { name: "Preset name" })).toHaveValue("Glass Cathedral");
  await page.getByRole("button", { name: "Add block", exact: true }).filter({ visible: true }).first().click();
  await page.getByRole("searchbox", { name: "Search blocks" }).fill("Trainer FiLM Amp");
  await page.getByRole("button", { name: /Trainer FiLM Amp/ }).click();
  await page.getByRole("group", { name: /^Trainer FiLM Amp,/ }).click();
  await page.getByRole("slider", { name: "Drive", exact: true }).press("End");
  await page.getByRole("slider", { name: "Tone", exact: true }).press("Home");
  await page.getByRole("radiogroup", { name: "Model size" }).getByRole("radio", { name: "Nano", exact: true }).click();
  const applyResponse = page.waitForResponse((response) => response.url().includes("/api/runtime/apply/") && response.ok());
  await page.getByRole("button", { name: "Save and load", exact: true }).filter({ visible: true }).click();
  await expect.poll(() => pedal.state().presets["0:0"].blocks[0]).toMatchObject({
    type: "nam", asset: "models/Trainer FiLM Amp.nam", params: { useNano: true, modelParameters: { Drive: 10, Tone: -5 } },
  });
  expect(await (await applyResponse).json()).toMatchObject({ state: "applied", bank: 0, slot: 0 });
  await page.reload();
  await page.getByRole("group", { name: /^Trainer FiLM Amp,/ }).click();
  await expect(page.getByRole("slider", { name: "Drive", exact: true })).toHaveAttribute("aria-valuenow", "10");
  await expect(page.getByRole("slider", { name: "Tone", exact: true })).toHaveAttribute("aria-valuenow", "-5");
  await expect(page.getByRole("radiogroup", { name: "Model size" }).getByRole("radio", { name: "Nano", exact: true })).toHaveAttribute("aria-checked", "true");
  expect(errors).toEqual([]);
  expect(await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth)).toBe(false);
});
