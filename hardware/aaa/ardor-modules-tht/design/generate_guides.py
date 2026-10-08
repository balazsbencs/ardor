"""Builder guides and exact pin tables from the checked connector contracts."""
from pathlib import Path
import json
import csv
from cad import ROOT

NOTES = {
    'midi-in': '''Use a female five-pin DIN panel socket. Wire numbered DIN contact 4 to J202.1 and contact 5 to J202.2. Leave DIN 1/2/3 and shell unwired; insulate the socket shell. Bond the enclosure to J203.1. Do not treat MIDI contact 5 as ground. Fit the exact H11L1M optocoupler; other optocouplers have different switching thresholds or pinouts.

Supply 5 V / 10 mA and 3.3 V / 5 mA. Set the host UART to 31250 baud, 8-N-1. J101.4 is a 3.3 V output into the host RX input.

Commission: inspect DIP orientation and D201 band, verify 5 V at U201.6 and 3.3 V at the output pull-up, then verify idle UART HIGH. Send known MIDI notes and check UART timing and decoding. Test long cables and real transmitters because the axial protection parts have different capacitance/RF behavior. Functional ground-loop isolation is not safety isolation.''',
    'expression': '''Fit an **Adafruit product 1085 ADS1115, STEMMA QT revision**, using two six-pin 2.54 mm sockets and mating headers, with the digital VDD/GND/SCL/SDA/ADDR/ALRT row toward the lower edge of the outlined module footprint. The VDD corner is the square carrier pad (U301 pad 8). Two header rows are 12.7 mm apart. Earlier Adafruit boards and generic blue modules have different layouts and are not substitutes. See the complete [breakout pin translation](../SOURCES.md#ads1115-breakout-pin-translation). Do not rotate it 180 degrees.

The purchased breakout already contains all small SMD parts. Only its ordinary header pins require soldering. AVDD/AGND and ALRT are deliberately not wired on the carrier. Leave the breakout's ADDR-to-VDD solder jumper **open**. JP302 must have one shunt: 1–2 selects 0x48, 2–3 selects 0x49. These are the same ADS1115 registers/addresses used by M1; no ADC-driver change is required.

Use a passive 10–100 kΩ expression pot; do not connect powered CV. JP301 needs **two** shunts: 1–3 plus 2–4 for tip wiper, or 3–5 plus 4–6 for ring wiper. TRS sleeve goes to J302.3. Change polarity with power off.

Leave **JP303 and JP304 unshunted initially**. The breakout has permanent onboard 10 kΩ SDA/SCL pull-ups; these jumpers control only the additional carrier 2.2 kΩ pair. With a Raspberry Pi's 1.8 kΩ pair, the effective resistance is approximately 1.53 kΩ before adding any other module. Add the carrier pair only after calculating total bus pull-up current and checking cable rise time. Removing the carrier shunts does not remove the breakout's pull-ups.

Supply 3.3 V / 15 mA. Commission without the breakout first: verify rail polarity at the socket, then fit the breakout and check I²C detection at 100 kHz. Select ±4.096 V PGA and 128 SPS, read AIN0 (wiper) and AIN1 (excitation reference), normalize AIN0/AIN1, calibrate endpoints and smooth. Test both polarity settings and open-cable behavior. Do not drive the pedal while the module is unpowered.''',
    'mixer': '''Supply 5 V / 10 mA. Fit **MCP6022-I/P** DIP-8 amplifiers; TLV9002 SOIC parts from M1 do not fit. Use bipolar 10 µF coupling capacitors C401/C402/C407/C408/C409. C101 and C403 are ordinary polarized radial capacitors, positive on pad 1.

Inputs are line-level, no greater than 1 Vrms per channel. Outputs drive loads of at least 10 kΩ. OUT_MONO is `(LEFT + RIGHT) / 2`; all three outputs are AC-coupled. Feeding only one channel gives half its amplitude at the mono output. Feed both channels for a full-level mono source. The board is not an instrument preamp or headphone amplifier.

Commission with no signal: check the filtered 5 V rail and about 2.5 V at VREF and the internal buffer outputs. Check near-zero settled DC on the external outputs. Feed separate 1 kHz left/right tones and verify unity stereo buffers, mono averaging and channel separation. Check clipping, noise and oscillation across the audio band. Keep external harnesses short; exposed audio sockets need entry protection.''',
    'line-out': '''Supply 5 V / 50 mA. Fit MCP6022-I/P, **onsemi 2N3904BU** (E1/B2/C3), and **Omron G5V-1 DC5**. The 470 Ω base resistor means LINE_ENABLE draws approximately 5 mA from a 3.3 V HIGH; confirm the host GPIO can source this current. Fit a banded 1N4148 flyback diode, cathode toward +5 V. C501 is polarized: its positive end goes toward the audio buffer. C401/C502 are bipolar.

The mono input limit is 1 Vrms. The line output requires at least 10 kΩ load; the internal AMP_FEED requires at least 100 kΩ. J502 carries a signal for an external amplifier; it cannot drive a speaker. Relay OFF grounds the panel tip and disconnects the source. Relay ON connects the AC-coupled line signal. This is a mute, not true bypass. The internal amp feed is not relay-muted.

Hold LINE_ENABLE LOW until power/audio have settled; allowing five seconds also settles the line coupling capacitor. Mute before power-down. Commission with the panel unplugged: check the 5 V rail, about 2.5 V internal bias, GPIO base current, relay coil voltage and contact behavior. With the relay off, verify the panel output is grounded. After settling, enable and measure external DC and unity audio into a 10 kΩ dummy load. Check startup/power-down transients before connecting an amplifier.''',
    'headphones': '''Supply 5 V / **150 mA** and use 32–300 Ω headphones. Fit two **LM386N-1/NOPB** DIP-8 amplifiers, **G5V-2-H1 DC5** high-sensitivity DPDT relay, and onsemi 2N3904BU. Do not substitute LM386N-4 or the standard non-H1 relay: their operating conditions differ. Keep LM386 gain pins 1 and 8 open. No SMD IC or charge pump is present.

Input limit is 1 Vrms. The 39 kΩ/1 kΩ divider and nominal gain-20 amplifier give about 0.49 unloaded gain. R631/R632 provide 2.2 Ω output isolation; C651/C652 are 470 µF / 16 V, with positive toward the amplifiers. Their can diameter must be at most 8 mm with 3.5 mm lead spacing. C631/C632 bypass the internal references. The 10 Ω/47 nF output Zobel networks must be fitted. Both amplifiers remain powered when muted.

**Keep HP_ENABLE LOW for at least five seconds after stable supply/audio; mute before powering down.** HIGH is 3.3 V and draws approximately 5 mA. This timing is supplied by the host, not an onboard timer. R641/R642 are 1 kΩ bleeders which let the output coupling capacitors charge while the relay grounds the panel channels. Switching early can produce a loud transient. The relay commons are panel left/right, NC contacts ground them, and NO contacts select the AC-coupled audio.

Commission into separate **32 Ω dummy resistors**, with no headphones connected. Check about 2.5 V at U601/U602 pin 5, correct capacitor polarity, grounded panel outputs when muted, and settled near-zero DC at the relay's audio feeds before enabling. Capture startup, enable, disable and power-down waveforms, including host reset/disconnect. Measure gain, 20 Hz–20 kHz response, clipping, channel separation, hum, hiss and thermal behavior. This DIY design has different noise/distortion characteristics from M1's dedicated headphone IC. Begin listening at low host volume only after these checks pass.'''
}

ACCESSORIES = {'midi-in': [['1', 'Optional DIP-6 socket, 7.62mm width', 'U201'],
             ['1', 'Female 5-pin DIN panel socket, insulated shell', 'Cable to J202'],
             ['2',
              'M2 nonconductive standoff / mounting hardware',
              'H1/H2; choose height and screws for enclosure'],
             ['as needed',
              'Insulated hookup wire and mating host connectors',
              'Match the connector pin guide']],
 'headphones': [['2', 'DIP-8 socket, 7.62mm width', 'U601/U602'],
                ['1', 'Stereo TRS headphone panel socket', 'Cable to J602'],
                ['2', '32 ohm dummy-load resistor, >=0.5W', 'Commissioning only; one per channel'],
                ['2',
                 'M2 nonconductive standoff / mounting hardware',
                 'H1/H2; choose height and screws for enclosure'],
                ['as needed',
                 'Insulated hookup wire and mating host connectors',
                 'Match the connector pin guide']],
 'mixer': [['2', 'DIP-8 socket, 7.62mm width', 'U401/U402'],
           ['2',
            'M2 nonconductive standoff / mounting hardware',
            'H1/H2; choose height and screws for enclosure'],
           ['as needed',
            'Insulated hookup wire and mating host connectors',
            'Match the connector pin guide']],
 'line-out': [['1', 'DIP-8 socket, 7.62mm width', 'U501'],
              ['1', 'TS line-output panel socket', 'Cable to J503'],
              ['2',
               'M2 nonconductive standoff / mounting hardware',
               'H1/H2; choose height and screws for enclosure'],
              ['as needed',
               'Insulated hookup wire and mating host connectors',
               'Match the connector pin guide']],
 'expression': [['2', 'Female 1x06 2.54mm socket strip', 'U301; rows 12.7mm apart'],
                ['2',
                 'Male 1x06 2.54mm header strip',
                 'ADC breakout; may be supplied with Adafruit 1085'],
                ['3', '2.54mm jumper shunt', 'Two for JP301 and one for JP302'],
                ['2',
                 'Optional additional 2.54mm jumper shunt',
                 'JP303/JP304; leave off initially'],
                ['1', 'TRS expression panel socket', 'Cable to J302'],
                ['2',
                 'M2 nonconductive standoff / mounting hardware',
                 'H1/H2; choose height and screws for enclosure'],
                ['as needed',
                 'Insulated hookup wire and mating host connectors',
                 'Match the connector pin guide']]}

for folder in ROOT.iterdir():
    if folder.name not in NOTES:
        continue
    with (folder/'assembly/accessories.csv').open('w', newline='') as out:
        writer = csv.writer(out, lineterminator='\n')
        writer.writerow(['Quantity', 'Purchasing specification', 'Use'])
        writer.writerows(ACCESSORIES[folder.name])
    spec = json.loads((folder/'verification/design.json').read_text())
    guide = json.loads((folder/'verification/connector-guide.json').read_text())
    w,h = spec['size_mm']
    lines = [f"# {spec['title']}", '',f"T1 hand-assembly PCB: {w} × {h} mm. **Unbuilt engineering prototype.**", '',
             '[KiCad project]('+spec['name']+'.kicad_pro) · [Manual BOM](BOM.csv) · [Schematic](review/schematic.pdf) · [Bare-board Gerbers](assembly/'+spec['name']+'-Gerbers.zip)', '',
             NOTES[folder.name], '', '## Connector pin assignments', '',
             'Directions are relative to this board. Standard header square pads identify pin 1. Read reference and pin numbers from the back silkscreen; mirrored CAD exports are labelled as back views.', '']
    for ref,port in guide['ports'].items():
        lines += ['### '+ref+' — '+port['title'], '', '| Pin | Net | Direction | Connect to |', '|---|---|---|---|']
        lines += [f"| {pin['number']} | {pin['net']} | {pin['direction']} | {pin['connect']} |" for pin in port['pins']]
        if port['setup']:
            lines += ['',port['setup']]
        lines += ['']
    lines += ['## Assembly and files', '',
              'Fit low-profile axial parts first, then sockets/headers and radial capacitors. Match diode bands, electrolytic polarity and IC notches to the PCB. Install the ICs or ADC breakout last. H1/H2 are 2.2 mm nonplated M2 mounting holes, not electrical terminals. Check enclosure clearance and capacitor/socket height: these boards are larger than M1.', '',
              'Use the manual BOM and the bare-board Gerber/drill ZIP. There is no SMT assembly order. Front silkscreen shows component references; the front fabrication drawing shows package outlines and orientation; use the BOM for values. Back silk includes port functions and pin numbers. Native checks and package manifests are under `verification/`; they do not replace prototype measurements.', '',
              '[Assembly accessories](assembly/accessories.csv) list sockets, shunts and panel items in addition to the PCB component BOM.', '',
              '[Family guide](../README.md) · [Substitution sources](../SOURCES.md) · [Native validation](verification/validation.json)', '']
    (folder/'README.md').write_text('\n'.join(lines))
