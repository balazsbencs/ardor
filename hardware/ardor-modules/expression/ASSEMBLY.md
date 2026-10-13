# Expression pedal: assembly

The board has factory-installed SMT parts.
The square connector pad is pin 1.
IN identifies an input to this board.
OUT identifies an output from this board.
I/O identifies a pin with both functions.
GND is the common 0 V connection.

[Assembly drawing](review/assembly-reference.pdf) · [All pin functions](README.md#connector-pins)

## Procedure

1. Disconnect the power supply.
2. Solder J101, J302, JP301, JP302, JP303 and JP304.
3. For a tip wiper, install JP301 shunts across pins 1–3 and 2–4.
4. For a ring wiper, install JP301 shunts across pins 3–5 and 4–6.
5. Install a JP302 shunt across pins 1–2 for address 0x48.
6. If you need address 0x49, move the JP302 shunt to pins 2–3.
7. If the host has I2C pull-ups, remove the JP303 and JP304 shunts.
8. If the host has no I2C pull-ups, install JP303 and JP304 shunts across pins 1–2.
9. Connect the wires to the pins in the table.
10. Use a passive 10 kΩ–100 kΩ expression pedal.
11. Do not connect a powered pedal.
12. Do not connect a wire to U301 pin 2.
13. Connect the power supply.
14. As you move the pedal, read ADC inputs AIN0 and AIN1.

## Wire connections

| Connector | Pin | Connection |
|---|---|---|
| J101 | 1 | Regulated 3.3 V: power IN |
| J101 | 2 | Host GND: 0 V |
| J101 | 3 | Host SDA: data I/O |
| J101 | 4 | Host SCL: clock IN |
| J302 | 1 | TRS tip: pedal I/O |
| J302 | 2 | TRS ring: pedal I/O |
| J302 | 3 | TRS sleeve: GND |

Use the pin numbers, not the left-to-right position in a picture.
JP301 pins 1 and 2 form the first row. Pins 3–4 and 5–6 form the next rows.
