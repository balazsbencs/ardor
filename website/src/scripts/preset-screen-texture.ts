// Draws the pedal's preset screen onto a canvas so the 3D model's display shows
// the real UI: dark ground, teal accent, four slots, header and footer bars.
// Mirrors the layout captured from the LVGL simulator.
export function drawPresetScreen(canvas: HTMLCanvasElement): void {
  // 125 x 74 viewable → keep that aspect at high resolution for a crisp texture.
  const W = (canvas.width = 1000);
  const H = (canvas.height = 592);
  const ctx = canvas.getContext('2d');
  if (!ctx) return;

  const accent = '#3ce0a6';
  const ink = '#e6f0ee';
  const muted = '#8aa39d';

  // ground
  ctx.fillStyle = '#0a0f11';
  ctx.fillRect(0, 0, W, H);

  const pad = 34;
  const headH = 92;

  // header buttons
  const btn = (x: number, w: number, label: string, active = false) => {
    ctx.fillStyle = active ? 'rgba(60,224,166,0.16)' : '#141c21';
    ctx.strokeStyle = active ? accent : 'rgba(120,200,170,0.22)';
    ctx.lineWidth = 2;
    roundRect(ctx, x, pad, w, headH - pad / 2, 8);
    ctx.fill();
    ctx.stroke();
    ctx.fillStyle = active ? accent : ink;
    ctx.font = '600 30px "Open Sans", sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(label, x + w / 2, pad + (headH - pad / 2) / 2);
  };
  btn(pad, 150, 'Tuner', true);
  btn(pad + 168, 150, 'Bank −');
  btn(W - pad - 318, 150, 'Bank +');
  btn(W - pad - 150, 150, 'Edit');

  ctx.fillStyle = ink;
  ctx.font = '700 38px "Chakra Petch", "Open Sans", sans-serif';
  ctx.textAlign = 'center';
  ctx.fillText('BANK 000', W / 2, pad + (headH - pad / 2) / 2);

  // 2 x 2 preset slots
  const slots = [
    { name: 'Clean Verb', active: true },
    { name: 'Crunch Rig', active: false },
    { name: 'Lead Bloom', active: false },
    { name: 'Ambient', active: false },
  ];
  const gridTop = headH + pad;
  const gridH = H - gridTop - 78;
  const gap = 20;
  const cellW = (W - pad * 2 - gap) / 2;
  const cellH = (gridH - gap) / 2;
  slots.forEach((s, i) => {
    const cx = pad + (i % 2) * (cellW + gap);
    const cy = gridTop + Math.floor(i / 2) * (cellH + gap);
    ctx.fillStyle = '#12191d';
    ctx.strokeStyle = 'rgba(120,200,170,0.16)';
    ctx.lineWidth = 2;
    roundRect(ctx, cx, cy, cellW, cellH, 10);
    ctx.fill();
    ctx.stroke();
    if (s.active) {
      ctx.fillStyle = accent;
      roundRect(ctx, cx + 8, cy + 14, 6, cellH - 28, 3);
      ctx.fill();
    }
    ctx.fillStyle = s.active ? accent : ink;
    ctx.font = '600 40px "Open Sans", sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(s.name, cx + cellW / 2, cy + cellH / 2);
  });

  // footer
  ctx.fillStyle = muted;
  ctx.font = '500 26px "IBM Plex Mono", monospace';
  ctx.textAlign = 'left';
  ctx.textBaseline = 'middle';
  ctx.fillText('BUFFER 41%', pad, H - 40);
  ctx.textAlign = 'center';
  ctx.fillStyle = ink;
  ctx.fillText('Master 82%', W / 2, H - 40);
  ctx.textAlign = 'right';
  ctx.fillStyle = muted;
  ctx.fillText('MIDI', W - pad, H - 40);
}

function roundRect(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  w: number,
  h: number,
  r: number,
): void {
  const rr = Math.min(r, w / 2, h / 2);
  ctx.beginPath();
  ctx.moveTo(x + rr, y);
  ctx.arcTo(x + w, y, x + w, y + h, rr);
  ctx.arcTo(x + w, y + h, x, y + h, rr);
  ctx.arcTo(x, y + h, x, y, rr);
  ctx.arcTo(x, y, x + w, y, rr);
  ctx.closePath();
}
