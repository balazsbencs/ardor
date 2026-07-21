import { useEffect, useState } from "react";

import type { NumberControl } from "../effects/types";
import { NumberInput } from "./ui";

export function displayValue(control: NumberControl, value: number): string {
  if (control.display) return control.display.format(value);
  if (control.unit === "percent") return `${Math.round(value * 100)}%`;
  if (control.unit === "ratio") return `${value.toFixed(value % 1 === 0 ? 0 : 1)}:1`;
  if (control.unit === "db") return `${value.toFixed(control.step < 1 ? 1 : 0)} dB`;
  if (control.unit === "ms") return `${value.toFixed(control.step < 1 ? 1 : 0)} ms`;
  if (control.unit === "hz") return `${Math.round(value)} Hz`;
  return String(value);
}

function inputValue(control: NumberControl, value: number): string {
  if (control.display) return String(Number(control.display.toInput(value).toFixed(4)));
  return String(control.unit === "percent" ? Math.round(value * 100) : value);
}

export function ParameterSlider({
  control,
  value,
  onChange,
}: {
  control: NumberControl;
  value: number;
  onChange(value: number): void;
}) {
  const [text, setText] = useState(inputValue(control, value));

  useEffect(() => setText(inputValue(control, value)), [control, value]);

  const applyText = () => {
    const parsed = Number(text);
    if (!Number.isFinite(parsed)) {
      setText(inputValue(control, value));
      return;
    }
    const stored = control.display
      ? control.display.fromInput(parsed)
      : control.unit === "percent" ? parsed / 100 : parsed;
    const clamped = Math.min(control.maximum, Math.max(control.minimum, stored));
    onChange(clamped);
    setText(inputValue(control, clamped));
  };

  const step = control.display?.step ?? (control.unit === "percent" ? control.step * 100 : control.step);
  const min = control.display?.minimum ?? (control.unit === "percent" ? control.minimum * 100 : control.minimum);
  const max = control.display?.maximum ?? (control.unit === "percent" ? control.maximum * 100 : control.maximum);
  const displayChoices = control.display?.choices;
  const selectedChoice = displayChoices?.reduce((best, choice, index) =>
    Math.abs(choice.value - value) < Math.abs(displayChoices[best].value - value) ? index : best, 0) ?? 0;

  return (
    <label className="parameter-slider">
      <span className="parameter-slider__label"><span>{control.label}</span><output>{displayValue(control, value)}</output></span>
      <span className="parameter-slider__controls">
        <input
          aria-label={control.label}
          type="range"
          min={displayChoices ? 0 : control.minimum}
          max={displayChoices ? displayChoices.length - 1 : control.maximum}
          step={displayChoices ? 1 : control.step}
          value={displayChoices ? selectedChoice : value}
          onChange={(event) => onChange(displayChoices
            ? displayChoices[Number(event.target.value)].value
            : Number(event.target.value))}
        />
        {control.display?.choices ? <select
          aria-label={`${control.label} precise value`}
          className="number-input"
          value={control.display.choices[selectedChoice].value}
          onChange={(event) => onChange(Number(event.target.value))}
        >{control.display.choices.map((choice) => <option value={choice.value} key={choice.label}>{choice.label}</option>)}</select> : <NumberInput
          aria-label={`${control.label} precise value`}
          type="number"
          min={min}
          max={max}
          step={step}
          value={text}
          onChange={(event) => setText(event.target.value)}
          onBlur={applyText}
          onKeyDown={(event) => {
            if (event.key === "Enter") event.currentTarget.blur();
            if (event.key === "Escape") {
              setText(inputValue(control, value));
              event.currentTarget.blur();
            }
          }}
        />}
      </span>
    </label>
  );
}
