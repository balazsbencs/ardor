import { afterEach, describe, expect, it, vi } from "vitest";

import { createEmptyPreset } from "./presetFactory";
import {
  createDebouncedRecoveryWriter,
  createUnsavedNavigationController,
  readRecovery,
  recoveryKey,
  removeRecovery,
  writeRecovery,
} from "./recovery";

const location = { bank: 4, slot: 2 };

afterEach(() => {
  localStorage.clear();
  vi.useRealTimers();
});

describe("recovery records", () => {
  it("creates, restores, and removes a matching dirty recovery", () => {
    const saved = createEmptyPreset("Saved");
    const draft = { ...saved, name: "Draft" };
    writeRecovery(localStorage, "http://pedal", location, draft, saved, 123);
    expect(readRecovery(localStorage, "http://pedal", location, saved)).toMatchObject({
      status: "available", record: { draft: { name: "Draft" }, timestamp: 123 },
    });
    removeRecovery(localStorage, "http://pedal", location);
    expect(localStorage.getItem(recoveryKey("http://pedal", location))).toBeNull();
  });

  it("labels a recovery stale when its saved basis changed", () => {
    const saved = createEmptyPreset("Old");
    writeRecovery(localStorage, "http://pedal", location, { ...saved, name: "Draft" }, saved);
    expect(readRecovery(localStorage, "http://pedal", location, createEmptyPreset("New")).status).toBe("stale");
  });

  it("debounces dirty writes and removes clean records", () => {
    vi.useFakeTimers();
    const saved = createEmptyPreset("Saved");
    const writer = createDebouncedRecoveryWriter(localStorage, 500);
    writer.schedule("http://pedal", location, { ...saved, name: "First" }, saved);
    writer.schedule("http://pedal", location, { ...saved, name: "Latest" }, saved);
    expect(localStorage.length).toBe(0);
    vi.advanceTimersByTime(500);
    expect(JSON.parse(localStorage.getItem(recoveryKey("http://pedal", location)) ?? "null").draft.name).toBe("Latest");
    writer.schedule("http://pedal", location, saved, saved);
    vi.advanceTimersByTime(500);
    expect(localStorage.length).toBe(0);
  });
});

describe("unsaved navigation controller", () => {
  it("continues immediately when clean", async () => {
    const navigate = vi.fn();
    const controller = createUnsavedNavigationController({ isDirty: () => false, save: vi.fn(), discard: vi.fn() });
    await expect(controller.request(navigate)).resolves.toBe("continued");
    expect(navigate).toHaveBeenCalledOnce();
  });

  it("supports Save, Discard, Cancel, and failed-save branches", async () => {
    const navigate = vi.fn();
    const discard = vi.fn();
    const save = vi.fn(async () => false);
    const controller = createUnsavedNavigationController({ isDirty: () => true, save, discard });

    await expect(controller.request(navigate)).resolves.toBe("prompt");
    await expect(controller.resolve("save")).resolves.toBe(false);
    expect(navigate).not.toHaveBeenCalled();
    save.mockResolvedValueOnce(true);
    await expect(controller.resolve("save")).resolves.toBe(true);
    expect(navigate).toHaveBeenCalledOnce();

    await controller.request(navigate);
    await expect(controller.resolve("cancel")).resolves.toBe(false);
    expect(navigate).toHaveBeenCalledOnce();

    await controller.request(navigate);
    await expect(controller.resolve("discard")).resolves.toBe(true);
    expect(discard).toHaveBeenCalledOnce();
    expect(navigate).toHaveBeenCalledTimes(2);
  });
});
