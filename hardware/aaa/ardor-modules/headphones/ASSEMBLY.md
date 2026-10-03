# Headphone output: assembly

The board has factory-installed SMT parts.
The square connector pad is pin 1.
IN identifies an input to this board.
OUT identifies an output from this board.
I/O identifies a pin with both functions.
GND is the common 0 V connection.

[Assembly drawing](review/assembly-reference.pdf) · [All pin functions](README.md#connector-pins)

## Procedure

1. Disconnect the power supply.
2. Solder J101, J102 and J602.
3. Connect the wires to the pins in the table.
4. Keep each audio input at or below 1 Vrms.
5. Set the host control output to 0 V.
6. Connect the power supply.
7. When the audio source is stable, set the host control output to 3.3 V.
8. Set the audio source to a low volume.
9. Connect headphones with an impedance of 32 Ω or more.

## Wire connections

| Connector | Pin | Connection |
|---|---|---|
| J101 | 1 | Regulated 5 V: power IN |
| J101 | 2 | Host GND: 0 V |
| J101 | 3 | Host control output: enable IN |
| J102 | 1 | Source left output: audio IN |
| J102 | 2 | Source GND: 0 V |
| J102 | 3 | Source right output: audio IN |
| J602 | 1 | TRS tip: left audio OUT |
| J602 | 2 | TRS ring: right audio OUT |
| J602 | 3 | TRS sleeve: GND |

Use the pin numbers, not the left-to-right position in a picture.
