"""Back off candidate widths/vias wherever KiCad DRC reports conflicts."""
from pathlib import Path
import pcbnew as p,json,wx
app=wx.App(False)
ROOT=Path(__file__).resolve().parents[1];b=p.LoadBoard(str(ROOT/'Ardor_IO.kicad_pcb'))
r=json.loads((ROOT/'routing/refine-drc.json').read_text());widths=json.loads((ROOT/'routing/widths-before.json').read_text());candidates=set(json.loads((ROOT/'routing/stitching-candidates.json').read_text()))
bad={i['uuid'] for v in r['violations']+r['unconnected_items'] for i in v['items']}
removed=[];n=0
for t in b.GetTracks():
 uid=str(t.m_Uuid.AsString())
 if uid not in bad:continue
 if uid in candidates:removed.append(t)
 elif uid in widths:t.SetWidth(widths[uid]);n+=1
for t in removed:b.Remove(t)
b.BuildConnectivity();p.ZONE_FILLER(b).Fill(b.Zones());p.SaveBoard(str(ROOT/'Ardor_IO.kicad_pcb'),b)
print('Restored',n,'narrow clearances; removed',len(removed),'conflicting stitching candidates')
