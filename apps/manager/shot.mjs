import { chromium } from "@playwright/test";
const OUT = "/private/tmp/claude-501/-Users-bbalazs-Documents-Ardor/4cd745a0-e95a-48f3-a0cb-32cf84660256/scratchpad";
const browser = await chromium.launch();

async function run(name, width, height, theme) {
  const page = await browser.newPage({ viewport: { width, height } });
  await page.addInitScript((t) => localStorage.setItem("ardor-manager.theme", t), theme);
  await page.goto("http://localhost:5199/", { waitUntil: "networkidle" });
  await page.screenshot({ path: `${OUT}/${name}-shell.png` });
  // the connection dialog is a Radix portal — the surface fix shows here or nowhere
  await page.getByRole("button", { name: "Connect to device" }).click();
  await page.waitForTimeout(250);
  await page.screenshot({ path: `${OUT}/${name}-dialog.png` });
  await page.keyboard.press("Escape");
  await page.waitForTimeout(200);
  await page.getByRole("button", { name: "Open settings" }).click();
  await page.waitForTimeout(400);
  await page.screenshot({ path: `${OUT}/${name}-settings.png` });
  await page.close();
}

await run("dark-desktop", 1440, 900, "dark");
await run("light-desktop", 1440, 900, "light");
await run("dark-mobile", 390, 844, "dark");
await browser.close();
console.log("done");
