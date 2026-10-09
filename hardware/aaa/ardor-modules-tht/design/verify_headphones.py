"""Review actual OPA1656 netlist, component values and retained local copper.

Ideal transfer/bias calculations are not amplifier simulation or bench evidence.
The native ERC/DRC and separate physical-pad checks remain mandatory.
"""
from pathlib import Path
import json
import math
import re
import xml.etree.ElementTree as ET
import pcbnew as p
from shapely.geometry import LineString
from shapely.ops import unary_union

ROOT = Path(__file__).resolve().parents[1]
folder = ROOT/'headphones'
xml = ET.parse(folder/'verification/netlist.xml')
values = {c.attrib['ref']:c.findtext('value') for c in xml.findall('.//components/comp')}
nets = {n.attrib['ref']+'.'+n.attrib['pin']:net.attrib['name']
        for net in xml.findall('.//nets/net') for n in net.findall('node')}
def same(*pins):
    assert len({nets[v] for v in pins})==1, pins
    return nets[pins[0]]
def distinct(*pins):
    assert len({nets[v] for v in pins})==len(pins), pins
def value(ref):
    number,suffix = re.match(r'([\d.]+)([RkMunp]?)', values[ref]).groups()
    return float(number)*{'':1,'R':1,'k':1e3,'M':1e6,'u':1e-6,'n':1e-9,'p':1e-12}[suffix]
assert values['U601']=='OPA1656ID' and 'U602' not in values
same('J101.1','FB101.1','K601.1','D603.1')
same('FB101.2','U601.8','C641.1','R621.1')
same('J101.2','U601.4','C641.2','R622.2','C631.2','C632.2','K601.6','K601.11')
same('R621.2','R622.1','C631.1','C632.1','U601.3','U601.5')
results = {}
for ch,prefix,inp,inv,out,panel,no in [('left','1','1','2','1','1','8'),('right','2','3','6','7','2','9')]:
    ci,ri,rf,cf,rs,co,rb = ['C60'+prefix,'R61'+prefix,'R65'+prefix,'C66'+prefix,'R63'+prefix,'C65'+prefix,'R64'+prefix]
    same('J102.'+inp,ci+'.1')
    same(ci+'.2',ri+'.1')
    same(ri+'.2',rf+'.2',cf+'.2','U601.'+inv)
    same('U601.'+out,rf+'.1',cf+'.1',rs+'.1')
    same(rs+'.2',co+'.1')
    same(co+'.2',rb+'.1','K601.'+no)
    same('J602.'+panel,'K601.'+('4' if ch=='left' else '13'),'D60'+prefix+'.1')
    distinct('U601.'+inv,'U601.'+out,co+'.1',co+'.2','J602.'+panel)
    same(rb+'.2','J101.2')
    assert 'BIPOLAR' in values[ci]
    assert value(ri)==20000 and value(rf)==10000 and value(rs)==10
    assert all(math.isclose(value(ref),expected) for ref,expected in [(ci,10e-6),(co,470e-6),(cf,100e-12)])
    load = 1/(1/32+1/value(rb))
    gain = -value(rf)/value(ri)*load/(load+value(rs))
    def response(f):
        s = 2j*math.pi*f
        return -value(rf)/(1+s*value(rf)*value(cf))/(value(ri)+1/(s*value(ci)))*load/(value(rs)+load+1/(s*value(co)))
    results[ch] = {'unloaded_midband_gain':-value(rf)/value(ri),
                   'loaded_midband_gain_32ohm':gain,'ideal_power_mW_at_1Vrms_32ohm':gain**2/32*1000,
                   'peak_output_current_mA_including_bleeder':math.sqrt(2)*abs(gain)/load*1000,
                   'input_highpass_Hz':1/(2*math.pi*value(ri)*value(ci)),
                   'output_highpass_Hz_32ohm':1/(2*math.pi*(load+value(rs))*value(co)),
                   'feedback_lowpass_Hz':1/(2*math.pi*value(rf)*value(cf)),
                   'response_relative_midband_dB':{str(f):20*math.log10(abs(response(f)/gain)) for f in [20,1000,20000]}}
# Worst positive bias at minimum supply and opposing 1% divider tolerances.
rail = 4.75-.15*.020
upper_bias = rail*value('R622')*1.01/(value('R621')*.99+value('R622')*1.01)
margin = rail-2.25-upper_bias
assert margin>.09
board = p.LoadBoard(str(folder/'Ardor_Headphones_THT.kicad_pcb'))
placement = p.LoadBoard(str(folder/'routing/placement.kicad_pcb'))
def line(track):
    return LineString([(p.ToMM(v.x),p.ToMM(v.y)) for v in [track.GetStart(),track.GetEnd()]])
by_net = {}
for track in board.GetTracks():
    if not isinstance(track,p.PCB_VIA):
        by_net.setdefault((track.GetNetname(),track.GetLayer()),[]).append(line(track))
fixed = list(placement.GetTracks())
assert len(fixed)==17
for track in fixed:
    assert track.GetLayer()==p.F_Cu and track.IsLocked()
    # Router splitting/reversal and nanometre coordinate rounding are allowed;
    # every fixed local segment must still be present on the front layer.
    copper = unary_union(by_net[(track.GetNetname(),track.GetLayer())]).buffer(.00001)
    assert line(track).difference(copper).length<.00001
pads = {f.GetReference()+'.'+a.GetNumber():a.GetPosition() for f in board.GetFootprints() for a in f.Pads()}
bypass_length = math.dist(tuple(pads['U601.8']),tuple(pads['C641.1']))/1e6
assert bypass_length<4.0
assert any({tuple(t.GetStart()),tuple(t.GetEnd())}=={tuple(pads['U601.8']),tuple(pads['C641.1'])} for t in fixed)
report = {'result':'PASS','source':'Native netlist values/connectivity and physical PCB copper',
          'ideal_linear_calculations_only':True,'hardware_tested':False,
          'minimum_common_mode_upper_margin_V_5pct_supply_1pct_divider':margin,
          'fixed_local_front_layer_segments_preserved':len(fixed),
          'decoupling_supply_track_length_mm':bypass_length,
          'channels':results}
(folder/'verification/headphone-circuit.json').write_text(json.dumps(report,indent=2)+'\n')
print('OPA1656 circuit/package and local-copper review PASS; ideal 32-ohm gain',results['left']['loaded_midband_gain_32ohm'])
