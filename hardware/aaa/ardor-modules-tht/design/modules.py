"""T1 hand-assembly circuits. All carrier pads are through-hole.

Reuse M1's independently checked drawn circuits for MIDI, expression, mixer
and line output. Change packages and the relay transistor, not the interfaces.
The headphone circuit is intentionally different: two LM386N-1 DIP devices,
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
                'Preassembled module; THT sockets' if ref=='U301' and module.slug=='expression' else 'THT manual'])


def headphones():
    m = Module('headphones','Ardor_Headphones_THT','Stereo headphones / LM386 / relay mute', (96,84),
               '5V stereo headphone output; through-hole assembly; approximately -6dB unloaded gain; active-high 3.3V relay enable.')
    def add(kind,ref,nets,sch,pcb,value,fp,mpn='',datasheet=''):
        pcb = {'FB101':[19,74], 'C101':[41,77], 'C102':[58,78],
               'R601':[35,70], 'R602':[47,70,90], 'Q601':[57,69],
               'D603':[73,76], 'R101':[89,68,90], 'R642':[85,55,90],
               'R651':[55,30,90], 'R652':[55,55,90],
               'C641':[52,13], 'C642':[52,38]}.get(ref,pcb)
        catalog_key = 'headphones/'+ref
        CAT[catalog_key] = dict(footprint=fp,mpn=mpn or value,bom_mpn=mpn or value,jlcpcb_part='',datasheet=datasheet)
        return m.add(kind,ref,nets,sch,pcb,proto=catalog_key,value=value,fp=fp)
    def two(kind,ref,a,b,sch,pcb,value,fp,rot=90,mpn='',datasheet=''):
        return add(kind,ref,{'1':a,'2':b},(*sch,rot),pcb,value,fp,mpn,datasheet)
    m.conn('J102',['AUDIO_L','GND','AUDIO_R'],(25.4,63.5),(5,22),rot=180)
    m.conn('J101',['+5V','GND','HP_ENABLE'],(25.4,205.74),(5,58),rot=180)
    m.conn('J602',['HP_L','HP_R','GND'],(383.54,81.28),(90,24))
    two('FerriteBead','FB101','+5V','+5V_A',(55.88,246.38),(24,66),
        '800R@100MHz / WE 7427501','Ardor_THT:Ferrite_WE_7427501_P15.24mm',
        mpn='Wurth Elektronik 7427501',datasheet='https://www.we-online.com/components/products/datasheet/7427501.pdf')
    two('C_Polarized','C101','+5V_A','GND',(93.98,246.38),(42,66),'100u / 25V',RADIAL,rot=0)
    two('C','C102','+5V_A','GND',(124.46,246.38),(52,66),'100n / 50V X7R','Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm',rot=0)
    for ch,y,py,u in [('L',63.5,20,'U601'),('R',132.08,45,'U602')]:
        s='1' if ch=='L' else '2'
        two('C','C60'+s,'AUDIO_'+ch,'AC_'+ch,(60.96,y),(17,py),'1u / 63V FILM','Capacitor_THT:C_Rect_L7.2mm_W3.5mm_P5.00mm_FKS2_FKP2_MKS2_MKP2')
        two('R','R61'+s,'AC_'+ch,'IN_'+ch,(93.98,y),(28,py),'39k / 1% 0.25W',AXIAL)
        two('R','R62'+s,'IN_'+ch,'GND',(121.92,y+25.4),(34,py+10),'1k / 1% 0.25W',AXIAL,rot=0)
        add('LM386_THT',u,{'1':None,'2':'GND','3':'IN_'+ch,'4':'GND','5':'RAW_'+ch,'6':'+5V_A','7':'BYP_'+ch,'8':None},
            (157.48,y),(44,py),'LM386N-1/NOPB',DIP8,'Texas Instruments LM386N-1/NOPB','https://www.ti.com/lit/ds/symlink/lm386.pdf')
        two('C_Polarized','C63'+s,'BYP_'+ch,'GND',(182.88,y+27.94),(45,py+12),'10u / 25V',RADIAL,rot=0)
        two('C','C64'+s,'+5V_A','GND',(198.12,y+35.56),(49,py-8),'100n / 50V X7R','Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm',rot=0)
        two('C_Polarized','C65'+s,'RAW_'+ch,'COUPLED_'+ch,(226.06,y),(60,py),'470u / 16V','Capacitor_THT:CP_Radial_D8.0mm_P3.50mm')
        two('R','R63'+s,'COUPLED_'+ch,'DRIVE_'+ch,(261.62,y),(72,py),'2.2R / 1% 0.25W',AXIAL)
        two('R','R64'+s,'DRIVE_'+ch,'GND',(287.02,y+25.4),(82,py+10),'1k / 1% 0.25W',AXIAL,rot=0)
        two('R','R65'+s,'RAW_'+ch,'ZOBEL_'+ch,(213.36,y+25.4),(56,py+10),'10R / 1% 0.25W',AXIAL,rot=0)
        two('C','C66'+s,'ZOBEL_'+ch,'GND',(238.76,y+27.94),(62,py+12),'47n / 50V FILM','Capacitor_THT:C_Rect_L7.2mm_W3.5mm_P5.00mm_FKS2_FKP2_MKS2_MKP2',rot=0)
        two('D_TVS','D60'+s,'HP_'+ch,'CHASSIS',(340.36,y+25.4),(87,py-8),'SA5.0CA-E3/54',TVS,
            mpn='Vishay SA5.0CA-E3/54',datasheet='https://www.vishay.com/doc/?88378=')
        m.link('AUDIO_'+ch,('J102','1' if ch=='L' else '3'),('C60'+s,'1'),[(43.18 if ch=='L' else 38.1,y)])
        m.link('AC_'+ch,('C60'+s,'2'),('R61'+s,'1'))
        m.link('IN_'+ch,('R61'+s,'2'),(u,'3'));m.link('IN_'+ch,('R62'+s,'1'),(121.92,y));m.joint(121.92,y)
        m.link('RAW_'+ch,(u,'5'),('C65'+s,'1'));m.link('RAW_'+ch,('R65'+s,'1'),(213.36,y));m.joint(213.36,y)
        m.link('ZOBEL_'+ch,('R65'+s,'2'),('C66'+s,'1'))
        m.link('BYP_'+ch,(u,'7'),('C63'+s,'1'))
        m.link('COUPLED_'+ch,('C65'+s,'2'),('R63'+s,'1'))
        m.link('DRIVE_'+ch,('R63'+s,'2'),('R64'+s,'1'))
        m.label('R63'+s,'2','DRIVE_'+ch)
        m.label('D60'+s,'1','HP_'+ch)
    add('G5V2_THT','K601',{'1':'+5V','16':'RELAY_LOW','4':'HP_L','13':'HP_R','6':'GND','11':'GND','8':'DRIVE_L','9':'DRIVE_R'},
        (335.28,193.04),(75,62),'G5V-2-H1 DC5','Relay_THT:Relay_DPDT_Omron_G5V-2',
        'Omron G5V-2-H1 DC5','https://components.omron.com/system/files/2023-01/datasheet_pdf/K046-E1.pdf')
    for pin,net in [('4','HP_L'),('13','HP_R'),('8','DRIVE_L'),('9','DRIVE_R'),('16','RELAY_LOW')]:m.label('K601',pin,net)
    m.label('J602','1','HP_L');m.label('J602','2','HP_R')
    add('2N3904_THT','Q601',{'1':'GND','2':'RELAY_BASE','3':'RELAY_LOW'},(154.94,198.12),(57,61),
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
    m.text('5V / 150mA budget. 32-300 ohm headphones; input <=1Vrms. Begin at low host volume.\nLM386 pins 1/8 OPEN: gain 20. 39k/1k divider gives ~0.49 unloaded system gain.\n470u output caps: + toward amplifier. Relay OFF grounds both panel channels.\nHP_ENABLE: 3.3V HIGH sources ~5mA; wait >=5s after stable power/audio before HIGH.\nNo charge pump. DIY audio tradeoff: noise/distortion must be measured; not the M1 hi-fi IC.',25.4,276.2,1.05)
    # Reuse the manual BOM writer, without scaling this explicitly placed board.
    Module.save(m)
    write_bom(m)
    return m


if __name__ == '__main__':
    prepare_symbols()
    smd_circuits.Module = THTModule
    for fn in [smd_circuits.midi,smd_circuits.expression,smd_circuits.mixer,smd_circuits.line_out,headphones]:
        fn()
