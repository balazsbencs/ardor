# Language reference for the short assembly guides

The five `ASSEMBLY.md` files use [ASD-STE100 Issue 9](https://www.asd-ste100.org/assets/files/ASD-STE100_ISSUE9.pdf) as the writing reference. Procedures use active commands, one instruction per sentence, and no more than 20 words per sentence. Descriptions use one subject per sentence. Tables use short terms with consistent meanings. General verbs and their meanings were reviewed against the official dictionary. This is an author review, not third-party certification.

The following subject-specific terms have the same meaning in all five guides. They are technical nouns under rule 1.5. Component identifiers, connector identifiers, numeric settings and units retain their engineering meanings.

| Technical noun | Meaning |
|---|---|
| ADC | Analog-to-digital converter; converts pedal voltage into a number. |
| audio input / audio output | A connection that receives / sends an audio signal. |
| audio source | A device that supplies an audio signal. |
| case bond | Electrical connection to the metal enclosure. |
| clock | Timing signal from the I2C host. |
| DIN contact | A numbered electrical contact in the MIDI socket. |
| enable | Control input: 0 V stops the output; 3.3 V starts the output. |
| expression pedal | Passive pedal with a potentiometer. |
| factory-installed SMT parts | Surface-mount components soldered by the assembly supplier. |
| GND | Common zero-volt electrical return. |
| host | Computer or controller connected to the module. |
| I2C / SDA / SCL | Two-wire control bus / data wire / clock wire. |
| IN / OUT / I/O | Into this board / from this board / both directions. |
| jumper / shunt | Pin group for configuration / removable link across two jumper pins. |
| MIDI | Digital control-message interface for musical devices. |
| pin / pad | Numbered component terminal / its copper solder land. |
| pin header | Connector with metal pins for wires or a shunt. |
| power supply | Device that supplies regulated DC voltage. |
| pull-up | Resistor connection that holds a signal at its HIGH voltage when inactive. |
| relay | Electrically controlled switch. |
| ring / tip / sleeve | Middle / end / outer ground contact of a TRS socket. |
| TRS / TS | Three-contact / two-contact audio socket. |
| UART / RX | Serial-data interface / host receive input. |
| volume | Audio signal level. |
| wiper | Moving potentiometer contact that supplies pedal position voltage. |

`Solder` is the subject-specific technical verb for joining component terminals to copper pads with solder (rule 1.12, manufacturing processes). It does not mean “connect a cable.” Labels retain short engineering terms; their full meanings appear in the pin tables and pin cards. The general engineering READMEs contain detailed design material and are not represented as STE-controlled procedures.
