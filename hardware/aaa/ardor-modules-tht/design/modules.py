"""T1 hand-assembly circuits. OPA1656 SOIC-8 is the sole loose SMD exception.

Reuse M1's independently checked drawn circuits for MIDI, expression, mixer
and line output. Change packages and the relay transistor, not the interfaces.
The headphone circuit is intentionally different: one OPA1656 SOIC-8 dual device,
AC-coupled outputs and a fail-muted DPDT relay, without a charge pump.
"""
import copy
import csv
import json
from pathlib import Path
import sexpdata as sx
import cad
from cad import ROOT, Module, LIB, CAT, S, key, get
import smd_circuits

AXIAL = 'Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal'
DIODE = 'Diode_THT:D_DO-35_SOD27_P7.62mm_Horizontal'
TVS = 'Diode_THT:D_DO-15_P10.16mm_Horizontal'
RADIAL = 'Capacitor_THT:CP_Radial_D6.3mm_P2.50mm'
DIP8 = 'Package_DIP:DIP-8_W7.62mm_Socket'
SOIC8 = 'Package_SO:SOIC-8_3.9x4.9mm_P1.27mm'
FEEDBACK = 'Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P2.54mm_Vertical'
DISC = 'Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm'
BIPOLAR = 'Ardor_THT:CP_Bipolar_D6.3mm_P2.50mm'
SCALE = 2.5


def rename(sym, name):
    sym = copy.deepcopy(sym)
    old = sym[1]
    sym[1] = name
    for sub in sym:
        if key(sub) == 'symbol':
            sub[1] = sub[1].replace(old + '_', name + '_')
    return sym


def prepare_symbols():
    # Frozen checked-in symbols make regeneration independent of system symbols.
    extra = sx.loads((ROOT/'design/tht-symbols.kicad_sym').read_text())
    for sym in extra:
        if key(sym) == 'symbol':
            LIB[sym[1]] = sym


def purchasing(kind, old, ref):
    """No SMT MPNs or supplier assembly identifiers survive substitution."""
    attrs = old.get('catalog_attributes', {}) if old else {}
    if kind == 'R':
        resistance = attrs['Resistance']
        if ref == 'R503':
            resistance = '470Ω'
        return AXIAL, resistance.replace('Ω','R')+' / 1% 0.25W', 'Axial metal film '+resistance+' 1% 0.25W', ''
    if kind == 'FerriteBead':
        return 'Ardor_THT:Ferrite_WE_7427501_P15.24mm', '800R@100MHz / WE 7427501', 'Wurth Elektronik 7427501', 'https://www.we-online.com/components/products/datasheet/7427501.pdf'
    if kind == 'D':
        return DIODE, '1N4148', 'Vishay 1N4148-TAP', 'https://www.vishay.com/docs/81857/1n4148.pdf'
    if kind == 'D_Schottky':
        return DIODE, 'BAT85S', 'Vishay BAT85S-TAP', 'https://www.vishay.com/docs/85513/bat85s.pdf'
    if kind == 'D_TVS':
        high = old and '24' in old['mpn']
        mpn = 'SA24CA-E3/54' if high else 'SA5.0CA-E3/54'
        return TVS, mpn, 'Vishay '+mpn, 'https://www.vishay.com/doc/?88378='
    if kind in ['C', 'C_Polarized']:
        cap = attrs['Capacitance']
        if cap == '100pF':
            return 'Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm', '100p / 1kV C0G', '100pF 1kV C0G disc, 5mm lead pitch', ''
        if cap == '100nF':
            return 'Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm', '100n / 50V X7R', '100nF 50V X7R radial, 5mm lead pitch', ''
        if ref in ['C401','C402','C407','C408','C409','C502']:
            return 'Ardor_THT:CP_Bipolar_D6.3mm_P2.50mm', '10u / 25V BIPOLAR', 'Nichicon UES1E100MDM', 'https://www.nichicon.co.jp/english/products/pdfs/e-ues.pdf'
        cap = '47uF' if ref == 'C501' else cap
        return RADIAL, cap.replace('F','')+' / 25V polarized', 'Radial electrolytic '+cap+' 25V, D<=6.3mm P2.5mm', ''
    if kind == 'TLV9002':
        return DIP8, 'MCP6022-I/P', 'Microchip MCP6022-I/P', 'https://www.microchip.com/content/dam/mchp/documents/APID/ProductDocuments/DataSheets/20001685E.pdf'
    if kind == 'AO3400A':
        return 'Package_TO_SOT_THT:TO-92_Inline_Wide', '2N3904 / E-B-C', 'onsemi 2N3904BU', 'https://www.onsemi.com/pdf/datasheet/2n3903-d.pdf'
    if kind == 'ADS1115IDGS':
        return 'Ardor_THT:Adafruit_ADS1115_1085_STEMMA_QT', 'Adafruit 1085 ADS1115 QT', 'Adafruit 1085 (STEMMA QT revision)', 'https://www.adafruit.com/product/1085'
    return None


class THTModule(Module):
    def __init__(self, slug, name, title, size, notes):
        super().__init__(slug, name+'_THT', title+' / HAND ASSEMBLY',
                         tuple(round(v*SCALE) for v in size), notes)

    def add(self, kind, ref, nets, sch, pcb, proto=None, value=None, fp=None, unit=1, dnp=False):
        old = CAT.get(proto or ref)
        selection = purchasing(kind, old, ref)
        if selection:
            fp, value, mpn, datasheet = selection
            if kind == 'AO3400A':
                kind = '2N3904_THT'
                # Circuit uses B1/E2/C3; actual onsemi TO-92 is E1/B2/C3.
                nets = {'2':nets['1'], '1':nets['2'], '3':nets['3']}
            if kind == 'ADS1115IDGS':
                kind = 'ADS1115_Module'
                nets = {**nets, '11':None, '12':None}
            if kind == 'TLV9002':
                kind = 'MCP6022'
            if kind == 'C' and 'polarized' in value:
                kind = 'C_Polarized'
            # Use explicit values; M1's value formatter only runs on its catalog.
            catalog_key = self.slug+'/'+ref
            CAT[catalog_key] = dict(footprint=fp, mpn=mpn, bom_mpn=mpn,
                                   jlcpcb_part='', datasheet=datasheet)
            proto = catalog_key
        scaled = [round(pcb[0]*SCALE,4), round(pcb[1]*SCALE,4), *pcb[2:]]
        adjustments = {
            'midi-in': {'J202':[6,33], 'FB201':[18,25,90], 'FB202':[18,45,90],
                        'R201':[30,25], 'D201':[30,41], 'D202':[6,15,90],
                        'D203':[6,54,90], 'D204':[32,62], 'C202':[30,8], 'C203':[18,60]},
            'expression': {'U301':[74,41], 'R305':[94,28,90], 'R306':[94,44,90],
                           'D304':[55,44], 'C301':[54,34], 'R303':[30,48],
                           'R304':[42.5,55], 'C101':[82,10], 'C102':[93,10], 'C302':[53,51]},
            'mixer': {'FB101':[22,10], 'C101':[39,10], 'C102':[48,10], 'R406':[70,62.5]},
            'line-out': {'FB101':[22,10], 'C101':[39,10], 'C102':[48,10]},
        }
        scaled = adjustments.get(self.slug,{}).get(ref,scaled)
        return super().add(kind, ref, nets, sch, scaled, proto=proto,
                           value=value, fp=fp, unit=unit, dnp=dnp)

    def save(self):
        self.vias = []  # THT ground pads already connect both ground planes.
        self.keepouts = [{**k,'polygon':[[round(x*SCALE,4),round(y*SCALE,4)] for x,y in k['polygon']]}
                         for k in self.keepouts]
        super().save()
        write_bom(self)


def write_bom(module):
    out = ROOT/module.slug
    with (out/'BOM.csv').open('w') as f:
        writer = csv.writer(f,lineterminator='\n')
        writer.writerow(['Reference','Value','Footprint','Purchasing specification','Assembly'])
        for ref,c in module.parts.items():
            writer.writerow([ref,c['value'],c['fp'],c['mpn'],
                'Preassembled module; THT sockets' if ref=='U301' and module.slug=='expression' else
                'SMD manual / SOIC-8 1.27mm pitch' if module.slug=='headphones' and ref=='U601' else 'THT manual'])


def headphones():
    m = Module('headphones','Ardor_Headphones_THT','Stereo headphones / OPA1656 / relay mute', (96,84),
               '5V stereo headphone output; one hand-soldered SOIC-8; -0.5 unloaded inverting gain; active-high 3.3V relay enable.')
    def add(kind,ref,nets,sch,pcb,value,fp,mpn='',datasheet='',unit=1):
        pcb = {'FB101':[19,74], 'C101':[41,77], 'C102':[58,78],
               'R601':[35,70], 'R602':[47,70,90], 'Q601':[57,69],
               'D603':[73,76], 'R101':[89,68,90], 'R642':[85,55,90]}.get(ref,pcb)
        catalog_key = 'headphones/'+ref
        CAT[catalog_key] = dict(footprint=fp,mpn=mpn or value,bom_mpn=mpn or value,jlcpcb_part='',datasheet=datasheet)
        return m.add(kind,ref,nets,sch,pcb,proto=catalog_key,value=value,fp=fp,unit=unit)
    def two(kind,ref,a,b,sch,pcb,value,fp,rot=90,mpn='',datasheet=''):
        return add(kind,ref,{'1':a,'2':b},(*sch,rot),pcb,value,fp,mpn,datasheet)
    m.conn('J102',['AUDIO_L','GND','AUDIO_R'],(25.4,66.04),(5,22),rot=180)
    m.conn('J101',['+5V','GND','HP_ENABLE'],(25.4,205.74),(5,58),rot=180)
    m.conn('J602',['HP_L','HP_R','GND'],(383.54,81.28),(90,24))
    two('FerriteBead','FB101','+5V','+5V_A',(55.88,246.38),(24,66),
        '800R@100MHz / WE 7427501','Ardor_THT:Ferrite_WE_7427501_P15.24mm',
        mpn='Wurth Elektronik 7427501',datasheet='https://www.we-online.com/components/products/datasheet/7427501.pdf')
    two('C_Polarized','C101','+5V_A','GND',(93.98,246.38),(42,66),'100u / 25V',RADIAL,rot=0)
    two('C','C102','+5V_A','GND',(124.46,246.38),(52,66),'100n / 50V X7R',DISC,rot=0)
    for ch,y,u in [('L',63.5,1),('R',137.16,2)]:
        s='1' if ch=='L' else '2'
        pn,nn,on = ('3','2','1') if u==1 else ('5','6','7')
        two('C','C60'+s,'AUDIO_'+ch,'AC_'+ch,(60.96,y+2.54),(16,31.23) if u==1 else (73,32.5),
            '10u / 25V BIPOLAR',BIPOLAR,mpn='Nichicon UES1E100MDM',datasheet='https://www.nichicon.co.jp/english/products/pdfs/e-ues.pdf')
        two('R','R61'+s,'AC_'+ch,'SUM_'+ch,(93.98,y+2.54),(27.5,31.23) if u==1 else (61,32.5,180),'20k / 1% 0.25W',AXIAL)
        add('OPA1656','U601',{pn:'VREF',nn:'SUM_'+ch,on:'RAW_'+ch},(152.4,y),(44,32.5),
            'OPA1656ID',SOIC8,'Texas Instruments OPA1656ID','https://www.ti.com/lit/ds/symlink/opa1656.pdf',unit=u)
        two('R','R65'+s,'RAW_'+ch,'SUM_'+ch,(152.4,y+22.86),(37.5,31.23) if u==1 else (50.5,32.5,180),
            '10k / 1% 0.25W / UPRIGHT',FEEDBACK,rot=270)
        two('C','C66'+s,'RAW_'+ch,'SUM_'+ch,(152.4,y+40.64),(37.5,24) if u==1 else (50.5,39,180),'100p / 50V C0G',DISC,rot=270)
        two('R','R63'+s,'RAW_'+ch,'ISOLATED_'+ch,(210.82,y),(31,18,90) if u==1 else (61,50),'10R / 1% 0.25W',AXIAL)
        two('C_Polarized','C65'+s,'ISOLATED_'+ch,'DRIVE_'+ch,(246.38,y),(58,16) if u==1 else (65,59),
            '470u / 16V','Capacitor_THT:CP_Radial_D8.0mm_P3.50mm')
        two('R','R64'+s,'DRIVE_'+ch,'GND',(287.02,y+25.4),(78,20,90) if u==1 else (82,55),'1k / 1% 0.25W',AXIAL,rot=0)
        two('D_TVS','D60'+s,'HP_'+ch,'CHASSIS',(340.36,y+25.4),(87,12) if u==1 else (87,37),'SA5.0CA-E3/54',TVS,
            mpn='Vishay SA5.0CA-E3/54',datasheet='https://www.vishay.com/doc/?88378=')
        m.link('AUDIO_'+ch,('J102','1' if ch=='L' else '3'),('C60'+s,'1'),[(43.18 if ch=='L' else 38.1,y+2.54)])
        m.link('AC_'+ch,('C60'+s,'2'),('R61'+s,'1'))
        m.link('SUM_'+ch,('R61'+s,'2'),('U601',nn,u))
        # Feedback is drawn below each channel; both passive branches terminate
        # at the same explicit summing/output nodes, not a unity-buffer short.
        inp=(132.08,y+2.54); out=(190.5,y)
        m.link('RAW_'+ch,('U601',on,u),('R63'+s,'1'));m.joint(*out)
        for ref in ['R65'+s,'C66'+s]:
            yy=y+(22.86 if ref.startswith('R') else 40.64)
            m.link('RAW_'+ch,('U601',on,u),(ref,'1'),[out,(190.5,yy)])
            m.link('SUM_'+ch,('U601',nn,u),(ref,'2'),[inp,(132.08,yy)])
        m.joint(*inp);m.joint(132.08,y+22.86);m.joint(190.5,y+22.86)
        m.link('ISOLATED_'+ch,('R63'+s,'2'),('C65'+s,'1'))
        m.link('DRIVE_'+ch,('C65'+s,'2'),('R64'+s,'1'))
        m.label('C65'+s,'2','DRIVE_'+ch)
        m.label('D60'+s,'1','HP_'+ch)
        m.label('U601',pn,'VREF',u)
    add('OPA1656','U601',{'8':'+5V_A','4':'GND'},(152.4,246.38),(44,32.5),
        'OPA1656ID',SOIC8,'Texas Instruments OPA1656ID','https://www.ti.com/lit/ds/symlink/opa1656.pdf',unit=3)
    two('C','C641','+5V_A','GND',(182.88,246.38),(50,27.5,90),'100n / 50V X7R / P2.5mm','Capacitor_THT:C_Disc_D3.8mm_W2.6mm_P2.50mm',rot=0,mpn='100nF 50V X7R radial, D<=3.8mm W<=2.6mm P2.5mm')
    two('R','R621','+5V_A','VREF',(208.28,223.52),(33,53),'10k / 1% 0.25W',AXIAL,rot=0)
    two('R','R622','VREF','GND',(208.28,246.38),(56,53),'10k / 1% 0.25W',AXIAL,rot=0)
    two('C_Polarized','C631','VREF','GND',(238.76,246.38),(44,50),'47u / 25V',RADIAL,rot=0)
    two('C','C632','VREF','GND',(266.7,246.38),(43,41),'100n / 50V X7R',DISC,rot=0)
    m.link('VREF',('R621','2'),('R622','1'));m.joint(208.28,234.95)
    m.link('VREF',('R621','2'),('C631','1'),[(208.28,234.95)]);m.link('VREF',('C631','1'),('C632','1'))
    m.label('R621','2','VREF')
    add('G5V2_THT','K601',{'1':'+5V','16':'RELAY_LOW','4':'HP_L','13':'HP_R','6':'GND','11':'GND','8':'DRIVE_L','9':'DRIVE_R'},
        (335.28,193.04),(75,62),'G5V-2-H1 DC5','Relay_THT:Relay_DPDT_Omron_G5V-2',
        'Omron G5V-2-H1 DC5','https://components.omron.com/system/files/2023-01/datasheet_pdf/K046-E1.pdf')
    for pin,net in [('4','HP_L'),('13','HP_R'),('8','DRIVE_L'),('9','DRIVE_R'),('16','RELAY_LOW')]:
        m.label('K601',pin,net,length=2.54 if pin=='8' else 10.16 if pin=='9' else 5.08)
    m.label('J602','1','HP_L');m.label('J602','2','HP_R')
    add('2N3904_THT','Q601',{'1':'GND','2':'RELAY_BASE','3':'RELAY_LOW'},(154.94,205.74),(57,61),
        '2N3904 / E-B-C','Package_TO_SOT_THT:TO-92_Inline_Wide','onsemi 2N3904BU','https://www.onsemi.com/pdf/datasheet/2n3903-d.pdf')
    two('R','R601','HP_ENABLE','RELAY_BASE',(99.06,198.12),(40,58),'470R / 1% 0.25W',AXIAL)
    two('R','R602','RELAY_BASE','GND',(124.46,220.98),(48,60),'100k / 1% 0.25W',AXIAL,rot=0)
    two('D','D603','+5V','RELAY_LOW',(254,198.12),(73,51),'1N4148',DIODE,
        mpn='Vishay 1N4148-TAP',datasheet='https://www.vishay.com/docs/81857/1n4148.pdf')
    two('R','R101','CHASSIS','GND',(350.52,233.68),(90,61),'0R / WIRE LINK',AXIAL)
    m.link('HP_ENABLE',('J101','3'),('R601','1'))
    m.link('RELAY_BASE',('R601','2'),('Q601','2'));m.link('RELAY_BASE',('R602','1'),(124.46,198.12));m.joint(124.46,198.12)
    m.label('Q601','3','RELAY_LOW');m.label('D603','2','RELAY_LOW')
    m.link('+5V_A',('FB101','2'),('C101','1'));m.link('+5V_A',('C101','1'),('C102','1'));m.label('FB101','2','+5V_A')
    m.text('Regulated 5V +/-5% / 150mA. 32-300 ohm headphones; source <=1Vrms.\nOPA1656ID: one SOIC-8, 1.27mm pitch; all other components THT.\nInverting gain -10k/20k = -0.5; both inputs held at VREF ~2.5V.\n10R output isolation BEFORE 470u caps; + toward amplifier. Relay OFF grounds panel.\nHP_ENABLE: 3.3V HIGH sources ~5mA; wait >=5s after stable supply/audio.\nNo onboard timer. Check DC/transients, oscillation, noise and clipping on dummy loads.',25.4,276.2,1.05)
    # Reuse the manual BOM writer, without scaling this explicitly placed board.
    Module.save(m)
    write_bom(m)
    return m


if __name__ == '__main__':
    prepare_symbols()
    smd_circuits.Module = THTModule
    for fn in [smd_circuits.midi,smd_circuits.expression,smd_circuits.mixer,smd_circuits.line_out,headphones]:
        fn()
