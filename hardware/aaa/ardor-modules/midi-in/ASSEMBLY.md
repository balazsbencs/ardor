# MIDI input: assembly

The board has factory-installed SMT parts.
The square connector pad is pin 1.
IN identifies an input to this board.
OUT identifies an output from this board.
I/O identifies a pin with both functions.
GND is the common 0 V connection.

[Assembly drawing](review/assembly-reference.pdf) · [All pin functions](README.md#connector-pins)

## Procedure

1. Disconnect the power supply.
2. Solder J101, J202 and J203.
3. Solder U201 with pin 1 at the square U201 pad.
4. Connect the wires to the pins in the table.
5. Connect J203 pin 1 to the metal enclosure.
6. Do not connect DIN contacts 1, 2, 3 or the DIN shell.
7. Use an insulated DIN socket.
8. Do not connect a wire to U201 pin 3.
9. Set the host UART to 31250 baud, 8 data bits, no parity and one stop bit.
10. Connect the power supply.
11. Send a MIDI message to the DIN socket.

## Wire connections

| Connector | Pin | Connection |
|---|---|---|
| J202 | 1 | DIN contact 4: MIDI input |
| J202 | 2 | DIN contact 5: MIDI return, not GND |
| J101 | 1 | Regulated 5 V: power IN |
| J101 | 2 | Regulated 3.3 V: power IN |
| J101 | 3 | Host GND: 0 V |
| J101 | 4 | Host UART RX: signal OUT |
| J203 | 1 | Metal enclosure: case bond |
| J203 | 2 | GND: 0 V |

Use the pin numbers, not the left-to-right position in a picture.
