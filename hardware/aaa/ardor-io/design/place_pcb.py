from pathlib import Path
import pcbnew as p, wx, xml.etree.ElementTree as ET, csv, json, math
app=wx.App(False)
ROOT=Path(__file__).resolve().parents[1]
LIB=Path('/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints')
b=p.BOARD();b.SetFileName(str(ROOT/'Ardor_IO.kicad_pcb'));b.SetCopperLayerCount(2)
courtyard=p.LSET();courtyard.AddLayer(p.F_CrtYd)
mm=p.FromMM;pt=lambda x,y:p.VECTOR2I(mm(x),mm(y));origin=(50,50)
# Coordinates below are millimetres from the top-left board corner; rotations are KiCad degrees.
poses={
'J101':(25,7,90),'J102':(8,13,90),'FB101':(59,14,0),'C101':(63,14,90),'C102':(66,14,90),'FB102':(37,14,0),'C103':(40,14,90),'C104':(43,14,90),'R101':(10,40,90),
'J202':(6,26,0),'D202':(10,27,90),'D203':(10,23,0),'D204':(10,32,0),'C202':(14,23,0),'C203':(14,32,0),'FB201':(18,24,0),'FB202':(18,29,0),'R201':(21,22,90),'D201':(21,31,90),'U201':(24,25,0),'C201':(34,24,90),'R202':(36,28,0),'R203':(36,31,0),
'J302':(6,57,0),'D301':(10,55,0),'D302':(10,63,0),'JP301':(15,57,0),'R301':(22,53,0),'R302':(24,58,0),'R303':(23,63,90),'U301':(34,59,0),'C301':(29,58,90),'C302':(29,61,90),'C303':(39,59,90),'R304':(25,66,0),'R305':(39,56,0),'R306':(39,53,0),'R307':(39,63,90),'D303':(29,54,0),'D304':(29,65,0),'D305':(34,53,0),'D306':(34,65,0),
'C401':(40,26,0),'C402':(40,36,0),'U401':(51,31,0),'R401':(48,24,90),'R402':(57,33,90),'C404':(54,27,0),'R403':(58,27,0),'R404':(58,30,0),'U402':(64,31,0),'C405':(67,27,0),'R405':(63,38,0),'R406':(67,38,0),'C403':(67,41,0),
'U501':(75,31,0),'C503':(78,27,0),'C501':(73,22,0),'R501':(66,22,90),'R502':(80.5,20,90),'K501':(84,18,0),'D501':(92,24,90),'J503':(95,27,0),'Q501':(86,35,0),'R503':(80,35,0),'R504':(84,38,0),'D502':(90,34,90),'C502':(74,42,0),'R505':(83,43,0),'R506':(88,43,90),'J502':(95,42,0),
'C601':(65,52,0),'C602':(65,63,0),'U601':(80,56,0),'R601':(74,47,0),'R602':(78,50,0),'R603':(85,50,0),'R604':(85,62,0),'D601':(91,54,0),'D602':(91,62,0),'J602':(95,55,0),'C603':(76,53,90),'C604':(80,52.5,0),'C605':(84,53,0),'C606':(80,60,0),'C607':(84,56.5,90),
'TP101':(63,18,0),'TP102':(40,18,0),'TP103':(46,14,0),'TP201':(35,35,0),'TP301':(27,70,0),'TP401':(57,39,0),'TP402':(70,33,0),'TP501':(96,35,0),'TP601':(87,66,0),
}
xml=ET.parse(ROOT/'verification/pcb-source-netlist.xml')
nets={};pinnets={}
for n in xml.findall('.//nets/net'):
 name=n.attrib['name'].replace('ALERT/RDY','ALERT{slash}RDY');ni=p.NETINFO_ITEM(b,name);b.Add(ni);nets[name]=ni
 for nd in n.findall('node'):pinnets[(nd.attrib['ref'],nd.attrib['pin'])]=name
comps={c.attrib['ref']:c for c in xml.findall('.//components/comp') if c.findtext('footprint')}
assert set(comps)==set(poses),(set(comps)-set(poses),set(poses)-set(comps))
fps={}
for ref,c in comps.items():
 lid=c.findtext('footprint');lib,nam=lid.split(':');fp=p.FootprintLoad(str(LIB/(lib+'.pretty')),nam);assert fp, lid
 b.Add(fp);fp.SetReference(ref);fp.SetValue(c.findtext('value'));fp.SetFPID(p.LIB_ID(lib,nam))
 x,y,angle=poses[ref];fp.SetPosition(pt(x+50,y+50));fp.SetOrientationDegrees(angle)
 pathname=c.find('sheetpath').attrib['tstamps']+c.findtext('tstamps').split()[0]
 kp=p.KIID_PATH(pathname);fp.SetPath(kp)
 props={v.attrib['name']:v.attrib.get('value','') for v in c.findall('property')}
 fp.SetSheetname(props.get('Sheetname',''));fp.SetSheetfile(props.get('Sheetfile',''))
 for pad in fp.Pads():
  if (ref,pad.GetNumber()) in pinnets:pad.SetNet(nets[pinnets[(ref,pad.GetNumber())]])
 fp.SetAttributes(fp.GetAttributes() & ~p.FP_EXCLUDE_FROM_BOM)
 fp.Value().SetVisible(False)
 rt=fp.Reference();rt.SetTextSize(pt(.8,.8));rt.SetTextThickness(mm(.11));rt.SetTextAngle(p.EDA_ANGLE(0,p.DEGREES_T));rt.SetLayer(p.F_SilkS)
 # Put references outside the physical body; small parts get alternate left/right offsets later if necessary.
 bb=fp.GetBoundingBox(False,False)
 rt.SetPosition(pt(x+50,p.ToMM(bb.GetTop())-0.75))
 fps[ref]=fp
# Four provisional M3 clearance holes, PCB-only mounting features.
for i,(x,y) in enumerate([(4,4),(96,4),(4,76),(96,76)],1):
 fp=p.FootprintLoad(str(LIB/'MountingHole.pretty'),'MountingHole_3.2mm_M3');b.Add(fp);fp.SetReference('H'+str(i));fp.SetValue('M3 / PROVISIONAL');fp.SetPosition(pt(x+50,y+50));fp.SetAttributes(fp.GetAttributes()|p.FP_EXCLUDE_FROM_BOM|p.FP_EXCLUDE_FROM_POS_FILES|p.FP_BOARD_ONLY);fp.Value().SetVisible(False);fp.Reference().SetVisible(False);fps['H'+str(i)]=fp
# Board outline.
for a,z in [((50,50),(150,50)),((150,50),(150,130)),((150,130),(50,130)),((50,130),(50,50))]:
 ln=p.PCB_SHAPE();ln.SetShape(p.SHAPE_T_SEGMENT);ln.SetStart(pt(*a));ln.SetEnd(pt(*z));ln.SetLayer(p.Edge_Cuts);ln.SetWidth(mm(.05));b.Add(ln)
def txt(text,x,y,size=1,layer=p.F_SilkS):
 t=p.PCB_TEXT(b);t.SetText(text);t.SetPosition(pt(x+50,y+50));t.SetTextSize(pt(size,size));t.SetTextThickness(mm(.13));t.SetLayer(layer);b.Add(t)
for text,x,y in [('PI / HOST',50,3),('AUX L G R',12,9),('MIDI',7,19),('EXPRESSION',15,49),('LINE',94,19),('AMP FEED',94,38),('HEADPHONES',88,70),('ARDOR IO  /  PLACEMENT A',40,76)]:txt(text,x,y,.9)
# Document-only functional boundaries; no restrictive copper islands.
for text,x,y in [('CONTROL ADC',35,74),('AUDIO BUFFERS',53,45),('RELAY',86,14),('100 x 80 mm / UNROUTED',51,79)]:txt(text,x,y,.85,p.Dwgs_User)
# Copper/track/via keepout across the optocoupler's input-output separation.
z=p.ZONE(b);z.SetIsRuleArea(True);z.SetLayerSet(p.LSET.AllCuMask());z.SetDoNotAllowTracks(True);z.SetDoNotAllowVias(True);z.SetDoNotAllowCopperPour(True);z.SetDoNotAllowPads(False);z.SetDoNotAllowFootprints(False)
poly=z.Outline();poly.NewOutline()
for x,y in [(26.2,19),(29.4,19),(29.4,37),(26.2,37)]:poly.Append(mm(x+50),mm(y+50))
b.Add(z)
txt('MIDI ISOLATION CORRIDOR',27.8,40,.65,p.Dwgs_User)
# Consistent sheet properties and no copper fills/routing in this placement-only board.
b.SetTitleBlock(p.TITLE_BLOCK());b.GetTitleBlock().SetTitle('Ardor Codec Zero IO - placement plan');b.GetTitleBlock().SetRevision('A-placement');b.GetTitleBlock().SetComment(0,'100 x 80 mm. Unrouted. Enclosure 180 x 150 mm. Verify mating connector and mounting before layout release.')
# Place reference text in clear space around each courtyard. This is not a silkscreen fabrication release.
boxes={}
for ref,fp in fps.items():
 bb=fp.GetLayerBoundingBox(courtyard);boxes[ref]=(p.ToMM(bb.GetLeft()),p.ToMM(bb.GetTop()),p.ToMM(bb.GetRight()),p.ToMM(bb.GetBottom()))
def intersect(a,c,gap=0):return a[0]<c[2]+gap and a[2]>c[0]-gap and a[1]<c[3]+gap and a[3]>c[1]-gap
occupied=[]
for drawing in b.GetDrawings():
 if isinstance(drawing,p.PCB_TEXT) and drawing.GetLayer()==p.F_SilkS:
  bb=drawing.GetBoundingBox();occupied.append((p.ToMM(bb.GetLeft()),p.ToMM(bb.GetTop()),p.ToMM(bb.GetRight()),p.ToMM(bb.GetBottom())))
for ref in sorted(poses,key=lambda r:(not r.startswith('U'),not r.startswith('J'),r)):
 fp=fps[ref];left,top,right,bottom=boxes[ref];cx=(left+right)/2;cy=(top+bottom)/2
 hw=len(ref)*.8*.48+.2;hh=.6
 candidates=[]
 for d in [.8,1.6,2.6,3.8,5]:
  candidates.extend([(cx,top-d),(cx,bottom+d),(left-d-hw,cy),(right+d+hw,cy)])
 best=None
 for xx,yy in candidates:
  box=(xx-hw,yy-hh,xx+hw,yy+hh)
  if not(50.8<box[0] and box[2]<149.2 and 50.8<box[1] and box[3]<129.2):continue
  score=sum(intersect(box,bb,.2) for bb in boxes.values())*10+sum(intersect(box,bb,.15) for bb in occupied)*5
  if best is None or score<best[0]:best=(score,xx,yy,box)
  if score==0:break
 _,xx,yy,box=best;fp.Reference().SetPosition(pt(xx,yy));occupied.append(box)
proj=ROOT/'Ardor_IO.kicad_pro';settings=json.loads(proj.read_text());rules=settings.setdefault('board',{}).setdefault('design_settings',{}).setdefault('rules',{})
rules['min_through_hole_diameter']=0.2;rules['min_text_height']=0.8;rules['min_text_thickness']=0.1
proj.write_text(json.dumps(settings,indent=2)+'\n')
p.SaveBoard(str(ROOT/'Ardor_IO.kicad_pcb'),b)
# Numerical placement audit: courtyard bounds and all copper pad net assignments.
rows=[];over=[]
for ref,fp in fps.items():
 fp.BuildCourtyardCaches();bb=fp.GetLayerBoundingBox(courtyard);
 if bb.GetWidth()==0:bb=fp.GetBoundingBox(False,False)
 rows.append((ref,bb))
for i,(ra,a) in enumerate(rows):
 for rb,bb in rows[i+1:]:
  if a.Intersects(bb):over.append((ra,rb))
print('Placed',len(comps),'schematic footprints + 4 mounting holes')
print('Courtyard bounding-box overlaps:',over)
with (ROOT/'pcb-plan/placement.csv').open('w') as f:
 w=csv.writer(f);w.writerow(['Reference','X_mm_from_left','Y_mm_from_top','Rotation_deg','Side','Footprint'])
 for ref in sorted(poses):w.writerow([ref,*poses[ref],'Front',comps[ref].findtext('footprint')])
(ROOT/'pcb-plan/initial-overlaps.json').write_text(json.dumps(over,indent=2))
