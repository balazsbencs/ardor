# Stereo buffer and mono mixer: assembly

The board has factory-installed SMT parts.
The square connector pad is pin 1.
IN identifies an input to this board.
OUT identifies an output from this board.
I/O identifies a pin with both functions.
GND is the common 0 V connection.

[Assembly drawing](review/assembly-reference.pdf) · [All pin functions](README.md#connector-pins)

## Procedure

1. Disconnect the power supply.
2. Solder J101, J102 and J103.
3. Connect the wires to the pins in the table.
4. Keep each audio input at or below 1 Vrms.
5. Use an output load of 10 kΩ or more.
6. Do not connect headphones or a speaker to J103.
7. Connect the power supply.
8. Apply audio to J102.
9. Do an audio test at J103.

## Wire connections

| Connector | Pin | Connection |
|---|---|---|
| J101 | 1 | Regulated 5 V: power IN |
| J101 | 2 | GND: 0 V |
| J102 | 1 | Source left output: audio IN |
| J102 | 2 | Source GND: 0 V |
| J102 | 3 | Source right output: audio IN |
| J103 | 1 | Left line input: audio OUT |
| J103 | 2 | GND: 0 V |
| J103 | 3 | Right line input: audio OUT |
| J103 | 4 | Mono line input: audio OUT |

Use the pin numbers, not the left-to-right position in a picture.
The mono output is the average of the left and right inputs.
