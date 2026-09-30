import { X } from "lucide-react";

import { IconButton } from "../components/ui";
import type { NumberControl } from "../effects/types";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { ChoiceStrip } from "../ui/ChoiceStrip";
import { Tag } from "../ui/Tag";
import { TravelScale } from "../ui/TravelScale";
import { ExpressionPanel } from "./ExpressionPanel";
import { SceneScope } from "./SceneScope";
import "./drawer.css";

const db = (key: string, label: string, minimum: number, maximum: number): NumberControl =>
  ({ kind: "number", key, label, minimum, maximum, step: 0.5, unit: "db", defaultValue: 0 });
const INPUT = db("inputGainDb", "Input gain", -60, 24);
const OUTPUT = db("outputGainDb", "Output level", -60, 12);
const LANE_LEVEL = db("levelDb", "Level", -60, 12);
const PAN: NumberControl = { kind: "number", key: "pan", label: "Pan", minimum: -1, maximum: 1, step: 0.01, unit: "plain", defaultValue: 0 };
const WIDTH: NumberControl = { kind: "number", key: "width", label: "Width", minimum: 0, maximum: 1, step: 0.01, unit: "percent", defaultValue: 1 };
const named = (control: NumberControl, label: string): NumberControl => ({ ...control, label });

export function GlobalDrawer({ onClose }: { onClose(): void }) {
  const editor = usePresetEditorContext();
  const { present, dispatch, editingScene, sceneInputOwned, displayedWdw } = editor;

  const editInput = (value: number, gesture: string) => {
    if (editingScene && !sceneInputOwned) dispatch({ type: "set-scene-input-scope", sceneId: editingScene.id, scope: "scene" });
    if (editingScene) dispatch({ type: "set-scene-input-gain", sceneId: editingScene.id, value, gesture });
    else dispatch({ type: "set-global", key: "inputGainDb", value, gesture });
  };

  return (
    <section className="drawer fam-util" aria-label="Global">
      <div className="drawer__head">
        <h2>Global</h2>
        <span className="drawer__actions"><SceneScope /><IconButton label="Close" onClick={onClose}><X size={16} /></IconButton></span>
      </div>
      <div className="ctlgrid">
        <TravelScale control={INPUT} value={editor.displayedInputGain} family="util" onChange={editInput}
          owned={editingScene ? (sceneInputOwned ? "scene" : "shared") : undefined}
          onShare={editingScene ? () => dispatch({ type: "set-scene-input-scope", sceneId: editingScene.id, scope: "shared" }) : undefined} />
        <TravelScale control={OUTPUT} value={present.global.outputGainDb} family="util"
          onChange={(value, gesture) => dispatch({ type: "set-global", key: "outputGainDb", value, gesture })} />
        <div className="lb-ctl fam-util">
          <div className="lb-ctl__top"><span className="lb-ctl__label">Safety limiter</span><Tag>FIXED</Tag></div>
          <div className="lb-ctl__value"><span>-1</span><small>dBFS</small></div>
          <p className="lb-note">Protection, not a tone control. It cannot be changed.</p>
        </div>
        <ChoiceStrip label="Topology" family="util" value={present.routing}
          options={[{ value: "serial", label: "Serial" }, { value: "wdw", label: "Wet dry wet" }]}
          onChange={(routing) => dispatch({ type: "set-routing", routing })} />
        {present.routing === "wdw" && displayedWdw && (["dry", "wet"] as const).map((lane) => {
          const title = lane === "dry" ? "Dry" : "Wet";
          const config = displayedWdw[lane];
          const shape = lane === "dry" ? PAN : WIDTH;
          const shapeKey = lane === "dry" ? "pan" : "width";
          return (
            <div key={lane} className="ctlgrid__group">
              <h3 className="ctlgrid__lane">{title} lane</h3>
              <ChoiceStrip label={`${title} lane`} family="util" value={config.enabled}
                options={[{ value: false, label: "Off" }, { value: true, label: "On" }]} onChange={(on) => editor.editWdwMix(lane, "enabled", on)} />
              <TravelScale control={named(LANE_LEVEL, `${title} level`)} value={config.levelDb} family="util" onChange={(value) => editor.editWdwMix(lane, "levelDb", value)} />
              <TravelScale control={named(shape, `${title} ${lane === "dry" ? "pan" : "width"}`)} value={config[shapeKey] ?? shape.defaultValue} family="util"
                onChange={(value) => editor.editWdwMix(lane, shapeKey, value)} />
            </div>
          );
        })}
        <ExpressionPanel />
      </div>
    </section>
  );
}
