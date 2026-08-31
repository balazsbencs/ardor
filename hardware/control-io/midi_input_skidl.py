"""Ardor Rev B galvanically isolated TRS MIDI input in SKiDL.

This is a functionally equivalent variant of the MIDI section in
``generate-schematic.tsx``. It uses the manufacturer-correct H11L1M pinout
(pin 4 VO, pin 5 GND), which intentionally exposes the pin 4/5 discrepancy in
the current tscircuit version. It stops at the companion-board boundaries
V3V3, DGND, CHASSIS, and MIDI_RX_GPIO instead of adding a fictitious standalone
host connector.

Discrete diode roles:
  D6       differential ESD/transient suppressor across TRS tip and ring
  D13-D16 low-forward-voltage bridge for TRS MIDI Type A/Type B polarity
  D7       reverse-voltage clamp across the H11L1M input LED

Run with SKiDL and the KiCad 9 symbol libraries installed:

    python midi_input_skidl.py

This performs SKiDL ERC and writes ``midi_input_skidl.kicad_sch``.
"""

from skidl import ERC, KICAD9, Net, Part, generate_schematic, set_default_tool


set_default_tool(KICAD9)


def component(
    library: str,
    symbol: str,
    *,
    ref: str,
    value: str,
    footprint: str,
    mpn: str,
) -> Part:
    """Create a fully identified KiCad/SKiDL part."""
    part = Part(
        library,
        symbol,
        ref=ref,
        value=value,
        footprint=footprint,
    )
    part.fields["MPN"] = mpn
    return part


# External and board-boundary nets. The four board-boundary nets are supplied
# or consumed by the complete Ardor board, so standalone ERC must not require
# a local power driver or a second pin on the GPIO output net.
midi_tip_iso = Net("MIDI_TIP_ISO")
midi_ring_iso = Net("MIDI_RING_ISO")
midi_shield = Net("MIDI_SHIELD")
midi_bridge_pos = Net("MIDI_BRIDGE_POS")
midi_bridge_neg = Net("MIDI_BRIDGE_NEG")
midi_led_a = Net("MIDI_LED_A")
midi_rx_raw = Net("MIDI_RX_RAW")
midi_rx_gpio = Net("MIDI_RX_GPIO")
v3v3 = Net("V3V3")
dgnd = Net("DGND")
chassis = Net("CHASSIS")

for boundary_net in (midi_rx_gpio, v3v3, dgnd, chassis):
    boundary_net.do_erc = False


# J6 is the three-wire harness to the isolated 3.5 mm TRS MIDI socket.
j6 = component(
    "Connector_Generic",
    "Conn_01x03",
    ref="J6",
    value="PANEL MIDI HARNESS: TIP / RING / SLEEVE",
    footprint="Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical",
    mpn="B3B-XH-A(LF)(SN)",
)
j6[1] += midi_tip_iso
j6[2] += midi_ring_iso
j6[3] += midi_shield


# Differential TVS: it clamps cable transients tip-to-ring and does not cross
# the optocoupler isolation boundary or dump them into Raspberry Pi ground.
d6 = component(
    "Device",
    "D_TVS",
    ref="D6",
    value="PESD12VL1BA",
    footprint="Diode_SMD:D_SOD-323",
    mpn="PESD12VL1BA",
)
d6[1] += midi_tip_iso
d6[2] += midi_ring_iso


# D13-D16 form a full-wave Schottky bridge. Whichever TRS conductor is
# positive, MIDI_BRIDGE_POS remains positive relative to MIDI_BRIDGE_NEG.
bridge_diodes = {}
for reference in ("D13", "D14", "D15", "D16"):
    bridge_diodes[reference] = component(
        "Device",
        "D_Schottky",
        ref=reference,
        value="PMEG4010CEJ-Q",
        footprint="Diode_SMD:D_SOD-323F",
        mpn="PMEG4010CEJ-Q",
    )

d13 = bridge_diodes["D13"]
d14 = bridge_diodes["D14"]
d15 = bridge_diodes["D15"]
d16 = bridge_diodes["D16"]

d13["A"] += midi_tip_iso
d13["K"] += midi_bridge_pos
d14["A"] += midi_ring_iso
d14["K"] += midi_bridge_pos
d15["A"] += midi_bridge_neg
d15["K"] += midi_tip_iso
d16["A"] += midi_bridge_neg
d16["K"] += midi_ring_iso


# Optocoupler LED current limiter.
r19 = component(
    "Device",
    "R",
    ref="R19",
    value="220R 0.25W",
    footprint="Resistor_SMD:R_1206_3216Metric",
    mpn="TBD_220R_1%_0.25W_1206",
)
r19[1] += midi_bridge_pos
r19[2] += midi_led_a


# H11L1M Schmitt-trigger optocoupler. Pin 3 is intentionally not connected.
u4 = component(
    "Isolator",
    "H11L1",
    ref="U4",
    value="H11L1M",
    footprint="Package_DIP:DIP-6_W7.62mm",
    mpn="H11L1M",
)
u4[1] += midi_led_a       # LED anode
u4[2] += midi_bridge_neg  # LED cathode
u4[3] += NC
u4[4] += midi_rx_raw
u4[5] += dgnd
u4[6] += v3v3


# Reverse clamp across the optocoupler LED. It is normally reverse-biased;
# the bridge provides the normal polarity correction.
d7 = component(
    "Device",
    "D",
    ref="D7",
    value="1N4148WS",
    footprint="Diode_SMD:D_SOD-323",
    mpn="1N4148WS",
)
d7["A"] += midi_bridge_neg
d7["K"] += midi_led_a


# Isolated-side logic: H11L1M has an open-collector-style Schmitt output.
r20 = component(
    "Device",
    "R",
    ref="R20",
    value="4.7k",
    footprint="Resistor_SMD:R_0603_1608Metric",
    mpn="TBD_4K7_1%_0603",
)
r20[1] += v3v3
r20[2] += midi_rx_raw

r21 = component(
    "Device",
    "R",
    ref="R21",
    value="220R",
    footprint="Resistor_SMD:R_0603_1608Metric",
    mpn="TBD_220R_1%_0603",
)
r21[1] += midi_rx_raw
r21[2] += midi_rx_gpio

c10 = component(
    "Device",
    "C",
    ref="C10",
    value="100nF",
    footprint="Capacitor_SMD:C_0603_1608Metric",
    mpn="TBD_100NF_X7R_0603",
)
c10[1] += v3v3
c10[2] += dgnd


# Cable shield references CHASSIS only through a high-value resistor and a
# safety/EMC capacitor. It never joins DGND, preserving functional isolation.
r22 = component(
    "Device",
    "R",
    ref="R22",
    value="1M",
    footprint="Resistor_SMD:R_0603_1608Metric",
    mpn="TBD_1M_1%_0603",
)
r22[1] += midi_shield
r22[2] += chassis

c11 = component(
    "Device",
    "C",
    ref="C11",
    value="1nF 1kV",
    footprint="Capacitor_THT:C_Disc_D7.5mm_W5.0mm_P7.50mm",
    mpn="TBD_1NF_1KV_P7.5MM",
)
c11[1] += midi_shield
c11[2] += chassis


if __name__ == "__main__":
    ERC()
    generate_schematic(
        filepath=".",
        top_name="midi_input_skidl",
        title="Ardor Rev B — Galvanically Isolated TRS MIDI Input",
        flatness=1.0,
        auto_stub=True,
    )
