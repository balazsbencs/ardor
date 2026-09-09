"""Widen low-impedance routes and add candidate stitching vias; DRC follows.
Routing/dimensions are validated by verify_pcb.py after this stage.
"""
from pathlib import Path
import json,math
import pcbnew as p
import wx
app=wx.App(False)
ROOT=Path(__file__).resolve().parents[1];b=p.LoadBoard(str(ROOT/'Ardor_IO.kicad_pcb'))
mm=p.FromMM
# U601's fine-pitch ground lead uses a solid pour connection; the nearby
# exposed pad and explicit ground tracks carry its return current.
for f in b.GetFootprints():
 if f.GetReference()=='U601':
  for pad in f.Pads():
   if pad.GetNumber()=='15':pad.SetLocalZoneConnection(p.ZONE_CONNECTION_FULL)
# Snapshot the pre-existing package geometry so library checks are portable across KiCad 9 versions.
local={'Package_SO','Package_DFN_QFN','Package_TO_SOT_SMD'};libs=set()
for f in b.GetFootprints():
 if f.GetReference().startswith('H'):f.SetFPID(p.LIB_ID('MountingHole','MountingHole_3.2mm_M3'))
 lib=str(f.GetFPID().GetLibNickname());libs.add(lib)
 if lib in local:
  path=ROOT/'footprints'/(lib+'.pretty');path.mkdir(parents=True,exist_ok=True)
  c=p.FOOTPRINT(f);c.SetPosition(p.VECTOR2I(0,0));c.SetOrientationDegrees(0);c.SetReference('REF**');c.SetValue(str(c.GetFPID().GetLibItemName()));c.SetPath(p.KIID_PATH())
  p.PCB_IO_KICAD_SEXPR().FootprintSave(str(path),c)
(ROOT/'fp-lib-table').write_text('(fp_lib_table\n (version 7)\n'+''.join(f' (lib (name "{l}")(type "KiCad")(uri "'+('${KIPRJMOD}/footprints/' if l in local|{'Ardor_Capacitor'} else '${KICAD9_FOOTPRINT_DIR}/')+f'{l}.pretty")(options "")(descr ""))\n' for l in sorted(libs))+')\n')
original={}
for t in b.GetTracks():
 if isinstance(t,p.PCB_VIA):continue
 name=t.GetNetname().rsplit('/',1)[-1];width=.2
 if name in ['CHASSIS']:width=.6
 elif name.startswith('+') or name in ['GND','RELAY_LOW']:width=.4
 elif name in ['HP_L','HP_R','HP_L_RAW','HP_R_RAW','HPVDD','HPVSS','CPP','CPN']:width=.3
 if mm(width)>t.GetWidth():original[str(t.m_Uuid.AsString())]=t.GetWidth();t.SetWidth(mm(width))
(ROOT/'routing/widths-before.json').write_text(json.dumps(original))
# Conservative open-space candidates; final KiCad clearance/unconnected checks decide acceptance.
new=[];boxes=[f.GetBoundingBox(False,False) for f in b.GetFootprints()]
for x in range(5,64,6):
 for y in range(10,44,6):
  if x<22 and 15<y<30:continue
  pos=p.VECTOR2I(mm(50+x),mm(50+y))
  if any(bb.Contains(pos) for bb in boxes):continue
  v=p.PCB_VIA(b);v.SetPosition(pos);v.SetWidth(p.F_Cu,mm(.6));v.SetDrill(mm(.3));v.SetViaType(p.VIATYPE_THROUGH);v.SetLayerPair(p.F_Cu,p.B_Cu);v.SetNet(b.FindNet('GND'));b.Add(v);new.append(str(v.m_Uuid.AsString()))
(ROOT/'routing/stitching-candidates.json').write_text(json.dumps(new))
b.BuildConnectivity();p.ZONE_FILLER(b).Fill(b.Zones());p.SaveBoard(str(ROOT/'Ardor_IO.kicad_pcb'),b)
print('Widened',len(original),'segments; added',len(new),'candidate ground vias')
