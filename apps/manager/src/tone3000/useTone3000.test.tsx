import { act, renderHook } from "@testing-library/react";
import { afterEach, describe, expect, it, vi } from "vitest";

import { useTone3000 } from "./useTone3000";

const session = { device: { capabilities: { tone3000: true } }, refreshAssets: vi.fn() };
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

afterEach(() => {
  vi.restoreAllMocks();
  sessionStorage.clear();
});

describe("useTone3000", () => {
  it("is available on a device with TONE3000 and only for models", () => {
    expect(renderHook(() => useTone3000()).result.current.available).toBe(true);
    expect(renderHook(() => useTone3000(undefined, { kind: "irs" })).result.current.available).toBe(false);
  });

  it("shows the intro first, then explains a blocked pop-up", () => {
    const { result } = renderHook(() => useTone3000());
    act(() => result.current.browse());
    expect(result.current.phase).toBe("intro");
    vi.spyOn(window, "open").mockReturnValue(null);
    act(() => result.current.continueFlow());
    expect(result.current.error).toBe("Allow pop-ups for Ardor to browse TONE3000.");
  });
});
