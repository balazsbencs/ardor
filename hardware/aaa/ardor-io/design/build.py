from pathlib import Path
import sys,uuid,math,json,csv,copy
import sexpdata as sx
from inspect_lib import get,key
OUT=Path(__file__).resolve().parents[1]
S=sx.Symbol
uid=lambda:str(uuid.uuid4())
q=lambda x:json.dumps(str(x),ensure_ascii=False)
lib={}; bom={}; expected={}
def fetch(libname,name,alias=None):
 alias=alias or name; lid='Ardor:'+alias
 if lid not in lib:
  a=copy.deepcopy(get(libname,name)); a[1]=alias
  for b in a:
   if key(b)=='symbol': b[1]=alias+b[1][len(name):]
  lib[lid]=a
 return lid
R=fetch('Device','R'); C=fetch('Device','C'); CP=fetch('Device','C_Polarized'); TVS=fetch('Device','D_TVS'); D=fetch('Device','D'); SCH=fetch('Device','D_Schottky'); FB=fetch('Device','FerriteBead'); FLAG=fetch('power','PWR_FLAG')
OP=fetch('Amplifier_Operational','LM2904','OPA2320'); HP=fetch('Amplifier_Audio','TPA6132A2RTE'); ADC=fetch('Analog_ADC','ADS1115IDGS'); ISO=fetch('Isolator','H11L1')
for u in lib[HP]:
 if key(u)=='symbol':
  for pin in u:
   if key(pin)=='pin' and next(v[1] for v in pin if key(v)=='number') in ['8','12']:pin[1]=S('power_out')
J40=fetch('Connector_Generic','Conn_02x20_Odd_Even'); J2=fetch('Connector_Generic','Conn_01x02'); J3=fetch('Connector_Generic','Conn_01x03'); J4=fetch('Connector_Generic','Conn_01x04'); J6=fetch('Connector_Generic','Conn_02x03_Odd_Even'); DIN=fetch('Connector','DIN-5_180degree'); JACK=fetch('Connector_Audio','AudioJack3'); JACK2=fetch('Connector_Audio','AudioJack2')
FP_R='Resistor_SMD:R_0603_1608Metric'; FP_C='Capacitor_SMD:C_0603_1608Metric'; FP_C8='Capacitor_SMD:C_0805_2012Metric'; FP_D='Diode_SMD:D_SOD-323'; FP_S='Diode_SMD:D_SOD-123'; FP_SO='Package_SO:SOIC-8_3.9x4.9mm_P1.27mm'
HDR=lambda n:f'Connector_PinHeader_2.54mm:PinHeader_1x{n:02d}_P2.54mm_Vertical'
class Page:
 def __init__(self,name,title,num):
  self.name=name;self.title=title;self.num=num;self.id=uid();self.items=[];self.used=set();self.pins={};self.instances={};self.pin_nets={};self.joints=set();self.stubitems={}
  self.text('ARDOR / CODEC ZERO I/O',20,17,3)
  self.text(title,20,25,2)
  self.text('REV A  |  ENGINEERING PROTOTYPE - VALIDATE BEFORE PRODUCTION',20,32,1.2)
 def text(self,t,x,y,size=1.4):self.items.append(f'(text {q(t)} (at {x} {y} 0) (effects (font (size {size} {size})) (justify left top)) (uuid {q(uid())}))')
 def wire(self,a,b):
  if a!=b:self.items.append(f'(wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) (stroke (width 0) (type default)) (uuid {q(uid())}))')
 def joint(self,p):
  if p not in self.joints:self.items.append(f'(junction (at {p[0]} {p[1]}) (diameter 0) (color 0 0 0 0) (uuid {q(uid())}))');self.joints.add(p)
 def label(self,net,p,ang=0):
  self.items.append(f'(global_label {q(net)} (shape passive) (at {p[0]} {p[1]} {ang}) (effects (font (size 1.1 1.1)) (justify {"right" if ang==180 else "left"})) (uuid {q(uid())}) (property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {p[0]} {p[1]} {ang}) (effects (font (size 1 1)) (hide yes))))')
 def add(self,lid,ref,val,x,y,rot=0,unit=1,fp='',mpn='',ds='',board=True,dnp=False):
  self.used.add(lid); sym=lib[lid]; id=uid(); pins={}
  for b in sym:
   if key(b)=='symbol' and int(b[1].split('_')[-2]) in [0,unit]:
    for p in b:
     if key(p)=='pin':
      at=next(v[1:] for v in p if key(v)=='at'); n=str(next(v[1] for v in p if key(v)=='number'))
      dx,dy,a=map(float,at);r=math.radians(rot);px=round(x+dx*math.cos(r)-dy*math.sin(r),4);py=round(y-dx*math.sin(r)-dy*math.cos(r),4)
      pins[n]=(px,py,(a+rot)%360,str(p[1]))
  self.pins[(ref,unit)]=pins
  # IC values above; passive horizontal values above; vertical beside.
  if lid in [R,C,CP,FB,D,SCH,TVS]:
   if rot in [90,270] or lid in [D,SCH,TVS] and rot==0: rx,ry=x,y-6.5;vx,vy=x,y-3.7
   else: rx,ry=x+4,y-1.8;vx,vy=x+4,y+1.2
  else: rx,ry=x,y-19 if lid==HP else y-14;vx,vy=x,y-16 if lid==HP else y-11
  if lid==OP and unit==3:rx,ry,vx,vy=x+14,y-2,x+14,y+1
  if lid==J40:rx,ry,vx,vy=x+27,y-4,x+27,y
  if ref=='K501':rx,ry,vx,vy=x-20,y-3,x-20,y
  if ref=='Q501':rx,ry,vx,vy=x+15,y-3,x+15,y
  if lid==ISO:rx,ry,vx,vy=x+17,y-13,x+17,y-10
  if lid==ADC:rx,ry,vx,vy=x+18,y-17,x+18,y-14
  if lid==HP:rx,ry,vx,vy=x+16,y-23,x+16,y-20
  if lid==FLAG:rx,ry,vx,vy=x,y,x,y
  props=''
  for pn,pv,px,py,hide in [('Reference',ref,rx,ry,lid==FLAG),('Value',val,vx,vy,lid==FLAG),('Footprint',fp,x,y,True),('Datasheet',ds,x,y,True),('MPN',mpn,x,y,True)]:
   props+=f'(property {q(pn)} {q(pv)} (at {px} {py} {rot}) (effects (font (size 1.15 1.15))'+(' (hide yes)' if hide else (' (justify left)' if lid in [R,C,CP,FB] and rot==0 else ''))+'))'
  paths=f'(instances (project "Ardor_IO" (path {q("/"+root.id+ ("/"+self.sheet_id if hasattr(self,"sheet_id") else ""))} (reference {q(ref)}) (unit {unit}))))'
  self.items.append(f'(symbol (lib_id {q(lid)}) (at {x} {y} {rot}) (unit {unit}) (in_bom {"no" if lid==FLAG else "yes"}) (on_board {"yes" if board else "no"}) (dnp {"yes" if dnp else "no"}) (uuid {q(id)}) {props} '+''.join(f'(pin {q(n)} (uuid {q(uid())}))' for n in pins)+paths+')')
  if lid!=FLAG: bom[ref]={'Reference':ref,'Value':val,'MPN':mpn,'Footprint':fp,'Datasheet':ds,'Assembly':'DNP' if dnp else ('PCB' if board else 'Panel / wired'),'Sheet':self.num}
  return (ref,unit)
 def p(self,o,n):return self.pins[o][str(n)][:2]
 def assign(self,o,n,net):
  self.pin_nets[(o[0],str(n))]=net;expected[(o[0],str(n))]=net
 def net(self,o,n,net,length=5.08):
  px,py,a,typ=self.pins[o][str(n)]
  if net is None:
   self.items.append(f'(no_connect (at {px} {py}) (uuid {q(uid())}))');return
  self.assign(o,n,net)
  first=len(self.items)
  # pin angle points inward; extend outward
  if o[0]=='U601' and str(n) in ['14','12','8']:
   length={'14':15.24,'12':7.62,'8':12.7}[str(n)]
  r=math.radians(a);p=(round(px-length*math.cos(r),4),round(py+length*math.sin(r),4));self.wire((px,py),p)
  self.label(net,p,180 if a==0 else 0)
  self.stubitems[(o[0],str(n))]=list(range(first,len(self.items)))
 def nets(self,o,m):
  for n,v in m.items():self.net(o,n,v)
 def connect(self,o,n,p,net):self.assign(o,n,net);self.wire(self.p(o,n),p)
 def two(self,lid,ref,val,x,y,n1,n2,rot=90,fp=None,mpn='',ds=''):
  fp=fp or (FP_R if lid in [R,FB] else FP_C if lid in [C,CP] else FP_D)
  o=self.add(lid,ref,val,x,y,rot,fp=fp,mpn=mpn,ds=ds);self.nets(o,{'1':n1,'2':n2});return o
 def cap(self,ref,val,x,y,net,gnd='GND',fp=FP_C):return self.two(C,ref,val,x,y,net,gnd,0,fp)
 def flag(self,net,x,y):
  o=self.add(FLAG,f'#FLG{self.num}{len(self.items)}','PWR_FLAG',x,y);self.net(o,'1',net)
 def save(self):
  defs=[]
  for lid in sorted(self.used):
   v=copy.deepcopy(lib[lid]);v[1]=lid;defs.append(sx.dumps(v))
  st=f'(kicad_sch (version 20250114) (generator "eeschema") (uuid {q(self.id)}) (paper "A3") (title_block (title {q(self.title)}) (date "2026-09-05") (rev "A") (company "ARDOR - Codec Zero expansion") (comment 1 "Prototype design; see DESIGN_NOTES.md for release checks")) (lib_symbols '+''.join(defs)+')'+''.join(self.items)
  if self==root:st+='(sheet_instances (path "/" (page "1")))'
  (OUT/(self.name+'.kicad_sch')).write_text(st+')')
root=Page('Ardor_IO','01 / System interface and power',1)
pages=[root]
def child(name,title,num,x,y):
 p=Page(name,title,num);p.sheet_id=uid();pages.append(p)
 root.items.append(f'(sheet (at {x} {y}) (size 65 17.78) (stroke (width 0.1524) (type default)) (fill (color 0 0 0 0.0000)) (uuid {q(p.sheet_id)}) (property "Sheetname" {q(title)} (at {x} {y-1} 0) (effects (font (size 1.2 1.2)) (justify left bottom))) (property "Sheetfile" {q(name+".kicad_sch")} (at {x} {y+19} 0) (effects (font (size 1.1 1.1)) (justify left top))) (instances (project "Ardor_IO" (path {q("/"+root.id)} (page {q(num)})))))')
 return p
m=child('02_MIDI','02 / Isolated MIDI input',2,320,55)
e=child('03_Expression','03 / Expression input',3,320,95)
a=child('04_Audio','04 / Audio distribution',4,320,135)
l=child('05_Line_Amp','05 / Line and amp feed',5,320,175)
h=child('06_Headphones','06 / Stereo headphones',6,320,215)
# Root
root.text('PI HEADER / STACKING INTERFACE',20,43,1.7)
j=root.add(J40,'J101','PI 40-PIN HEADER',63.5,91.44,fp='Connector_PinSocket_2.54mm:PinSocket_2x20_P2.54mm_Vertical',mpn='Samtec SSQ-120-03-G-D (verify stack height)')
mp={str(n):None for n in range(1,41)}
mp.update({'1':'+3V3_PI','2':'+5V_PI','4':'+5V_PI','3':'PI_SDA','5':'PI_SCL','6':'GND','9':'GND','14':'GND','20':'GND','25':'GND','30':'GND','34':'GND','39':'GND','10':'MIDI_RX','11':'HP_ENABLE','15':'LINE_ENABLE'})
root.nets(j,mp)
root.text('GPIO15 / pin 10: UART RX, 31250 baud, 8-N-1\nGPIO17 / pin 11: headphone enable, active high\nGPIO27 / pin 13: reserved for Codec Zero button',20,164,1.3)
# fix selected line enable to GPIO22 physical15
# replacement of net map performed below in generated object net expectations by choosing pin15 instead of13 earlier
root.text('GPIO22 / pin 15: line relay enable, active high\nI2C1: GPIO2/3. ADS1115 address 0x48.\nReserved Codec pins: GPIO18-21 I2S; GPIO23/24 LEDs;\nGPIO27 button; GPIO0/1 HAT EEPROM. No added loads.',20,182,1.3)
root.text('AUX HEADER HARNESS',165,43,1.7)
j=root.add(J3,'J102','CODEC AUX L / GND / R',195.58,63.5,fp=HDR(3))
root.nets(j,{'1':'AUX_L','2':'GND','3':'AUX_R'})
root.text('J102 pin order is OUR harness definition.\nMap to the actual Codec Zero L/R/GND pads.\nNever connect the mono speaker BTL output here.',165,81,1.3)
root.text('FILTERED BRANCHES / 5 V REGULATED INPUT ONLY',165,105,1.7)
root.two(FB,'FB101','600R @100MHz',190.5,121.92,'+5V_PI','+5V_A',mpn='Murata BLM21PG601SN1D',fp='Inductor_SMD:L_0805_2012Metric')
root.cap('C101','22u / 10V X7R',228.6,124.46,'+5V_A',fp=FP_C8)
root.cap('C102','100n / 16V',266.7,124.46,'+5V_A')
root.two(FB,'FB102','600R @100MHz',190.5,157.48,'+3V3_PI','+3V3_ADC',mpn='Murata BLM18AG601SN1D')
root.cap('C103','10u / 10V X7R',228.6,160.02,'+3V3_ADC',fp=FP_C8)
root.cap('C104','100n / 16V',266.7,160.02,'+3V3_ADC')
root.two(R,'R101','0R / chassis bond',195.58,203.2,'GND','CHASSIS',fp='Resistor_SMD:R_1206_3216Metric')
root.text('Bond enclosure at connector entry. Short, wide copper.\nUse one continuous PCB ground plane; place by function.\nDo not route ESD or charge-pump return through ADC/audio.\n5 V source: 4.75-5.25 V; reserve 150 mA beyond Pi/Codec.\nPi provides 3.3 V. No second supply or USB back-feed.',165,222,1.3)
for net,x in [('+5V_PI',30),('+3V3_PI',70),('GND',110),('+5V_A',150),('+3V3_ADC',190),('CHASSIS',230)]:root.flag(net,round(x/1.27)*1.27,260.35)
# MIDI
m.text('5-PIN DIN / FLOATING CURRENT LOOP',20,45,1.7)
j=m.add(DIN,'J201','MIDI IN - PANEL DIN',43.18,76.2,board=False)
m.nets(j,{'4':'MIDI_4','5':'MIDI_5','1':None,'2':None,'3':None})
m.text('Female DIN, 180 degrees. Use contact numbers,\nnot an assumed solder-side drawing.\nPins 1, 2, 3 have NO PCB connection.\nMetal shell bonded to enclosure mechanically.',20,99,1.3)
m.two(FB,'FB201','600R @100MHz',104.14,66.04,'MIDI_4','MIDI_4_F',mpn='Murata BLM18AG601SN1D')
m.two(R,'R201','220R / 1%',149.86,66.04,'MIDI_4_F','MIDI_A')
m.two(FB,'FB202','600R @100MHz',104.14,96.52,'MIDI_5','MIDI_K',mpn='Murata BLM18AG601SN1D')
m.two(D,'D201','1N4148W',157.48,96.52,'MIDI_A','MIDI_K',rot=0,mpn='1N4148W',fp='Diode_SMD:D_SOD-123')
u=m.add(ISO,'U201','H11L1M',226.06,71.12,fp='Package_DIP:DIP-6_W7.62mm',mpn='onsemi H11L1M',ds='https://www.onsemi.com/pdf/datasheet/h11l3m-d.pdf')
m.nets(u,{'1':'MIDI_A','2':'MIDI_K','3':None,'6':'+5V_PI','5':'GND','4':'MIDI_OC'})
m.cap('C201','100n / 16V',279.4,60.96,'+5V_PI')
m.two(R,'R202','1k / 1%',279.4,101.6,'+3V3_PI','MIDI_OC',rot=0)
m.two(R,'R203','100R',330.2,76.2,'MIDI_OC','MIDI_RX')
m.text('MIDI LED on -> UART low. No inverter required.\nOpen collector pulled up ONLY to 3.3 V.\nDisable serial console; enable stable UART RX.\nNever use a 5 V pull-up on Raspberry Pi GPIO.',267,124,1.3)
m.text('CONNECTOR ESD / RF NETWORK',20,153,1.7)
m.two(TVS,'D202','PESD5V0S1BA',66.04,175.26,'MIDI_4','MIDI_5',rot=0,mpn='Nexperia PESD5V0S1BA',ds='https://assets.nexperia.com/documents/data-sheet/PESD5V0S1BA.pdf')
m.cap('C202','100p / 1kV C0G',137.16,175.26,'MIDI_4','CHASSIS',fp='Capacitor_SMD:C_1206_3216Metric')
m.cap('C203','100p / 1kV C0G',205.74,175.26,'MIDI_5','CHASSIS',fp='Capacitor_SMD:C_1206_3216Metric')
m.two(TVS,'D203','PESD24VL1BA',274.32,175.26,'MIDI_4','CHASSIS',rot=0,mpn='Nexperia PESD24VL1BA',ds='https://assets.nexperia.com/documents/data-sheet/PESD24VL1BA.pdf')
m.two(TVS,'D204','PESD24VL1BA',347.98,175.26,'MIDI_5','CHASSIS',rot=0,mpn='Nexperia PESD24VL1BA',ds='https://assets.nexperia.com/documents/data-sheet/PESD24VL1BA.pdf')
m.text('D202 clamps differential transients; D203/D204 divert common-mode ESD to CHASSIS (24 V standoff).\nKeep the MIDI loop copper isolated from logic GND: >= 3 mm clearance around U201 input and nets.\nCommon-mode range is limited by D203/D204. Functional ground-loop isolation; NOT safety isolation.\nNo MIDI-pin connection to logic supply. Place TVS and 100p RF capacitors at DIN harness entry.\nQualify IEC 61000-4-2 on the assembled enclosure, including common-mode strikes and UART recovery.',20,215,1.4)
# Expression
E=e
E.text('PASSIVE TRS PEDAL / TIP-RING SELECT',20,45,1.7)
j=E.add(JACK,'J301','EXPRESSION / 6.3mm TRS',38.1,68.58,board=False);E.nets(j,{'T':'EXP_TIP','R':'EXP_RING','S':'GND'})
j=E.add(J6,'JP301','POLARITY / 2 SHUNTS',114.3,71.12,fp='Connector_PinHeader_2.54mm:PinHeader_2x03_P2.54mm_Vertical')
E.nets(j,{'1':'EXP_TIP','2':'EXP_RING','3':'EXP_WIPER','4':'EXP_EXC','5':'EXP_RING','6':'EXP_TIP'})
E.text('Normal: shunts 1-3 and 2-4 (tip=wiper).\nReverse: shunts 3-5 and 4-6 (ring=wiper).\nFit BOTH shunts in the same position.\nSleeve = ground. 10k-100k linear pot.',20,99,1.3)
E.two(R,'R301','1k / 1%',175.26,63.5,'+3V3_ADC','EXP_EXC')
E.two(R,'R302','10k / 1%',175.26,93.98,'EXP_WIPER','EXP_ADC')
E.two(R,'R303','1M',220.98,93.98,'EXP_WIPER','GND',rot=0)
E.cap('C301','100n / 16V',264.16,93.98,'EXP_ADC')
E.two(SCH,'D303','BAT54H',312.42,68.58,'+3V3_ADC','EXP_ADC',rot=0,mpn='Nexperia BAT54H,115',fp='Diode_SMD:D_SOD-123F')
E.two(SCH,'D304','BAT54H',312.42,99.06,'EXP_ADC','GND',rot=0,mpn='Nexperia BAT54H,115',fp='Diode_SMD:D_SOD-123F')
E.two(TVS,'D301','PESD5V0S1BA',55.88,157.48,'EXP_TIP','CHASSIS',rot=0,mpn='Nexperia PESD5V0S1BA')
E.two(TVS,'D302','PESD5V0S1BA',119.38,157.48,'EXP_RING','CHASSIS',rot=0,mpn='Nexperia PESD5V0S1BA')
E.text('TVS at jack entry; R302 then clamp pair at ADC.\nR301 limits insertion/short current to <=3.5 mA.\nPassive pedals only; no powered CV source.\nCalibrate endpoints to remove pot loading error.',20,184,1.3)
u=E.add(ADC,'U301','ADS1115IDGS',256.54,172.72,fp='Package_SO:VSSOP-10_3x3mm_P0.5mm',mpn='TI ADS1115IDGSR',ds='https://www.ti.com/lit/ds/symlink/ads1115.pdf')
E.nets(u,{'1':'GND','2':None,'3':'GND','4':'EXP_ADC','5':'EXP_REF_ADC','6':'GND','7':'GND','8':'+3V3_ADC','9':'ADC_SDA','10':'ADC_SCL'})
E.two(R,'R304','10k / 1%',175.26,231.14,'EXP_EXC','EXP_REF_ADC')
E.cap('C302','100n / 16V',220.98,231.14,'EXP_REF_ADC')
E.cap('C303','100n / 16V',264.16,231.14,'+3V3_ADC')
E.two(R,'R305','33R',330.2,162.56,'ADC_SCL','PI_SCL')
E.two(R,'R306','33R',330.2,193.04,'ADC_SDA','PI_SDA')
E.two(R,'R307','2.2k / rail bleed',330.2,231.14,'+3V3_ADC','GND',rot=0)
E.two(SCH,'D305','BAT54H',60.96,231.14,'+3V3_ADC','EXP_REF_ADC',rot=0,mpn='Nexperia BAT54H,115',fp='Diode_SMD:D_SOD-123F')
E.two(SCH,'D306','BAT54H',119.38,231.14,'EXP_REF_ADC','GND',rot=0,mpn='Nexperia BAT54H,115',fp='Diode_SMD:D_SOD-123F')
E.text('I2C address 0x48; internal reference; PGA +/-4.096 V; 128 SPS. Read AIN0 and AIN1 single-ended.\nUse AIN0 / AIN1 ratio, endpoint calibration, smoothing and deadband. No added I2C pull-ups (Pi has them).',20,258,1.35)
# Audio buffers
A=a; A.text('AC-COUPLED STEREO INPUT / UNITY BUFFERS',20,45,1.7)
# buffer helper wired loop and input chain

def buffer(pg,ref,unit,x,y,inputnet,outnet):
 u=pg.add(OP,ref,'OPA2320AIDR',x,y,unit=unit,fp=FP_SO,mpn='TI OPA2320AIDR',ds='https://www.ti.com/lit/ds/symlink/opa2320.pdf')
 pn,nn,on=('3','2','1') if unit==1 else ('5','6','7')
 pg.net(u,pn,inputnet)
 po=pg.p(u,on);pi=pg.p(u,nn);p1=(po[0]+7.62,po[1]);p2=(p1[0],y+10.16);p3=(pi[0]-5.08,p2[1]);p4=(p3[0],pi[1])
 for aa,bb in [(po,p1),(p1,p2),(p2,p3),(p3,p4),(p4,pi)]:pg.wire(aa,bb)
 pg.label(outnet,p1);pg.assign(u,on,outnet);pg.assign(u,nn,outnet)
 return u
for i,(yy,nn) in enumerate([(73.66,'L'),(124.46,'R')]):
 A.two(C,f'C40{1+i}', '2.2u / 63V film',66.04,yy,'AUX_'+nn,'BIAS_'+nn,fp='Capacitor_THT:C_Rect_L7.2mm_W7.2mm_P5.00mm_FKS2_FKP2_MKS2_MKP2',mpn='WIMA MKS2C042201K00KSSD')
 A.two(R,f'R40{1+i}','100k / 1%',114.3,yy+20.32,'BIAS_'+nn,'VREF',rot=0)
 buffer(A,'U401',i+1,177.8,yy,'BIAS_'+nn,'BUF_'+nn)
 A.two(R,f'R40{3+i}','10k / 0.1%',261.62,yy,'BUF_'+nn,'MONO_MIX')
buffer(A,'U402',1,340.36,101.6,'MONO_MIX','MONO_BUF')
A.text('Mono = (L + R) / 2; equal 10k resistors prevent channel contention.\nDo not tie AUX L and R together. Software can also produce dual mono.\nBUF_L, BUF_R and MONO_BUF carry VREF DC bias; AC-couple every external load.',210,150,1.3)
A.text('MIDRAIL / LOCAL BYPASS',20,180,1.7)
A.two(R,'R405','10k / 1%',48.26,208.28,'+5V_A','VREF_DIV',rot=0)
A.two(R,'R406','10k / 1%',96.52,208.28,'VREF_DIV','GND',rot=0)
A.cap('C403','10u / 10V X7R',144.78,208.28,'VREF_DIV',fp=FP_C8)
buffer(A,'U402',2,220.98,208.28,'VREF_DIV','VREF')
for ref,x in [('U401',292.1),('U402',350.52)]:
 u=A.add(OP,ref,'OPA2320AIDR',x,205.74,unit=3,fp=FP_SO,mpn='TI OPA2320AIDR',ds='https://www.ti.com/lit/ds/symlink/opa2320.pdf');A.nets(u,{'8':'+5V_A','4':'GND'})
A.cap('C404','100n / 16V',292.1,238.76,'+5V_A');A.cap('C405','100n / 16V',350.52,238.76,'+5V_A')
A.text('Nominal audio target <=1.0 Vrms per AUX channel. Unity gain throughout.\nOPA2320: RRIO on 5 V, unity stable; 2.5 V bias permits bipolar audio swing.\n2.2u/100k input pole ~0.72 Hz. Keep VREF local; no large capacitor directly on buffer output.',20,252,1.3)
# line and amp feed
L=l;L.text('MONO LINE / DC BLOCK / STARTUP RELAY MUTE',20,45,1.7)
buffer(L,'U501',1,50.8,71.12,'MONO_BUF','LINE_BUF')
L.two(CP,'C501','47u / 16V',114.3,71.12,'LINE_BUF','LINE_AC',fp='Capacitor_SMD:CP_Elec_6.3x5.4',mpn='Panasonic EEE-FK1C470R')
L.two(R,'R501','10k',162.56,96.52,'LINE_AC','GND',rot=0)
L.two(R,'R502','100R / 1%',203.2,71.12,'LINE_AC','LINE_DRIVE')
# relay using custom imported signal relay
REL=fetch('Relay','G5V-1');print('relay pins',[(b[1],[(next(v[1] for v in p if key(v)=='number'),next(v[1:] for v in p if key(v)=='at')) for p in b if key(p)=='pin']) for b in lib[REL] if key(b)=='symbol'])
k=L.add(REL,'K501','G5V-1 DC5',269.24,76.2,fp='Relay_THT:Relay_SPDT_Omron_G5V-1',mpn='Omron G5V-1 DC5')
# datasheet/library confirms: 2/9 coil, 5 common, 1 NC, 10 NO (checked after generation)
L.nets(k,{'2':'+5V_PI','9':'RELAY_LOW','5':'LINE_JACK','6':'LINE_JACK','1':'GND','10':'LINE_DRIVE'})
j=L.add(JACK2,'J501','LINE OUT / 6.3mm TS',370.84,71.12,board=False);L.nets(j,{'T':'LINE_JACK','S':'GND'})
L.two(TVS,'D501','PESD5V0S1BA',325.12,121.92,'LINE_JACK','CHASSIS',rot=0,mpn='Nexperia PESD5V0S1BA')
L.text('Relay de-energized: jack tip grounded, source disconnected.\nEnergized: buffered mono reaches tip. Load >=10k.\nC501 positive faces LINE_BUF; resistor outside feedback.\nNo balanced / +4 dBu / phantom-power capability specified.',20,123,1.3)
NM=fetch('Transistor_FET','2N7002')
u=L.add(NM,'Q501','2N7002',220.98,165.1,fp='Package_TO_SOT_SMD:SOT-23',mpn='Nexperia 2N7002');L.nets(u,{'1':'RELAY_GATE','2':'GND','3':'RELAY_LOW'})
L.two(R,'R503','1k',134.62,162.56,'LINE_ENABLE','RELAY_GATE')
L.two(R,'R504','100k',175.26,193.04,'RELAY_GATE','GND',rot=0)
L.two(D,'D502','1N4148W',279.4,162.56,'+5V_PI','RELAY_LOW',rot=0,fp=FP_S,mpn='1N4148W')
L.text('AMP CIRCUIT PLACEHOLDER / INTERNAL HARNESS',20,222,1.7)
buffer(L,'U501',2,50.8,243.84,'MONO_BUF','AMP_BUF')
L.two(C,'C502','2.2u / 63V film',119.38,243.84,'AMP_BUF','AMP_AC',fp='Capacitor_THT:C_Rect_L7.2mm_W7.2mm_P5.00mm_FKS2_FKP2_MKS2_MKP2',mpn='WIMA MKS2C042201K00KSSD')
L.two(R,'R505','1k / isolation',185.42,243.84,'AMP_AC','AMP_FEED')
L.two(R,'R506','100k',243.84,243.84,'AMP_FEED','GND',rot=0)
j=L.add(J2,'J502','TO YOUR AMP CIRCUIT',307.34,243.84,fp=HDR(2));L.nets(j,{'1':'AMP_FEED','2':'GND'})
L.text('Amp input >=100k. Add your gain, output connector,\nexternal-port protection and mute in that project.\nJ502 is internal, NOT a speaker / amp output jack.',315,205,1.2)
u=L.add(OP,'U501','OPA2320AIDR',340.36,175.26,unit=3,fp=FP_SO,mpn='TI OPA2320AIDR',ds='https://www.ti.com/lit/ds/symlink/opa2320.pdf');L.nets(u,{'8':'+5V_A','4':'GND'})
L.cap('C503','100n / 16V',383.54,175.26,'+5V_A')
# HP
H=h;H.text('STEREO DIRECTPATH HEADPHONE DRIVER / -6 dB',20,45,1.7)
H.two(C,'C601','2.2u / 63V film',68.58,66.04,'BUF_R','HP_IN_R',fp='Capacitor_THT:C_Rect_L7.2mm_W7.2mm_P5.00mm_FKS2_FKP2_MKS2_MKP2',mpn='WIMA MKS2C042201K00KSSD')
H.two(C,'C602','2.2u / 63V film',68.58,101.6,'BUF_L','HP_IN_L',fp='Capacitor_THT:C_Rect_L7.2mm_W7.2mm_P5.00mm_FKS2_FKP2_MKS2_MKP2',mpn='WIMA MKS2C042201K00KSSD')
u=H.add(HP,'U601','TPA6132A2RTER',195.58,91.44,fp='Package_DFN_QFN:WQFN-16-1EP_3x3mm_P0.5mm_EP1.6x1.6mm_ThermalVias',mpn='TI TPA6132A2RTER',ds='https://www.ti.com/lit/ds/symlink/tpa6132a2.pdf')
H.nets(u,{'1':'HP_IN_L','2':'GND','3':'GND','4':'HP_IN_R','5':'HP_R_RAW','6':'GND','7':'GND','8':'HPVSS','9':'CPN','11':'CPP','12':'HPVDD','13':'HP_EN','14':'+5V_A','16':'HP_L_RAW','17':'GND'})
for pn in ['10','15']:
 H.assign(u,pn,'GND');pp=H.p(u,pn);H.wire(pp,(pp[0],111.76));H.wire((pp[0],111.76),(198.12,111.76))
H.joint((198.12,111.76))
H.two(R,'R601','1k',66.04,137.16,'HP_ENABLE','HP_EN');H.two(R,'R602','100k',132.08,137.16,'HP_EN','GND',rot=0)
H.two(R,'R603','2R2 / 1%',271.78,71.12,'HP_L_RAW','HP_L')
H.two(R,'R604','2R2 / 1%',271.78,104.14,'HP_R_RAW','HP_R')
j=H.add(JACK,'J601','HEADPHONES / 3.5mm TRS',355.6,88.9,board=False);H.nets(j,{'T':'HP_L','R':'HP_R','S':'GND'})
H.two(TVS,'D601','PESD5V0S1BA',302.26,147.32,'HP_L','CHASSIS',rot=0,mpn='Nexperia PESD5V0S1BA')
H.two(TVS,'D602','PESD5V0S1BA',365.76,147.32,'HP_R','CHASSIS',rot=0,mpn='Nexperia PESD5V0S1BA')
H.text('CHARGE PUMP / BYPASS - FOLLOW TI LAYOUT',20,187,1.7)
H.cap('C603','2.2u / 10V X7R',50.8,213.36,'+5V_A',fp=FP_C8)
H.cap('C604','100n / 16V',109.22,213.36,'+5V_A')
H.cap('C605','2.2u / 10V X7R',167.64,213.36,'HPVDD',fp=FP_C8)
H.cap('C606','2.2u / 10V X7R',226.06,213.36,'HPVSS',fp=FP_C8)
H.two(C,'C607','2.2u / 10V X7R',312.42,213.36,'CPP','CPN',fp=FP_C8)
H.text('HPVDD is an INTERNAL regulator output. Never connect it to +5V_A. HPVSS is negative; use nonpolar ceramic.\nG0=0, G1=0 gives -6 dB inverting gain. INL+ / INR+ grounded per TI single-ended application.\n32 ohm or higher stereo headphones. 1 Vrms AUX gives ~0.47 Vrms at 32 ohm (~6.8 mW); start volume low.\nKeep EN low until codec routing is stable; ramp volume. Short, separate SGND jack return to local ground plane.\nPlace pump capacitors at pins; keep switching loops away from AUX, VREF and expression input traces.',20,250,1.3)
# Board-side harness connectors for the off-board panel sockets.
for pg,ref,val,libid,x,y,ns in [(m,'J202','TO MIDI PANEL / 4,5',J2,220.98,132.08,{'1':'MIDI_4','2':'MIDI_5'}),(e,'J302','TO EXP PANEL / T,R,S',J3,60.96,132.08,{'1':'EXP_TIP','2':'EXP_RING','3':'GND'}),(l,'J503','TO LINE PANEL / T,S',J2,368.3,139.7,{'1':'LINE_JACK','2':'GND'}),(h,'J602','TO HP PANEL / T,R,S',J3,345.44,180.34,{'1':'HP_L','2':'HP_R','3':'GND'})]:
 jj=pg.add(libid,ref,val,x,y,fp=HDR(len(ns)));pg.nets(jj,ns)
TP=fetch('Connector','TestPoint')
for pg,ref,net,x,y in [(root,'TP101','+5V_A',30.48,228.6),(root,'TP102','+3V3_ADC',81.28,228.6),(root,'TP103','GND',132.08,228.6),(m,'TP201','MIDI_RX',353.06,50.8),(e,'TP301','EXP_ADC',373.38,76.2),(a,'TP401','VREF',48.26,152.4),(a,'TP402','MONO_BUF',180.34,152.4),(l,'TP501','LINE_JACK',345.44,96.52),(h,'TP601','HP_L',345.44,53.34)]:
 t=pg.add(TP,ref,net,x,y,fp='TestPoint:TestPoint_Pad_D1.5mm');pg.net(t,'1',net)
# Route the main audio signal paths conventionally; labels remain for sheet crossings.
def unhook(pg,ref,pin):
 for idx in pg.stubitems.get((ref,str(pin)),[]):pg.items[idx]=''
def path(pg,*pts):
 for aa,bb in zip(pts,pts[1:]):pg.wire(aa,bb)
def erase_labels(pg,net):
 for ix,item in enumerate(pg.items):
  if item.startswith('(global_label '+q(net)+' '):pg.items[ix]=''
for i,(yy,nn) in enumerate([(73.66,'L'),(124.46,'R')]):
 c='C40'+str(i+1);r='R40'+str(i+1);rin='R40'+str(i+3);pn='3' if i==0 else '5'
 for ref,pin in [(c,'2'),(r,'1'),('U401',pn)]:unhook(A,ref,pin)
 cp=A.p((c,1),'2');rp=A.p((r,1),'1');ip=A.p(('U401',i+1),pn)
 path(A,cp,(149.86,yy),(149.86,ip[1]),ip)
 path(A,(114.3,yy),rp);A.joint((114.3,yy));A.label('BIAS_'+nn,(134.62,yy))
 unhook(A,rin,'1');erase_labels(A,'BUF_'+nn)
 path(A,(193.04,yy),A.p((rin,1),'1'));A.label('BUF_'+nn,(223.52,yy));A.joint((193.04,yy))
# line and amp coupling path
for cref,rref,yy,buf,unit in [('C501','R502',71.12,'LINE_BUF',1),('C502','R505',243.84,'AMP_BUF',2)]:
 unhook(L,cref,'1');erase_labels(L,buf)
 cp=L.p((cref,1),'1');path(L,(66.04,yy),cp);L.label(buf,(86.36,yy));L.joint((66.04,yy))
 unhook(L,cref,'2');unhook(L,rref,'1')
 path(L,L.p((cref,1),'2'),L.p((rref,1),'1'))
 ac='LINE_AC' if unit==1 else 'AMP_AC';L.label(ac,(144.78,yy))
unhook(L,'R501','1');path(L,(162.56,71.12),L.p(('R501',1),'1'));L.joint((162.56,71.12))
# line driver to relay NO (vertical top pin)
unhook(L,'R502','2');unhook(L,'K501','10')
path(L,L.p(('R502',1),'2'),(246.38,71.12),(246.38,55.88),(276.86,55.88),L.p(('K501',1),'10'))
L.label('LINE_DRIVE',(246.38,55.88))
# amp output and pulldown
for ref,pin in [('R505','2'),('R506','1'),('J502','1')]:unhook(L,ref,pin)
# move R506 below horizontal signal to avoid drawing through its body
# route output on a track above the pulldown, then down to the header.
path(L,L.p(('R505',1),'2'),(210.82,243.84),(210.82,228.6),(281.94,228.6),(281.94,243.84),L.p(('J502',1),'1'))
path(L,(243.84,228.6),L.p(('R506',1),'1'));L.joint((243.84,228.6));L.label('AMP_FEED',(266.7,228.6))
# Headphone coupling capacitors routed into the driver.
for cref,pn,net,xx in [('C601','4','HP_IN_R',121.92),('C602','1','HP_IN_L',134.62)]:
 unhook(H,cref,'2');unhook(H,'U601',pn)
 p1=H.p((cref,1),'2');p2=H.p(('U601',1),pn)
 path(H,p1,(xx,p1[1]),(xx,p2[1]),p2);H.label(net,(154.94,p2[1]))
# KiCad requires explicit wire segments at every T-junction and label anchor.
for pg in pages:
 wires=[]; anchors=set(); keep=[]
 for it in pg.items:
  if not it:continue
  obj=sx.loads(it);k=key(obj)
  if k=='wire':
   pts=next(v for v in obj if key(v)=='pts');p1=tuple(pts[1][1:]);p2=tuple(pts[2][1:]);wires.append((p1,p2));anchors.update([p1,p2])
  else:
   keep.append(it)
   if k in ['global_label','junction','no_connect']:
    at=next(v for v in obj if key(v)=='at');anchors.add(tuple(at[1:3]))
 for pins in pg.pins.values():
  for x,y,*_ in pins.values():anchors.add((x,y))
 pg.items=keep;seen=set()
 for p1,p2 in wires:
  if p1[0]==p2[0]:points=sorted([z for z in anchors if abs(z[0]-p1[0])<1e-6 and min(p1[1],p2[1])-1e-6<=z[1]<=max(p1[1],p2[1])+1e-6],key=lambda z:z[1])
  elif p1[1]==p2[1]:points=sorted([z for z in anchors if abs(z[1]-p1[1])<1e-6 and min(p1[0],p2[0])-1e-6<=z[0]<=max(p1[0],p2[0])+1e-6],key=lambda z:z[0])
  else:raise ValueError(('Nonorthogonal wire',pg.name,p1,p2))
  for p1,p2 in zip(points,points[1:]):
   if (p1,p2) not in seen:pg.wire(p1,p2);seen.add((p1,p2))
# Local labels for nets wholly routed on one sheet; no dangling global labels.
from collections import Counter
counts=Counter()
for pg in pages:
 for it in pg.items:
  if it.startswith('(global_label '):counts[sx.loads(it)[1]]+=1
for pg in pages:
 for ix,it in enumerate(pg.items):
  if it.startswith('(global_label '):
   ob=sx.loads(it)
   if counts[ob[1]]==1:
    pg.items[ix]=sx.dumps([S('label'),ob[1]]+[v for v in ob[2:] if key(v) not in ['shape','property']])
for p in pages:p.save()
(OUT/'Ardor.kicad_sym').write_text('(kicad_symbol_lib (version 20241209) (generator "kicad_symbol_editor")'+''.join(sx.dumps(v) for v in lib.values())+')')
(OUT/'sym-lib-table').write_text('(sym_lib_table (version 7) (lib (name "Ardor")(type "KiCad")(uri "${KIPRJMOD}/Ardor.kicad_sym")(options "")(descr "Embedded verified symbols")))')
(OUT/'Ardor_IO.kicad_pro').write_text(json.dumps({'meta':{'filename':'Ardor_IO.kicad_pro','version':1},'net_settings':{'classes':[{'name':'Default','clearance':0.2,'track_width':0.25,'via_diameter':0.6,'via_drill':0.3,'microvia_diameter':0.3,'microvia_drill':0.1,'diff_pair_width':0.2,'diff_pair_gap':0.25,'diff_pair_via_gap':0.25}]}}))
with (OUT/'BOM.csv').open('w') as f:
 w=csv.DictWriter(f,fieldnames=list(next(iter(bom.values()))));w.writeheader();w.writerows(bom.values())
(OUT/'design/expected_nets.json').write_text(json.dumps({r+'.'+n:v for (r,n),v in expected.items()},indent=2))
print('Saved',len(pages),'sheets,',len(bom),'components')
