import { useMemo, useState } from "react";

import type { EqBand, EqPassFilter } from "../editor/editorTypes";

const WIDTH = 640;
const HEIGHT = 220;
const LEFT = 42;
const RIGHT = 16;
const TOP = 18;
const BOTTOM = 30;
const MIN_FREQUENCY = 20;
const MAX_FREQUENCY = 20_000;
const MIN_GAIN = -18;
const MAX_GAIN = 18;
const STAGE_COUNT = 7;

type Point = { x: number; y: number };
type PassFilterKey = "high_pass" | "low_pass";

export function frequencyForGraphX(x: number): number {
  const proportion = Math.max(0, Math.min(1, (x - LEFT) / (WIDTH - LEFT - RIGHT)));
  return MIN_FREQUENCY * Math.pow(MAX_FREQUENCY / MIN_FREQUENCY, proportion);
}

export function graphXForFrequency(frequency: number): number {
  const proportion = Math.log(Math.max(MIN_FREQUENCY, Math.min(MAX_FREQUENCY, frequency)) / MIN_FREQUENCY) / Math.log(MAX_FREQUENCY / MIN_FREQUENCY);
  return LEFT + proportion * (WIDTH - LEFT - RIGHT);
}

export function gainForGraphY(y: number): number {
  const proportion = Math.max(0, Math.min(1, (y - TOP) / (HEIGHT - TOP - BOTTOM)));
  return MAX_GAIN - proportion * (MAX_GAIN - MIN_GAIN);
}

export function graphYForGain(gain: number): number {
  const proportion = (MAX_GAIN - Math.max(MIN_GAIN, Math.min(MAX_GAIN, gain))) / (MAX_GAIN - MIN_GAIN);
  return TOP + proportion * (HEIGHT - TOP - BOTTOM);
}

function passResponseAt(frequency: number, filter: EqPassFilter, highPass: boolean): number {
  if (!filter.enabled) return 0;
  const ratio = frequency / Math.max(1, filter.frequency_hz);
  const denominator = Math.sqrt(
    Math.pow(1 - ratio * ratio, 2) + Math.pow(ratio / Math.max(0.0001, filter.q), 2),
  );
  const secondOrder = (highPass ? ratio * ratio : 1) / Math.max(denominator, 1e-12);
  const firstOrder = highPass
    ? ratio / Math.sqrt(1 + ratio * ratio)
    : 1 / Math.sqrt(1 + ratio * ratio);
  const slope = filter.slope_db_per_octave;
  const magnitude = slope === 6
    ? firstOrder
    : slope === 18
      ? firstOrder * secondOrder
      : slope === 24
        ? secondOrder * secondOrder
        : secondOrder;
  return 20 * Math.log10(Math.max(magnitude, 1e-12));
}

function responseAt(
  frequency: number,
  bands: EqBand[],
  highPass: EqPassFilter,
  lowPass: EqPassFilter,
): number {
  const bandResponse = bands.reduce((total, band) => {
    if (!band.enabled) return total;
    const distance = Math.log2(frequency / Math.max(1, band.frequency_hz));
    const width = 1 / Math.max(0.15, band.q);
    return total + band.gain_db * Math.exp(-(distance * distance) / (2 * width * width));
  }, 0);
  return bandResponse
    + passResponseAt(frequency, highPass, true)
    + passResponseAt(frequency, lowPass, false);
}

function graphPoint(event: React.PointerEvent<SVGSVGElement>): Point {
  const bounds = event.currentTarget.getBoundingClientRect();
  if (bounds.width === 0 || bounds.height === 0) return { x: event.clientX, y: event.clientY };
  return {
    x: (event.clientX - bounds.left) * WIDTH / bounds.width,
    y: (event.clientY - bounds.top) * HEIGHT / bounds.height,
  };
}

function defaultBand(index: number): EqBand {
  return { enabled: true, frequency_hz: [80, 250, 800, 2500, 8000][index], q: 1, gain_db: 0 };
}

function stageLabel(stage: number): string {
  if (stage === 0) return "HP";
  if (stage === STAGE_COUNT - 1) return "LP";
  return `B${stage}`;
}

function stageName(stage: number): string {
  if (stage === 0) return "High-pass";
  if (stage === STAGE_COUNT - 1) return "Low-pass";
  return `Band ${stage}`;
}

export function EqResponseGraph({
  bands: sourceBands,
  highPass,
  lowPass,
  activeStage,
  onActiveStage,
  onBandChange,
  onPassFilterChange,
}: {
  bands: EqBand[];
  highPass: EqPassFilter;
  lowPass: EqPassFilter;
  activeStage: number;
  onActiveStage(index: number): void;
  onBandChange(index: number, patch: Partial<EqBand>): void;
  onPassFilterChange(key: PassFilterKey, patch: Partial<EqPassFilter>): void;
}) {
  const [dragging, setDragging] = useState<number>();
  const bands = Array.from({ length: 5 }, (_, index) => sourceBands[index] ?? defaultBand(index));
  const curve = useMemo(() => Array.from({ length: 121 }, (_, index) => {
    const x = LEFT + index * (WIDTH - LEFT - RIGHT) / 120;
    const gain = responseAt(frequencyForGraphX(x), bands, highPass, lowPass);
    return `${x.toFixed(1)},${graphYForGain(gain).toFixed(1)}`;
  }).join(" "), [bands, highPass, lowPass]);
  const stages = [highPass, ...bands, lowPass];

  const editAt = (stage: number, point: Point) => {
    const frequency_hz = Math.round(frequencyForGraphX(point.x));
    if (stage === 0 || stage === STAGE_COUNT - 1) {
      onPassFilterChange(stage === 0 ? "high_pass" : "low_pass", { frequency_hz });
      return;
    }
    onBandChange(stage - 1, {
      frequency_hz,
      gain_db: Math.round(gainForGraphY(point.y) * 2) / 2,
    });
  };
  const startDrag = (stage: number, event: React.PointerEvent<SVGCircleElement>) => {
    event.preventDefault();
    event.stopPropagation();
    if (typeof event.currentTarget.setPointerCapture === "function") {
      event.currentTarget.setPointerCapture(event.pointerId);
    }
    setDragging(stage);
    onActiveStage(stage);
  };
  const move = (event: React.PointerEvent<SVGSVGElement>) => {
    if (dragging === undefined) return;
    editAt(dragging, graphPoint(event));
  };
  const finish = () => setDragging(undefined);

  return <section className="eq-graph-panel" aria-label="EQ response editor">
    <div className="eq-graph-panel__heading"><div><p className="section-label">Frequency response</p><p>Drag a band to shape gain. Drag HP or LP to set its cutoff.</p></div><output aria-live="polite">{stageName(activeStage)}</output></div>
    <svg className="eq-graph" viewBox={`0 0 ${WIDTH} ${HEIGHT}`} role="img" aria-label="EQ response graph" onPointerMove={move} onPointerUp={finish} onPointerCancel={finish}>
      {[18, 12, 6, 0, -6, -12, -18].map((gain) => <g key={gain}><line x1={LEFT} x2={WIDTH - RIGHT} y1={graphYForGain(gain)} y2={graphYForGain(gain)} /><text x={LEFT - 8} y={graphYForGain(gain) + 3} textAnchor="end">{gain > 0 ? `+${gain}` : gain}</text></g>)}
      {[20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000].map((frequency) => <g key={frequency}><line x1={graphXForFrequency(frequency)} x2={graphXForFrequency(frequency)} y1={TOP} y2={HEIGHT - BOTTOM} /><text x={graphXForFrequency(frequency)} y={HEIGHT - 9} textAnchor="middle">{frequency >= 1000 ? `${frequency / 1000}k` : frequency}</text></g>)}
      <polyline className="eq-graph__zero" points={`${LEFT},${graphYForGain(0)} ${WIDTH - RIGHT},${graphYForGain(0)}`} />
      <polyline className="eq-graph__curve" points={curve} />
      {stages.map((stage, index) => {
        const isPass = index === 0 || index === STAGE_COUNT - 1;
        const gain = isPass
          ? passResponseAt(stage.frequency_hz, stage as EqPassFilter, index === 0)
          : (stage as EqBand).gain_db;
        return <circle key={index} className={`eq-graph__node ${isPass ? "is-pass" : ""} ${index === activeStage ? "is-active" : ""} ${stage.enabled ? "" : "is-disabled"}`} cx={graphXForFrequency(stage.frequency_hz)} cy={graphYForGain(gain)} r={index === activeStage ? 8 : 6} role="button" aria-label={`Adjust ${stageName(index)}`} tabIndex={0} onPointerDown={(event) => startDrag(index, event)} onFocus={() => onActiveStage(index)} onKeyDown={(event) => { if (event.key === "Enter" || event.key === " ") onActiveStage(index); }} />;
      })}
    </svg>
    <div className="eq-graph__bands" role="tablist" aria-label="EQ filters">{stages.map((stage, index) => <button key={index} role="tab" aria-selected={index === activeStage} className={`${index === activeStage ? "is-active" : ""} ${index === 0 || index === STAGE_COUNT - 1 ? "is-pass" : ""}`} onClick={() => onActiveStage(index)}>{stageLabel(index)}<span>{Math.round(stage.frequency_hz)} Hz</span></button>)}</div>
  </section>;
}
