// Canvas fallback for the product render. This is the same Slate Panel language
// used by the real LVGL preset screen: flat plates, hard rules, and one live lamp.
export function drawPresetScreen(canvas: HTMLCanvasElement): void {
  const W = (canvas.width = 1000);
  const H = (canvas.height = 592);
  const ctx = canvas.getContext('2d');
  if (!ctx) return;

  const plate = '#212528';
  const plate2 = '#2a2f33';
  const plate3 = '#191c1f';
  const engrave = '#e2e4e3';
  const muted = '#8d9499';
  const rule = '#3b4247';
  const lamp = '#d8422f';
  const family = ['#a8814e', '#939a9e', '#5f7f9c', '#5d8f80', '#8175a0', '#a8785c'];

  ctx.fillStyle = plate;
  ctx.fillRect(0, 0, W, H);

  const pad = 24;
  const headH = 52;
  ctx.strokeStyle = rule;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(0, headH);
  ctx.lineTo(W, headH);
  ctx.stroke();

  ctx.fillStyle = engrave;
  ctx.font = '600 28px "Saira Condensed", sans-serif';
  ctx.textAlign = 'left';
  ctx.textBaseline = 'middle';
  ctx.fillText('ARDOR', pad, headH / 2);
  ctx.fillStyle = muted;
  ctx.font = '500 18px "Saira Condensed", sans-serif';
  ctx.fillText('BANK 000 · SET A', 168, headH / 2);
  ctx.textAlign = 'right';
  ctx.fillText('48 KHZ · BLK 64', W - 116, headH / 2);
  ctx.fillStyle = lamp;
  ctx.fillRect(W - 90, headH / 2 - 6, 12, 12);
  ctx.fillStyle = engrave;
  ctx.fillText('MIDI', W - pad, headH / 2);

  const slots = [
    { number: '1', name: 'MIDNIGHT CLEAN', fs: 'FS 1', colors: [family[0], family[1], family[5]] },
    { number: '3', name: 'SPLIT RIG / WIDE', fs: 'FS 3', colors: [family[2], family[3], family[4]] },
    { number: '2', name: 'DRIVE STACK', fs: 'FS 2', live: true, colors: [family[0], family[1], family[4]] },
    { number: '4', name: 'AMBIENT BLOOM', fs: 'FS 4', fault: true, colors: [] },
  ];
  const gridTop = headH + 16;
  const gridBottom = H - 74;
  const gap = 14;
  const cellW = (W - pad * 2 - gap) / 2;
  const cellH = (gridBottom - gridTop - gap) / 2;

  slots.forEach((slot, i) => {
    const x = pad + (i % 2) * (cellW + gap);
    const y = gridTop + Math.floor(i / 2) * (cellH + gap);
    ctx.fillStyle = slot.live ? plate2 : slot.fault ? plate3 : plate2;
    ctx.fillRect(x, y, cellW, cellH);
    ctx.strokeStyle = slot.live ? lamp : slot.fault ? '#6b463c' : rule;
    ctx.lineWidth = 2;
    ctx.strokeRect(x + 1, y + 1, cellW - 2, cellH - 2);

    ctx.fillStyle = slot.live ? lamp : muted;
    ctx.font = '600 18px "Saira Condensed", sans-serif';
    ctx.textAlign = 'left';
    ctx.fillText(`PRESET ${slot.number}${slot.live ? ' · RUNNING' : ''}`, x + 18, y + 23);
    ctx.fillStyle = slot.live ? lamp : slot.fault ? '#bb9186' : muted;
    ctx.font = '300 58px "Saira", sans-serif';
    ctx.fillText(slot.number, x + 22, y + cellH * 0.57);
    ctx.fillStyle = slot.fault ? '#bb9186' : engrave;
    ctx.font = '600 28px "Saira Condensed", sans-serif';
    ctx.fillText(slot.name, x + 86, y + cellH * 0.57);

    if (slot.colors.length > 0) {
      slot.colors.forEach((color, colorIndex) => {
        ctx.fillStyle = color;
        ctx.fillRect(x + 86 + colorIndex * 78, y + cellH - 24, 58, 4);
      });
    } else {
      ctx.strokeStyle = '#6b463c';
      ctx.strokeRect(x + 86, y + cellH - 42, 170, 24);
      ctx.fillStyle = '#bb9186';
      ctx.font = '600 14px "Saira Condensed", sans-serif';
      ctx.fillText('ASSET NOT FOUND', x + 96, y + cellH - 29);
    }
    ctx.fillStyle = slot.live ? lamp : muted;
    ctx.font = '600 16px "Saira Condensed", sans-serif';
    ctx.textAlign = 'right';
    ctx.fillText(slot.fs, x + cellW - 18, y + cellH - 16);
  });

  ctx.fillStyle = plate;
  ctx.fillRect(0, H - 74, W, 74);
  ctx.strokeStyle = rule;
  ctx.beginPath();
  ctx.moveTo(0, H - 74);
  ctx.lineTo(W, H - 74);
  ctx.stroke();
  ctx.textAlign = 'left';
  ctx.fillStyle = engrave;
  ctx.font = '600 20px "Saira Condensed", sans-serif';
  ctx.fillText('EDIT', pad, H - 38);
  ctx.fillStyle = muted;
  ctx.fillText('TUNER', pad + 88, H - 38);
  ctx.font = '500 17px "Saira Condensed", sans-serif';
  ctx.fillText('BANK −', pad + 194, H - 38);
  ctx.fillText('BANK +', pad + 288, H - 38);
  ctx.textAlign = 'right';
  ctx.fillStyle = muted;
  ctx.fillText('MASTER', W - 170, H - 52);
  ctx.fillStyle = engrave;
  ctx.font = '300 28px "Saira", sans-serif';
  ctx.fillText('72', W - 112, H - 50);
  ctx.fillStyle = lamp;
  ctx.fillRect(W - 90, H - 29, 62, 4);
}
