import { act, renderHook } from "@testing-library/react";
import { expect, it, vi } from "vitest";

import { useFileDrop } from "./useFileDrop";

function dragEvent(type: string, files: File[] = [], types = ["Files"]) {
  const event = new Event(type, { bubbles: true, cancelable: true }) as DragEvent;
  Object.defineProperty(event, "dataTransfer", { value: { types, files } });
  return event;
}

it("reports dragging and hands over dropped files", () => {
  const onFiles = vi.fn();
  const { result } = renderHook(() => useFileDrop(onFiles));
  act(() => { window.dispatchEvent(dragEvent("dragenter")); });
  expect(result.current).toBe(true);
  const file = new File(["x"], "A.nam");
  act(() => { window.dispatchEvent(dragEvent("drop", [file])); });
  expect(onFiles).toHaveBeenCalledWith([file]);
  expect(result.current).toBe(false);
});

it("ignores drags that carry no files", () => {
  const onFiles = vi.fn();
  const { result } = renderHook(() => useFileDrop(onFiles));
  act(() => { window.dispatchEvent(dragEvent("dragenter", [], ["text/plain"])); });
  expect(result.current).toBe(false);
  act(() => { window.dispatchEvent(dragEvent("drop", [], ["text/plain"])); });
  expect(onFiles).not.toHaveBeenCalled();
});

it("stays dragging until the last leave and accepts the drop by cancelling dragover", () => {
  const { result } = renderHook(() => useFileDrop(vi.fn()));
  act(() => { window.dispatchEvent(dragEvent("dragenter")); window.dispatchEvent(dragEvent("dragenter")); });
  act(() => { window.dispatchEvent(dragEvent("dragleave")); });
  expect(result.current).toBe(true);
  act(() => { window.dispatchEvent(dragEvent("dragleave")); });
  expect(result.current).toBe(false);
  const over = dragEvent("dragover");
  window.dispatchEvent(over);
  expect(over.defaultPrevented).toBe(true);
});

it("stops listening after unmount", () => {
  const onFiles = vi.fn();
  const { unmount } = renderHook(() => useFileDrop(onFiles));
  unmount();
  window.dispatchEvent(dragEvent("drop", [new File(["x"], "A.nam")]));
  expect(onFiles).not.toHaveBeenCalled();
});
