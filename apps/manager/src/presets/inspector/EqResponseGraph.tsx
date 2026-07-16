import { useMemo, useRef, useState } from "react";

import type { EqBand } from "../editor/editorTypes";

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

type Point = { x: number; y: number };

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

function responseAt(frequency: number, bands: EqBand[]): number {
  return bands.reduce((total, band) => {
    if (!band.enabled) return total;
    const distance = Math.log2(frequency / Math.max(1, band.frequency_hz));
    // A bell curve on a log-frequency axis is a clear, stable editor preview.
    const width = 1 / Math.max(0.15, band.q);
    return total + band.gain_db * Math.exp(-(distance * distance) / (2 * width * width));
  }, 0);
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

export function EqResponseGraph({
  bands: sourceBands,
  activeBand,
  onActiveBand,
  onChange,
}: {
  bands: EqBand[];
  activeBand: number;
  onActiveBand(index: number): void;
  onChange(index: number, patch: Partial<EqBand>): void;
}) {
  const svgRef = useRef<SVGSVGElement>(null);
  const [dragging, setDragging] = useState<number>();
  const bands = Array.from({ length: 5 }, (_, index) => sourceBands[index] ?? defaultBand(index));
  const curve = useMemo(() => Array.from({ length: 121 }, (_, index) => {
    const x = LEFT + index * (WIDTH - LEFT - RIGHT) / 120;
    const gain = responseAt(frequencyForGraphX(x), bands);
    return `${x.toFixed(1)},${graphYForGain(gain).toFixed(1)}`;
  }).join(" "), [bands]);

  const editAt = (index: number, point: Point) => {
    onChange(index, {
      frequency_hz: Math.round(frequencyForGraphX(point.x)),
      gain_db: Math.round(gainForGraphY(point.y) * 2) / 2,
    });
  };
  const startDrag = (index: number, event: React.PointerEvent<SVGCircleElement>) => {
    event.preventDefault();
    event.stopPropagation();
    // SVG pointer capture is unavailable in a few embedded webviews; dragging
    // still works there because the graph itself also listens for pointer moves.
    if (typeof event.currentTarget.setPointerCapture === "function") {
      event.currentTarget.setPointerCapture(event.pointerId);
    }
    setDragging(index);
    onActiveBand(index);
  };
  const move = (event: React.PointerEvent<SVGSVGElement>) => {
    if (dragging === undefined) return;
    editAt(dragging, graphPoint(event));
  };
  const finish = () => setDragging(undefined);

  return <section className="eq-graph-panel" aria-label="EQ response editor">
    <div className="eq-graph-panel__heading"><div><p className="section-label">Frequency response</p><p>Drag a node to set its frequency and gain.</p></div><output aria-live="polite">Band {activeBand + 1}</output></div>
    <svg ref={svgRef} className="eq-graph" viewBox={`0 0 ${WIDTH} ${HEIGHT}`} role="img" aria-label="EQ response graph" onPointerMove={move} onPointerUp={finish} onPointerCancel={finish}>
      {[18, 12, 6, 0, -6, -12, -18].map((gain) => <g key={gain}><line x1={LEFT} x2={WIDTH - RIGHT} y1={graphYForGain(gain)} y2={graphYForGain(gain)} /><text x={LEFT - 8} y={graphYForGain(gain) + 3} textAnchor="end">{gain > 0 ? `+${gain}` : gain}</text></g>)}
      {[20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000].map((frequency) => <g key={frequency}><line x1={graphXForFrequency(frequency)} x2={graphXForFrequency(frequency)} y1={TOP} y2={HEIGHT - BOTTOM} /><text x={graphXForFrequency(frequency)} y={HEIGHT - 9} textAnchor="middle">{frequency >= 1000 ? `${frequency / 1000}k` : frequency}</text></g>)}
      <polyline className="eq-graph__zero" points={`${LEFT},${graphYForGain(0)} ${WIDTH - RIGHT},${graphYForGain(0)}`} />
      <polyline className="eq-graph__curve" points={curve} />
      {bands.map((band, index) => <circle key={index} className={`eq-graph__node ${index === activeBand ? "is-active" : ""} ${band.enabled ? "" : "is-disabled"}`} cx={graphXForFrequency(band.frequency_hz)} cy={graphYForGain(band.gain_db)} r={index === activeBand ? 8 : 6} role="button" aria-label={`Adjust band ${index + 1}`} tabIndex={0} onPointerDown={(event) => startDrag(index, event)} onFocus={() => onActiveBand(index)} onKeyDown={(event) => { if (event.key === "Enter" || event.key === " ") onActiveBand(index); }} />)}
    </svg>
    <div className="eq-graph__bands" role="tablist" aria-label="EQ bands">{bands.map((band, index) => <button key={index} role="tab" aria-selected={index === activeBand} className={index === activeBand ? "is-active" : ""} onClick={() => onActiveBand(index)}>B{index + 1}<span>{Math.round(band.frequency_hz)} Hz</span></button>)}</div>
  </section>;
}
