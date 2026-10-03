from pathlib import Path
import pcbnew as p,json,html
R=Path(__file__).resolve().parents[1];out=R/'assembly';b=p.LoadBoard(str(R/'Ardor_IO.kicad_pcb'))
selected={r for a in json.loads((out/'parts.json').read_text())['parts'] for r in a['references']}
mm=p.ToMM;s=['<svg xmlns="http://www.w3.org/2000/svg" width="1440" height="1100" viewBox="0 0 1440 1100">','<rect width="1440" height="1100" fill="white"/>','<g font-family="sans-serif" fill="#153348"><text x="40" y="38" font-size="25">Ardor IO — top-side assembly reference</text><text x="40" y="67" font-size="17">Blue: JLCPCB SMT. Grey: hand assembly / bare pads. Red pad: pin 1. Compare polarized parts with polarity-reference.csv.</text></g>','<g transform="translate(40,105) scale(20) translate(-50,-50)">','<rect x="50" y="50" width="68" height="46" fill="#fcfcfa" stroke="#34495e" stroke-width="0.08"/>']
for f in b.GetFootprints():
 ref=f.GetReference();box=f.GetBoundingBox(False,False);color='#d9eaf6' if ref in selected else '#eeeeee'
 s.append(f'<rect x="{mm(box.GetLeft())}" y="{mm(box.GetTop())}" width="{mm(box.GetWidth())}" height="{mm(box.GetHeight())}" fill="{color}" stroke="#8192a1" stroke-width="0.025"/>')
 for pad in f.Pads():
  at=pad.GetPosition();size=pad.GetSize();x,y=mm(at.x),mm(at.y);w,h=mm(size.x),mm(size.y)
  fill='#c23c37' if pad.GetNumber()=='1' else '#888e91';angle=-pad.GetOrientationDegrees()
  if pad.GetShape()==p.PAD_SHAPE_CIRCLE:s.append(f'<circle cx="{x}" cy="{y}" r="{w/2}" fill="{fill}"/>')
  else:s.append(f'<rect x="{x-w/2}" y="{y-h/2}" width="{w}" height="{h}" rx="0.06" fill="{fill}" transform="rotate({angle} {x} {y})"/>')
  drill=mm(pad.GetDrillSize().x)
  if drill:s.append(f'<circle cx="{x}" cy="{y}" r="{drill/2}" fill="white"/>')
 pos=f.GetPosition();x,y=mm(pos.x),mm(pos.y);font=.58 if ref in selected else .54
 # Reference centered over the package; white halo keeps it readable over copper.
 s.append(f'<text x="{x}" y="{y}" font-family="sans-serif" font-size="{font}" font-weight="bold" text-anchor="middle" dominant-baseline="central" fill="#102b3e" stroke="white" stroke-width="0.09" paint-order="stroke">{html.escape(ref)}</text>')
 s.append(f'<text x="{x}" y="{y}" font-family="sans-serif" font-size="{font}" font-weight="bold" text-anchor="middle" dominant-baseline="central" fill="#102b3e">{html.escape(ref)}</text>')
s+=['</g>','<text x="40" y="1060" font-family="sans-serif" font-size="17">Top view, 68 × 46 mm. Pin numbers follow the KiCad footprint; this drawing is not JLCPCB’s placement preview.</text>','</svg>']
(out/'assembly-top.svg').write_text('\n'.join(s)+'\n')
