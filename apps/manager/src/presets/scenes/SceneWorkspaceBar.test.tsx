import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import type { PresetSceneSet } from "../../api/types";
import { SceneWorkspaceBar, sceneComparisonRows } from "./SceneWorkspaceBar";

const sceneSet: PresetSceneSet = {
  defaultSceneId: "verse",
  openIn: "scenes",
  scenes: [
    { id: "verse", name: "Verse", enterTimeMs: 0, outputTrimDb: 0, targets: [] },
    { id: "chorus", name: "Chorus", enterTimeMs: 500, outputTrimDb: 0, targets: [] },
    { id: "solo", name: "Solo", enterTimeMs: 0, outputTrimDb: 2, targets: [] },
    { id: "outro", name: "Outro", enterTimeMs: 2000, outputTrimDb: -1, targets: [] },
  ],
};

describe("SceneWorkspaceBar", () => {
  it("keeps editing selection separate from explicit pedal recall", () => {
    const onSelect = vi.fn();
    const onRecall = vi.fn();
    render(<SceneWorkspaceBar
      sceneSet={sceneSet}
      editingSceneId="chorus"
      liveSceneId="solo"
      recallDisabled={false}
      recallHint=""
      recalling={false}
      onSelect={onSelect}
      onRecall={onRecall}
      onName={vi.fn()}
      onEnterTime={vi.fn()}
      onTrim={vi.fn()}
      onDefault={vi.fn()}
      onOpenIn={vi.fn()}
      onCopy={vi.fn()}
      onSwap={vi.fn()}
      onCopyRow={vi.fn()}
    />);

    fireEvent.click(screen.getByRole("tab", { name: /Outro/ }));
    expect(onSelect).toHaveBeenCalledWith("outro");
    expect(onRecall).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole("button", { name: "Recall on pedal" }));
    expect(onRecall).toHaveBeenCalledOnce();
  });

  it("supports arrow-key tab navigation and explains blocked recall", () => {
    const onSelect = vi.fn();
    render(<SceneWorkspaceBar
      sceneSet={sceneSet}
      editingSceneId="verse"
      recallDisabled
      recallHint="Apply this version to recall it."
      recalling={false}
      onSelect={onSelect}
      onRecall={vi.fn()}
      onName={vi.fn()}
      onEnterTime={vi.fn()}
      onTrim={vi.fn()}
      onDefault={vi.fn()}
      onOpenIn={vi.fn()}
      onCopy={vi.fn()}
      onSwap={vi.fn()}
      onCopyRow={vi.fn()}
    />);
    fireEvent.keyDown(screen.getByRole("tab", { name: /Verse/ }), { key: "ArrowRight" });
    expect(onSelect).toHaveBeenCalledWith("chorus");
    expect(screen.getByText("Apply this version to recall it.")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: "Recall on pedal" })).toBeDisabled();
  });

  it("confirms destructive copy and exposes target-level comparison rows", () => {
    const onCopy = vi.fn();
    const onCopyRow = vi.fn();
    const targets = structuredClone(sceneSet);
    targets.scenes[0].targets = [{ target: "parameter", blockId: "delay-1", parameter: "mix", value: 0.2 }];
    targets.scenes[1].targets = [{ target: "parameter", blockId: "delay-1", parameter: "mix", value: 0.5 }];
    targets.scenes[2].targets = [{ target: "parameter", blockId: "delay-1", parameter: "mix", value: 0.8 }];
    targets.scenes[3].targets = [{ target: "parameter", blockId: "delay-1", parameter: "mix", value: 0.2 }];
    expect(sceneComparisonRows(targets)).toContainEqual({
      key: "parameter:delay-1:mix", label: "delay-1 · mix", values: ["0.2", "0.5", "0.8", "0.2"],
    });
    expect(sceneComparisonRows(targets, (_target, value) => ({
      label: "Digital Delay · Mix", value: `${Math.round(Number(value) * 100)}%`,
    }))).toContainEqual({
      key: "parameter:delay-1:mix", label: "Digital Delay · Mix", values: ["20%", "50%", "80%", "20%"],
    });

    render(<SceneWorkspaceBar sceneSet={targets} editingSceneId="verse" recallDisabled recallHint="Apply"
      recalling={false} onSelect={vi.fn()} onRecall={vi.fn()} onName={vi.fn()} onEnterTime={vi.fn()}
      onTrim={vi.fn()} onDefault={vi.fn()} onOpenIn={vi.fn()} onCopy={onCopy} onSwap={vi.fn()}
      onCopyRow={onCopyRow} sharedRows={[{ key: "routing", label: "Preset · topology", value: "Serial" }]} />);
    fireEvent.click(screen.getByRole("button", { name: "Compare" }));
    fireEvent.click(screen.getAllByRole("button", { name: "Copy Verse to all" })[0]);
    expect(onCopyRow).toHaveBeenCalledWith("enterTime", "verse");
    fireEvent.click(screen.getByRole("button", { name: "Shared" }));
    expect(screen.getByText("Preset · topology")).toBeInTheDocument();
    expect(screen.getByText("Serial")).toBeInTheDocument();
    fireEvent.click(screen.getByRole("button", { name: "Scene settings" }));
    expect(screen.getByRole("spinbutton", { name: "Enter time (ms)" })).toHaveAttribute("step", "100");
    fireEvent.click(screen.getByRole("button", { name: "Copy to…" }));
    expect(screen.getByRole("alertdialog")).toHaveTextContent("destination keeps its name and identity");
    fireEvent.click(screen.getByRole("button", { name: "Confirm" }));
    expect(onCopy).toHaveBeenCalledWith("verse", "chorus");
  });
});
