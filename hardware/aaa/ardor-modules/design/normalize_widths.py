"""Remove router neckdown widths, refill, and require a clean native DRC."""
import wx
app=wx.App(False)
import pcbnew as p
from pathlib import Path
import json,sys,shutil,subprocess
ROOT=Path(__file__).resolve().parents[1];folder=ROOT/sys.argv[1]
spec=json.loads((folder/'verification/design.json').read_text());file=folder/(spec['name']+'.kicad_pcb')
b=p.LoadBoard(str(file));changed=[]
for t in b.GetTracks():
    if isinstance(t,p.PCB_VIA):continue
    target=.6 if t.GetNetname()=='/CHASSIS' else .2
    if abs(p.ToMM(t.GetWidth())-target)>1e-6:
        changed.append({'net':t.GetNetname(),'old_width_mm':p.ToMM(t.GetWidth()),'width_mm':target,'uuid':t.m_Uuid.AsString()});t.SetWidth(p.FromMM(target))
if changed:
    for z in b.Zones():
        if not z.GetIsRuleArea():z.UnFill()
    b.BuildConnectivity();assert p.ZONE_FILLER(b).Fill(b.Zones());p.SaveBoard(str(file),b)
    shutil.copyfile(folder/'verification/project-config.json',folder/(spec['name']+'.kicad_pro'))
subprocess.run([sys.executable,str(ROOT/'design/drc.py'),folder.name],check=True)
(folder/'verification/width-normalization.json').write_text(json.dumps({'changed_segments':changed,'ordinary_width_mm':.2,'chassis_width_mm':.6},indent=2)+'\n')
