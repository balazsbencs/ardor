import type { NumberControl } from "../effects/types";
import { usePresetEditorContext } from "../presets/editor/EditorContext";
import { ChoiceStrip } from "../ui/ChoiceStrip";
import { TravelScale } from "../ui/TravelScale";

const rangeControl = (parameter: NumberControl, label: string, defaultValue: number): NumberControl => ({ ...parameter, label, defaultValue });

/** Expression pedal: one effect parameter, its heel and toe values and direction. */
export function ExpressionPanel() {
  const editor = usePresetEditorContext();
  const { present, dispatch, expressionTargets, expressionTarget, expressionParameter, enableExpression, patchExpression } = editor;
  const expression = present.expression;
  const noTargets = expressionTargets.length === 0;
  return (
    <>
      <h3 className="ctlgrid__lane">Expression pedal</h3>
      <div className="lb-ctl fam-util">
        <label className="lb-ctl__label">
          <input type="checkbox" checked={expression !== undefined} disabled={noTargets}
            onChange={(event) => (event.target.checked ? enableExpression() : dispatch({ type: "set-expression" }))} />
          {" "}Expression pedal
        </label>
        {!expression && <p className="lb-note">{noTargets ? "Add a supported effect before assigning the pedal." : "Enable to assign one effect parameter in this preset."}</p>}
      </div>
      {expression && <>
        <div className="lb-ctl fam-util">
          <label className="lb-ctl__label" htmlFor="exp-effect">Effect</label>
          <select id="exp-effect" value={expression.blockId} onChange={(event) => {
            const target = expressionTargets.find(({ block }) => block.id === event.target.value);
            const parameter = target?.parameters[0];
            if (!target || !parameter) return;
            dispatch({ type: "set-expression", expression: { blockId: target.block.id, parameter: parameter.key, minimum: parameter.minimum, maximum: parameter.maximum, inverted: expression.inverted } });
          }}>
            {expressionTargets.map(({ block, name }) => <option key={block.id} value={block.id}>{name} · {block.id}</option>)}
          </select>
        </div>
        <div className="lb-ctl fam-util">
          <label className="lb-ctl__label" htmlFor="exp-parameter">Parameter</label>
          <select id="exp-parameter" value={expression.parameter} onChange={(event) => {
            const parameter = expressionTarget?.parameters.find(({ key }) => key === event.target.value);
            if (parameter) patchExpression({ parameter: parameter.key, minimum: parameter.minimum, maximum: parameter.maximum });
          }}>
            {expressionTarget?.parameters.map((parameter) => <option key={parameter.key} value={parameter.key}>{parameter.label}</option>)}
          </select>
        </div>
        {expressionParameter && <>
          <TravelScale control={rangeControl(expressionParameter, "Heel", expressionParameter.minimum)} value={expression.minimum} family="util"
            onChange={(minimum, gesture) => patchExpression({ minimum }, gesture)} />
          <TravelScale control={rangeControl(expressionParameter, "Toe", expressionParameter.maximum)} value={expression.maximum} family="util"
            onChange={(maximum, gesture) => patchExpression({ maximum }, gesture)} />
        </>}
        <div className="lb-ctl fam-util">
          <label className="lb-ctl__label">
            <input type="checkbox" checked={expression.inverted} onChange={(event) => patchExpression({ inverted: event.target.checked })} />
            {" "}Invert
          </label>
        </div>
      </>}
    </>
  );
}
