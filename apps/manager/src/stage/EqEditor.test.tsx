import { act, fireEvent, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import type { Preset } from "../api/types";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { blockOf } from "../test/blocks";
import { EqEditor } from "./EqEditor";
import { renderWithEditor } from "./renderWithEditor";

const preset: Preset = {
  version: 1, name: "EQ", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [blockOf("eq:parametric_eq_5", "e1")],
};
const session = {
  status: "connected" as const, current: { location: { bank: 0, slot: 0 }, preset: structuredClone(preset), exists: true },
  device: { active: { bank: 0, slot: 0 }, capabilities: {} }, presets: [], irs: [], reverbIrs: [], models: [],
  busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(), applyCurrent: vi.fn(), refreshPresets: vi.fn(), selectLocation: vi.fn(async () => undefined),
};
vi.mock("../connection/deviceSession", () => ({ useDeviceSession: () => session }));

function EqHarness() {
  const editor = usePresetEditorContext();
  return <EqEditor block={editor.present.blocks[0]}
    onEqBand={(blockId, band, patch, gesture) => editor.dispatch({ type: "set-eq-band", blockId, band, patch, gesture })}
    onParam={(blockId, key, value, gesture) => editor.editParameter(blockId, key, value, gesture)} />;
}

describe("EqEditor", () => {
  it("makes one node drag one undo step", () => {
    const { editor } = renderWithEditor(<EqHarness />);
    const start = structuredClone(editor().present.blocks[0].params);
    const graph = screen.getByRole("img", { name: "EQ response graph" });
    fireEvent.pointerDown(screen.getByRole("button", { name: "Adjust Band 3" }), { pointerId: 1, clientX: 330, clientY: 100 });
    for (const [clientX, clientY] of [[340, 90], [360, 80], [380, 60]]) fireEvent.pointerMove(graph, { pointerId: 1, clientX, clientY });
    fireEvent.pointerUp(graph, { pointerId: 1 });
    expect(editor().editor.history.past).toHaveLength(1);
    act(() => editor().dispatch({ type: "undo" }));
    expect(editor().present.blocks[0].params).toEqual(start);
  });

  it("starts a new undo step for the next drag", () => {
    const { editor } = renderWithEditor(<EqHarness />);
    const graph = screen.getByRole("img", { name: "EQ response graph" });
    for (const x of [300, 420]) {
      fireEvent.pointerDown(screen.getByRole("button", { name: "Adjust Band 3" }), { pointerId: 1, clientX: x, clientY: 100 });
      fireEvent.pointerMove(graph, { pointerId: 1, clientX: x + 10, clientY: 70 });
      fireEvent.pointerUp(graph, { pointerId: 1 });
    }
    expect(editor().editor.history.past).toHaveLength(2);
  });
});
