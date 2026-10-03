import { act, render, renderHook, screen } from "@testing-library/react";
import type { ReactNode } from "react";
import { describe, expect, it, vi } from "vitest";

import type { Preset } from "../../api/types";
import { EditorProvider, usePresetEditorContext } from "./EditorContext";

const preset: Preset = {
  version: 1, name: "Clean", routing: "serial", global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 },
  blocks: [{ id: "b1", type: "dynamics", enabled: true, asset: "", params: { mode: "compressor", threshold_db: -18 } }],
};
const session = {
  status: "connected" as const, current: { location: { bank: 0, slot: 0 }, preset, exists: true },
  device: { active: { bank: 0, slot: 0 }, capabilities: {} }, models: [], irs: [], reverbIrs: [], presets: [],
  busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(), applyCurrent: vi.fn(), refreshPresets: vi.fn(), selectLocation: vi.fn(async () => undefined),
};
vi.mock("../../connection/deviceSession", () => ({ useDeviceSession: () => session }));

const wrapper = ({ children }: { children: ReactNode }) => <EditorProvider>{children}</EditorProvider>;

describe("EditorProvider", () => {
  it("shares one draft between two consumers, so a view switch keeps edits", () => {
    const first = renderHook(() => usePresetEditorContext(), { wrapper });
    act(() => first.result.current.editParameter("b1", "threshold_db", -30));
    expect(first.result.current.dirty).toBe(true);
    function Probe() { return <p>{String(usePresetEditorContext().present.blocks[0].params.threshold_db)}</p>; }
    render(<EditorProvider><Probe /></EditorProvider>);
    expect(screen.getByText("-18")).toBeInTheDocument();
  });

  it("holds a dirty navigation until the person chooses", () => {
    const { result } = renderHook(() => usePresetEditorContext(), { wrapper });
    act(() => result.current.editParameter("b1", "threshold_db", -30));
    act(() => result.current.selectLocation({ bank: 0, slot: 1 }));
    expect(result.current.pendingLocation).toEqual({ bank: 0, slot: 1 });
    expect(session.selectLocation).not.toHaveBeenCalled();
  });

  it("fails loudly outside a provider", () => {
    expect(() => renderHook(() => usePresetEditorContext())).toThrow("usePresetEditorContext needs an EditorProvider");
  });
});
