"""Small, deterministic one-sheet KiCad schematic writer.

Signal paths are wires. Labels are reserved for power, repeated ground and
explicit jumper/interface branches. The exported netlist is checked separately.
"""
from pathlib import Path
import copy, csv, json, math, uuid, textwrap
import sexpdata as sx
from interfaces import INTERFACES, UNUSED, contract

ROOT = Path(__file__).resolve().parents[1]
S = sx.Symbol
key = lambda v: str(v[0]) if isinstance(v, list) and v else ''
get = lambda v, k: next((x for x in v if key(x) == k), None)
LIB = {x[1]: x for x in sx.loads((ROOT/'design/symbols.kicad_sym').read_text()) if key(x) == 'symbol'}
CAT = json.loads((ROOT/'design/part-catalog.json').read_text())
def uid(name):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, 'https://ardor.dev/hardware/modules/'+name))
q = lambda s: json.dumps(str(s), ensure_ascii=False)
HDR = lambda n: f'Connector_PinHeader_2.54mm:PinHeader_1x{n:02d}_P2.54mm_Vertical'

class Module:
    def __init__(self, slug, name, title, size, notes):
        self.slug, self.name, self.title, self.size, self.notes = slug, name, title, size, notes
        self.id = uid(slug); self.items = []; self.parts = {}; self.pins = {}; self.used = set(); self.wires = []
        self.connected = set(); self.joints = set(); self.expected = {}; self.nc = set(); self.counter = 0
        self.routes = []; self.vias = []; self.keepouts = []
        self.text('ARDOR / INDEPENDENT FUNCTION MODULE', 16, 12, 2)
        self.text(title, 16, 20, 2.5)
        self.text('M1  |  INDEPENDENT MODULE  |  SEE PIN GUIDE AT RIGHT', 16, 29, 1.15)
    def uuid(self):
        self.counter += 1; return uid(self.slug+'/'+str(self.counter))
    def text(self, s, x, y, size=1.2):
        self.items.append(f'(text {q(s)} (at {x} {y} 0) (effects (font (size {size} {size})) (justify left top)) (uuid {q(self.uuid())}))')
    def add(self, kind, ref, nets, sch, pcb, proto=None, value=None, fp=None, unit=1, dnp=False):
        part = copy.deepcopy(CAT[proto or ref]) if proto or ref in CAT else None
        if part:
            fp = fp or part['footprint']
            readable=part.get('schematic_value',part['mpn'])
            if kind=='R':
                resistance=part['catalog_attributes']['Resistance'].replace('Ω','')
                readable=resistance+('' if any(c in resistance for c in 'kM') else 'R')+' / 1%'
            if kind in ['C','C_Polarized']:
                attrs=part['catalog_attributes'];readable=attrs['Capacitance'].replace('F','')+' / '+attrs['Voltage Rating']+' '+attrs.get('Temperature Coefficient','polarized')
            if kind=='FerriteBead':readable=part['catalog_attributes']['Impedance @ Frequency'].replace('Ω','R')
            value=value or readable
        if kind.startswith('Conn') and ref in INTERFACES[self.slug]:value='\n'.join(textwrap.wrap(INTERFACES[self.slug][ref]['title'],width=32))
        assert fp and value, ref
        x,y,*angle = sch; rot = angle[0] if angle else 0
        self.used.add(kind); pins = {}
        for sub in LIB[kind]:
            if key(sub) == 'symbol' and int(sub[1].split('_')[-2]) in [0,unit]:
                for pin in sub:
                    if key(pin) != 'pin': continue
                    dx,dy,a = get(pin,'at')[1:]; n = str(get(pin,'number')[1]); r=math.radians(rot)
                    pins[n]=(round(x+dx*math.cos(r)-dy*math.sin(r),4),round(y-dx*math.sin(r)-dy*math.cos(r),4),(a+rot)%360)
        self.pins[(ref,unit)] = pins
        if kind in ['R','C','C_Polarized','FerriteBead','D','D_Schottky','D_TVS']:
            if rot in [90,270] or kind.startswith('D') and rot==0: rx,ry,vx,vy=x,y-7,x,y-4
            else: rx,ry,vx,vy=x+4,y-1.5,x+4,y+1.5
        else: rx,ry,vx,vy=x,y-17,x,y-14
        if kind=='TLV9002': rx,ry,vx,vy=x,y-10,x,y-7
        if unit==3: rx,ry,vx,vy=x+8,y-1.5,x+8,y+1.5
        if kind=='G5V-1':rx,ry,vx,vy=x,y-24,x,y-21
        if kind=='H11L1': rx,ry,vx,vy=x+25.4,y-17,x+25.4,y-14
        if kind=='ADS1115IDGS':rx,ry,vx,vy=x+25.4,y-17,x+25.4,y-14
        if kind=='TPA6132A2RTE':rx,ry,vx,vy=x+30.48,y-24,x+30.48,y-21
        if kind.startswith('Conn'): rx,ry,vx,vy=x,y-12,x,y-9
        props = ''
        attrs={'Reference':ref,'Value':value,'Footprint':fp,'Datasheet':part['datasheet'] if part else ('https://www.onsemi.com/download/data-sheet/pdf/h11l3m-d.pdf' if kind=='H11L1' else 'https://omronfs.omron.com/en_US/ecb/products/pdf/en-g5v_1.pdf' if kind=='G5V-1' else ''),
               'MPN':part['bom_mpn'] if part else ('onsemi H11L1M' if kind=='H11L1' else 'Omron G5V-1 DC5' if kind=='G5V-1' else fp.split(':')[1]),'JLCPCB Part #':part['jlcpcb_part'] if part else ''}
        for k,v in attrs.items():
            xx,yy=(rx,ry) if k=='Reference' else (vx,vy) if k=='Value' else (x,y)
            props+=f'(property {q(k)} {q(v)} (at {xx} {yy} {rot%180}) (effects (font (size 1.1 1.1))'+(' (hide yes)' if k not in ['Reference','Value'] else (' (justify left)' if kind in ['R','C','C_Polarized','FerriteBead'] and rot==0 else ''))+'))'
        sid=self.uuid()
        self.items.append(f'(symbol (lib_id "Ardor:{kind}") (at {x} {y} {rot}) (unit {unit}) (in_bom yes) (on_board yes) (dnp {"yes" if dnp else "no"}) (uuid {q(sid)}) {props} '+''.join(f'(pin {q(n)} (uuid {q(self.uuid())}))' for n in pins)+f'(instances (project {q(self.name)} (path {q("/"+self.id)} (reference {q(ref)}) (unit {unit})))))')
        self.parts.setdefault(ref, dict(ref=ref,kind=kind,value=value,fp=fp,pcb=pcb,part=part,dnp=dnp,uuid=sid,mpn=attrs['MPN'],datasheet=attrs['Datasheet']))
        for n,net in nets.items():
            n=str(n); assert n in pins,(ref,unit,n)
            if net is None:
                xx,yy,*_=pins[n]; self.nc.add(ref+'.'+n)
                self.items.append(f'(no_connect (at {xx} {yy}) (uuid {q(self.uuid())}))')
            else:
                assert ref+'.'+n not in self.expected or self.expected[ref+'.'+n]==net
                self.expected[ref+'.'+n]=net
        return ref,unit
    def conn(self, ref, names, sch, pcb, rot=0):
        return self.add(f'Conn_01x{len(names):02d}',ref,{str(i+1):n for i,n in enumerate(names)},(*sch,rot),pcb,value=' / '.join(names),fp=HDR(len(names)))
    def p(self, ref, n, unit=1):
        return self.pins[(ref,unit)][str(n)][:2]
    def two(self, kind, ref, a, b, sch, pcb, proto=None, rot=90, value=None):
        return self.add(kind,ref,{'1':a,'2':b},(*sch,rot),pcb,proto=proto,value=value)
    def path(self, net, *nodes):
        pts=[]
        for n in nodes:
            if isinstance(n[0],str):
                ref,pin,*un=n; unit=un[0] if un else 1
                assert self.expected[ref+'.'+str(pin)]==net,(net,n)
                self.connected.add((ref,unit,str(pin))); pts.append(self.p(ref,pin,unit))
            else: pts.append(tuple(n))
        for a,b in zip(pts,pts[1:]):
            assert a[0]==b[0] or a[1]==b[1],(self.slug,net,a,b)
            if a!=b:self.wires.append((a,b))
    def label(self, ref, n, net, unit=1, length=5.08):
        assert self.expected[ref+'.'+str(n)]==net
        x,y,a=self.pins[(ref,unit)][str(n)]; r=math.radians(a)
        p=(round(x-length*math.cos(r),4),round(y+length*math.sin(r),4))
        self.path(net,(ref,n,unit),p)
        self.items.append(f'(label {q(net)} (at {p[0]} {p[1]} {180 if a==0 else 0}) (effects (font (size 1.1 1.1)) (justify {"right" if a==0 else "left"})) (uuid {q(self.uuid())}))')
    def joint(self,x,y): self.joints.add((x,y))
    def link(self, net, a, b, via=()):
        """Join named pins/points, with explicit bends at each supplied waypoint."""
        def point(n):return self.p(*n) if isinstance(n[0],str) else tuple(n)
        nodes=[a,*via,b];expanded=[a]
        for c in nodes[1:]:
            prev=point(expanded[-1]);dest=point(c)
            if prev[0]!=dest[0] and prev[1]!=dest[1]:expanded.append((dest[0],prev[1]))
            expanded.append(c)
        self.path(net,*expanded)
    def buffer(self, ref, unit, inp, out, sch, pcb):
        pn,nn,on=('3','2','1') if unit==1 else ('5','6','7')
        x,y=sch
        self.add('TLV9002',ref,{pn:inp,nn:out,on:out},sch,pcb,proto='U401',unit=unit)
        a=self.p(ref,on,unit); b=self.p(ref,nn,unit)
        xx=a[0]+5.08; yy=y+10.16; left=b[0]-5.08
        self.path(out,(ref,on,unit),(xx,a[1]),(xx,yy),(left,yy),(left,b[1]),(ref,nn,unit))
        self.joint(xx,a[1]); return (xx,a[1])
    def power_unit(self,ref,rail,sch,pcb):
        self.add('TLV9002',ref,{'8':rail,'4':'GND'},sch,pcb,proto='U401',unit=3)
    def save(self):
        out=ROOT/self.slug;out.mkdir(exist_ok=True)
        for d in ['assembly','verification','review','routing']: (out/d).mkdir(exist_ok=True)
        self.text('PIN GUIDE: IN = into this board / OUT = out / I/O = both / GND = 0 V',190,16,1.0)
        for i,(ref,c) in enumerate(INTERFACES[self.slug].items()):
            x=190+(i%2)*110;y=20+(i//2)*9
            self.text(ref+' / '+c['silk'],x,y,1.2)
            for j in range(0,len(c['pins']),2):
                self.text('   '.join(str(p['number'])+': '+p['short'] for p in c['pins'][j:j+2]),x,y+2+(j//2)*1.8,1.2)
        if UNUSED[self.slug]:self.text(' / '.join(pin+': '+note for pin,note in UNUSED[self.slug].items()),300,44.5,1.0)
        # All signal endpoints must be accounted for; power labels are conventional.
        for (ref,unit),pins in self.pins.items():
            for n in pins:
                if ref+'.'+n in self.nc:continue
                if (ref,unit,n) not in self.connected:
                    net=self.expected[ref+'.'+n]
                    assert net in ['GND','CHASSIS','+5V','+3V3','+5V_A','+3V3_A','VREF','HPVDD','HPVSS'],(self.slug,ref,n,net)
                    self.label(ref,n,net,unit)
        # Every T-junction/label/pin anchor explicitly splits the wire.
        anchors=set(self.joints)
        for pins in self.pins.values(): anchors.update(v[:2] for v in pins.values())
        for a,b in self.wires:anchors.update([a,b])
        seen=set()
        for a,b in self.wires:
            if a[0]==b[0]: pts=sorted([v for v in anchors if v[0]==a[0] and min(a[1],b[1])<=v[1]<=max(a[1],b[1])],key=lambda v:v[1])
            else:pts=sorted([v for v in anchors if v[1]==a[1] and min(a[0],b[0])<=v[0]<=max(a[0],b[0])],key=lambda v:v[0])
            for a,b in zip(pts,pts[1:]):
                if (a,b) not in seen:
                    self.items.append(f'(wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) (stroke (width 0) (type default)) (uuid {q(self.uuid())}))');seen.add((a,b))
        for x,y in self.joints:
            self.items.append(f'(junction (at {x} {y}) (diameter 0) (color 0 0 0 0) (uuid {q(self.uuid())}))')
        # External supply flags do not alter any functional connection.
        for i,net in enumerate(sorted({v for v in self.expected.values() if v in ['GND','CHASSIS','+5V','+3V3','+5V_A','+3V3_A']})):
            x,y=20.32+i*25.4,270.51; f=uid(self.slug+'/flag/'+net);self.used.add('PWR_FLAG')
            self.items.append(f'(symbol (lib_id "Ardor:PWR_FLAG") (at {x} {y} 0) (unit 1) (in_bom no) (on_board no) (dnp no) (uuid {q(f)}) (property "Reference" "#FLG{i:02d}" (at {x} {y} 0) (effects (font (size 1 1)) (hide yes))) (property "Value" "PWR_FLAG" (at {x} {y} 0) (effects (font (size 1 1)) (hide yes))) (pin "1" (uuid {q(uid(f+"pin"))})) (instances (project {q(self.name)} (path {q("/"+self.id)} (reference "#FLG{i:02d}") (unit 1)))))')
            self.items.append(f'(label {q(net)} (at {x} {y} 0) (effects (font (size 1 1)) (justify left)) (uuid {q(uid(f+"label"))}))')
        defs=[];lib=[]
        for kind in sorted(self.used):
            sym=copy.deepcopy(LIB[kind]);lib.append(sx.dumps(sym));sym[1]='Ardor:'+kind;defs.append(sx.dumps(sym))
        sch=f'(kicad_sch (version 20250114) (generator "eeschema") (uuid {q(self.id)}) (paper "A3") (title_block (title {q(self.title)}) (date "2026-10-02") (rev "M1") (company "Ardor") (comment 1 "Engineering prototype - bench validation required")) (lib_symbols '+''.join(defs)+')'+''.join(self.items)+'(sheet_instances (path "/" (page "1"))))'
        (out/(self.name+'.kicad_sch')).write_text(sch+'\n')
        (out/'Ardor.kicad_sym').write_text('(kicad_symbol_lib (version 20241209) (generator "kicad_symbol_editor")'+''.join(lib)+')\n')
        (out/'sym-lib-table').write_text('(sym_lib_table (version 7) (lib (name "Ardor") (type "KiCad") (uri "${KIPRJMOD}/Ardor.kicad_sym") (options "") (descr "Local module symbols")))\n')
        pro=json.loads((ROOT/'design/project-template.json').read_text());pro['meta']['filename']=self.name+'.kicad_pro'
        pro['net_settings']['classes'].append({**pro['net_settings']['classes'][0],'name':'Ground','track_width':0.4,'priority':0})
        pro['net_settings']['classes'].append({**pro['net_settings']['classes'][0],'name':'Chassis','track_width':0.6,'priority':1})
        pro['net_settings']['netclass_patterns']=[{'netclass':'Ground','pattern':'/GND'},{'netclass':'Chassis','pattern':'/CHASSIS'}]
        (out/(self.name+'.kicad_pro')).write_text(json.dumps(pro,indent=2)+'\n')
        (out/'verification/project-config.json').write_text(json.dumps(pro,indent=2)+'\n')
        spec={'name':self.name,'title':self.title,'slug':self.slug,'size_mm':self.size,'parts':self.parts,'pins':self.expected,'nc':sorted(self.nc),'routes':self.routes,'ground_vias':self.vias,'keepouts':self.keepouts,'notes':self.notes}
        (out/'verification/design.json').write_text(json.dumps(spec,indent=2)+'\n')
        (out/'verification/connector-guide.json').write_text(json.dumps(contract(self.slug,spec),indent=2)+'\n')
        bycode={}
        for ref,c in self.parts.items():
            if c['part']:
                p=c['part']; code=p['jlcpcb_part']
                if code not in bycode: bycode[code]={**p,'references':[]}
                bycode[code]['references'].append(ref)
        (out/'assembly/parts.json').write_text(json.dumps({'board_quantity':2,'assembly_scope':'Top-side SMT only. Headers, jumpers, optocoupler and relay fitted by builder.','parts':list(bycode.values())},indent=2)+'\n')
        with (out/'BOM.csv').open('w') as f:
            w=csv.writer(f,lineterminator='\n');w.writerow(['Reference','Value','Footprint','MPN','JLCPCB Part #','Assembly'])
            for ref,c in self.parts.items():w.writerow([ref,c['value'],c['fp'],c['mpn'],c['part']['jlcpcb_part'] if c['part'] else '', 'SMT' if c['part'] else 'Manual'])
        print(self.slug,len(self.parts),'parts',len(self.expected),'connected pins')
