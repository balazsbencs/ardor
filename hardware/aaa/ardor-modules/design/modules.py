"""Circuit, schematic wiring and compact placement specifications for M1.

Each entry is a complete circuit. There are no inter-project sheet references,
shared rails, exported midrails or mandatory carrier/inter-module connectors.
PCB coordinates are in mm relative to the outline's upper-left corner; SMT
positions describe the centre of the pad bounding box, header positions likewise.
"""
from cad import Module

def filter_power(m, voltage, sch=(60.96,220.98), pcb=(10,4)):
    x,y=sch;px,py=pcb; raw='+5V' if voltage==5 else '+3V3';rail=raw+'_A'
    m.two('FerriteBead','FB101',raw,rail,(x,y),(px,py),proto='FB101' if voltage==5 else 'FB102')
    m.two('C','C101',rail,'GND',(x+25.4,y),(px+4,py),proto='C101' if voltage==5 else 'C103',rot=0)
    m.two('C','C102',rail,'GND',(x+50.8,y),(px+8,py),proto='C102',rot=0)
    m.link(rail,('FB101','2'),('C101','1'))
    m.link(rail,('C101','1'),('C102','1'))
    m.label('FB101','2',rail)
    return rail

def bond(m, sch, pcb):
    m.two('R','R101','CHASSIS','GND',sch,pcb,proto='R101')

def midi():
    m=Module('midi-in','Ardor_MIDI','Isolated MIDI input / 3.3 V UART', (34,28),
      'Standalone DIN MIDI receiver. Supply 5 V and 3.3 V from the host. UART: 31250 baud, 8-N-1. No other module required.')
    m.conn('J202',['MIDI_4','MIDI_5'],(30.48,76.2),(3,12),rot=180)
    m.conn('J101',['+5V','+3V3','GND','MIDI_RX'],(350.52,76.2),(30,13))
    m.conn('J203',['CHASSIS','GND'],(350.52,160.02),(31,24))
    m.two('FerriteBead','FB201','MIDI_4','MIDI_4_F',(76.2,63.5),(7,10))
    m.two('R','R201','MIDI_4_F','MIDI_A',(111.76,63.5),(11,10))
    m.two('FerriteBead','FB202','MIDI_5','MIDI_K',(76.2,101.6),(7,15))
    m.add('H11L1','U201',{'1':'MIDI_A','2':'MIDI_K','3':None,'4':'MIDI_OC','5':'GND','6':'+5V'},(172.72,81.28),(20,12),value='onsemi H11L1M',fp='Package_DIP:DIP-6_W7.62mm')
    m.two('D','D201','MIDI_A','MIDI_K',(139.7,81.28),(12,15),rot=270)
    m.two('C','C201','+5V','GND',(223.52,60.96),(26.5,6),rot=0)
    m.two('C','C204','+3V3','GND',(276.86,60.96),(27,4),proto='C201',rot=0)
    m.two('R','R202','+3V3','MIDI_OC',(223.52,101.6),(26,17),rot=0)
    m.two('R','R203','MIDI_OC','MIDI_RX',(289.56,81.28),(28,20))
    m.two('D_TVS','D202','MIDI_4','MIDI_5',(55.88,160.02),(4,8),rot=90)
    m.two('C','C202','MIDI_4','CHASSIS',(91.44,160.02),(9,5),rot=0)
    m.two('C','C203','MIDI_5','CHASSIS',(129.54,160.02),(9,21),rot=0)
    m.two('D_TVS','D203','MIDI_4','CHASSIS',(175.26,160.02),(5,4),rot=90)
    m.two('D_TVS','D204','MIDI_5','CHASSIS',(218.44,160.02),(7.5,24),rot=90)
    bond(m,(302.26,160.02),(26,24))
    m.link('MIDI_4',('J202','1'),('FB201','1'),[(50.8,m.p('J202','1')[1]),(50.8,63.5)])
    m.link('MIDI_4_F',('FB201','2'),('R201','1'))
    m.link('MIDI_A',('R201','2'),('U201','1'),[(157.48,63.5),(157.48,78.74)])
    m.link('MIDI_5',('J202','2'),('FB202','1'),[(43.18,m.p('J202','2')[1]),(43.18,101.6)])
    m.link('MIDI_K',('FB202','2'),('U201','2'),[(160.02,101.6),(160.02,83.82)])
    m.link('MIDI_A',('D201','1'),(139.7,63.5));m.joint(139.7,63.5)
    m.link('MIDI_K',('D201','2'),(139.7,101.6));m.joint(139.7,101.6)
    m.label('FB201','1','MIDI_4');m.label('FB202','1','MIDI_5')
    m.link('MIDI_OC',('U201','4'),('R203','1'))
    m.link('MIDI_OC',('R202','2'),(241.3,81.28),[(241.3,105.41)]);m.joint(241.3,81.28)
    m.link('MIDI_RX',('R203','2'),('J101','4'),[(330.2,81.28)])
    # Protection branches use explicitly named loop rails, separate from logic.
    for ref,pin,net in [('D202','1','MIDI_4'),('D202','2','MIDI_5'),('C202','1','MIDI_4'),('C203','1','MIDI_5'),('D203','1','MIDI_4'),('D204','1','MIDI_5')]:m.label(ref,pin,net)
    for ref in ['D203','D204']:m.label(ref,'2','CHASSIS')
    m.text('DIN panel socket: contact 4 -> J202.1; contact 5 -> J202.2. Pins 1/2/3 and shell unconnected.\nInsulate DIN shell. J203.1 bonds the enclosure; R101 returns ESD to local GND.\nOptocoupler LED current -> UART low. The output pull-up is 3.3 V, never 5 V.\nFunctional ground-loop isolation; transient clamps limit common-mode range. Not a safety isolator.',25.4,200.66,1.3)
    m.vias=[(24,4),(31,20),(22,22),(28,9)]
    m.keepouts=[{'polygon':[(.5,.5),(20.5,.5),(20.5,27.5),( .5,27.5)],'pour_only':True}]
    # All logic stays right of the optocoupler. MIDI current-loop copper is 3 mm
    # from logic copper, except the deliberate CHASSIS transient/RF network.
    m.save();return m

def expression():
    m=Module('expression','Ardor_Expression','Passive expression pedal / I2C ADC',(44,34),
      'Independent 3.3 V passive-pot interface. Local ADC, filtering, rail clamps, selectable polarity/address and removable I2C pull-ups.')
    m.conn('J302',['EXP_TIP','EXP_RING','GND'],(30.48,68.58),(3,13),rot=180)
    m.conn('J101',['+3V3','GND','SDA','SCL'],(373.38,167.64),(40,12))
    m.add('Conn_02x03_Odd_Even','JP301',{'1':'EXP_TIP','2':'EXP_RING','3':'EXP_WIPER','4':'EXP_EXC','5':'EXP_RING','6':'EXP_TIP'},(96.52,71.12),(10,13),value='TIP/RING / TWO SHUNTS',fp='Connector_PinHeader_2.54mm:PinHeader_2x03_P2.54mm_Vertical')
    rail=filter_power(m,3.3,(50.8,228.6),(27,4))
    m.two('R','R301',rail,'EXP_EXC',(190.5,53.34),(15,10))
    m.two('R','R302','EXP_WIPER','EXP_ADC',(162.56,96.52),(16,15))
    m.two('R','R303','EXP_WIPER','GND',(134.62,121.92),(13,21),rot=0)
    m.two('C','C301','EXP_ADC','GND',(198.12,121.92),(23,14),rot=0)
    m.two('R','R304','EXP_EXC','EXP_REF_ADC',(162.56,154.94),(17,21))
    m.two('C','C302','EXP_REF_ADC','GND',(198.12,180.34),(23,20),rot=0)
    m.add('ADS1115IDGS','U301',{'1':'ADDR','2':None,'3':'GND','4':'EXP_ADC','5':'EXP_REF_ADC','6':'GND','7':'GND','8':rail,'9':'ADC_SDA','10':'ADC_SCL'},(254,154.94),(28,16))
    m.two('C','C303',rail,'GND',(254,228.6),(29,11),rot=0)
    m.two('R','R307',rail,'GND',(299.72,228.6),(32,22),rot=0)
    for ref,a,b,pos,p in [('D303',rail,'EXP_ADC',(208.28,71.12),(23,10)),('D304','EXP_ADC','GND',(208.28,96.52),(22,17)),('D305',rail,'EXP_REF_ADC',(208.28,154.94),(19,24)),('D306','EXP_REF_ADC','GND',(208.28,205.74),(24,24))]:m.two('D_Schottky',ref,a,b,pos,p,rot=90)
    m.two('R','R305','ADC_SCL','SCL',(302.26,154.94),(35,13))
    m.two('R','R306','ADC_SDA','SDA',(335.28,157.48),(35,16))
    m.conn('JP302',['GND','ADDR',rail],(312.42,198.12),(28,26))
    m.conn('JP303',['SDA_PULL','SDA'],(350.52,76.2),(40,23))
    m.conn('JP304',['SCL_PULL','SCL'],(350.52,116.84),(40,30))
    m.two('R','R308',rail,'SDA_PULL',(302.26,73.66),(35,24),proto='R307')
    m.two('R','R309',rail,'SCL_PULL',(302.26,114.3),(35,29),proto='R307')
    m.two('D_TVS','D301','EXP_TIP','CHASSIS',(43.18,139.7),(5,7),rot=90)
    m.two('D_TVS','D302','EXP_RING','CHASSIS',(83.82,139.7),(5,19),rot=90)
    bond(m,(43.18,203.2),(8,24))
    # Main pedal -> filter -> ADC paths.
    m.label('J302','1','EXP_TIP');m.label('J302','2','EXP_RING')
    for pin,net in [('1','EXP_TIP'),('2','EXP_RING'),('5','EXP_RING'),('6','EXP_TIP')]:m.label('JP301',pin,net)
    m.link('EXP_WIPER',('JP301','3'),('R302','1'),[(83.82,71.12),(83.82,96.52)])
    m.link('EXP_WIPER',('R303','1'),(134.62,96.52));m.joint(134.62,96.52)
    m.link('EXP_EXC',('R301','2'),('JP301','4'),[(223.52,53.34),(223.52,43.18),(116.84,43.18),(116.84,71.12)])
    m.label('R304','1','EXP_EXC');m.label('R301','2','EXP_EXC')
    m.link('EXP_ADC',('R302','2'),('U301','4'),[(236.22,96.52),(236.22,152.4)])
    m.link('EXP_ADC',('C301','1'),(198.12,96.52));m.joint(198.12,96.52)
    m.link('EXP_ADC',('D303','2'),(236.22,96.52),[(236.22,71.12)]);m.joint(236.22,96.52)
    m.link('EXP_ADC',('D304','1'),(236.22,96.52))
    m.link('EXP_REF_ADC',('R304','2'),('U301','5'))
    m.link('EXP_REF_ADC',('C302','1'),(198.12,154.94));m.joint(198.12,154.94)
    m.link('EXP_REF_ADC',('D305','2'),(228.6,154.94));m.joint(228.6,154.94)
    m.link('EXP_REF_ADC',('D306','1'),(228.6,154.94),[(228.6,205.74)])
    m.link('ADC_SCL',('U301','10'),('R305','1'))
    m.link('ADC_SDA',('U301','9'),('R306','1'))
    m.link('SCL',('R305','2'),('J101','4'),[(325.12,154.94),(325.12,172.72)])
    m.link('SDA',('R306','2'),('J101','3'),[(350.52,157.48),(350.52,170.18)])
    m.link('ADDR',('U301','1'),('JP302','2'),[(279.4,160.02),(279.4,198.12)])
    m.link('SDA_PULL',('R308','2'),('JP303','1'))
    m.link('SCL_PULL',('R309','2'),('JP304','1'))
    m.label('JP303','2','SDA');m.label('JP304','2','SCL')
    m.label('R306','2','SDA');m.label('R305','2','SCL')
    m.label('D301','1','EXP_TIP');m.label('D302','1','EXP_RING')
    m.text('Passive 10k-100k pot only. Sleeve = GND. JP301: both shunts 1-3 / 2-4 for tip wiper; 3-5 / 4-6 for ring wiper.\nJP302: 1-2 = 0x48; 2-3 = 0x49. Never leave ADDR floating. JP303/304 enable local 2.2k I2C pull-ups.\nRemove BOTH pull-up shunts when host already has pull-ups (Raspberry Pi). Start at 100 kHz I2C.\nADS1115: +/-4.096 V, 128 SPS. Read AIN0 and AIN1; normalize AIN0/AIN1, calibrate endpoints and smooth.',25.4,250.19,1.15)
    m.vias=[(12,5),(15,25),(27,20),(30,8),(37,5),(19,28)]
    m.save();return m

def mixer():
    m=Module('mixer','Ardor_Mixer','Stereo buffer / mono average / AC outputs',(40,30),
      'Independent 5 V line-level stereo buffer and mono average. Accepts an external DAC/codec; all outputs are DC blocked. No shared VREF.')
    m.conn('J102',['AUDIO_L','GND','AUDIO_R'],(30.48,71.12),(3,12),rot=180)
    m.conn('J101',['+5V','GND'],(30.48,203.2),(22,4))
    m.conn('J103',['OUT_L','GND','OUT_R','OUT_MONO'],(375.92,99.06),(36,14))
    rail=filter_power(m,5,(50.8,228.6),(10,4))
    for i,yy in enumerate([60.96,121.92]):
        ch='L' if i==0 else 'R'; cref='C40'+str(i+1);rref='R40'+str(i+1);mixref='R40'+str(i+3)
        m.two('C',cref,'AUDIO_'+ch,'BIAS_'+ch,(66.04,yy),(8,8+8*i))
        m.two('R',rref,'BIAS_'+ch,'VREF',(96.52,yy+22.86),(9,11+8*i),rot=0)
        o=m.buffer('U401',i+1,'BIAS_'+ch,'BUF_'+ch,(139.7,yy+2.54),(15,12))
        m.two('R',mixref,'BUF_'+ch,'MONO_MIX',(195.58,yy+2.54),(23,8+4*i))
        m.link('AUDIO_'+ch,('J102',str(1+2*i)),(45.72 if i==0 else 55.88,yy),[(45.72 if i==0 else 55.88,m.p('J102',str(1+2*i))[1]),(45.72 if i==0 else 55.88,yy)])
        m.link('AUDIO_'+ch,(45.72 if i==0 else 55.88,yy),(cref,'1'))
        m.link('BIAS_'+ch,(cref,'2'),('U401','3' if i==0 else '5',i+1))
        m.link('BIAS_'+ch,(rref,'1'),(96.52,yy));m.joint(96.52,yy)
        m.link('BUF_'+ch,o,(mixref,'1'))
    mon=o2=m.buffer('U402',1,'MONO_MIX','MONO_BUF',(261.62,88.9),(27,17))
    m.link('MONO_MIX',('R403','2'),('U402','3',1),[(226.06,63.5),(226.06,86.36)])
    m.link('MONO_MIX',('R404','2'),(226.06,86.36),[(226.06,124.46)]);m.joint(226.06,86.36)
    # Each output crosses the module boundary at zero DC bias.
    for ref,rin,rbleed,ch,unit,y,pp in [('C407','R407','R410','L',1,53.34,(28,7)),('C408','R408','R411','R',2,144.78,(28,12)),('C409','R409','R412','MONO',1,91.44,(29,21))]:
        source='MONO_BUF' if ch=='MONO' else 'BUF_'+ch
        m.two('C',ref,source,'AC_'+ch,(302.26,y),pp,proto='C401')
        m.two('R',rin,'AC_'+ch,'OUT_'+ch,(335.28,y),({'L':32,'R':32,'MONO':33}[ch],{'L':7,'R':12,'MONO':24}[ch]),proto='R203')
        m.two('R',rbleed,'AC_'+ch,'GND',(320.04,y+20.32),({'L':29,'R':33,'MONO':29}[ch],{'L':10,'R':21,'MONO':27}[ch]),proto='R401',rot=0)
        src=('U402','1',1) if ch=='MONO' else ('U401','1' if ch=='L' else '7',unit)
        branch=167.64 if ch=='L' else 177.8 if ch=='R' else 281.94
        lane=45.72 if ch=='L' else 157.48 if ch=='R' else y
        m.link(source,src,(ref,'1'),[(branch,m.p(*src)[1]),(branch,lane),(281.94,lane),(281.94,y)])
        m.joint(branch,m.p(*src)[1])
        m.link('AC_'+ch,(ref,'2'),(rin,'1'));m.link('AC_'+ch,(rbleed,'1'),(320.04,y));m.joint(320.04,y)
        pin={'L':'1','R':'3','MONO':'4'}[ch]
        column={'L':350.52,'R':363.22,'MONO':355.6}[ch]
        m.link('OUT_'+ch,(rin,'2'),('J103',pin),[(column,y),(column,m.p('J103',pin)[1])])
    m.two('R','R405',rail,'VREF_DIV',(162.56,190.5),(22,25),rot=0)
    m.two('R','R406','VREF_DIV','GND',(162.56,218.44),(26,25),rot=0)
    m.two('C','C403','VREF_DIV','GND',(198.12,218.44),(18,24),rot=0)
    m.buffer('U402',2,'VREF_DIV','VREF',(276.86,203.2),(27,17))
    m.link('VREF_DIV',('R405','2'),('R406','1'))
    m.link('VREF_DIV',(162.56,203.2),('U402','5',2),[(248.92,203.2),(248.92,200.66)])
    m.joint(162.56,203.2)
    m.link('VREF_DIV',('C403','1'),(198.12,203.2));m.joint(198.12,203.2)
    m.label('U402','7','VREF',2)
    m.power_unit('U401',rail,(335.28,193.04),(15,12));m.power_unit('U402',rail,(373.38,193.04),(27,17))
    m.two('C','C404',rail,'GND',(335.28,228.6),(13,7),rot=0)
    m.two('C','C405',rail,'GND',(373.38,228.6),(32.5,16),rot=0)
    m.text('Line-level input <=1 Vrms/channel. Output loads >=10k. Mono = (L + R)/2, unity buffers.\nAll three outputs AC coupled; no 2.5 V bias leaves this board. For mono input, feed both channels\nor accept -6 dB when using one channel. Harness connectors are internal; add jack-entry ESD protection\nfor exposed ports. This board is neither an instrument preamp nor an ADC, headphone or power amplifier.',25.4,250.19,1.2)
    m.vias=[(8,24),(14,20),(25,10),(31,26),(36,5),(19,12)]
    m.save();return m

def line_out():
    m=Module('line-out','Ardor_Line_Out','Mono line output / relay mute / internal amp feed',(46,34),
      'Independent 5 V mono line driver, powered-off tip grounding and internal amp feed. External audio source and 3.3 V enable; no mixer required.')
    m.conn('J102',['AUDIO_IN','GND'],(30.48,76.2),(3,20),rot=180)
    m.conn('J101',['+5V','GND','LINE_ENABLE'],(30.48,200.66),(3,9),rot=180)
    m.conn('J503',['LINE_JACK','GND'],(381,83.82),(42,17))
    m.conn('J502',['AMP_FEED','GND'],(373.38,243.84),(42,26))
    rail=filter_power(m,5,(50.8,228.6),(10,4))
    m.two('C','C401','AUDIO_IN','BIAS_IN',(68.58,78.74),(8,20))
    m.two('R','R401','BIAS_IN','VREF',(96.52,104.14),(9,24),rot=0)
    out=m.buffer('U501',1,'BIAS_IN','MONO_BUF',(144.78,81.28),(14,15))
    m.link('AUDIO_IN',('J102','1'),('C401','1'))
    m.link('BIAS_IN',('C401','2'),('U501','3',1));m.link('BIAS_IN',('R401','1'),(96.52,78.74));m.joint(96.52,78.74)
    m.two('C_Polarized','C501','MONO_BUF','LINE_AC',(203.2,81.28),(25,12))
    m.two('R','R501','LINE_AC','GND',(228.6,111.76),(24,19),rot=0)
    m.two('R','R502','LINE_AC','LINE_DRIVE',(259.08,81.28),(27,7))
    m.add('G5V-1','K501',{'2':'+5V','9':'RELAY_LOW','1':'GND','5':'LINE_JACK','6':'LINE_JACK','10':'LINE_DRIVE'},(304.8,83.82),(35,9),value='Omron G5V-1 DC5',fp='Relay_THT:Relay_SPDT_Omron_G5V-1')
    m.link('MONO_BUF',out,('C501','1'))
    m.link('LINE_AC',('C501','2'),('R502','1'));m.link('LINE_AC',('R501','1'),(228.6,81.28));m.joint(228.6,81.28)
    m.link('LINE_DRIVE',('R502','2'),('K501','10'),[(279.4,81.28),(279.4,63.5),(312.42,63.5)])
    m.link('LINE_JACK',('K501','5'),('J503','1'),[(330.2,91.44),(330.2,81.28)])
    m.link('LINE_JACK',('K501','6'),('K501','5'))
    m.two('D_TVS','D501','LINE_JACK','CHASSIS',(350.52,111.76),(39,21),rot=90)
    m.link('LINE_JACK',('D501','1'),(330.2,81.28),[(330.2,111.76)]);m.joint(330.2,81.28)
    bond(m,(355.6,203.2),(33,25))
    m.add('AO3400A','Q501',{'1':'RELAY_GATE','2':'GND','3':'RELAY_LOW'},(215.9,154.94),(28,25))
    m.two('R','R503','LINE_ENABLE','RELAY_GATE',(134.62,154.94),(18,29))
    m.two('R','R504','RELAY_GATE','GND',(180.34,182.88),(23,25),rot=0)
    m.two('D','D502','+5V','RELAY_LOW',(269.24,154.94),(32,20),rot=90)
    m.link('LINE_ENABLE',('J101','3'),('R503','1'),[(53.34,198.12),(53.34,154.94)])
    m.link('RELAY_GATE',('R503','2'),('Q501','1'));m.link('RELAY_GATE',('R504','1'),(180.34,154.94));m.joint(180.34,154.94)
    m.link('RELAY_LOW',('Q501','3'),('K501','9'),[(218.44,132.08),(299.72,132.08)])
    m.link('RELAY_LOW',('D502','2'),(299.72,132.08),[(284.48,m.p("D502","2")[1]),(284.48,132.08)]);m.joint(284.48,132.08)
    m.two('R','R405',rail,'VREF_DIV',(99.06,190.5),(18,22),proto='R405',rot=0)
    m.two('R','R406','VREF_DIV','GND',(99.06,218.44),(18,25),proto='R406',rot=0)
    m.two('C','C403','VREF_DIV','GND',(142.24,218.44),(14,26),proto='C403',rot=0)
    m.buffer('U501',2,'VREF_DIV','VREF',(228.6,213.36),(14,15))
    m.link('VREF_DIV',('R405','2'),('R406','1'))
    m.link('VREF_DIV',(99.06,203.2),('U501','5',2),[(195.58,203.2),(195.58,210.82)])
    m.joint(99.06,203.2)
    m.link('VREF_DIV',('C403','1'),(142.24,203.2));m.joint(142.24,203.2)
    m.label('U501','7','VREF',2)
    m.power_unit('U501',rail,(284.48,205.74),(14,15))
    m.two('C','C404',rail,'GND',(307.34,228.6),(13,9),proto='C404',rot=0)
    m.two('C','C502','MONO_BUF','AMP_AC',(208.28,243.84),(22,29))
    m.two('R','R505','AMP_AC','AMP_FEED',(276.86,243.84),(28,30))
    m.two('R','R506','AMP_FEED','GND',(320.04,243.84),(37,27),rot=0)
    m.link('MONO_BUF',('U501','1',1),('C502','1'),[(177.8,81.28),(177.8,243.84)]);m.joint(177.8,81.28)
    m.link('AMP_AC',('C502','2'),('R505','1'))
    m.link('AMP_FEED',('R505','2'),('J502','1'),[(299.72,243.84),(299.72,228.6),(342.9,228.6),(342.9,241.3)])
    m.link('AMP_FEED',('R506','1'),(320.04,228.6));m.joint(320.04,228.6)
    m.text('Input <=1 Vrms, input impedance ~100k. LINE load >=10k; internal AMP load >=100k.\nRelay OFF: jack tip grounded, source disconnected. Relay ON: unity mono output. No true bypass.\n3.3 V active-high enable; leave LOW until audio is stable. 5 V supply budget >=50 mA.\nC501 + toward buffer. J502 is a signal feed, not a speaker driver; jack sleeves bond the enclosure.',25.4,250.19,1.15)
    m.vias=[(8,28),(19,17),(23,23),(34,29),(40,22),(13,6)]
    m.save();return m

def headphones():
    m=Module('headphones','Ardor_Headphones','Stereo headphone amplifier / optional module',(36,30),
      'Independent stereo headphone amplifier. External stereo DAC/codec, 5 V and enable. Charge pump and input coupling are local; no mixer required.')
    m.conn('J102',['AUDIO_L','GND','AUDIO_R'],(30.48,73.66),(3,9),rot=180)
    m.conn('J101',['+5V','GND','HP_ENABLE'],(30.48,172.72),(3,20),rot=180)
    m.conn('J602',['HP_L','HP_R','GND'],(375.92,86.36),(32,12))
    rail=filter_power(m,5,(50.8,228.6),(9,22))
    m.two('C','C601','AUDIO_R','HP_IN_R',(106.68,60.96),(10,7),proto='C401')
    m.two('C','C602','AUDIO_L','HP_IN_L',(106.68,81.28),(10,11),proto='C401')
    ns={'1':'HP_IN_L','2':'GND','3':'GND','4':'HP_IN_R','5':'HP_R_RAW','6':'GND','7':'GND','8':'HPVSS','9':'CPN','10':'GND','11':'CPP','12':'HPVDD','13':'HP_EN','14':rail,'15':'GND','16':'HP_L_RAW','17':'GND'}
    m.add('TPA6132A2RTE','U601',ns,(205.74,78.74),(18,13))
    m.two('R','R601','HP_ENABLE','HP_EN',(111.76,157.48),(10,19),proto='R503')
    m.two('R','R602','HP_EN','GND',(152.4,180.34),(10,16),proto='R504',rot=0)
    m.two('R','R603','HP_L_RAW','HP_L',(292.1,88.9),(27,8))
    m.two('R','R604','HP_R_RAW','HP_R',(292.1,71.12),(27,14))
    m.two('D_TVS','D601','HP_L','CHASSIS',(335.28,132.08),(29,19),proto='D501',rot=90)
    m.two('D_TVS','D602','HP_R','CHASSIS',(373.38,132.08),(29,23),proto='D501',rot=90)
    bond(m,(335.28,203.2),(25,26))
    m.two('C','C603',rail,'GND',(185.42,228.6),(20,25),rot=0)
    m.two('C','C604',rail,'GND',(218.44,228.6),(17,9),proto='C404',rot=0)
    m.two('C','C605','HPVDD','GND',(243.84,228.6),(21.5,8.5),rot=0)
    m.two('C','C606','HPVSS','GND',(276.86,228.6),(14,16),rot=0)
    m.two('C','C607','CPP','CPN',(271.78,99.06),(22,13),rot=90)
    m.link('AUDIO_R',('J102','3'),('C601','1'),[(50.8,71.12),(50.8,60.96)])
    m.link('AUDIO_L',('J102','1'),('C602','1'),[(55.88,76.2),(55.88,81.28)])
    m.link('HP_IN_R',('C601','2'),('U601','4'),[(177.8,60.96),(177.8,71.12)])
    m.link('HP_IN_L',('C602','2'),('U601','1'))
    m.label('U601','14',rail,length=15.24)
    for pin in ['15','10']:
        m.link('GND',('U601',pin),(m.p('U601',pin)[0],116.84))
        m.link('GND',(m.p('U601',pin)[0],116.84),(208.28,116.84))
    m.label('U601','17','GND',length=27.94);m.joint(203.2,116.84);m.joint(208.28,116.84)
    m.link('HP_ENABLE',('J101','3'),('R601','1'),[(55.88,170.18),(55.88,157.48)])
    m.link('HP_EN',('R601','2'),('U601','13'),[(177.8,157.48),(177.8,88.9)])
    m.link('HP_EN',('R602','1'),(152.4,157.48));m.joint(152.4,157.48)
    m.link('HP_L_RAW',('U601','16'),('R603','1'),[(266.7,76.2),(266.7,88.9)]);m.link('HP_R_RAW',('U601','5'),('R604','1'))
    m.link('HP_L',('R603','2'),('J602','1'),[(355.6,88.9),(355.6,83.82)])
    m.link('HP_R',('R604','2'),('J602','2'),[(360.68,71.12),(360.68,86.36)])
    m.label('D601','1','HP_L');m.label('D602','1','HP_R')
    m.label('R603','2','HP_L');m.label('R604','2','HP_R')
    m.link('CPP',('U601','11'),('C607','1'),[(243.84,83.82),(243.84,99.06)])
    m.link('CPN',('U601','9'),('C607','2'),[(251.46,86.36),(251.46,109.22),(294.64,109.22),(294.64,99.06)])
    m.text('Stereo headphones >=32 ohm. -6 dB inverting gain (G0/G1 grounded). Inputs <=1 Vrms.\nNo output coupling capacitors needed. HPVDD/HPVSS are local internal rails; do not connect to supply.\nEnable LOW until audio is stable; ramp volume and start quietly. 5 V budget >=50 mA.\nJ602 to TRS: 1 tip (L), 2 ring (R), 3 sleeve (GND). This optional QFN module adds assembly cost.',25.4,250.19,1.2)
    m.vias=[(20.5,16.5),(16,19),(15,13),(25,20),(32,26),(12,27),(7,15)]
    m.save();return m

if __name__=='__main__':
    for build in [midi,expression,mixer,line_out,headphones]:build()
