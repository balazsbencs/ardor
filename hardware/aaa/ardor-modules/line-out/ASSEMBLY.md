# Mono line output: assembly

The board has factory-installed SMT parts.
The square connector pad is pin 1.
IN identifies an input to this board.
OUT identifies an output from this board.
I/O identifies a pin with both functions.
GND is the common 0 V connection.

[Assembly drawing](review/assembly-reference.pdf) · [All pin functions](README.md#connector-pins)

## Procedure

1. Disconnect the power supply.
2. Solder J101, J102, J503 and J502.
3. Solder K501 with pin 1 at the square K501 pad.
4. Connect the wires to the pins in the table.
5. Do not connect a speaker to J502.
6. Keep the audio input at or below 1 Vrms.
7. Set the host control output to 0 V.
8. Connect the power supply.
9. When the audio source is stable, set the host control output to 3.3 V.
10. Do an audio test at J503.

## Wire connections

| Connector | Pin | Connection |
|---|---|---|
| J101 | 1 | Regulated 5 V: power IN |
| J101 | 2 | Host GND: 0 V |
| J101 | 3 | Host control output: enable IN |
| J102 | 1 | Mono source output: audio IN |
| J102 | 2 | Source GND: 0 V |
| J503 | 1 | TS tip: line audio OUT |
| J503 | 2 | TS sleeve: GND |
| J502 | 1 | Amplifier signal input: audio OUT |
| J502 | 2 | Amplifier GND: 0 V |

Use the pin numbers, not the left-to-right position in a picture.
The relay stops the line output when enable is LOW. J502 remains an audio output.
