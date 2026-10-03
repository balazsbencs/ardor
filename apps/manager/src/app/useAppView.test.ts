import { act, renderHook } from "@testing-library/react";
import { expect, it } from "vitest";

import { useAppView } from "./useAppView";

it("keeps the view in the hash", () => {
  window.history.replaceState(null, "", "/#assets/irs");
  const { result } = renderHook(() => useAppView());
  expect(result.current).toMatchObject({ view: "assets", assetKind: "irs" });
  act(() => result.current.goto("edit"));
  expect(window.location.hash).toBe("");
  expect(result.current.view).toBe("edit");
});

it("follows a hash change and ignores an unknown asset kind", () => {
  window.history.replaceState(null, "", "/");
  const { result } = renderHook(() => useAppView());
  expect(result.current.view).toBe("edit");
  act(() => {
    window.history.replaceState(null, "", "/#assets/songs");
    window.dispatchEvent(new HashChangeEvent("hashchange"));
  });
  expect(result.current).toMatchObject({ view: "assets", assetKind: undefined });
  act(() => result.current.goto("assets", "models"));
  expect(window.location.hash).toBe("#assets/models");
  window.history.replaceState(null, "", "/");
});
