p='ardor-io/design/build.py';s=open(p).read()
s=s.replace('(at {px} {py} 0) (effects (font (size 1.15 1.15))','(at {px} {py} {rot}) (effects (font (size 1.15 1.15))')
s=s.replace('C_Rect_L7.2mm_W5.0mm_P5.00mm','C_Rect_L7.2mm_W5.5mm_P5.00mm_FKS2_FKP2_MKS2_MKP2').replace('Relay_SPDT_Omron-G5V-1','Relay_SPDT_Omron_G5V-1')
s=s.replace('root.flag(net,x,260)','root.flag(net,round(x/1.27)*1.27,260.35)')
# correct generated supply electrical pin types in project library
pos=s.index("J40=fetch")
s=s[:pos]+'''for u in lib[HP]:
 if key(u)=='symbol':
  for pin in u:
   if key(pin)=='pin' and next(v[1] for v in pin if key(v)=='number') in ['8','12']:pin[1]=S('power_out')
'''+s[pos:]
# IC vertical labels fan-out; join adjacent ground pins with one local bus for HP
s=s.replace("r=math.radians(a);p=(round(px-length*math.cos(r),4),round(py+length*math.sin(r),4));self.wire((px,py),p)","""if o[0]=='U601' and str(n) in ['14','12','8']:
   length={'14':15.24,'12':7.62,'8':12.7}[str(n)]
  r=math.radians(a);p=(round(px-length*math.cos(r),4),round(py+length*math.sin(r),4));self.wire((px,py),p)""")
# refs for HP move away from top power nets
s=s.replace("if lid==FLAG:rx,ry,vx,vy=x,y,x,y","if lid==HP:rx,ry,vx,vy=x+16,y-23,x+16,y-20\n  if lid==FLAG:rx,ry,vx,vy=x,y,x,y")
# relay hidden duplicate6 connects5 internally; explicit assertion
s=s.replace("'5':'LINE_JACK','1'","'5':'LINE_JACK','6':'LINE_JACK','1'")
# line page rearrangement
s=s.replace("L.two(CP,'C501','47u / 16V',63.5,71.12,'MONO_BUF'","buffer(L,'U501',1,50.8,71.12,'MONO_BUF','LINE_BUF')\nL.two(CP,'C501','47u / 16V',114.3,71.12,'LINE_BUF'")
s=s.replace("'R501','100k',109.22,91.44","'R501','10k',162.56,96.52").replace("'R502','100R / 1%',144.78,71.12","'R502','100R / 1%',203.2,71.12").replace("226.06,76.2,fp='Relay", "269.24,76.2,fp='Relay").replace("340.36,71.12,board=False", "370.84,71.12,board=False")
s=s.replace('C501 positive faces MONO_BUF','C501 positive faces LINE_BUF')
s=s.replace("L.two(CP,'C502','47u / 16V',66.04,246.38,'MONO_BUF','AMP_AC',fp='Capacitor_SMD:CP_Elec_6.3x5.4',mpn='Panasonic EEE-FK1C470R')","buffer(L,'U501',2,50.8,243.84,'MONO_BUF','AMP_BUF')\nL.two(C,'C502','2.2u / 50V film',119.38,243.84,'AMP_BUF','AMP_AC',fp='Capacitor_THT:C_Rect_L7.2mm_W5.5mm_P5.00mm_FKS2_FKP2_MKS2_MKP2',mpn='WIMA MKS2B042201F00KSSD')")
s=s.replace("139.7,246.38,'AMP_AC'","185.42,243.84,'AMP_AC'").replace("210.82,246.38,'AMP_FEED'","243.84,243.84,'AMP_FEED'").replace("292.1,246.38,fp=HDR(2)","307.34,243.84,fp=HDR(2)").replace("320,232,1.2","330,252,1.2")
s=s.replace('# HP\n',"""u=L.add(OP,'U501','OPA2320AIDR',340.36,175.26,unit=3,fp=FP_SO,mpn='TI OPA2320AIDR',ds='https://www.ti.com/lit/ds/symlink/opa2320.pdf');L.nets(u,{'8':'+5V_A','4':'GND'})
L.cap('C503','100n / 16V',383.54,175.26,'+5V_A')
# HP
""")
# expression reference input also clamps for cable ESD surviving main primary TVS
s=s.replace("E.text('I2C address", "E.two(SCH,'D305','BAT54H',60.96,231.14,'+3V3_ADC','EXP_REF_ADC',rot=0,mpn='Nexperia BAT54H',fp=FP_S)\nE.two(SCH,'D306','BAT54H',119.38,231.14,'EXP_REF_ADC','GND',rot=0,mpn='Nexperia BAT54H',fp=FP_S)\nE.text('I2C address")
# remove merged duplicate ground stubs at headphone IC: join ground pins at bus then one label
needle="H.nets(u,{'1'";start=s.index(needle);end=s.index('\n',start)
line=s[start:end].replace("'10':'GND',",'').replace("'15':'GND',",'').replace("'17':'GND'", "'17':'GND'")
s=s[:start]+line+"\nfor pn in ['10','15']:\n H.assign(u,pn,'GND');pp=H.p(u,pn);H.wire(pp,(pp[0],111.76));H.wire((pp[0],111.76),(198.12,111.76))\nH.joint((198.12,111.76))"+s[end:]
open(p,'w').write(s)
