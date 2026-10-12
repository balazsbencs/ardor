"""Human-facing connector contract. Directions are relative to this module.

A pin's net is checked against native CAD; prose is never used to rename nets.
Jumper terminals describe configuration roles rather than pretend cable ports.
"""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
def pin(n,net,short,direction,meaning,connect):
    return {'number':n,'net':net,'short':short,'direction':direction,'meaning':meaning,'connect':connect}
def port(title,silk,pins,setup=''):
    return {'title':title,'silk':silk,'pins':pins,'setup':setup}
def ground(n,net='GND',connect='Host or cable ground / 0 V.'):
    return pin(n,net,'GND 0V','GROUND','Common 0 V return; not a signal or positive supply.',connect)
def power(n,net,volts):
    return pin(n,net,volts+' IN','POWER INPUT','Feed regulated '+volts+' into the module. This pin does not supply power.','Positive output of the matching regulated supply.')
def audio(n,net,channel,output=False,connect=''):
    return pin(n,net,channel+(' OUT' if output else ' IN'),'OUTPUT' if output else 'INPUT',channel+' audio '+('leaves' if output else 'enters')+' the module.',connect)
def enable(n,net):
    return pin(n,net,'ENABLE IN','CONTROL INPUT','3.3 V HIGH = on; 0 V LOW or disconnected = off.','Host control output. Keep LOW until audio and supply are stable.')
INTERFACES={
'midi-in':{
 'J202':port('MIDI INPUT FROM DIN SOCKET','MIDI IN',[
  pin(1,'MIDI_4','DIN4 IN','MIDI INPUT','MIDI current-loop input, DIN contact 4.','Female DIN socket numbered contact 4.'),
  pin(2,'MIDI_5','DIN5 IN','MIDI INPUT','MIDI current-loop return, DIN contact 5; NOT GND.','Female DIN socket numbered contact 5.')]),
 'J101':port('POWER INPUT + UART OUTPUT TO HOST','PWR IN / UART OUT',[
  power(1,'+5V','5V'),power(2,'+3V3','3V3'),ground(3),
  pin(4,'MIDI_RX','UART OUT','OUTPUT','3.3 V decoded MIDI logic leaves the module; this is not a DIN MIDI output.','Host UART RX input, 31250 baud, 8-N-1. Do not connect to host TX.')]),
 'J203':port('METAL CASE BOND + GROUND','CASE BOND',[
  pin(1,'CHASSIS','CASE','BOND','Enclosure/chassis connection, locally joined to GND through R101.','Aluminium enclosure bonding point; insulate the MIDI DIN shell.'),ground(2)])},
'expression':{
 'J302':port('PASSIVE PEDAL INPUT / EXCITATION OUTPUT','PASSIVE PEDAL',[
  pin(1,'EXP_TIP','TIP I/O','INPUT / OUTPUT','TRS tip: pedal wiper INPUT or excitation OUTPUT, selected by JP301.','Passive expression socket tip; not a powered CV pedal.'),
  pin(2,'EXP_RING','RING I/O','INPUT / OUTPUT','TRS ring: excitation OUTPUT or pedal wiper INPUT, selected by JP301.','Passive expression socket ring; not a powered CV pedal.'),ground(3,connect='TRS sleeve; pedal pot ground and enclosure bond.')]),
 'J101':port('3V3 POWER INPUT + I2C TO HOST','PWR IN / I2C',[
  power(1,'+3V3','3V3'),ground(2),
  pin(3,'SDA','DATA I/O','INPUT / OUTPUT','I2C SDA data travels both ways; 3.3 V bus only.','Host I2C SDA data pin.'),
  pin(4,'SCL','CLOCK IN','INPUT','I2C clock comes from the host into this module.','Host I2C SCL clock pin.')]),
 'JP301':port('JUMPER ONLY: PEDAL POLARITY','PEDAL JUMPER',[
  pin(1,'EXP_TIP','TIP','JUMPER','Tip contact side of polarity selector.','Fit a shunt to pin 3 for tip-wiper mode.'),
  pin(2,'EXP_RING','RING','JUMPER','Ring contact side of polarity selector.','Fit a shunt to pin 4 for tip-wiper mode.'),
  pin(3,'EXP_WIPER','PEDAL IN','INPUT / JUMPER','Pedal wiper signal enters the ADC circuit.','Shunt 1-3 for tip wiper, or 3-5 for ring wiper.'),
  pin(4,'EXP_EXC','EXC OUT','OUTPUT / JUMPER','Current-limited excitation goes out to the passive pedal.','Shunt 2-4 for tip wiper, or 4-6 for ring wiper.'),
  pin(5,'EXP_RING','RING','JUMPER','Ring contact side of polarity selector.','Fit a shunt to pin 3 for ring-wiper mode.'),
  pin(6,'EXP_TIP','TIP','JUMPER','Tip contact side of polarity selector.','Fit a shunt to pin 4 for ring-wiper mode.')], 'TIP: 1-3 + 2-4; RING: 3-5 + 4-6. Use TWO shunts; no external cable.'),
 'JP302':port('JUMPER ONLY: ADC ADDRESS','ADDR JUMPER',[
  pin(1,'GND','LOW GND','JUMPER','Ground/LOW address selection terminal.','Shunt 1-2 selects address 0x48.'),
  pin(2,'ADDR','SELECT','CONTROL / JUMPER','ADC address selection input; do not leave floating.','Fit ONE shunt: 1-2 or 2-3.'),
  pin(3,'+3V3_A','HIGH 3V3','JUMPER','3.3 V/HIGH address selection terminal, not a power connector.','Shunt 2-3 selects address 0x49.')], '1-2: 0x48; 2-3: 0x49. One shunt required; no external cable.'),
 'JP303':port('JUMPER ONLY: SDA PULLUP','DATA PULLUP',[
  pin(1,'SDA_PULL','PULLUP','JUMPER','3.3 V through the local 2.2k pull-up resistor.','Shunt 1-2 only if the host has no SDA pull-up.'),
  pin(2,'SDA','DATA I/O','INPUT / OUTPUT / JUMPER','SDA data side of the pull-up jumper.','Same shunt; this is not a host cable connector.')], 'Fit 1-2 to enable. Remove if host already has a pull-up.'),
 'JP304':port('JUMPER ONLY: SCL PULLUP','CLOCK PULLUP',[
  pin(1,'SCL_PULL','PULLUP','JUMPER','3.3 V through the local 2.2k pull-up resistor.','Shunt 1-2 only if the host has no SCL pull-up.'),
  pin(2,'SCL','CLOCK IN','INPUT / JUMPER','SCL clock side of the pull-up jumper.','Same shunt; this is not a host cable connector.')], 'Fit 1-2 to enable. Remove if host already has a pull-up.')},
'mixer':{
 'J101':port('5V POWER INPUT','POWER IN',[power(1,'+5V','5V'),ground(2)]),
 'J102':port('STEREO AUDIO INPUT FROM SOURCE','AUDIO IN',[
  audio(1,'AUDIO_L','LEFT',connect='DAC/codec left audio output, <=1 Vrms.'),ground(2),audio(3,'AUDIO_R','RIGHT',connect='DAC/codec right audio output, <=1 Vrms.')]),
 'J103':port('BUFFERED STEREO + MONO AUDIO OUTPUT','AUDIO OUT',[
  audio(1,'OUT_L','LEFT',True,'Left line input of another device, >=10k load.'),ground(2),audio(3,'OUT_R','RIGHT',True,'Right line input of another device, >=10k load.'),audio(4,'OUT_MONO','MONO',True,'Mono line input, >=10k load. Signal is (L+R)/2.')])},
'line-out':{
 'J101':port('5V POWER + ENABLE INPUT','PWR + ENABLE IN',[power(1,'+5V','5V'),ground(2),enable(3,'LINE_ENABLE')]),
 'J102':port('MONO AUDIO INPUT FROM SOURCE','AUDIO IN',[audio(1,'AUDIO_IN','AUDIO',connect='Mono DAC/codec audio output, <=1 Vrms.'),ground(2)]),
 'J503':port('LINE AUDIO OUTPUT TO TS SOCKET','LINE OUT',[audio(1,'LINE_JACK','LINE',True,'TS socket tip; line load >=10k. Grounded when relay is off.'),ground(2,connect='TS socket sleeve; enclosure bond.')]),
 'J502':port('AUDIO OUTPUT TO AMP INPUT','AMP AUDIO OUT',[audio(1,'AMP_FEED','AUDIO',True,'Signal input of a separate amplifier, >=100k. NOT a speaker.'),ground(2)], 'Audio signal only. Never connect a speaker here.')},
'headphones':{
 'J101':port('5V POWER + ENABLE INPUT','PWR + ENABLE IN',[power(1,'+5V','5V'),ground(2),enable(3,'HP_ENABLE')]),
 'J102':port('STEREO AUDIO INPUT FROM SOURCE','AUDIO IN',[audio(1,'AUDIO_L','LEFT',connect='DAC/codec left audio output, <=1 Vrms.'),ground(2),audio(3,'AUDIO_R','RIGHT',connect='DAC/codec right audio output, <=1 Vrms.')]),
 'J602':port('STEREO HEADPHONE AUDIO OUTPUT','HEADPHONE OUT',[audio(1,'HP_L','LEFT',True,'TRS socket tip; headphones >=32 ohm.'),audio(2,'HP_R','RIGHT',True,'TRS socket ring; headphones >=32 ohm.'),ground(3,connect='TRS socket sleeve; enclosure bond.')])}}
UNUSED={'midi-in':{'U201.3':'NC - LEAVE OPEN'},'expression':{'U301.2':'UNUSED ALERT - LEAVE OPEN'},'mixer':{},'line-out':{},'headphones':{}}
def contract(slug,spec):
    ports=INTERFACES[slug];external={k:v for k,v in spec['pins'].items() if k.startswith('J')}
    described={ref+'.'+str(p['number']):p['net'] for ref,c in ports.items() for p in c['pins']}
    assert external==described,(slug,'Connector description must match every physical pin')
    assert set(UNUSED[slug])==set(spec['nc'])
    return {'viewpoint':'Directions are relative to this board; square pad is pin 1.','abbreviations':{'IN':'Into this board','OUT':'Out of this board','I/O':'Both directions','GND':'Common 0 V return','NC':'Unused; do not connect'},'ports':ports,'unused_pins':UNUSED[slug],'mounting_holes':'H1/H2 are M2 mounting holes, not electrical connections. Small vias are not user connectors.'}
