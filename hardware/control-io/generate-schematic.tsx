import React from "react"
import { Circuit } from "tscircuit"
import { CircuitJsonToKicadSchConverter } from "circuit-json-to-kicad"
import { writeFileSync } from "node:fs"

const left = (pins: number[]) => ({
  leftSide: { pins, direction: "top-to-bottom" as const },
})

const split = (leftPins: number[], rightPins: number[]) => ({
  leftSide: { pins: leftPins, direction: "top-to-bottom" as const },
  rightSide: { pins: rightPins, direction: "top-to-bottom" as const },
})

async function main() {
  const circuit = new Circuit()

  circuit.add(
    <board width="180mm" height="110mm">
      <schematictext
        text="ARDOR CONTROL I/O — TRS-A MIDI IN + EXPRESSION ADC"
        schX={-7.4}
        schY={5.0}
        fontSize={0.35}
      />
      <schematictext
        text="MIDI IN: 3.5 mm TRS Type A, galvanically isolated"
        schX={-7.4}
        schY={4.3}
        fontSize={0.23}
      />
      <schematictext
        text="EXPRESSION: passive 6.35 mm TRS pedal, hardware polarity selector, ADS1115"
        schX={-7.4}
        schY={0.95}
        fontSize={0.23}
      />
      <schematictext
        text="HOST: Raspberry Pi GPIO header"
        schX={-7.4}
        schY={-3.65}
        fontSize={0.23}
      />

      <chip
        name="J1"
        manufacturerPartNumber="3.5mm TRS MIDI IN"
        pinLabels={{ pin1: "TIP", pin2: "RING", pin3: "SLEEVE" }}
        schPinArrangement={left([1, 2, 3])}
        schX={-6.4}
        schY={2.8}
      />
      <resistor name="R1" resistance="220" schX={-4.25} schY={3.2} />
      <diode
        name="D1"
        manufacturerPartNumber="1N4148"
        schX={-3.5}
        schY={1.85}
      />
      <chip
        name="U1"
        manufacturerPartNumber="6N138"
        pinLabels={{
          pin2: "LED_A",
          pin3: "LED_K",
          pin5: "GND",
          pin6: "VO",
          pin7: "VB",
          pin8: "VCC",
        }}
        schPinArrangement={split([2, 3], [8, 7, 6, 5])}
        noConnect={["pin1", "pin4"]}
        schX={-1.15}
        schY={2.8}
      />
      <resistor name="R2" resistance="10k" schX={0.7} schY={1.75} />
      <resistor name="R3" resistance="4.7k" schX={3.15} schY={3.0} />
      <capacitor name="C1" capacitance="100nF" schX={0.55} schY={4.05} />

      <trace from="J1.TIP" to="net.MIDI_SINK_TIP" />
      <trace from="J1.RING" to="net.MIDI_SOURCE_RING" />
      <trace from="J1.SLEEVE" to="net.SHIELD" />
      <trace from="R1.pin1" to="net.MIDI_SOURCE_RING" />
      <trace from="R1.pin2" to="net.MIDI_LED_A" />
      <trace from="U1.LED_A" to="net.MIDI_LED_A" />
      <trace from="U1.LED_K" to="net.MIDI_SINK_TIP" />
      <trace from="D1.anode" to="net.MIDI_SINK_TIP" />
      <trace from="D1.cathode" to="net.MIDI_LED_A" />
      <trace from="U1.VCC" to="net.V5" />
      <trace from="U1.GND" to="net.GND" />
      <trace from="U1.VB" to="net.OPTO_BASE" />
      <trace from="R2.pin1" to="net.OPTO_BASE" />
      <trace from="R2.pin2" to="net.GND" />
      <trace from="U1.VO" to="net.MIDI_RX" />
      <trace from="R3.pin1" to="net.V3V3" />
      <trace from="R3.pin2" to="net.MIDI_RX" />
      <trace from="C1.pin1" to="net.V5" />
      <trace from="C1.pin2" to="net.GND" />

      <chip
        name="J2"
        manufacturerPartNumber="6.35mm TRS EXPRESSION"
        pinLabels={{ pin1: "TIP", pin2: "RING", pin3: "SLEEVE" }}
        schPinArrangement={left([1, 2, 3])}
        schX={-6.4}
        schY={-0.9}
      />
      <chip
        name="SW1A"
        manufacturerPartNumber="DPDT POLE A"
        pinLabels={{ pin1: "COM", pin2: "POS_A", pin3: "POS_B" }}
        schPinArrangement={split([1], [2, 3])}
        schX={-4.25}
        schY={-0.25}
      />
      <chip
        name="SW1B"
        manufacturerPartNumber="DPDT POLE B"
        pinLabels={{ pin1: "COM", pin2: "POS_A", pin3: "POS_B" }}
        schPinArrangement={split([1], [2, 3])}
        schX={-4.25}
        schY={-1.55}
      />
      <resistor name="R4" resistance="1k" schX={-1.95} schY={0.1} />
      <resistor name="R5" resistance="4.7k" schX={-1.95} schY={-1.85} />
      <capacitor name="C2" capacitance="100nF" schX={0.0} schY={-2.6} />
      <diode
        name="D2"
        manufacturerPartNumber="BAT54"
        schottky
        schX={1.25}
        schY={-2.6}
      />
      <diode
        name="D3"
        manufacturerPartNumber="BAT54"
        schottky
        schX={1.25}
        schY={-1.55}
      />
      <chip
        name="U2"
        manufacturerPartNumber="ADS1115 (0x48)"
        pinLabels={{
          pin1: "ADDR",
          pin2: "ALERT",
          pin3: "GND",
          pin4: "AIN0",
          pin5: "AIN1",
          pin6: "AIN2",
          pin7: "AIN3",
          pin8: "VDD",
          pin9: "SDA",
          pin10: "SCL",
        }}
        schPinArrangement={split([4, 5, 6, 7, 1], [8, 9, 10, 2, 3])}
        schX={3.35}
        schY={-0.5}
      />
      <resistor name="R6" resistance="10k" schX={5.5} schY={0.15} />
      <capacitor name="C3" capacitance="100nF" schX={5.5} schY={-1.45} />

      <trace from="J2.TIP" to="net.EXP_TIP" />
      <trace from="J2.RING" to="net.EXP_RING" />
      <trace from="J2.SLEEVE" to="net.GND" />
      <trace from="SW1A.COM" to="net.EXP_TIP" />
      <trace from="SW1A.POS_A" to="net.VEXP" />
      <trace from="SW1A.POS_B" to="net.ADC_SENSE" />
      <trace from="SW1B.COM" to="net.EXP_RING" />
      <trace from="SW1B.POS_A" to="net.ADC_SENSE" />
      <trace from="SW1B.POS_B" to="net.VEXP" />
      <trace from="R4.pin1" to="net.V3V3" />
      <trace from="R4.pin2" to="net.VEXP" />
      <trace from="R5.pin1" to="net.ADC_SENSE" />
      <trace from="R5.pin2" to="net.ADC_FILTER" />
      <trace from="C2.pin1" to="net.ADC_FILTER" />
      <trace from="C2.pin2" to="net.GND" />
      <trace from="D2.anode" to="net.GND" />
      <trace from="D2.cathode" to="net.ADC_FILTER" />
      <trace from="D3.anode" to="net.ADC_FILTER" />
      <trace from="D3.cathode" to="net.V3V3" />
      <trace from="U2.AIN0" to="net.ADC_FILTER" />
      <trace from="U2.AIN1" to="net.GND" />
      <trace from="U2.AIN2" to="net.GND" />
      <trace from="U2.AIN3" to="net.GND" />
      <trace from="U2.ADDR" to="net.GND" />
      <trace from="U2.VDD" to="net.V3V3" />
      <trace from="U2.GND" to="net.GND" />
      <trace from="U2.SDA" to="net.I2C_SDA" />
      <trace from="U2.SCL" to="net.I2C_SCL" />
      <trace from="U2.ALERT" to="net.ADC_ALERT" />
      <trace from="R6.pin1" to="net.V3V3" />
      <trace from="R6.pin2" to="net.ADC_ALERT" />
      <trace from="C3.pin1" to="net.V3V3" />
      <trace from="C3.pin2" to="net.GND" />

      <chip
        name="J3"
        manufacturerPartNumber="RASPBERRY PI CONTROL HEADER"
        pinLabels={{
          pin1: "5V",
          pin2: "3V3",
          pin3: "GND",
          pin4: "SDA_GPIO2",
          pin5: "SCL_GPIO3",
          pin6: "MIDI_RX_GPIO9",
          pin7: "ADC_ALERT_GPIO25",
          pin8: "MIDI_TX_GPIO8_RSVD",
        }}
        schPinArrangement={left([1, 2, 3, 4, 5, 6, 7, 8])}
        schX={-3.25}
        schY={-4.65}
      />
      <trace from="J3.5V" to="net.V5" />
      <trace from="J3.3V3" to="net.V3V3" />
      <trace from="J3.GND" to="net.GND" />
      <trace from="J3.SDA_GPIO2" to="net.I2C_SDA" />
      <trace from="J3.SCL_GPIO3" to="net.I2C_SCL" />
      <trace from="J3.MIDI_RX_GPIO9" to="net.MIDI_RX" />
      <trace from="J3.ADC_ALERT_GPIO25" to="net.ADC_ALERT" />
      <trace from="J3.MIDI_TX_GPIO8_RSVD" to="net.MIDI_TX_RSVD" />

      <schematictext
        text="SW1A/SW1B are the two mechanically linked poles of one DPDT switch."
        schX={1.0}
        schY={-3.75}
        fontSize={0.2}
      />
      <schematictext
        text="R4 limits a shorted pedal cable to 3.3 mA. D2/D3: BAT54 Schottky clamps."
        schX={1.0}
        schY={-4.2}
        fontSize={0.2}
      />
      <schematictext
        text="I2C pull-ups are already present on the host bus."
        schX={1.0}
        schY={-4.65}
        fontSize={0.2}
      />
    </board>,
  )

  await circuit.renderUntilSettled()
  const converter = new CircuitJsonToKicadSchConverter(circuit.getCircuitJson())
  converter.runUntilFinished()
  writeFileSync(
    new URL("control-io.kicad_sch", import.meta.url),
    converter.getOutputString(),
  )
}

main().catch((error) => {
  console.error(error)
  process.exitCode = 1
})
