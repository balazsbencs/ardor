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
    expect(splitDisplay("30%")).toEqual({ value: "30", unit: "%" });
    expect(splitDisplay("1.5 s")).toEqual({ value: "1.5", unit: "s" });
    expect(splitDisplay("2.3x")).toEqual({ value: "2.3", unit: "x" });
    expect(splitDisplay("12 bit")).toEqual({ value: "12", unit: "bit" });
    expect(splitDisplay("-12.0 dBFS")).toEqual({ value: "-12.0", unit: "dBFS" });
    expect(splitDisplay("0.50")).toEqual({ value: "0.50", unit: "" });
  });

  it("leaves labels that merely start with a digit whole", () => {
    expect(splitDisplay("3rd down")).toEqual({ value: "3rd down", unit: "" });
    expect(splitDisplay("16th")).toEqual({ value: "16th", unit: "" });
    expect(splitDisplay("2 stages")).toEqual({ value: "2 stages", unit: "" });
    expect(splitDisplay("Q 1.2")).toEqual({ value: "Q 1.2", unit: "" });
  });

  it("formats file sizes and stems", () => {
    expect(fileSize(96044)).toBe("94 KB");
    expect(fileSize(2310400)).toBe("2.2 MB");
    expect(fileStem("Glass Clean.nam")).toBe("Glass Clean");
  });
});
