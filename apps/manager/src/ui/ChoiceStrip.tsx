import type { Family } from "./family";
import "./TravelScale.css";

export function ChoiceStrip<T extends string | boolean>({ label, options, value, onChange, family }: {
  label: string;
  options: Array<{ value: T; label: string }>;
  value: T;
  onChange(value: T): void;
  family: Family;
}) {
  return (
    <div className={`lb-ctl fam-${family}`}>
      <div className="lb-ctl__top"><span className="lb-ctl__label">{label}</span></div>
      <div className="lb-choice" role="radiogroup" aria-label={label}>
        {options.map((option) => (
          <button key={String(option.value)} type="button" role="radio" aria-checked={option.value === value} onClick={() => onChange(option.value)}>{option.label}</button>
        ))}
      </div>
    </div>
  );
}
