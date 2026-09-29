"""Generate the review-only, one-page schematic from the fresh KiCad netlist.
Requires cairosvg; original CAD is read-only. Geometry is deliberately hand arranged.
Each rendered physical pin is checked against the source netlist. Power/ground and
J101's distributed pin groups are the only implicit connections.
"""
from pathlib import Path
import xml.etree.ElementTree as ET
import json, html, math
import cairosvg
O=Path(__file__).resolve().parent
root=ET.parse(O/'fresh-netlist.xml')
N={n.attrib['ref']+'.'+n.attrib['pin']:(net.attrib['name'] if 'unconnected-' in net.attrib['name'] else net.attrib['name'].split('/')[-1]) for net in root.findall('.//nets/net') for n in net.findall('node')}
V={c.attrib['ref']:c.findtext('value') for c in root.findall('.//components/comp')}
parts=[]; pins={}; nets={}; segs=[]; marks=[]; junctions=set()
W,H=3000,2120
INK='#172c38'; WIRE='#246a66'; MUTED='#536775'
def put(s):parts.append(s)
def text(x,y,s,size=17,fill=INK,anchor='start',weight='normal'):
 put(f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}">{html.escape(str(s))}</text>')
def line(a,b,color=INK,width=2,dash=''):
 put(f'<path d="M {a[0]} {a[1]} L {b[0]} {b[1]}" fill="none" stroke="{color}" stroke-width="{width}"'+(f' stroke-dasharray="{dash}"' if dash else '')+'/>')
def rect(x,y,w,h,fill='white',stroke=INK):put(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{fill}" stroke="{stroke}" stroke-width="2"/>')
def dot(p):junctions.add(p);put(f'<circle cx="{p[0]}" cy="{p[1]}" r="4" fill="{WIRE}"/>')
def pin(ref,num,p,net=None):
 key=ref+'.'+str(num); actual=N[key]
 if net is not None:assert actual==net,(key,actual,net)
 assert key not in pins,key
 pins[key]=p; nets[key]=actual
 return key
def pt(p):return pins[p] if isinstance(p,str) else p
def wire(*ps):
 keys=[p for p in ps if isinstance(p,str)]
 if keys:assert len({nets[p] for p in keys})==1,keys
 ps=[pt(p) for p in ps]
 for a,b in zip(ps,ps[1:]):
  assert a[0]==b[0] or a[1]==b[1],(a,b)
  line(a,b,WIRE,2.4);segs.append((a,b,keys))
def ground(p,chassis=False):
 x,y=pt(p);wire(p,(x,y+15))
 if chassis:
  line((x-14,y+15),(x+14,y+15));
  for dx in [-10,0,10]:line((x+dx,y+15),(x+dx-6,y+23))
 else:
  for dy,w in [(15,14),(21,9),(27,4)]:line((x-w,y+dy),(x+w,y+dy))
 if isinstance(p,str):assert nets[p]==('CHASSIS' if chassis else 'GND'),p
 marks.append((p,'CHASSIS' if chassis else 'GND'))
def supply(p,name=None):
 x,y=pt(p);name=name or nets[p];assert not isinstance(p,str) or nets[p]==name,(p,name);wire(p,(x,y-18));line((x-8,y-10),(x,y-18));line((x+8,y-10),(x,y-18));text(x,y-27,name,15,anchor='middle');marks.append((p,name))
def two(ref,x,y,vertical=False,value=None,kind=None):
 # Physical pin 1 is left/top. Diodes: pin 1 is cathode, pin 2 anode.
 kind=kind or ('C' if ref.startswith('C') else 'FB' if ref.startswith('FB') else 'D' if ref.startswith('D') else 'R')
 a=(x,y-32) if vertical else (x-40,y);b=(x,y+32) if vertical else (x+40,y)
 pin(ref,1,a);pin(ref,2,b)
 put(f'<g transform="translate({x} {y})'+(' rotate(90)' if vertical else '')+'">')
 line((-40 if not vertical else -32,0),(-16,0));line((16,0),(40 if not vertical else 32,0))
 if kind in ['R','FB']:
  rect(-16,-7,32,14)
  if kind=='FB':line((-12,7),(12,-7))
 elif kind=='C':
  line((-5,-15),(-5,15));line((5,-15),(5,15));line((-16,0),(-5,0));line((5,0),(16,0))
  if ref=='C501':text(-27,-17,'+',19)
 elif kind=='D':
  line((-12,-12),(-12,12));put(f'<path d="M -12 0 L 12 -12 L 12 12 Z" fill="white" stroke="{INK}" stroke-width="2"/>');line((-16,0),(-12,0));line((12,0),(16,0))
  if ref in ['D202','D203','D204','D301','D302','D501','D601','D602']:
   # Symmetric TVS drawn as opposing zener triangles; bidirectional, no polarity.
   rect(-16,-16,32,32,'white','none');put(f'<path d="M -16 -11 L 0 0 L -16 11 Z M 16 -11 L 0 0 L 16 11 Z M 0 -12 L 0 12" fill="white" stroke="{INK}" stroke-width="2"/>')
 put('</g>')
 val=value or V[ref].replace(' / 1%','').replace(' / 0.1%',' 0.1%').replace(' / isolation','').replace(' / rail bleed','')
 if vertical:text(x+23,y-5,ref,16,weight='bold');text(x+23,y+16,val,15)
 else:text(x,y-34,ref,16,anchor='middle',weight='bold');text(x,y-15,val,15,anchor='middle')
 return ref+'.1',ref+'.2'
def op(ref,unit,x,y):
 p,n,o=('3','2','1') if unit=='A' else ('5','6','7')
 put(f'<path d="M {x} {y-28} L {x} {y+42} L {x+66} {y+7} Z" fill="white" stroke="{INK}" stroke-width="2"/>')
 pin(ref,p,(x-25,y));pin(ref,n,(x-25,y+25));pin(ref,o,(x+90,y+7))
 line((x-25,y),(x,y));line((x-25,y+25),(x,y+25));line((x+66,y+7),(x+90,y+7))
 text(x+7,y+6,'+',15);text(x+7,y+31,'−',15);text(x+30,y-48,ref+unit,18,anchor='middle',weight='bold');text(x+30,y-31,'OPA2320',14,anchor='middle')
 # Show feedback as a connected loop, routed under the amplifier.
 wire(ref+'.'+o,(x+100,y+7),(x+100,y+65),(x-35,y+65),(x-35,y+25),ref+'.'+n);dot((x+100,y+7))
 for pn in [p,n,o]:px,py=pins[ref+'.'+pn];text(px-4,py-5,pn,12,MUTED)
 return ref+'.'+p,ref+'.'+o
def ic(ref,title,x,y,w,h,spec):
 rect(x,y,w,h);text(x+w/2,y+27,ref+'  '+title,19,anchor='middle',weight='bold')
 for pn,label,side,off in spec:
  if side=='l':p=(x-25,y+off);line(p,(x,y+off));text(x+10,y+off+5,label,15);text(x-7,y+off-6,pn,12,MUTED,'end')
  elif side=='r':p=(x+w+25,y+off);line((x+w,y+off),p);text(x+w-10,y+off+5,label,15,anchor='end');text(x+w+7,y+off-6,pn,12,MUTED)
  elif side=='b':p=(x+off,y+h+25);line((x+off,y+h),p);text(x+off,y+h-10,label,13,anchor='middle');text(x+off+5,y+h+18,pn,12,MUTED)
  else:p=(x+off,y-25);line(p,(x+off,y));text(x+off,y+48,label,13,anchor='middle');text(x+off+5,y-9,pn,12,MUTED)
  pin(ref,pn,p)
def port(ref,pn,x,y,label,right=False):
 pin(ref,pn,(x,y));put(f'<circle cx="{x}" cy="{y}" r="5" fill="white" stroke="{INK}" stroke-width="2"/>');text(x+12 if right else x-12,y-10,f'{pn}  {label}',16,anchor='start' if right else 'end')
def tp(ref,p,x,y):
 pin(ref,1,(x,y));wire(p,(x,pt(p)[1]),ref+'.1');put(f'<circle cx="{x}" cy="{y}" r="6" fill="white" stroke="{WIRE}" stroke-width="2"/>');text(x+10,y-7,ref,14,MUTED)
def section(x,y,n,title,desc):
 text(x,y,n,17,WIRE,weight='bold');text(x+42,y,title,25,weight='bold');text(x+42,y+26,desc,16,MUTED)
def group(x,y,title):text(x,y,title,18,weight='bold')
# Paper and title.
rect(0,0,W,H,'white','none')
text(45,50,'ARDOR / Codec Zero I/O',34,weight='bold');text(45,80,'CONNECTED SCHEMATIC  •  Rev A · review fixes applied  •  Review drawing, 29 September 2026',18,MUTED)
text(2950,49,'ONE PAGE / A1 LANDSCAPE',20,anchor='end',weight='bold');text(2950,78,'Dots join wires. Crossings without dots do not join.',16,MUTED,anchor='end')
line((45,100),(2950,100),'#aab7bd',1)
section(45,140,'01','Audio signal flow','Follow left to right. All six op-amp sections use +5V_A and GND; power pins are drawn at lower right.')
# Codec harness and buffered channels.
group(70,218,'J102 · CODEC AUX')
port('J102',1,230,275,'L');port('J102',3,230,410,'R');port('J102',2,230,500,'GND');ground('J102.2')
for c,r,unit,y,pn in [('C401','R401','A',275,'1'),('C402','R402','B',410,'3')]:
 two(c,350,y);two(r,450,y+60,True,value='100k');a,o=op('U401',unit,560,y)
 wire('J102.'+pn,c+'.1');wire(c+'.2',(450,y),a);wire((450,y),r+'.1');dot((450,y))
# Bias rail connects both resistors to the buffered midpoint.
wire('R401.2',(510,367),(510,525),(450,525),'R402.2');wire('R402.2',(450,525));dot((450,525))
# Mono averaging.
two('R403',810,282,value='10k 0.1%');two('R404',810,417,value='10k 0.1%')
wire('U401.1','R403.1');wire('U401.7','R404.1');wire('R403.2',(900,282),(900,350));wire('R404.2',(900,417),(900,350));dot((900,350))
a,o=op('U402','A',980,350);wire((900,350),a);tp('TP402',o,1110,322)
# Output buffers.
a,o=op('U501','A',1220,260);wire('U402.1',(1140,357),(1140,260),a)
a,o=op('U501','B',1220,480);wire((1140,357),(1140,480),a);dot((1140,357))
two('C501',1430,267,value='47u / 16V +');wire('U501.1','C501.1')
two('R501',1530,330,True,value='10k');wire('C501.2',(1530,267),(1530,298));ground('R501.2');dot((1530,267))
two('R502',1690,267,value='100R');wire('C501.2','R502.1')
# SPDT contact, shown in resting (grounded) position. Coil is elsewhere on this page.
group(1820,212,'K501 · G5V-1 DC5')
for pn,p in [('10',(1820,267)),('1',(1820,337)),('5',(1940,302)),('6',(1940,332))]:pin('K501',pn,p)
wire('R502.2','K501.10');ground('K501.1');wire('K501.5','K501.6');dot(pins['K501.5'])
line((1940,302),(1830,337));put(f'<circle cx="1820" cy="267" r="5" fill="white" stroke="{INK}" stroke-width="2"/>');put(f'<circle cx="1820" cy="337" r="5" fill="white" stroke="{INK}" stroke-width="2"/>')
text(1800,257,'10 NO',14);text(1800,326,'1 NC',14);text(1950,286,'5, 6 COM',14)
two('D501',2170,362,True,value='5 V bidir. TVS');wire('K501.5',(2170,302),'D501.1');ground('D501.2',True);dot((2170,302))
port('J503',1,2500,302,'TIP',True);port('J503',2,2500,365,'SLEEVE',True);wire('K501.5','J503.1');ground('J503.2')
port('J501','T',2760,302,'T',True);port('J501','S',2760,365,'S',True);wire('J503.1','J501.T');wire('J503.2','J501.S');group(2440,250,'J503 → J501 · LINE OUT');text(2510,412,'6.3 mm TS · load ≥10k',16,MUTED);tp('TP501','J503.1',2340,277)
# Amp feed.
two('C502',1430,487,value='10u / 50V');wire('U501.7','C502.1');two('R505',1690,487,value='1k');wire('C502.2','R505.1');two('R506',1940,550,True,value='100k');wire('R505.2',(1940,487),'R506.1');dot((1940,487));ground('R506.2')
port('J502',1,2500,487,'SIGNAL',True);port('J502',2,2500,550,'GND',True);wire('R505.2','J502.1');ground('J502.2');group(2440,451,'J502 · INTERNAL AMP');text(2500,600,'Load ≥100k; external amp owns its mute.',16,MUTED)
# Headphone branches physically connected to the two stereo buffers.
wire('U401.1',(700,282),(700,695),(995,695));dot((700,282));wire('U401.7',(735,417),(735,765),(995,765));dot((735,417))
two('C602',1035,695,value='10u / 50V');two('C601',1035,765,value='10u / 50V')
ic('U601','TPA6132A2',1150,625,350,265,[('1','INL−','l',70),('4','INR−','l',140),('2','INL+','l',190),('3','INR+','l',230),('16','OUTL','r',70),('5','OUTR','r',140),('13','EN','r',205),('14','VDD','t',60),('12','HPVDD','b',50),('8','HPVSS','b',125),('11','CPP','b',205),('9','CPN','b',285)])
wire('C602.2','U601.1');wire('C601.2','U601.4');ground('U601.2');ground('U601.3');supply('U601.14')
# Ground-only device pins shown explicitly as a small pin bank alongside the IC.
for i,pn in enumerate(['6','7','10','15','17']):
 x=1620+i*105;port('U601',pn,x,955,{'6':'G0','7':'G1','10':'PGND','15':'SGND','17':'EP'}[pn],True);ground('U601.'+pn)
text(1620,925,'U601 grounded pins · G0/G1 = −6 dB',16,MUTED)
two('R603',1790,695,value='2R2');two('R604',1790,765,value='2R2');wire('U601.16','R603.1');wire('U601.5','R604.1')
for d,x,y,rs in [('D601',2190,830,'R603'),('D602',2320,870,'R604')]:
 two(d,x,y,True,value='5 V TVS');wire(rs+'.2',(x,pt(rs+'.2')[1]),(x,y-32));dot((x,pt(rs+'.2')[1]));ground(d+'.2',True)
for pn,y,label,jp in [(1,695,'LEFT / TIP','T'),(2,765,'RIGHT / RING','R'),(3,835,'GND / SLEEVE','S')]:
 port('J602',pn,2500,y,label,True);port('J601',jp,2760,y,jp,True);wire('J602.'+str(pn),'J601.'+jp)
wire('R603.2','J602.1');wire('R604.2','J602.2');ground('J602.3');group(2440,650,'J602 → J601 · HEADPHONES');text(2500,887,'3.5 mm TRS · ≥32 ohm',16,MUTED);text(2500,912,'≈0.47 Vrms / 6.8 mW at 1 Vrms AUX',16,MUTED);tp('TP601','J602.1',2400,667)
# Pump reservoirs and flying capacitor.
for ref,pn,x in [('C605','12',1090),('C606','8',1275)]:
 two(ref,x,978,True,value='2.2u');wire('U601.'+pn,(pt('U601.'+pn)[0],935),(x,935),ref+'.1');ground(ref+'.2')
two('C607',1415,998,value='2.2u');wire('U601.11',(1355,998),'C607.1');wire('U601.9',(1435,945),(1480,945),(1480,998),'C607.2')
# Headphone enable local host pin group.
port('J101',11,1780,830,'GPIO17',True);two('R601',1960,830,value='1k');wire('J101.11','R601.1');wire('R601.2',(2060,830),(2060,890),(1560,890),(1560,830),'U601.13');two('R602',2090,955,True,value='100k');wire((2060,890),(2090,890),'R602.1');dot((2060,890));ground('R602.2')
# Midrail with explicit wire up to input bias resistors.
group(65,613,'2.5 V AUDIO BIAS')
two('R405',145,685,True,value='10k');supply('R405.1');two('R406',145,805,True,value='10k');wire('R405.2','R406.1');dot((145,745));ground('R406.2')
two('C403',285,805,True,value='10u / 10V');wire((145,745),(285,745),'C403.1');ground('C403.2')
a,o=op('U402','B',405,745);wire((285,745),a);wire(o,(535,752),(535,525),(450,525));dot((535,525));tp('TP401',o,550,790)
text(65,919,'Stereo is buffered before averaging.',17,MUTED);text(65,945,'Mono = (L + R) / 2; antiphase cancels.',17,MUTED);text(65,971,'C401/C402/C502/C601/C602: nonpolar X7R.',17,MUTED)
text(65,997,'C501: Panasonic EEEFK1C470P · 47u / 16V',17,MUTED)
# Divider between audio and utility rows.
line((45,1060),(2950,1060),'#aab7bd',1)
section(45,1100,'02','MIDI input','Floating current loop; optocoupler output is pulled up only to Pi 3.3 V.')
# MIDI input panel / header.
group(50,1172,'J201 · DIN');group(245,1172,'J202')
for pn,jp,y in [(1,'4',1220),(2,'5',1370)]:
 port('J201',jp,110,y,jp);port('J202',pn,290,y,str(pn));wire('J201.'+jp,'J202.'+str(pn))
two('FB201',595,1220,value='600R @100MHz');two('R201',755,1220,value='220R');wire('J202.1','FB201.1');wire('FB201.2','R201.1')
two('FB202',595,1370,value='600R @100MHz');wire('J202.2','FB202.1')
ic('U201','H11L1M',965,1170,200,245,[('1','LED A','l',50),('2','LED K','l',200),('4','OUT','r',90),('6','VCC','t',100),('5','GND','b',100)])
wire('R201.2','U201.1');wire('FB202.2','U201.2');supply('U201.6');ground('U201.5')
two('D201',850,1290,True,value='1N4148W');wire((850,1220),'D201.1');wire('D201.2',(850,1370));dot((850,1220));dot((850,1370))
two('R202',1270,1190,True,value='1k');supply('R202.1');wire('R202.2',(1270,1260),'U201.4');dot((1270,1260));two('R203',1410,1260,value='100R');wire('U201.4','R203.1');port('J101',10,1580,1260,'GPIO15 / UART RX',True);wire('R203.2','J101.10');tp('TP201','R203.2',1500,1220)
two('C201',1430,1380,True,value='100n');supply('C201.1');ground('C201.2')
# Differential + common mode protection clearly drawn as branches at entry.
two('D202',380,1290,True,value='5 V TVS');wire((380,1220),'D202.1');wire('D202.2',(380,1370));dot((380,1220));dot((380,1370))
# Shunts placed beneath, routes down outside main wiring to avoid apparent junctions.
for ref,x,src,y,val in [('D203',110,'J202.1',1465,'24 V TVS'),('C202',260,'J202.1',1465,'100p / 1kV'),('D204',470,'J202.2',1465,'24 V TVS'),('C203',620,'J202.2',1465,'100p / 1kV')]:
 two(ref,x,y,True,value=val)
 sx=315 if src.endswith('1') else 445
 routey=1400 if src.endswith('1') else 1415
 wire(src,(sx,pt(src)[1]),(sx,routey),(x,routey),ref+'.1');dot((sx,pt(src)[1]));ground(ref+'.2',True)
text(795,1485,'DIN 1/2/3: NC. Shell insulated and unconnected.',16,MUTED);text(795,1510,'31,250 baud · 8-N-1 · idle high',16,MUTED)
# Right-side utility column: relay and supplies.
section(1880,1100,'03','Line mute relay','Shown de-energized above: external tip grounded.')
# coil rectangle as distinct unit of K501.
rect(2230,1195,70,65);text(2265,1180,'K501 coil',17,anchor='middle',weight='bold');pin('K501',2,(2265,1170));pin('K501',9,(2265,1285));line((2265,1170),(2265,1195));line((2265,1260),(2265,1285));supply('K501.2')
two('D502',2430,1235,True,value='1N4148W');wire('K501.2',(2430,1170),'D502.1');wire('K501.9',(2430,1285),'D502.2');dot((2265,1285))
ic('Q501',V['Q501'],2205,1330,170,100,[('1','G','l',50),('3','D','t',60),('2','S','b',60)])
wire('K501.9','Q501.3');ground('Q501.2');two('R503',2050,1380,value='1k');port('J101',15,1920,1380,'GPIO22',True);wire('J101.15','R503.1');wire('R503.2','Q501.1');two('R504',2120,1450,True,value='100k');wire((2120,1380),'R504.1');dot((2120,1380));ground('R504.2');text(2460,1360,'AO3400A: RDS(on) ≤48 mΩ',16,MUTED);text(2460,1384,'at VGS = 2.5 V (datasheet conditions).',16,MUTED);text(2460,1430,'GPIO low = mute.',17);text(2460,1454,'GPIO high = line connected.',17)
# Expression lower left.
line((45,1555),(2950,1555),'#aab7bd',1)
section(45,1595,'04','Passive expression pedal','JP301 normal: 1–3 + 2–4. Reversed: 3–5 + 4–6. Both shunts move together.')
for pn,jp,y in [(1,'T',1710),(2,'R',1830),(3,'S',1950)]:
 port('J301',jp,110,y,jp);port('J302',pn,290,y,str(pn));wire('J301.'+jp,'J302.'+str(pn))
group(65,1672,'J301 · TRS');group(240,1672,'J302');ground('J302.3')
# Header rendered as two rows in functional order; physical pin numbers explicit.
rect(465,1680,230,180);group(490,1668,'JP301 · 2 × 3')
for pn,x,y in [('1',490,1710),('3',580,1710),('5',670,1710),('2',490,1830),('4',580,1830),('6',670,1830)]:
 port('JP301',pn,x,y,'');text(x,y+24,pn,14,anchor='middle')
wire('J302.1','JP301.1');wire('J302.2','JP301.2');wire('JP301.1',(430,1710),(430,1880),(715,1880),(715,1830),'JP301.6');wire('JP301.2',(410,1830),(410,1645),(715,1645),(715,1710),'JP301.5')
# Shunts are user-installed, not permanent circuit wires.
line((495,1705),(575,1705),'#946523',5,'9 5');line((495,1825),(575,1825),'#946523',5,'9 5')
text(472,1770,'Dashed: normal shunts',14,'#946523');
for ref,x,src in [('D301',180,'J302.1'),('D302',345,'J302.2')]:
 two(ref,x,1990,True,value='5 V TVS');wire(src,(x,pt(src)[1]),ref+'.1');dot((x,pt(src)[1]));ground(ref+'.2',True)
# Wiper filter.
two('R302',845,1710,value='10k');wire('JP301.3',(580,1685),(745,1685),(745,1710),'R302.1')
two('R303',770,1790,True,value='1M');wire((745,1710),(770,1710),'R303.1');dot((770,1710));ground('R303.2')
two('C301',940,1770,True,value='100n');wire('R302.2',(940,1710),'C301.1');dot((940,1710));ground('C301.2')
ic('U301','ADS1115 / 0x48',1310,1640,295,300,[('4','AIN0','l',70),('5','AIN1','l',200),('8','VDD','t',70),('9','SDA','r',80),('10','SCL','r',160),('1','ADDR','b',35),('3','GND','b',100),('6','AIN2','b',175),('7','AIN3','b',255)])
wire('R302.2','U301.4');supply('U301.8');tp('TP301','U301.4',1260,1680)
# Excitation path / measurement.
two('R301',845,1920,value='1k');supply('R301.1');wire('R301.2',(930,1920),(930,1880),(745,1880),(745,1860),(580,1860),'JP301.4')
two('R304',1000,1840,value='10k');wire((930,1880),(930,1840),'R304.1');dot((930,1880));wire('R304.2','U301.5');two('C302',1080,1920,True,value='100n');wire((1080,1840),'C302.1');dot((1080,1840));ground('C302.2')
# Compact secondary clamp columns for AIN0 and AIN1.
for top,bot,x,y,src in [('D303','D304',1130,1710,'U301.4'),('D305','D306',1235,1840,'U301.5')]:
 two(top,x,y-55,True,value='BAT54H');two(bot,x,y+55,True,value='BAT54H');wire(top+'.2',bot+'.1');dot((x,y));supply(top+'.1');ground(bot+'.2')
for pn in ['1','3','6','7']:ground('U301.'+pn)
for ref,pn,host,y in [('R306','9','3',1720),('R305','10','5',1800)]:
 two(ref,1725,y,value='33R');wire('U301.'+pn,ref+'.1');port('J101',host,1825,y,'SDA' if host=='3' else 'SCL',True);wire(ref+'.2','J101.'+host)
text(1310,2040,'AIN0/AIN1 ratio · PGA ±4.096 V · 128 SPS',16,MUTED)
# Power bank in right lower corner. Host distributed connector pin groups.
section(1930,1595,'05','Power & bypass','Rail symbols connect repeated supplies. Ground ≠ chassis until R101.')
for pn,x,y,net in [('2',1990,1680,'+5V_PI'),('4',2100,1680,'+5V_PI'),('1',2500,1680,'+3V3_PI')]:
 port('J101',pn,x,y,'',True);supply('J101.'+pn)
two('FB101',2210,1680,value='600R');wire('J101.2','J101.4','FB101.1');supply('FB101.2','+5V_A')
two('FB102',2720,1680,value='600R');wire('J101.1','FB102.1');supply('FB102.2','+3V3_ADC')
# Physical supply pins of opamps and bypass devices in explicit rows.
for ref,x in [('U401',2000),('U402',2300),('U501',2600)]:
 port(ref,8,x,1780,'V+',True);supply(ref+'.8');port(ref,4,x+125,1780,'V−',True);ground(ref+'.4');text(x,1730,ref+' supply',16,weight='bold')
# Each decoupling capacitor keeps its own reference and rail; no omitted components.
cap_specs=[('C101','22u / 10V','+5V_A'),('C102','100n','+5V_A'),('C404','100n','+5V_A'),('C405','100n','+5V_A'),('C503','100n','+5V_A'),('C603','2.2u','+5V_A'),('C604','100n','+5V_A'),('C103','10u / 16V','+3V3_ADC'),('C104','100n','+3V3_ADC'),('C303','100n','+3V3_ADC')]
for i,(ref,val,rail) in enumerate(cap_specs):
 x=1980+(i%5)*190;y=1885+(i//5)*130
 two(ref,x,y,True,value=val);supply(ref+'.1',rail);ground(ref+'.2')
two('R307',2875,1910,True,value='2.2k');supply('R307.1');ground('R307.2')
# Chassis bond and supply testpoints in the upper utility gap.
group(1930,1518,'R101 · TVS return → GND → jack sleeves → aluminium enclosure')
two('R101',2800,1505,value='0R');ground('R101.1');ground('R101.2',True)
for ref,net,x in [('TP101','+5V_A',2020),('TP102','+3V3_ADC',2210),('TP103','GND',2400)]:
 pin(ref,1,(x,1030));text(x+25,1035,ref,14,MUTED,anchor='middle')
 if net=='GND':ground(ref+'.1')
 else:supply(ref+'.1',net)
# All ground pins of the distributed Pi connector accounted for.
text(2510,997,'J101 GND: 6, 9, 14, 20, 25, 30, 34, 39',15,MUTED)
for i,pn in enumerate(['6','9','14','20','25','30','34','39']):pin('J101',pn,(2510+i*48,1020))
wire(*['J101.'+p for p in ['6','9','14','20','25','30','34','39']]);ground('J101.39')
# Footnotes include deliberately unused pins, without drawing unused connector bulk.
line((45,2080),(2950,2080),'#aab7bd',1)
text(45,2107,'J101 is ONE 40-pin header shown in functional groups. Unused J101 pins, U201.3, U301.2 and DIN 1/2/3 are NC on this expansion.',15,MUTED)
text(2950,2107,'All 95 PCB components + 4 panel sockets retained. KiCad source is authoritative.',15,MUTED,anchor='end')
# Audit every non-NC physical pin. Power flags are CAD annotations, not parts.
expected={a:n for a,n in N.items() if not a.startswith('#') and not n.startswith('unconnected-')}
missing=set(expected)-set(pins);extra=set(pins)-set(expected)
assert not missing and not extra,{'missing':sorted(missing),'extra':sorted(extra)}
assert all(nets[a]==n for a,n in expected.items())
# Independently infer connected sets from drawn line geometry, including pin-on-wire T joins.
points=junctions | set(pins.values()) | {p for a,b,_ in segs for p in [a,b]}
parent={p:p for p in points}
def find(p):
 while parent[p]!=p:parent[p]=parent[parent[p]];p=parent[p]
 return p
def union(a,b):parent[find(a)]=find(b)
for a,b,_ in segs:
 for p in points:
  if (a[0]==b[0]==p[0] and min(a[1],b[1])<=p[1]<=max(a[1],b[1])) or (a[1]==b[1]==p[1] and min(a[0],b[0])<=p[0]<=max(a[0],b[0])):union(a,p)
clusters={}
for key,p in pins.items():clusters.setdefault(find(p),[]).append(key)
shorts=[ks for ks in clusters.values() if len({nets[k] for k in ks})>1]
assert not shorts,{'drawn_wire_shorts':shorts}
# A signal net should have one drawn connected set; repeated rail symbols may join separate sets.
implicit={'GND','CHASSIS','+5V_A','+5V_PI','+3V3_ADC','+3V3_PI'}
opens={n:[k for k in pins if nets[k]==n] for n in set(nets.values())-implicit if len({find(pins[k]) for k in pins if nets[k]==n})>1}
assert not opens,{'drawn_wire_opens':opens}
svg=f'<svg xmlns="http://www.w3.org/2000/svg" width="841mm" height="594mm" viewBox="0 0 {W} {H}"><g font-family="DejaVu Sans, sans-serif">'+''.join(parts)+'</g></svg>'
(O/'Ardor_IO_connected.svg').write_text(svg)
cairosvg.svg2pdf(bytestring=svg.encode(),write_to=str(O/'Ardor_IO_connected.pdf'))
cairosvg.svg2png(bytestring=svg.encode(),write_to=str(O/'Ardor_IO_connected.png'),output_width=3000,output_height=2120)
(O/'drawing-audit.json').write_text(json.dumps({'represented_components':len({a.split('.')[0] for a in pins}),'represented_non_nc_pins':len(pins),'all_source_non_nc_pins_present':True,'wire_geometry_no_shorts_or_signal_opens':True,'note':'Pin coverage, direct wire endpoints and geometric connectivity checked. Power/ground symbols and J101 functional groups are implicit connections; manual visual review complements the checks.'},indent=2)+'\n')
print('Rendered',len(pins),'pins;',len({a.split('.')[0] for a in pins}),'components')
