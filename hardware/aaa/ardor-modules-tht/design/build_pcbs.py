"""Generate independent two-layer PCB placements and critical local routes.

Run under xvfb with wx initialized before pcbnew. Footprint copies are embedded
in each project so opening/manufacturing a module does not require another board.
"""
import wx
app=wx.App(False)
import pcbnew as p
from pathlib import Path
import json, shutil, sys, subprocess, xml.etree.ElementTree as ET
import sexpdata as sx

ROOT=Path(__file__).resolve().parents[1];mm=p.FromMM
def build(folder):
    spec=json.loads((folder/'verification/design.json').read_text());w,h=spec['size_mm']
    x=ET.parse(folder/'verification/netlist.xml');comps={c.attrib['ref']:c for c in x.findall('.//components/comp')}
    mapping=json.loads((folder/'verification/logical-net-map.json').read_text())
    pins={n.attrib['ref']+'.'+n.attrib['pin']:net.attrib['name'].replace('ALERT/RDY','ALERT{slash}RDY') for net in x.findall('.//nets/net') for n in net.findall('node')}
    b=p.BOARD();b.SetFileName(str(folder/(spec['name']+'.kicad_pcb')));b.SetCopperLayerCount(2)
    nets={}
    for i,name in enumerate(sorted(set(pins.values())),1):
        ni=p.NETINFO_ITEM(b,name,i);b.Add(ni);nets[name]=ni
    pt=lambda xy:p.VECTOR2I(mm(50+xy[0]),mm(50+xy[1]))
    fps={};aliases=set()
    for ref,c in spec['parts'].items():
        alias,fpname=c['fp'].split(':');aliases.add(alias)
        dest=folder/'footprints'/(alias+'.pretty');dest.mkdir(parents=True,exist_ok=True)
        src=ROOT/'design/footprints'/(alias+'.pretty')/(fpname+'.kicad_mod')
        if not src.exists():src=dest/(fpname+'.kicad_mod')
        if not src.exists():src=Path('/usr/share/kicad/footprints')/(alias+'.pretty')/(fpname+'.kicad_mod')
        assert src.exists(),c['fp']
        if src.resolve()!=(dest/src.name).resolve():shutil.copyfile(src,dest/src.name)
        tree=sx.loads((dest/(fpname+'.kicad_mod')).read_text())
        tree[:]=[v for v in tree if not(isinstance(v,list) and v and str(v[0])=='fp_text' and str(v[1])=='user' and v[2]=='${REFERENCE}')]
        (dest/(fpname+'.kicad_mod')).write_text(sx.dumps(tree)+'\n')
        f=p.FootprintLoad(str(dest),fpname);assert f
        f.SetFPID(p.LIB_ID(alias,fpname));f.SetReference(ref);f.SetValue(c['value'])
        if len(c['pcb'])>2:f.SetOrientationDegrees(c['pcb'][2])
        ps=[a.GetPosition() for a in f.Pads()]
        centre=p.VECTOR2I((min(a.x for a in ps)+max(a.x for a in ps))//2,(min(a.y for a in ps)+max(a.y for a in ps))//2)
        f.SetPosition(pt(c['pcb'][:2])-centre)
        # Fields remain available in the assembly drawing; eliminate crowded silk.
        f.Reference().SetLayer(p.F_Fab);f.Reference().SetTextSize(p.VECTOR2I(mm(.8),mm(.8)));f.Reference().SetTextThickness(mm(.1))
        f.Value().SetVisible(False)
        f.SetPath(p.KIID_PATH(comps[ref].find('sheetpath').attrib['tstamps']+comps[ref].findtext('tstamps').split()[0]))
        for prop in ['MPN','JLCPCB Part #']:
            field=p.PCB_FIELD(f,f.GetNextFieldId(),prop);field.SetText(comps[ref].findtext("fields/field[@name='"+prop+"']",''));field.SetVisible(False);f.AddField(field)
        f.GetFieldByName('Datasheet').SetText(comps[ref].findtext('datasheet',''))
        for a in f.Pads():
            if not a.GetNumber():continue
            a.SetNet(nets[pins[ref+'.'+a.GetNumber()]])
            if spec['pins'].get(ref+'.'+a.GetNumber())=='GND' and a.GetAttribute()==p.PAD_ATTRIB_SMD:a.SetLocalZoneConnection(p.ZONE_CONNECTION_FULL)
        b.Add(f);fps[ref]=f
    # Two insulated M2 mounting holes, no accidental chassis bonds via screws.
    for i,xy in enumerate([(3,h-3),(w-3,3)],1):
        ref='H'+str(i);dest=folder/'footprints/MountingHole.pretty';dest.mkdir(parents=True,exist_ok=True);aliases.add('MountingHole')
        fpname='MountingHole_2.2mm_M2';src=dest/(fpname+'.kicad_mod')
        if not src.exists():shutil.copyfile(Path('/usr/share/kicad/footprints/MountingHole.pretty')/(fpname+'.kicad_mod'),src)
        f=p.FootprintLoad(str(dest),fpname);f.SetFPID(p.LIB_ID('MountingHole',fpname));f.SetReference(ref);f.SetAttributes(f.GetAttributes() | p.FP_BOARD_ONLY);f.SetPosition(pt(xy));f.Reference().SetVisible(False);f.Value().SetVisible(False)
        for item in f.GraphicalItems():
            if isinstance(item,p.PCB_TEXT) and item.GetText()=='${REFERENCE}':item.SetVisible(False)
        b.Add(f)
    for a,c in zip([(0,0),(w,0),(w,h),(0,h)],[(w,0),(w,h),(0,h),(0,0)]):
        line=p.PCB_SHAPE(b);line.SetShape(p.SHAPE_T_SEGMENT);line.SetStart(pt(a));line.SetEnd(pt(c));line.SetLayer(p.Edge_Cuts);line.SetWidth(mm(.05));b.Add(line)
    for layer in [p.F_Cu,p.B_Cu]:
        z=p.ZONE(b);z.SetLayer(layer);z.SetNet(nets[mapping['GND']]);z.SetLocalClearance(mm(.2));z.SetMinThickness(mm(.2));z.SetThermalReliefGap(mm(.25));z.SetThermalReliefSpokeWidth(mm(.3));z.SetPadConnection(p.ZONE_CONNECTION_THERMAL)
        poly=z.Outline();poly.NewOutline()
        for xy in [(.5,.5),(w-.5,.5),(w-.5,h-.5),(.5,h-.5)]:poly.Append(pt(xy).x,pt(xy).y)
        b.Add(z)
    for xy in spec['ground_vias']:
        via=p.PCB_VIA(b);via.SetPosition(pt(xy));via.SetWidth(p.F_Cu,mm(.6));via.SetWidth(p.B_Cu,mm(.6));via.SetDrill(mm(.3));via.SetViaType(p.VIATYPE_THROUGH);via.SetLayerPair(p.F_Cu,p.B_Cu);via.SetNet(nets[mapping['GND']]);b.Add(via)
    def pad(ref,n):return next(a for a in fps[ref].Pads() if a.GetNumber()==str(n))
    def track(ref,n,other,m,via=(),width=.2):
        a,c=pad(ref,n),pad(other,m);assert a.GetNetCode()==c.GetNetCode()
        pts=[a.GetPosition(),*[pt(xy) for xy in via],c.GetPosition()]
        for start,end in zip(pts,pts[1:]):
            t=p.PCB_TRACK(b);t.SetStart(start);t.SetEnd(end);t.SetLayer(p.F_Cu);t.SetWidth(mm(width));t.SetNet(a.GetNet());b.Add(t)
    # Buffer feedback loops stay local, outside autorouter changes.
    for ref,c in spec['parts'].items():
        if c['kind'] in ['TLV9002','MCP6022']:
            track(ref,1,ref,2);track(ref,6,ref,7)
    # Export route-only DSN before pour keepouts; the router handles copper,
    # while the final KiCad rules independently enforce isolation and geometry.
    p.SaveBoard(str(folder/'routing/placement.kicad_pcb'),b)
    subprocess.run([sys.executable,str(ROOT/'design/export_dsn.py'),folder.name],check=True)
    (folder/'fp-lib-table').write_text('(fp_lib_table (version 7)'+''.join(f'(lib (name "{a}") (type "KiCad") (uri "${{KIPRJMOD}}/footprints/{a}.pretty") (options "") (descr "Module-local footprint"))' for a in sorted(aliases))+')\n')
    p.SaveBoard(str(folder/(spec['name']+'.kicad_pcb')),b)
    shutil.copyfile(folder/'verification/project-config.json',folder/(spec['name']+'.kicad_pro'))
    print(folder.name,'placed',len(fps),'electrical parts')

if __name__=='__main__':
    if len(sys.argv)>1:build(ROOT/sys.argv[1])
    else:
        for folder in ROOT.iterdir():
            if (folder/'verification/design.json').exists():subprocess.run([sys.executable,__file__,folder.name],check=True)
