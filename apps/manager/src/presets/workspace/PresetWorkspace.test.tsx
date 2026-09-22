import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { beforeEach, describe, expect, it, vi } from "vitest";

import type { Preset } from "../../api/types";
import { PresetWorkspace } from "./PresetWorkspace";

const preset: Preset = {
  version: 4, name: "Afterglow", routing: "serial",
  global: { inputGainDb: 0, outputGainDb: 0, safetyLimitDb: -1 }, blocks: [],
  sceneSet: {
    defaultSceneId: "verse", openIn: "scenes", scenes: [
      { id: "verse", name: "Verse", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
      { id: "chorus", name: "Chorus", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
      { id: "solo", name: "Solo", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
      { id: "outro", name: "Outro", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
    ],
  },
};

const session = {
  status: "connected" as const,
  current: { location: { bank: 0, slot: 0 }, preset, exists: true },
  device: {
    active: { bank: 0, slot: 0, generation: 42, storedRevisionMatches: true, liveSceneId: "verse" },
    capabilities: { sceneRecall: true },
  },
  models: [], irs: [], reverbIrs: [], presets: [], busy: { save: false, apply: false, upload: false },
  saveCurrent: vi.fn(async () => ({ bank: 0, slot: 0, preset })),
  applyCurrent: vi.fn(async () => { throw new Error("DSP preparation failed"); }),
  refreshPresets: vi.fn(async () => undefined),
  selectLocation: vi.fn(async () => undefined),
  recallScene: vi.fn(async () => true),
};

vi.mock("../../connection/deviceSession", () => ({ useDeviceSession: () => session }));

describe("PresetWorkspace scene save/apply semantics", () => {
  beforeEach(() => vi.clearAllMocks());

  it("reports when save succeeds but the pedal keeps playing the previous revision", async () => {
    render(<PresetWorkspace onAssets={vi.fn()} onConnection={vi.fn()} />);
    await userEvent.click(screen.getByRole("button", { name: "Save & Apply" }));
    expect(await screen.findByText(/Saved; pedal still playing the previous version/)).toHaveTextContent("DSP preparation failed");
    expect(session.saveCurrent).toHaveBeenCalled();
    expect(session.applyCurrent).toHaveBeenCalledWith("verse");
  });

  it("shows physically formatted preset values in the shared comparison", async () => {
    render(<PresetWorkspace onAssets={vi.fn()} onConnection={vi.fn()} />);
    await userEvent.click(screen.getByRole("button", { name: "Compare" }));
    await userEvent.click(screen.getByRole("button", { name: "Shared" }));
    expect(screen.getByText("Preset · output gain")).toBeInTheDocument();
    expect(screen.getByText("Preset · safety limit").nextElementSibling).toHaveTextContent("-1.0 dB");
  });
});
