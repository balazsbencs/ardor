"""Explicit MIDI loop-to-logic spacing rule, including filled planes."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
folder=ROOT/'midi-in';mapping=json.loads((folder/'verification/logical-net-map.json').read_text())
isolated=[mapping[n] for n in ['MIDI_4','MIDI_5','MIDI_4_F','MIDI_A','MIDI_K']]
a='('+' || '.join("A.NetName == '"+n+"'" for n in isolated)+')'
b=' && '.join("B.NetName != '"+n+"'" for n in isolated)
condition=a+' && '+b+" && B.NetName != '/CHASSIS' && B.NetName != '' && B.NetName != 'unconnected-(U201-Pad3)'"
(folder/'Ardor_MIDI.kicad_dru').write_text('(version 1)\n(rule "MIDI loop to logic: functional isolation"\n (condition '+json.dumps(condition)+')\n (constraint clearance (min 3mm)))\n')
