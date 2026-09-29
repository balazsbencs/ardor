import { describe, expect, it } from "vitest";

import { bankLabel, fileSize, fileStem, slotLabel, splitDisplay } from "./format";

describe("format", () => {
  it("formats banks like the pedal header", () => {
    expect(bankLabel(0)).toBe("BANK 00");
    expect(bankLabel(99)).toBe("BANK 99");
    expect(slotLabel(0)).toBe("FS 1");
  });

  it("splits a display value into number and unit", () => {
    expect(splitDisplay("412 ms")).toEqual({ value: "412", unit: "ms" });
    expect(splitDisplay("-1.0 dB")).toEqual({ value: "-1.0", unit: "dB" });
    expect(splitDisplay("4:1")).toEqual({ value: "4:1", unit: "" });
    expect(splitDisplay("Dotted 8ths")).toEqual({ value: "Dotted 8ths", unit: "" });
  });

  it("formats file sizes and stems", () => {
    expect(fileSize(96044)).toBe("94 KB");
    expect(fileSize(2310400)).toBe("2.2 MB");
    expect(fileStem("Glass Clean.nam")).toBe("Glass Clean");
  });
});
