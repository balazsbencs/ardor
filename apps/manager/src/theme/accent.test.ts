import { describe, expect, it } from "vitest";

import { paletteVariables } from "./accent";

describe("palettes", () => {
  it("gives Slate the Lamp Black values from src/ui/LvglUiStyle.cpp", () => {
    const vars = paletteVariables("slate") as Record<string, string>;
    expect(vars["--bg"]).toBe("#0b0c0d");
    expect(vars["--surface"]).toBe("#16181a");
    expect(vars["--lamp"]).toBe("#e8472f");
    expect(vars["--plate-hi"]).toBe("#202326");
    expect(vars["--lamp-ink"]).toBe("#1a0b08");
    expect(vars["--delay"]).toBe("#9a82d6");
  });
});
