import React from "react"
import { createWriteStream, mkdirSync, writeFileSync } from "node:fs"
import { pathToFileURL } from "node:url"
import PDFDocument from "pdfkit"
import SVGtoPDF from "svg-to-pdfkit"
import { Circuit } from "tscircuit"
import {
  CircuitJsonToKicadLibraryConverter,
  CircuitJsonToKicadProConverter,
  CircuitJsonToKicadSchConverter,
} from "circuit-json-to-kicad"
import {
  convertCircuitJsonToSchematicSvg,
} from "circuit-to-svg"

const left = (pins: number[]) => ({
  leftSide: { pins, direction: "top-to-bottom" as const },
})

const split = (leftPins: number[], rightPins: number[]) => ({
  leftSide: { pins: leftPins, direction: "top-to-bottom" as const },
  rightSide: { pins: rightPins, direction: "top-to-bottom" as const },
})

const clampPins = { pin1: "LOW", pin2: "IO", pin3: "HIGH" }

type ResistorProps = React.JSX.IntrinsicElements["resistor"]
type CapacitorProps = React.JSX.IntrinsicElements["capacitor"]
type DiodeProps = React.JSX.IntrinsicElements["diode"]
type FuseProps = React.JSX.IntrinsicElements["fuse"]

const R0603 = (props: ResistorProps) => <resistor footprint="0603" {...props} />
const C0603 = (props: CapacitorProps) => <capacitor footprint="0603" {...props} />
const D_SOD323 = (props: DiodeProps) => <diode footprint="sod323" {...props} />
const Fuse1206 = (props: FuseProps) => <fuse footprint="1206" {...props} />

const Tq2Footprint = () => (
  <footprint name="ATQ209_TQ2-5V">
    {[
      ["1", -3.81, 3.81], ["2", -3.81, 1.27], ["3", -3.81, -1.27], ["4", -3.81, -3.81],
      ["10", 3.81, 3.81], ["9", 3.81, 1.27], ["8", 3.81, -1.27], ["7", 3.81, -3.81],
    ].map(([pin, x, y]) => (
      <platedhole
        key={pin}
        portHints={[String(pin)]}
        shape="circle"
        holeDiameter="0.9mm"
        outerDiameter="1.5mm"
        pcbX={`${x}mm`}
        pcbY={`${y}mm`}
      />
    ))}
    <silkscreenrect width="9mm" height="14mm" filled={false} strokeWidth="0.2mm" />
  </footprint>
)

const Js202Footprint = () => (
  <footprint name="JS202011CQN">
    {[
      ["1", -1.65, 2.5], ["3", -1.65, 0], ["2", -1.65, -2.5],
      ["4", 1.65, 2.5], ["6", 1.65, 0], ["5", 1.65, -2.5],
    ].map(([pin, x, y]) => (
      <platedhole
        key={pin}
        portHints={[String(pin)]}
        shape="circle"
        holeDiameter="0.9mm"
        outerDiameter="1.5mm"
        pcbX={`${x}mm`}
        pcbY={`${y}mm`}
      />
    ))}
    <silkscreenrect width="3.5mm" height="9mm" filled={false} strokeWidth="0.2mm" />
  </footprint>
)

const Da7212Footprint = () => (
  <footprint name="DA7212-01UM2_WLCSP34">
    {Array.from({ length: 17 }, (_, index) => {
      const column = index + 1
      const rows = column % 2 === 1 ? ["A", "C"] : ["B", "D"]
      return rows.map((row, rowIndex) => {
        const logicalPin = index * 2 + rowIndex + 1
        const y = { A: 0.75, B: 0.25, C: -0.25, D: -0.75 }[row]!
        return (
          <smtpad
            key={`${row}${column}`}
            portHints={[`${row}${column}`, String(logicalPin)]}
            shape="circle"
            radius="0.125mm"
            pcbX={`${(column - 9) * 0.25}mm`}
            pcbY={`${y}mm`}
          />
        )
      })
    })}
    <silkscreenrect width="4.54mm" height="1.66mm" filled={false} strokeWidth="0.12mm" />
    <silkscreencircle radius="0.15mm" pcbX="-2.45mm" pcbY="1.05mm" isFilled />
  </footprint>
)

const Asfl1Footprint = () => (
  <footprint name="ASFL1_7.0X5.0MM">
    {[
      ["1", -2.54, 1.9], ["2", -2.54, -1.9],
      ["3", 2.54, -1.9], ["4", 2.54, 1.9],
    ].map(([pin, x, y]) => (
      <smtpad
        key={pin}
        portHints={[String(pin)]}
        shape="rect"
        width="1.8mm"
        height="1.4mm"
        pcbX={`${x}mm`}
        pcbY={`${y}mm`}
      />
    ))}
    <silkscreenrect width="7mm" height="5mm" filled={false} strokeWidth="0.15mm" />
    <silkscreencircle radius="0.25mm" pcbX="-3.65mm" pcbY="2.6mm" isFilled />
  </footprint>
)

export type SchematicOptions = {
  integratedCodec?: boolean
}

export function addSchematic(circuit: Circuit, options: SchematicOptions = {}) {
  const integratedCodec = options.integratedCodec ?? false
  circuit.add(
    <board width="300mm" height="200mm" routingDisabled>
      <schematictext
        text={integratedCodec
          ? "ARDOR REV C INTEGRATED DA7212 AUDIO + CONTROL HAT"
          : "ARDOR PROFESSIONAL AUDIO + CONTROL I/O FOR RASPBERRY PI / CODEC ZERO"}
        schX={-12.8}
        schY={8.7}
        fontSize={0.4}
      />
      <schematictext
        text={integratedCodec
          ? "Rev C — onboard 24-bit codec, fail-safe outputs, protected guitar/line/amp, isolated MIDI, expression ADC"
          : "Rev B — fail-safe mute, bipolar audio ESD, protected guitar/line/amp, isolated MIDI, expression ADC"}
        schX={-12.8}
        schY={8.15}
        fontSize={0.24}
      />

      {/* POWER, REFERENCE, CHASSIS */}
      <schematictext text="1. POWER / REFERENCE / CHASSIS" schX={-12.8} schY={7.35} fontSize={0.28} />
      <chip
        name="J8"
        manufacturerPartNumber="2x20 STACK-THROUGH"
        footprint="pinrow40_rows2_p2.54mm_id1mm_od1.7mm_female"
        pinLabels={{
          pin1: "3V3", pin2: "5V", pin3: "GPIO2_SDA", pin4: "5V_2",
          pin5: "GPIO3_SCL", pin6: "GND", pin7: "GPIO4", pin8: "GPIO14",
          pin9: "GND_2", pin10: "GPIO15", pin11: "GPIO17", pin12: "GPIO18_I2S_CLK",
          pin13: "GPIO27_CODEC_SW", pin14: "GND_3", pin15: "GPIO22", pin16: "GPIO23_CODEC_LED",
          pin17: "3V3_2", pin18: "GPIO24_CODEC_LED", pin19: "GPIO10_MUTE", pin20: "GND_4",
          pin21: "GPIO9_UART4_RX", pin22: "GPIO25_ADC_ALERT", pin23: "GPIO11", pin24: "GPIO8_UART4_TX_RSVD",
          pin25: "GND_5", pin26: "GPIO7", pin27: "ID_SD", pin28: "ID_SC",
          pin29: "GPIO5", pin30: "GND_6", pin31: "GPIO6", pin32: "GPIO12",
          pin33: "GPIO13", pin34: "GND_7", pin35: "GPIO19_I2S_FS", pin36: "GPIO16",
          pin37: "GPIO26", pin38: "GPIO20_I2S_DIN", pin39: "GND_8", pin40: "GPIO21_I2S_DOUT",
        }}
        schPinArrangement={split(
          [1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31, 33, 35, 37, 39],
          [2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40],
        )}
        noConnect={[
          "pin7", "pin8", "pin10", "pin11", ...(integratedCodec ? [] : ["pin12"]), "pin13", "pin15", "pin16", "pin18", "pin23",
          "pin24", "pin26", "pin27", "pin28", "pin29", "pin31", "pin32", "pin33", ...(integratedCodec ? [] : ["pin35"]), "pin36",
          "pin37", ...(integratedCodec ? [] : ["pin38", "pin40"]),
        ]}
        schX={-10.7}
        schY={5.25}
      />
      <Fuse1206 name="F1" currentRating="500mA" schX={-7.6} schY={6.45} />
      <Fuse1206 name="F2" currentRating="150mA" schX={-7.6} schY={5.65} />
      <chip
        name="FB1"
        manufacturerPartNumber="BLM21PG221SN1D"
        footprint="0805"
        pinLabels={{ pin1: "IN", pin2: "OUT" }}
        schPinArrangement={split([1], [2])}
        schX={-6.35}
        schY={6.45}
      />
      <C0603 name="C20" capacitance="22uF" footprint="1210" schX={-5.35} schY={5.65} />
      <C0603 name="C21" capacitance="100nF" schX={-4.7} schY={5.65} />
      <chip
        name="U2"
        manufacturerPartNumber="TPS7A2045PDBVR"
        footprint="sot23_5"
        pinLabels={{ pin1: "IN", pin2: "GND", pin3: "EN", pin4: "NC", pin5: "OUT" }}
        schPinArrangement={split([1, 3, 2], [5, 4])}
        noConnect={["pin4"]}
        schX={-3.2}
        schY={6.35}
      />
      <C0603 name="C22" capacitance="1uF" schX={-1.75} schY={5.55} />
      <C0603 name="C23" capacitance="10uF" footprint="0805" schX={-1.05} schY={5.55} />
      <C0603 name="C24" capacitance="100nF" schX={-0.35} schY={5.55} />
      <chip
        name="U3"
        manufacturerPartNumber="TLE2426IDR"
        footprint="soic8"
        pinLabels={{
          pin1: "OUT", pin2: "COMMON", pin3: "IN", pin4: "NC1",
          pin5: "NC2", pin6: "NC3", pin7: "NC4", pin8: "NOISE_REDUCTION",
        }}
        schPinArrangement={split([3, 2, 8], [1, 4, 5, 6, 7])}
        noConnect={["pin4", "pin5", "pin6", "pin7"]}
        schX={1.2}
        schY={6.35}
      />
      <C0603 name="C25" capacitance="1uF" schX={2.55} schY={5.55} />
      <C0603 name="C26" capacitance="100nF" schX={0.2} schY={5.55} />
      <chip
        name="J9"
        manufacturerPartNumber="CHASSIS STUD"
        footprint="pinrow1_rows1_p2.54mm_id1.2mm_od2mm_male"
        pinLabels={{ pin1: "CHASSIS" }}
        schPinArrangement={left([1])}
        schX={5.1}
        schY={6.45}
      />
      <R0603 name="R30" resistance="1M" schX={6.4} schY={6.8} />
      <C0603 name="C27" capacitance="4.7nF 1kV" footprint="pinrow2_rows1_p7.5mm_id0.8mm_od1.6mm_male" schX={6.4} schY={6.05} />
      <R0603 name="R31" resistance="0R DNI" doNotPlace schX={8.0} schY={6.45} />

      <trace from="J8.5V" to="net.V5_RAW" />
      <trace from="J8.5V_2" to="net.V5_RAW" />
      <trace from="J8.3V3" to="net.V3V3" />
      <trace from="J8.3V3_2" to="net.V3V3" />
      <trace from="J8.GND" to="net.DGND" />
      <trace from="J8.GND_2" to="net.DGND" />
      <trace from="J8.GND_3" to="net.DGND" />
      <trace from="J8.GND_4" to="net.DGND" />
      <trace from="J8.GND_5" to="net.DGND" />
      <trace from="J8.GND_6" to="net.DGND" />
      <trace from="J8.GND_7" to="net.DGND" />
      <trace from="J8.GND_8" to="net.DGND" />
      <trace from="F1.pin1" to="net.V5_RAW" />
      <trace from="F1.pin2" to="net.V5_FUSED" />
      <trace from="F2.pin1" to="net.V5_RAW" />
      <trace from="F2.pin2" to="net.V5_RELAY_FUSED" />
      <trace from="FB1.IN" to="net.V5_FUSED" />
      <trace from="FB1.OUT" to="net.V5_FILTERED" />
      <trace from="C20.pin1" to="net.V5_FILTERED" />
      <trace from="C20.pin2" to="net.AGND" />
      <trace from="C21.pin1" to="net.V5_FILTERED" />
      <trace from="C21.pin2" to="net.AGND" />
      <trace from="U2.IN" to="net.V5_FILTERED" />
      <trace from="U2.EN" to="net.V5_FILTERED" />
      <trace from="U2.GND" to="net.AGND" />
      <trace from="U2.OUT" to="net.V4V5_A" />
      <trace from="C22.pin1" to="net.V5_FILTERED" />
      <trace from="C22.pin2" to="net.AGND" />
      <trace from="C23.pin1" to="net.V4V5_A" />
      <trace from="C23.pin2" to="net.AGND" />
      <trace from="C24.pin1" to="net.V4V5_A" />
      <trace from="C24.pin2" to="net.AGND" />
      <trace from="U3.IN" to="net.V4V5_A" />
      <trace from="U3.COMMON" to="net.AGND" />
      <trace from="U3.OUT" to="net.VREF_2V25" />
      <trace from="U3.NOISE_REDUCTION" to="net.VREF_NR" />
      <trace from="C25.pin1" to="net.VREF_NR" />
      <trace from="C25.pin2" to="net.AGND" />
      <trace from="C26.pin1" to="net.V4V5_A" />
      <trace from="C26.pin2" to="net.AGND" />
      <trace from="net.DGND" to="net.AGND" />
      <trace from="J9.CHASSIS" to="net.CHASSIS" />
      <trace from="R30.pin1" to="net.CHASSIS" />
      <trace from="R30.pin2" to="net.AGND" />
      <trace from="C27.pin1" to="net.CHASSIS" />
      <trace from="C27.pin2" to="net.AGND" />
      <trace from="R31.pin1" to="net.CHASSIS" />
      <trace from="R31.pin2" to="net.AGND" />

      {/* GUITAR INPUT */}
      <schematictext text={integratedCodec ? "2. HIGH-Z GUITAR INPUT → ONBOARD DA7212" : "2. HIGH-Z GUITAR INPUT → CODEC ZERO AUX IN"} schX={-12.8} schY={3.55} fontSize={0.28} />
      <chip
        name="J1"
        manufacturerPartNumber="B2B-XH-A(LF)(SN) / PANEL TS HARNESS"
        footprint="pinrow2_rows1_p2.5mm_id1mm_od1.7mm_male"
        pinLabels={{ pin1: "TIP", pin2: "SLEEVE" }}
        schPinArrangement={left([1, 2])}
        schX={-11.9}
        schY={2.2}
      />
      <D_SOD323 name="D1" manufacturerPartNumber="PESD5V0U1BA-Q" schX={-10.5} schY={1.25} />
      <R0603 name="R1" resistance="1k" schX={-9.75} schY={2.55} />
      <C0603 name="C1" capacitance="220nF FILM" footprint="pinrow2_rows1_p5mm_id0.8mm_od1.6mm_male" schX={-8.45} schY={2.55} />
      <R0603 name="R2" resistance="1M" schX={-7.4} schY={1.55} />
      <R0603 name="R3" resistance="10k" schX={-6.25} schY={2.55} />
      <C0603 name="C2" capacitance="100pF C0G" schX={-5.1} schY={1.55} />
      <chip
        name="D2"
        manufacturerPartNumber="BAT54S"
        footprint="sot23"
        pinLabels={clampPins}
        schPinArrangement={split([1, 2], [3])}
        schX={-4.0}
        schY={2.5}
      />
      <C0603 name="C3" capacitance="2.2uF FILM" footprint="pinrow2_rows1_p5mm_id0.8mm_od1.6mm_male" schX={0.0} schY={2.95} />
      <R0603 name="R4" resistance={integratedCodec ? "10k" : "100R"} schX={1.2} schY={2.95} />
      <R0603 name="R5" resistance={integratedCodec ? "10k" : "100k"} schX={2.15} schY={2.15} />
      <C0603 name="C4" capacitance="2.2uF FILM" footprint="pinrow2_rows1_p5mm_id0.8mm_od1.6mm_male" schX={0.0} schY={1.45} />
      <R0603 name="R6" resistance={integratedCodec ? "10k" : "100R"} schX={1.2} schY={1.45} />
      <R0603 name="R7" resistance={integratedCodec ? "10k" : "100k"} schX={2.15} schY={0.75} />
      <chip
        name="J2"
        manufacturerPartNumber={integratedCodec ? "22-27-2041 / DNI CODEC INPUT TEST" : "22-27-2041 / CODEC ZERO P1 HARNESS"}
        doNotPlace={integratedCodec}
        footprint="pinrow4_rows1_p2.54mm_id1mm_od1.7mm_male"
        pinLabels={{ pin1: "LEFT", pin2: "GND_L", pin3: "RIGHT", pin4: "GND_R" }}
        schPinArrangement={left([1, 2, 3, 4])}
        schX={4.15}
        schY={2.2}
      />
      <C0603 name="C28" capacitance="100nF" schX={-1.0} schY={0.8} />

      <trace from="J1.TIP" to="net.GUITAR_TIP" />
      <trace from="J1.SLEEVE" to="net.AGND" />
      <trace from="D1.anode" to="net.CHASSIS" />
      <trace from="D1.cathode" to="net.GUITAR_TIP" />
      <trace from="R1.pin1" to="net.GUITAR_TIP" />
      <trace from="R1.pin2" to="net.GUITAR_RF_LIMITED" />
      <trace from="C1.pin1" to="net.GUITAR_RF_LIMITED" />
      <trace from="C1.pin2" to="net.GUITAR_BIAS" />
      <trace from="R2.pin1" to="net.GUITAR_BIAS" />
      <trace from="R2.pin2" to="net.VREF_2V25" />
      <trace from="R3.pin1" to="net.GUITAR_BIAS" />
      <trace from="R3.pin2" to="net.GUITAR_PROTECTED" />
      <trace from="C2.pin1" to="net.GUITAR_PROTECTED" />
      <trace from="C2.pin2" to="net.VREF_2V25" />
      <trace from="D2.IO" to="net.GUITAR_PROTECTED" />
      <trace from="D2.LOW" to="net.AGND" />
      <trace from="D2.HIGH" to="net.V4V5_A" />
      <trace from="U1.IN_PLUS_A" to="net.GUITAR_PROTECTED" />
      <trace from="U1.IN_MINUS_A" to="net.GUITAR_BUFFERED" />
      <trace from="U1.OUT_A" to="net.GUITAR_BUFFERED" />
      <trace from="C28.pin1" to="net.V4V5_A" />
      <trace from="C28.pin2" to="net.AGND" />
      <trace from="C3.pin1" to="net.GUITAR_BUFFERED" />
      <trace from="C3.pin2" to="net.CODEC_IN_L_AC" />
      <trace from="R4.pin1" to="net.CODEC_IN_L_AC" />
      <trace from="R4.pin2" to="net.CODEC_IN_L" />
      <trace from="R5.pin1" to="net.CODEC_IN_L" />
      <trace from="R5.pin2" to="net.AGND" />
      <trace from="C4.pin1" to="net.GUITAR_BUFFERED" />
      <trace from="C4.pin2" to="net.CODEC_IN_R_AC" />
      <trace from="R6.pin1" to="net.CODEC_IN_R_AC" />
      <trace from="R6.pin2" to="net.CODEC_IN_R" />
      <trace from="R7.pin1" to="net.CODEC_IN_R" />
      <trace from="R7.pin2" to="net.AGND" />
      <trace from="J2.LEFT" to="net.CODEC_IN_L" />
      <trace from="J2.RIGHT" to="net.CODEC_IN_R" />
      <trace from="J2.GND_L" to="net.AGND" />
      <trace from="J2.GND_R" to="net.AGND" />

      {/* AUDIO OUTPUTS */}
      <schematictext text={integratedCodec ? "3. ONBOARD DA7212 → STEREO LINE + MONO GUITAR AMP" : "3. CODEC ZERO AUX OUT → STEREO LINE + MONO GUITAR AMP"} schX={5.4} schY={3.55} fontSize={0.28} />
      <chip
        name="J3"
        manufacturerPartNumber={integratedCodec ? "22-27-2041 / DNI CODEC OUTPUT TEST" : "22-27-2041 / CODEC ZERO P2 HARNESS"}
        doNotPlace={integratedCodec}
        footprint="pinrow4_rows1_p2.54mm_id1mm_od1.7mm_male"
        pinLabels={{ pin1: "LEFT", pin2: "GND_L", pin3: "RIGHT", pin4: "GND_R" }}
        schPinArrangement={left([1, 2, 3, 4])}
        schX={6.25}
        schY={2.2}
      />
      <C0603 name="C5" capacitance="2.2uF FILM" footprint="pinrow2_rows1_p5mm_id0.8mm_od1.6mm_male" schX={7.65} schY={2.95} />
      <R0603 name="R8" resistance="100k" schX={8.6} schY={2.15} />
      <C0603 name="C6" capacitance="2.2uF FILM" footprint="pinrow2_rows1_p5mm_id0.8mm_od1.6mm_male" schX={7.65} schY={1.45} />
      <R0603 name="R9" resistance="100k" schX={8.6} schY={0.75} />
      <chip
        name="U1"
        manufacturerPartNumber="OPA4377AIPWR"
        footprint="tssop14"
        pinLabels={{
          pin1: "OUT_A", pin2: "IN_MINUS_A", pin3: "IN_PLUS_A", pin4: "VPLUS",
          pin5: "IN_PLUS_B", pin6: "IN_MINUS_B", pin7: "OUT_B", pin8: "OUT_C",
          pin9: "IN_MINUS_C", pin10: "IN_PLUS_C", pin11: "VMINUS",
          pin12: "IN_PLUS_D", pin13: "IN_MINUS_D", pin14: "OUT_D",
        }}
        schPinArrangement={split([3, 2, 5, 6, 10, 9, 12, 13, 4, 11], [1, 7, 8, 14])}
        schX={10.3}
        schY={1.2}
      />
      <R0603 name="R10" resistance="20k" schX={8.8} schY={0.0} />
      <R0603 name="R11" resistance="20k" schX={8.8} schY={-0.75} />
      <R0603 name="R12" resistance="10k" schX={11.55} schY={-1.2} />
      <C0603 name="C29" capacitance="100nF" schX={11.8} schY={0.15} />
      <C0603 name="C7" capacitance="10uF BIPOLAR" footprint="pinrow2_rows1_p2mm_id0.8mm_od1.6mm_male" schX={10.5} schY={3.8} />
      <R0603 name="R13" resistance="100R" schX={12.0} schY={3.8} />
      <C0603 name="C8" capacitance="10uF BIPOLAR" footprint="pinrow2_rows1_p2mm_id0.8mm_od1.6mm_male" schX={10.5} schY={1.4} />
      <R0603 name="R14" resistance="100R" schX={12.0} schY={1.4} />
      <C0603 name="C9" capacitance="10uF BIPOLAR" footprint="pinrow2_rows1_p2mm_id0.8mm_od1.6mm_male" schX={10.5} schY={-1.8} />
      <R0603 name="R15" resistance="100R" schX={12.0} schY={-1.8} />
      <R0603 name="R36" resistance="100k" schX={13.1} schY={4.6} />
      <R0603 name="R37" resistance="100k" schX={13.1} schY={0.4} />
      <R0603 name="R38" resistance="100k" schX={13.1} schY={-3.0} />
      <chip
        name="K1"
        manufacturerPartNumber="ATQ209 / TQ2-5V"
        footprint={<Tq2Footprint />}
        pinLabels={{
          pin1: "COIL_P", pin2: "NC_L", pin3: "COM_L", pin4: "NO_L",
          pin7: "NO_R", pin8: "COM_R", pin9: "NC_R", pin10: "COIL_N",
        }}
        schPinArrangement={split([1, 10, 3, 8], [2, 4, 7, 9])}
        noConnect={["pin2", "pin9"]}
        schX={15.0}
        schY={2.6}
      />
      <chip
        name="K2"
        manufacturerPartNumber="ATQ209 / TQ2-5V"
        footprint={<Tq2Footprint />}
        pinLabels={{
          pin1: "COIL_P", pin2: "NC_A", pin3: "COM_A", pin4: "NO_A",
          pin7: "NO_B", pin8: "COM_B", pin9: "NC_B", pin10: "COIL_N",
        }}
        schPinArrangement={split([1, 10, 3, 8], [2, 4, 7, 9])}
        noConnect={["pin2", "pin9"]}
        schX={15.0}
        schY={-1.8}
      />
      <chip
        name="J4"
        manufacturerPartNumber="B3B-XH-A(LF)(SN) / PANEL TRS HARNESS"
        footprint="pinrow3_rows1_p2.5mm_id1mm_od1.7mm_male"
        pinLabels={{ pin1: "TIP_L", pin2: "RING_R", pin3: "SLEEVE" }}
        schPinArrangement={left([1, 2, 3])}
        schX={19.0}
        schY={2.6}
      />
      <chip
        name="J5"
        manufacturerPartNumber="B2B-XH-A(LF)(SN) / PANEL TS HARNESS"
        footprint="pinrow2_rows1_p2.5mm_id1mm_od1.7mm_male"
        pinLabels={{ pin1: "TIP", pin2: "SLEEVE" }}
        schPinArrangement={left([1, 2])}
        schX={19.0}
        schY={-1.8}
      />
      <R0603 name="R16" resistance="100k" schX={17.4} schY={0.4} />
      <R0603 name="R17" resistance="100k" schX={18.6} schY={0.4} />
      <R0603 name="R18" resistance="100k" schX={18.0} schY={-3.0} />
      <D_SOD323 name="D3" manufacturerPartNumber="PESD5V0U1BA-Q" schX={17.5} schY={4.6} />
      <D_SOD323 name="D4" manufacturerPartNumber="PESD5V0U1BA-Q" schX={18.7} schY={4.6} />
      <D_SOD323 name="D5" manufacturerPartNumber="PESD5V0U1BA-Q" schX={18.0} schY={-0.2} />

      <trace from="J3.LEFT" to="net.CODEC_OUT_L" />
      <trace from="C5.pin1" to="net.CODEC_OUT_L" />
      <trace from="J3.RIGHT" to="net.CODEC_OUT_R" />
      <trace from="C6.pin1" to="net.CODEC_OUT_R" />
      <trace from="J3.GND_L" to="net.AGND" />
      <trace from="J3.GND_R" to="net.AGND" />
      <trace from="C5.pin2" to="net.CODEC_OUT_L_BIAS" />
      <trace from="R8.pin1" to="net.CODEC_OUT_L_BIAS" />
      <trace from="R8.pin2" to="net.VREF_2V25" />
      <trace from="C6.pin2" to="net.CODEC_OUT_R_BIAS" />
      <trace from="R9.pin1" to="net.CODEC_OUT_R_BIAS" />
      <trace from="R9.pin2" to="net.VREF_2V25" />
      <trace from="U1.IN_PLUS_B" to="net.CODEC_OUT_L_BIAS" />
      <trace from="U1.IN_MINUS_B" to="net.LINE_L_BUFFERED" />
      <trace from="U1.OUT_B" to="net.LINE_L_BUFFERED" />
      <trace from="U1.IN_PLUS_C" to="net.CODEC_OUT_R_BIAS" />
      <trace from="U1.IN_MINUS_C" to="net.LINE_R_BUFFERED" />
      <trace from="U1.OUT_C" to="net.LINE_R_BUFFERED" />
      <trace from="R10.pin1" to="net.LINE_L_BUFFERED" />
      <trace from="R10.pin2" to="net.MONO_SUM_NODE" />
      <trace from="R11.pin1" to="net.LINE_R_BUFFERED" />
      <trace from="R11.pin2" to="net.MONO_SUM_NODE" />
      <trace from="U1.IN_PLUS_D" to="net.VREF_2V25" />
      <trace from="U1.IN_MINUS_D" to="net.MONO_SUM_NODE" />
      <trace from="U1.OUT_D" to="net.MONO_BUFFERED" />
      <trace from="U1.VPLUS" to="net.V4V5_A" />
      <trace from="U1.VMINUS" to="net.AGND" />
      <trace from="R12.pin1" to="net.MONO_SUM_NODE" />
      <trace from="R12.pin2" to="net.MONO_BUFFERED" />
      <trace from="C29.pin1" to="net.V4V5_A" />
      <trace from="C29.pin2" to="net.AGND" />
      <trace from="C7.pin1" to="net.LINE_L_BUFFERED" />
      <trace from="C7.pin2" to="net.LINE_L_AC" />
      <trace from="R13.pin1" to="net.LINE_L_AC" />
      <trace from="R13.pin2" to="net.LINE_L_RELAY_IN" />
      <trace from="K1.COM_L" to="net.LINE_L_RELAY_IN" />
      <trace from="R36.pin1" to="net.LINE_L_RELAY_IN" />
      <trace from="R36.pin2" to="net.AGND" />
      <trace from="C8.pin1" to="net.LINE_R_BUFFERED" />
      <trace from="C8.pin2" to="net.LINE_R_AC" />
      <trace from="R14.pin1" to="net.LINE_R_AC" />
      <trace from="R14.pin2" to="net.LINE_R_RELAY_IN" />
      <trace from="K1.COM_R" to="net.LINE_R_RELAY_IN" />
      <trace from="R37.pin1" to="net.LINE_R_RELAY_IN" />
      <trace from="R37.pin2" to="net.AGND" />
      <trace from="C9.pin1" to="net.MONO_BUFFERED" />
      <trace from="C9.pin2" to="net.AMP_AC" />
      <trace from="R15.pin1" to="net.AMP_AC" />
      <trace from="R15.pin2" to="net.AMP_RELAY_IN" />
      <trace from="K2.COM_A" to="net.AMP_RELAY_IN" />
      <trace from="K2.COM_B" to="net.AMP_RELAY_IN" />
      <trace from="R38.pin1" to="net.AMP_RELAY_IN" />
      <trace from="R38.pin2" to="net.AGND" />
      <trace from="K1.NO_L" to="net.LINE_OUT_L" />
      <trace from="K1.NO_R" to="net.LINE_OUT_R" />
      <trace from="K2.NO_A" to="net.AMP_OUT" />
      <trace from="K2.NO_B" to="net.AMP_OUT" />
      <trace from="J4.TIP_L" to="net.LINE_OUT_L" />
      <trace from="J4.RING_R" to="net.LINE_OUT_R" />
      <trace from="J4.SLEEVE" to="net.AGND" />
      <trace from="J5.TIP" to="net.AMP_OUT" />
      <trace from="J5.SLEEVE" to="net.AGND" />
      <trace from="R16.pin1" to="net.LINE_OUT_L" />
      <trace from="R16.pin2" to="net.AGND" />
      <trace from="R17.pin1" to="net.LINE_OUT_R" />
      <trace from="R17.pin2" to="net.AGND" />
      <trace from="R18.pin1" to="net.AMP_OUT" />
      <trace from="R18.pin2" to="net.AGND" />
      <trace from="D3.anode" to="net.CHASSIS" />
      <trace from="D3.cathode" to="net.LINE_OUT_L" />
      <trace from="D4.anode" to="net.CHASSIS" />
      <trace from="D4.cathode" to="net.LINE_OUT_R" />
      <trace from="D5.anode" to="net.CHASSIS" />
      <trace from="D5.cathode" to="net.AMP_OUT" />

      {/* MIDI */}
      <schematictext text="4. GALVANICALLY ISOLATED TRS MIDI INPUT" schX={-12.8} schY={-2.3} fontSize={0.28} />
      <chip
        name="J6"
        manufacturerPartNumber="B3B-XH-A(LF)(SN) / PANEL MIDI HARNESS"
        footprint="pinrow3_rows1_p2.5mm_id1mm_od1.7mm_male"
        pinLabels={{ pin1: "TIP", pin2: "RING", pin3: "SLEEVE" }}
        schPinArrangement={left([1, 2, 3])}
        schX={-11.9}
        schY={-3.7}
      />
      <D_SOD323 name="D6" manufacturerPartNumber="PESD12VL1BA" schX={-10.45} schY={-4.75} />
      <D_SOD323 name="D13" manufacturerPartNumber="PMEG4010CEJ-Q" footprint="sod323f" schX={-9.25} schY={-3.0} />
      <D_SOD323 name="D14" manufacturerPartNumber="PMEG4010CEJ-Q" footprint="sod323f" schX={-9.25} schY={-3.6} />
      <D_SOD323 name="D15" manufacturerPartNumber="PMEG4010CEJ-Q" footprint="sod323f" schX={-8.4} schY={-4.2} />
      <D_SOD323 name="D16" manufacturerPartNumber="PMEG4010CEJ-Q" footprint="sod323f" schX={-7.55} schY={-4.2} />
      <R0603 name="R19" resistance="220R 0.25W" footprint="1206" schX={-7.15} schY={-3.1} />
      <chip
        name="U4"
        manufacturerPartNumber="H11L1M"
        footprint="dip6"
        pinLabels={{ pin1: "LED_A", pin2: "LED_K", pin3: "NC", pin4: "GND", pin5: "VO", pin6: "VCC" }}
        schPinArrangement={split([1, 2, 3], [6, 5, 4])}
        noConnect={["pin3"]}
        schX={-5.25}
        schY={-3.65}
      />
      <D_SOD323 name="D7" manufacturerPartNumber="1N4148WS" schX={-6.25} schY={-4.65} />
      <R0603 name="R20" resistance="4.7k" schX={-3.55} schY={-3.05} />
      <R0603 name="R21" resistance="220R" schX={-1.95} schY={-3.65} />
      <C0603 name="C10" capacitance="100nF" schX={-3.55} schY={-4.75} />
      <R0603 name="R22" resistance="1M" schX={-10.7} schY={-5.55} />
      <C0603 name="C11" capacitance="1nF 1kV" footprint="pinrow2_rows1_p7.5mm_id0.8mm_od1.6mm_male" schX={-9.45} schY={-5.55} />

      <trace from="J6.TIP" to="net.MIDI_TIP_ISO" />
      <trace from="J6.RING" to="net.MIDI_RING_ISO" />
      <trace from="J6.SLEEVE" to="net.MIDI_SHIELD" />
      <trace from="D6.anode" to="net.MIDI_TIP_ISO" />
      <trace from="D6.cathode" to="net.MIDI_RING_ISO" />
      <trace from="D13.anode" to="net.MIDI_TIP_ISO" />
      <trace from="D13.cathode" to="net.MIDI_BRIDGE_POS" />
      <trace from="D14.anode" to="net.MIDI_RING_ISO" />
      <trace from="D14.cathode" to="net.MIDI_BRIDGE_POS" />
      <trace from="D15.anode" to="net.MIDI_BRIDGE_NEG" />
      <trace from="D15.cathode" to="net.MIDI_TIP_ISO" />
      <trace from="D16.anode" to="net.MIDI_BRIDGE_NEG" />
      <trace from="D16.cathode" to="net.MIDI_RING_ISO" />
      <trace from="R19.pin1" to="net.MIDI_BRIDGE_POS" />
      <trace from="R19.pin2" to="net.MIDI_LED_A" />
      <trace from="U4.LED_A" to="net.MIDI_LED_A" />
      <trace from="U4.LED_K" to="net.MIDI_BRIDGE_NEG" />
      <trace from="D7.anode" to="net.MIDI_BRIDGE_NEG" />
      <trace from="D7.cathode" to="net.MIDI_LED_A" />
      <trace from="U4.VCC" to="net.V3V3" />
      <trace from="U4.GND" to="net.DGND" />
      <trace from="U4.VO" to="net.MIDI_RX_RAW" />
      <trace from="R20.pin1" to="net.V3V3" />
      <trace from="R20.pin2" to="net.MIDI_RX_RAW" />
      <trace from="R21.pin1" to="net.MIDI_RX_RAW" />
      <trace from="R21.pin2" to="net.MIDI_RX_GPIO" />
      <trace from="J8.GPIO9_UART4_RX" to="net.MIDI_RX_GPIO" />
      <trace from="C10.pin1" to="net.V3V3" />
      <trace from="C10.pin2" to="net.DGND" />
      <trace from="R22.pin1" to="net.MIDI_SHIELD" />
      <trace from="R22.pin2" to="net.CHASSIS" />
      <trace from="C11.pin1" to="net.MIDI_SHIELD" />
      <trace from="C11.pin2" to="net.CHASSIS" />

      {/* EXPRESSION */}
      <schematictext text="5. PROTECTED PASSIVE EXPRESSION PEDAL INPUT" schX={0.0} schY={-2.3} fontSize={0.28} />
      <chip
        name="J7"
        manufacturerPartNumber="B3B-XH-A(LF)(SN) / PANEL EXP HARNESS"
        footprint="pinrow3_rows1_p2.5mm_id1mm_od1.7mm_male"
        pinLabels={{ pin1: "TIP", pin2: "RING", pin3: "SLEEVE" }}
        schPinArrangement={left([1, 2, 3])}
        schX={0.85}
        schY={-3.75}
      />
      <D_SOD323 name="D8" manufacturerPartNumber="PESD3V3U1UL" footprint="sod882" schX={2.15} schY={-4.85} />
      <D_SOD323 name="D9" manufacturerPartNumber="PESD3V3U1UL" footprint="sod882" schX={2.85} schY={-4.85} />
      <chip
        name="SW1"
        manufacturerPartNumber="JS202011CQN"
        footprint={<Js202Footprint />}
        pinLabels={{
          pin1: "A_A", pin3: "COM_A", pin2: "B_A",
          pin4: "A_B", pin6: "COM_B", pin5: "B_B",
        }}
        schPinArrangement={split([3, 6], [1, 2, 4, 5])}
        schX={3.35}
        schY={-3.85}
      />
      <D_SOD323 name="D17" manufacturerPartNumber="PMEG4010CEJ-Q" footprint="sod323f" schX={4.65} schY={-2.85} />
      <R0603 name="R23" resistance="1k" schX={5.25} schY={-2.85} />
      <C0603 name="C12" capacitance="1uF" schX={6.15} schY={-3.6} />
      <R0603 name="R24" resistance="4.7k" schX={5.4} schY={-4.85} />
      <C0603 name="C13" capacitance="100nF" schX={6.55} schY={-5.55} />
      <chip
        name="D10"
        manufacturerPartNumber="BAT54S"
        footprint="sot23"
        pinLabels={clampPins}
        schPinArrangement={split([1, 2], [3])}
        schX={7.45}
        schY={-4.85}
      />
      <R0603 name="R25" resistance="33R" schX={7.2} schY={-2.65} />
      <R0603 name="R26" resistance="33R" schX={7.2} schY={-3.35} />
      <chip
        name="U5"
        manufacturerPartNumber="ADS1115IDGSR (0x48)"
        footprint="vssop10"
        pinLabels={{
          pin1: "ADDR", pin2: "ALERT", pin3: "GND", pin4: "AIN0", pin5: "AIN1",
          pin6: "AIN2", pin7: "AIN3", pin8: "VDD", pin9: "SDA", pin10: "SCL",
        }}
        schPinArrangement={split([4, 5, 6, 7, 1], [8, 9, 10, 2, 3])}
        schX={9.75}
        schY={-3.8}
      />
      <R0603 name="R27" resistance="1k" schX={8.2} schY={-5.85} />
      <R0603 name="R28" resistance="1k" schX={9.0} schY={-5.85} />
      <R0603 name="R29" resistance="1k" schX={9.8} schY={-5.85} />
      <R0603 name="R32" resistance="10k" schX={11.5} schY={-3.0} />
      <R0603 name="R33" resistance="220R" schX={12.85} schY={-3.75} />
      <C0603 name="C14" capacitance="1uF" schX={11.45} schY={-5.25} />
      <C0603 name="C15" capacitance="100nF" schX={12.15} schY={-5.25} />
      <D_SOD323 name="D18" manufacturerPartNumber="SMBJ5.0CA" footprint="smb" schX={1.45} schY={-5.55} />

      <trace from="J7.TIP" to="net.EXP_TIP" />
      <trace from="J7.RING" to="net.EXP_RING" />
      <trace from="J7.SLEEVE" to="net.AGND" />
      <trace from="D8.anode" to="net.CHASSIS" />
      <trace from="D8.cathode" to="net.EXP_TIP" />
      <trace from="D9.anode" to="net.CHASSIS" />
      <trace from="D9.cathode" to="net.EXP_RING" />
      <trace from="SW1.COM_A" to="net.EXP_TIP" />
      <trace from="SW1.A_A" to="net.VEXP" />
      <trace from="SW1.B_A" to="net.EXP_SENSE" />
      <trace from="SW1.COM_B" to="net.EXP_RING" />
      <trace from="SW1.A_B" to="net.EXP_SENSE" />
      <trace from="SW1.B_B" to="net.VEXP" />
      <trace from="D17.anode" to="net.V3V3" />
      <trace from="D17.cathode" to="net.VEXP_SOURCE" />
      <trace from="R23.pin1" to="net.VEXP_SOURCE" />
      <trace from="R23.pin2" to="net.VEXP" />
      <trace from="C12.pin1" to="net.VEXP" />
      <trace from="C12.pin2" to="net.AGND" />
      <trace from="R24.pin1" to="net.EXP_SENSE" />
      <trace from="R24.pin2" to="net.EXP_ADC_FILTER" />
      <trace from="C13.pin1" to="net.EXP_ADC_FILTER" />
      <trace from="C13.pin2" to="net.AGND" />
      <trace from="D10.IO" to="net.EXP_ADC_FILTER" />
      <trace from="D10.LOW" to="net.AGND" />
      <trace from="D10.HIGH" to="net.V3V3" />
      <trace from="U5.AIN0" to="net.EXP_ADC_FILTER" />
      <trace from="U5.AIN1" to="net.ADC_UNUSED_1" />
      <trace from="R27.pin1" to="net.ADC_UNUSED_1" />
      <trace from="U5.AIN2" to="net.ADC_UNUSED_2" />
      <trace from="R28.pin1" to="net.ADC_UNUSED_2" />
      <trace from="U5.AIN3" to="net.ADC_UNUSED_3" />
      <trace from="R29.pin1" to="net.ADC_UNUSED_3" />
      <trace from="R27.pin2" to="net.AGND" />
      <trace from="R28.pin2" to="net.AGND" />
      <trace from="R29.pin2" to="net.AGND" />
      <trace from="U5.ADDR" to="net.AGND" />
      <trace from="U5.GND" to="net.DGND" />
      <trace from="U5.VDD" to="net.V3V3" />
      <trace from="J8.GPIO2_SDA" to="net.I2C_SDA_HOST" />
      <trace from="J8.GPIO3_SCL" to="net.I2C_SCL_HOST" />
      <trace from="R25.pin1" to="net.I2C_SDA_HOST" />
      <trace from="R25.pin2" to="net.I2C_SDA_ADC" />
      <trace from="U5.SDA" to="net.I2C_SDA_ADC" />
      <trace from="R26.pin1" to="net.I2C_SCL_HOST" />
      <trace from="R26.pin2" to="net.I2C_SCL_ADC" />
      <trace from="U5.SCL" to="net.I2C_SCL_ADC" />
      <trace from="U5.ALERT" to="net.ADC_ALERT_RAW" />
      <trace from="R32.pin1" to="net.V3V3" />
      <trace from="R32.pin2" to="net.ADC_ALERT_RAW" />
      <trace from="R33.pin1" to="net.ADC_ALERT_RAW" />
      <trace from="R33.pin2" to="net.ADC_ALERT_GPIO" />
      <trace from="J8.GPIO25_ADC_ALERT" to="net.ADC_ALERT_GPIO" />
      <trace from="C14.pin1" to="net.V3V3" />
      <trace from="C14.pin2" to="net.DGND" />
      <trace from="C15.pin1" to="net.V3V3" />
      <trace from="C15.pin2" to="net.DGND" />
      <trace from="D18.anode" to="net.CHASSIS" />
      <trace from="D18.cathode" to="net.AGND" />

      {/* OUTPUT RELAY DRIVER */}
      <schematictext text="6. FAIL-SAFE OUTPUT MUTE" schX={13.6} schY={-2.3} fontSize={0.28} />
      <R0603 name="R34" resistance="1k" schX={14.5} schY={-3.15} />
      <R0603 name="R35" resistance="10k" schX={15.45} schY={-4.0} />
      <chip
        name="Q1"
        manufacturerPartNumber="AO3400A"
        footprint="sot23"
        pinLabels={{ pin1: "G", pin2: "S", pin3: "D" }}
        schPinArrangement={split([1, 2], [3])}
        schX={16.75}
        schY={-3.45}
      />
      <D_SOD323 name="D11" manufacturerPartNumber="1N4148W" footprint="sod123" schX={18.0} schY={-3.0} />
      <D_SOD323 name="D12" manufacturerPartNumber="BZT52C5V1" footprint="sod123" schX={18.0} schY={-3.8} />
      <trace from="J8.GPIO10_MUTE" to="net.GPIO10_MUTE" />
      <trace from="R34.pin1" to="net.GPIO10_MUTE" />
      <trace from="R34.pin2" to="net.RELAY_GATE" />
      <trace from="R35.pin1" to="net.RELAY_GATE" />
      <trace from="R35.pin2" to="net.DGND" />
      <trace from="Q1.G" to="net.RELAY_GATE" />
      <trace from="Q1.S" to="net.DGND" />
      <trace from="Q1.D" to="net.RELAY_COIL_LOW" />
      <trace from="K1.COIL_P" to="net.V5_RELAY_FUSED" />
      <trace from="K2.COIL_P" to="net.V5_RELAY_FUSED" />
      <trace from="K1.COIL_N" to="net.RELAY_COIL_LOW" />
      <trace from="K2.COIL_N" to="net.RELAY_COIL_LOW" />
      <trace from="D11.anode" to="net.RELAY_COIL_LOW" />
      <trace from="D11.cathode" to="net.RELAY_CLAMP_MID" />
      <trace from="D12.cathode" to="net.RELAY_CLAMP_MID" />
      <trace from="D12.anode" to="net.V5_RELAY_FUSED" />
      {integratedCodec && <>
        <schematictext text="7. INTEGRATED DA7212 CODEC / I2S / CLOCK / 1.8 V" schX={23.8} schY={7.35} fontSize={0.28} />
        <chip
          name="U6"
          manufacturerPartNumber="DA7212-01UM2"
          footprint={<Da7212Footprint />}
          pinLabels={{
            pin1: "HPCSP", pin2: "HPCSN", pin3: "GND_CP", pin4: "HPCFP",
            pin5: "HP_L", pin6: "HPCFN", pin7: "GND_SENSE", pin8: "BCLK",
            pin9: "HP_R", pin10: "DATIN", pin11: "VDD_A", pin12: "WCLK",
            pin13: "DACREF", pin14: "DATOUT", pin15: "VREF", pin16: "SCL",
            pin17: "VMID", pin18: "SDA", pin19: "GND_A", pin20: "VDD_IO",
            pin21: "VDD_SP", pin22: "MCLK", pin23: "SP_P", pin24: "VDIG",
            pin25: "SP_N", pin26: "AUX_L", pin27: "VDD_MIC", pin28: "AUX_R",
            pin29: "MICBIAS1", pin30: "MIC2_N", pin31: "MIC1_N", pin32: "MIC2_P",
            pin33: "MICBIAS2", pin34: "MIC1_P",
          }}
          schPinArrangement={split(
            [26, 28, 5, 9, 1, 2, 4, 6, 13, 15, 17, 11, 27, 24, 3, 7, 19],
            [8, 12, 10, 14, 16, 18, 22, 20, 21, 23, 25, 29, 30, 31, 32, 33, 34],
          )}
          noConnect={["pin21", "pin23", "pin25", "pin29", "pin30", "pin31", "pin32", "pin33", "pin34"]}
          schX={24.0}
          schY={4.2}
        />
        <chip
          name="U7"
          manufacturerPartNumber="TPS7A2018PDBVR"
          footprint="sot23_5"
          pinLabels={{ pin1: "IN", pin2: "GND", pin3: "EN", pin4: "NC", pin5: "OUT" }}
          schPinArrangement={split([1, 3, 2], [5, 4])}
          noConnect={["pin4"]}
          schX={21.0}
          schY={6.2}
        />
        <chip
          name="U8"
          manufacturerPartNumber="ASFL1-12.288MHZ-EC-T"
          footprint={<Asfl1Footprint />}
          pinLabels={{ pin1: "OE", pin2: "GND", pin3: "OUT", pin4: "VDD" }}
          schPinArrangement={split([1, 2, 4], [3])}
          schX={28.2}
          schY={6.15}
        />
        <R0603 name="R39" resistance="33R" schX={29.6} schY={6.15} />
        <R0603 name="R40" resistance="33R" schX={29.9} schY={4.95} />
        <R0603 name="R41" resistance="33R" schX={29.9} schY={4.55} />
        <R0603 name="R42" resistance="33R" schX={29.9} schY={4.15} />
        <R0603 name="R43" resistance="33R" schX={29.9} schY={3.75} />
        <C0603 name="C30" capacitance="1uF X5R" schX={20.0} schY={5.35} />
        <C0603 name="C31" capacitance="1uF X5R" schX={22.0} schY={5.35} />
        <C0603 name="C32" capacitance="1uF X5R" footprint="0201" schX={21.8} schY={2.0} />
        <C0603 name="C33" capacitance="1uF X5R" footprint="0201" schX={22.6} schY={2.0} />
        <C0603 name="C34" capacitance="1uF X5R" footprint="0201" schX={25.2} schY={1.9} />
        <C0603 name="C35" capacitance="1uF X5R" footprint="0201" schX={26.0} schY={1.9} />
        <C0603 name="C36" capacitance="1uF X5R" footprint="0201" schX={26.8} schY={1.9} />
        <C0603 name="C37" capacitance="1uF X5R" footprint="0201" schX={27.6} schY={1.9} />
        <C0603 name="C38" capacitance="1uF X5R" footprint="0201" schX={28.4} schY={1.9} />
        <C0603 name="C39" capacitance="1uF X5R" footprint="0201" schX={29.2} schY={1.9} />
        <C0603 name="C40" capacitance="100nF X7R" schX={27.3} schY={5.35} />
        <C0603 name="C41" capacitance="1uF X5R" footprint="0201" schX={23.4} schY={2.0} />
        <C0603 name="C42" capacitance="1uF X5R" schX={28.1} schY={5.35} />
        <chip
          name="J10"
          manufacturerPartNumber="I2S CODEC TEST POINTS DNI"
          doNotPlace
          footprint="pinrow6_rows1_p2.54mm_id1mm_od1.7mm_male"
          pinLabels={{ pin1: "MCLK", pin2: "BCLK", pin3: "WCLK", pin4: "DATIN", pin5: "DATOUT", pin6: "GND" }}
          schPinArrangement={left([1, 2, 3, 4, 5, 6])}
          schX={31.3}
          schY={3.8}
        />

        <trace from="U7.IN" to="net.V3V3" />
        <trace from="U7.EN" to="net.V3V3" />
        <trace from="U7.GND" to="net.AGND" />
        <trace from="U7.OUT" to="net.V1V8_CODEC" />
        <trace from="C30.pin1" to="net.V3V3" />
        <trace from="C30.pin2" to="net.AGND" />
        <trace from="C31.pin1" to="net.V1V8_CODEC" />
        <trace from="C31.pin2" to="net.AGND" />
        <trace from="U6.VDD_A" to="net.V1V8_CODEC" />
        <trace from="U6.VDD_IO" to="net.V3V3" />
        <trace from="U6.VDD_MIC" to="net.V3V3" />
        <trace from="U6.GND_A" to="net.AGND" />
        <trace from="U6.GND_CP" to="net.AGND" />
        <trace from="U6.GND_SENSE" to="net.AGND" />
        <trace from="C32.pin1" to="net.V1V8_CODEC" />
        <trace from="C32.pin2" to="net.AGND" />
        <trace from="C33.pin1" to="net.V3V3" />
        <trace from="C33.pin2" to="net.AGND" />
        <trace from="C41.pin1" to="U6.VDD_MIC" />
        <trace from="C41.pin2" to="net.AGND" />
        <trace from="C34.pin1" to="U6.VDIG" />
        <trace from="C34.pin2" to="net.AGND" />
        <trace from="C35.pin1" to="U6.DACREF" />
        <trace from="C35.pin2" to="net.AGND" />
        <trace from="C36.pin1" to="U6.VREF" />
        <trace from="C36.pin2" to="net.AGND" />
        <trace from="C37.pin1" to="U6.VMID" />
        <trace from="C37.pin2" to="net.AGND" />
        <trace from="C38.pin1" to="U6.HPCSP" />
        <trace from="C38.pin2" to="U6.HPCSN" />
        <trace from="C39.pin1" to="U6.HPCFP" />
        <trace from="C39.pin2" to="U6.HPCFN" />
        <trace from="U8.VDD" to="net.V3V3" />
        <trace from="U8.OE" to="net.V3V3" />
        <trace from="U8.GND" to="net.AGND" />
        <trace from="U8.OUT" to="net.MCLK_RAW" />
        <trace from="R39.pin1" to="net.MCLK_RAW" />
        <trace from="R39.pin2" to="net.CODEC_MCLK" />
        <trace from="U6.MCLK" to="net.CODEC_MCLK" />
        <trace from="C40.pin1" to="net.V3V3" />
        <trace from="C40.pin2" to="net.AGND" />
        <trace from="C42.pin1" to="net.V3V3" />
        <trace from="C42.pin2" to="net.AGND" />
        <trace from="U6.AUX_L" to="net.CODEC_IN_L" />
        <trace from="U6.AUX_R" to="net.CODEC_IN_R" />
        <trace from="U6.HP_L" to="net.CODEC_OUT_L" />
        <trace from="U6.HP_R" to="net.CODEC_OUT_R" />
        <trace from="U6.BCLK" to="R40.pin1" />
        <trace from="R40.pin2" to="net.I2S_BCLK" />
        <trace from="J8.GPIO18_I2S_CLK" to="net.I2S_BCLK" />
        <trace from="U6.WCLK" to="R41.pin1" />
        <trace from="R41.pin2" to="net.I2S_WCLK" />
        <trace from="J8.GPIO19_I2S_FS" to="net.I2S_WCLK" />
        <trace from="U6.DATOUT" to="R42.pin1" />
        <trace from="R42.pin2" to="net.I2S_CODEC_TO_PI" />
        <trace from="J8.GPIO20_I2S_DIN" to="net.I2S_CODEC_TO_PI" />
        <trace from="J8.GPIO21_I2S_DOUT" to="R43.pin1" />
        <trace from="R43.pin2" to="net.I2S_PI_TO_CODEC" />
        <trace from="U6.DATIN" to="net.I2S_PI_TO_CODEC" />
        <trace from="U6.SDA" to="net.I2C_SDA_HOST" />
        <trace from="U6.SCL" to="net.I2C_SCL_HOST" />
        <trace from="J10.MCLK" to="net.CODEC_MCLK" />
        <trace from="J10.BCLK" to="net.I2S_BCLK" />
        <trace from="J10.WCLK" to="net.I2S_WCLK" />
        <trace from="J10.DATIN" to="net.I2S_PI_TO_CODEC" />
        <trace from="J10.DATOUT" to="net.I2S_CODEC_TO_PI" />
        <trace from="J10.GND" to="net.AGND" />
        <schematictext text="DA7212: 12.288 MHz master clock, 1.8 V analogue rail, 3.3 V I/O; VDD_SP and microphone paths unused." schX={24.0} schY={0.8} fontSize={0.19} />
      </>}

      <schematictext
        text="GPIO10 HIGH closes K1/K2 only after codec clocks and bias are stable; GPIO10 defaults LOW at reset."
        schX={13.4}
        schY={-5.05}
        fontSize={0.19}
      />
      <schematictext
        text="Use isolated-bushing audio jacks. Bond CHASSIS at J9; fit R31 only after hum/EMC verification."
        schX={-12.8}
        schY={-6.7}
        fontSize={0.2}
      />
      <schematictext
        text="U1 is one OPA4377AIPWR in TSSOP-14; place 100nF directly at pins 4 and 11."
        schX={-12.8}
        schY={-7.15}
        fontSize={0.2}
      />
      <schematictext
        text="MIDI cable side is isolated: never join MIDI_TIP/RING/SHIELD to AGND/DGND. No extra I2C pull-ups fitted."
        schX={-12.8}
        schY={-7.6}
        fontSize={0.2}
      />
      <schematictext
        text={integratedCodec
          ? "Rev C J2/J3 are DNI bring-up headers. DA7212 input divider provides active-pickup headroom; compensate in codec gain."
          : "Codec P1/P2 pin order (square pin 1): LEFT, GND, RIGHT, GND. AUX maximum: 1 V RMS."}
        schX={-12.8}
        schY={-8.05}
        fontSize={0.2}
      />
    </board>,
  )
}

type JsonElement = Record<string, any> & { type: string }
type Point = { x: number; y: number }

const revBSheets = [
  { id: "schematic_sheet_gpio_header", name: "gpio_header", index: 1 },
  { id: "schematic_sheet_power_reference", name: "power_reference", index: 2 },
  { id: "schematic_sheet_guitar_input", name: "guitar_input", index: 3 },
  { id: "schematic_sheet_codec_output_processing", name: "codec_output_processing", index: 4 },
  { id: "schematic_sheet_relay_outputs", name: "relay_outputs", index: 5 },
  { id: "schematic_sheet_midi_input", name: "midi_input", index: 6 },
  { id: "schematic_sheet_expression_input", name: "expression_input", index: 7 },
  { id: "schematic_sheet_output_mute", name: "output_mute", index: 8 },
] as const

type RevBSheetName = (typeof revBSheets)[number]["name"]

function sheetFromPosition({ x, y }: Point): RevBSheetName {
  if (y > 3.7) return x < -8 ? "gpio_header" : "power_reference"
  if (y > -1.7) return x < 5 ? "guitar_input" : x < 12 ? "codec_output_processing" : "relay_outputs"
  if (x < 0) return "midi_input"
  if (x < 13.2) return "expression_input"
  return "output_mute"
}

const revBComponentSheets: Record<RevBSheetName, string[]> = {
  gpio_header: ["J8", "F1", "F2"],
  power_reference: [
    "FB1", "C20", "C21", "U2", "C22", "C23", "C24", "U3", "C25", "C26",
    "J9", "R30", "C27", "R31",
  ],
  guitar_input: [
    "J1", "D1", "R1", "C1", "R2", "R3", "C2", "D2", "C3", "R4", "R5",
    "C4", "R6", "R7", "J2", "C28",
  ],
  codec_output_processing: ["J3", "C5", "R8", "C6", "R9", "U1", "R10", "R11", "R12", "C29"],
  relay_outputs: [
    "C7", "R13", "C8", "R14", "C9", "R15", "R36", "R37", "R38", "K1", "K2",
    "J4", "J5", "R16", "R17", "R18", "D3", "D4", "D5",
  ],
  midi_input: ["J6", "D6", "D13", "D14", "D15", "D16", "R19", "U4", "D7", "R20", "R21", "C10", "R22", "C11"],
  expression_input: [
    "J7", "D8", "D9", "SW1", "D17", "R23", "C12", "R24", "C13", "D10", "R25",
    "R26", "U5", "R27", "R28", "R29", "R32", "R33", "C14", "C15", "D18",
  ],
  output_mute: ["R34", "R35", "Q1", "D11", "D12"],
}

function elementPosition(element: JsonElement): Point | undefined {
  if (element.center) return element.center
  if (element.position) return element.position
  if (element.anchor_position) return element.anchor_position
  if (element.edges?.length) {
    const points = element.edges.flatMap((edge: { from: Point; to: Point }) => [edge.from, edge.to])
    return {
      x: points.reduce((sum: number, point: Point) => sum + point.x, 0) / points.length,
      y: points.reduce((sum: number, point: Point) => sum + point.y, 0) / points.length,
    }
  }
  return undefined
}

function spreadVerticalPins(
  circuitJson: JsonElement[],
  component: JsonElement,
  spacing: number,
) {
  const ports = circuitJson.filter(
    (element) => element.type === "schematic_port" &&
      element.schematic_component_id === component.schematic_component_id,
  )
  const verticalSides = ["left", "right"]
  const oldPortPositions = new Map<string, Point>()
  const portDeltas = new Map<string, Point>()
  const oldHeight = component.size.height
  let requiredHeight = oldHeight

  for (const side of verticalSides) {
    const sidePorts = ports
      .filter((port) => port.side_of_component === side)
      .sort((a, b) => b.center.y - a.center.y)
    if (sidePorts.length === 0) continue
    requiredHeight = Math.max(requiredHeight, sidePorts.length * spacing + 0.2)
    for (const [index, port] of sidePorts.entries()) {
      const oldPosition = { ...port.center }
      const newPosition = {
        x: oldPosition.x,
        y: component.center.y + ((sidePorts.length - 1) / 2 - index) * spacing,
      }
      oldPortPositions.set(port.schematic_port_id, oldPosition)
      portDeltas.set(port.schematic_port_id, {
        x: newPosition.x - oldPosition.x,
        y: newPosition.y - oldPosition.y,
      })
      port.center = newPosition
    }
  }

  component.size.height = requiredHeight
  component.pin_spacing = spacing
  const heightIncrease = requiredHeight - oldHeight
  for (const text of circuitJson.filter(
    (element) => element.type === "schematic_text" &&
      element.schematic_component_id === component.schematic_component_id,
  )) {
    text.position.y += text.position.y >= component.center.y ? heightIncrease / 2 : -heightIncrease / 2
  }

  const nearestDelta = (point: Point): Point | undefined => {
    let best: { distance: number; delta: Point } | undefined
    for (const [portId, oldPosition] of oldPortPositions) {
      const distance = Math.hypot(point.x - oldPosition.x, point.y - oldPosition.y)
      if (!best || distance < best.distance) best = { distance, delta: portDeltas.get(portId)! }
    }
    return best && best.distance <= 0.45 ? best.delta : undefined
  }

  for (const element of circuitJson) {
    if (element.type === "schematic_net_label") {
      const delta = nearestDelta(element.anchor_position)
      if (!delta) continue
      element.anchor_position = {
        x: element.anchor_position.x + delta.x,
        y: element.anchor_position.y + delta.y,
      }
      element.center = { x: element.center.x + delta.x, y: element.center.y + delta.y }
    }
    if (element.type === "schematic_trace") {
      for (const edge of element.edges) {
        for (const endpoint of [edge.from, edge.to]) {
          const delta = nearestDelta(endpoint)
          if (!delta) continue
          endpoint.x += delta.x
          endpoint.y += delta.y
        }
      }
      for (const junction of element.junctions ?? []) {
        const delta = nearestDelta(junction)
        if (!delta) continue
        junction.x += delta.x
        junction.y += delta.y
      }
    }
  }
}

/**
 * Create documentation sheets from the settled flat Rev B schematic.
 *
 * tscircuit currently does not propagate a <schematicsheet> id to trace
 * primitives. Assigning sheet ownership after routing keeps the authoritative
 * netlist and PCB source JSON unchanged; the documentation wires are then
 * rebuilt as deterministic orthogonal paths within each focused sheet.
 */
function createRevBSheetJson(circuitJson: JsonElement[]): JsonElement[] {
  const result = structuredClone(circuitJson)
  const sheetIdByName = new Map(revBSheets.map((sheet) => [sheet.name, sheet.id]))
  const sheetNameByComponentName = new Map< string, RevBSheetName>()
  for (const [sheetName, names] of Object.entries(revBComponentSheets) as [RevBSheetName, string[]][]) {
    for (const name of names) sheetNameByComponentName.set(name, sheetName)
  }
  const sourceComponents = new Map(
    result
      .filter((element) => element.type === "source_component")
      .map((element) => [element.source_component_id, element]),
  )
  const schematicComponents = new Map(
    result
      .filter((element) => element.type === "schematic_component")
      .map((element) => [element.schematic_component_id, element]),
  )
  const initialSchematicPorts = result.filter((element) => element.type === "schematic_port")
  const netLabelSheetNames = new Map<string, RevBSheetName>()
  for (const label of result.filter((element) => element.type === "schematic_net_label")) {
    const nearestPort = initialSchematicPorts
      .map((port) => ({
        port,
        distance: Math.hypot(
          label.anchor_position.x - port.center.x,
          label.anchor_position.y - port.center.y,
        ),
      }))
      .sort((a, b) => a.distance - b.distance)[0]
    const component = nearestPort
      ? schematicComponents.get(nearestPort.port.schematic_component_id)
      : undefined
    const componentName = component
      ? String(sourceComponents.get(component.source_component_id)?.name ?? "")
      : ""
    const sheetName = sheetNameByComponentName.get(componentName)
    if (sheetName && nearestPort.distance < 0.75) {
      netLabelSheetNames.set(label.schematic_net_label_id, sheetName)
    }
  }
  const componentSheetIds = new Map<string, string>()

  for (const component of schematicComponents.values()) {
    const sourceComponent = sourceComponents.get(component.source_component_id)
    const componentName = String(sourceComponent?.name ?? "")
    const sheetName = sheetNameByComponentName.get(componentName)
    if (!sheetName) throw new Error(`No Rev B sheet assigned to component ${componentName}`)
    const sheetId = sheetIdByName.get(sheetName)!
    component.schematic_sheet_id = sheetId
    componentSheetIds.set(component.schematic_component_id, sheetId)
  }

  // Relay symbols carry four pins per side. Give those pins enough vertical
  // pitch that names, pin numbers, and the two changeover channels cannot
  // visually collapse into one another on the focused output sheet.
  for (const component of schematicComponents.values()) {
    const componentName = String(sourceComponents.get(component.source_component_id)?.name ?? "")
    if (componentName === "K1" || componentName === "K2") {
      spreadVerticalPins(result, component, 0.48)
    }
  }

  for (const element of result) {
    if (!element.type.startsWith("schematic_") || element.type === "schematic_group") continue
    if (element.type === "schematic_component") continue

    if (element.schematic_component_id) {
      const sheetId = componentSheetIds.get(element.schematic_component_id)
      if (!sheetId) throw new Error(`Missing sheet owner for ${element.type}`)
      element.schematic_sheet_id = sheetId
      continue
    }

    if (element.type === "schematic_trace") {
      const match = String(element.source_trace_id ?? "").match(/^([^.]+)\..*-([^.]+)\./)
      const fromSheet = match ? sheetNameByComponentName.get(match[1]) : undefined
      const toSheet = match ? sheetNameByComponentName.get(match[2]) : undefined
      if (fromSheet && toSheet && fromSheet !== toSheet) {
        continue
      }
      if (fromSheet && fromSheet === toSheet) {
        element.schematic_sheet_id = sheetIdByName.get(fromSheet)!
        continue
      }
    }

    const position = elementPosition(element)
    if (!position) continue
    let sheetName: RevBSheetName
    if (element.type === "schematic_net_label") {
      sheetName = netLabelSheetNames.get(element.schematic_net_label_id) ?? sheetFromPosition(position)
    } else {
      sheetName = sheetFromPosition(position)
    }

    // The flat drawing keeps general assembly notes in a footer. Route each
    // note to the feature page it documents instead of its geometric quadrant.
    if (element.type === "schematic_text") {
      const text = String(element.text ?? "")
      if (text.startsWith("ARDOR ") || text.startsWith("Rev B")) sheetName = "gpio_header"
      if (text.startsWith("1.") || text.startsWith("Use isolated-bushing")) sheetName = "power_reference"
      if (text.startsWith("2.")) sheetName = "guitar_input"
      if (text.startsWith("3.") || text.startsWith("U1 ") || text.startsWith("Codec ")) sheetName = "codec_output_processing"
      if (text.startsWith("4.") || text.startsWith("MIDI ")) sheetName = "midi_input"
      if (text.startsWith("5.")) sheetName = "expression_input"
      if (text.startsWith("6.") || text.startsWith("GPIO10 ")) sheetName = "output_mute"

      if (text.startsWith("3.")) element.text = text.replace("3.", "3A.")
      if (text.startsWith("Use isolated-bushing")) element.position = { x: -6.2, y: 4.65 }
      if (text.startsWith("U1 ")) element.position = { x: 6.0, y: -2.05 }
      if (text.startsWith("Codec ")) element.position = { x: 6.0, y: -2.45 }
    }
    element.schematic_sheet_id = sheetIdByName.get(sheetName)!
  }

  // sourceComponents is deliberately resolved here: it catches malformed
  // component ownership before the KiCad converter gets an opaque reference.
  for (const component of schematicComponents.values()) {
    if (!sourceComponents.has(component.source_component_id)) {
      throw new Error(`Missing source component for ${component.schematic_component_id}`)
    }
  }

  const sourceNets = new Map(
    result
      .filter((element) => element.type === "source_net")
      .map((element) => [element.source_net_id, element]),
  )
  const netSheetNames = new Map<string, Set<RevBSheetName>>()
  for (const sourcePort of result.filter(
    (element) => element.type === "source_port" && !element.do_not_connect,
  )) {
    const componentName = String(sourceComponents.get(sourcePort.source_component_id)?.name ?? "")
    const sheetName = sheetNameByComponentName.get(componentName)
    const key = sourcePort.subcircuit_connectivity_map_key
    if (!sheetName || !key) continue
    if (!netSheetNames.has(key)) netSheetNames.set(key, new Set())
    netSheetNames.get(key)!.add(sheetName)
  }
  const syntheticLocalTraces: JsonElement[] = []
  for (const [key, sheetNames] of netSheetNames) {
    if (sheetNames.size !== 1) continue
    const sheetName = [...sheetNames][0]
    const netPorts = result
      .filter((element) =>
        element.type === "source_port" &&
        !element.do_not_connect &&
        element.subcircuit_connectivity_map_key === key,
      )
      .map((sourcePort) => result.find((element) =>
        element.type === "schematic_port" && element.source_port_id === sourcePort.source_port_id,
      ))
      .filter(Boolean)
      .sort((a, b) => a!.center.x - b!.center.x || b!.center.y - a!.center.y) as JsonElement[]
    if (netPorts.length < 2) continue
    const edges: Array<{ from: Point; to: Point }> = []
    for (let index = 1; index < netPorts.length; index += 1) {
      const from = netPorts[index - 1].center
      const to = netPorts[index].center
      if (Math.abs(from.y - to.y) < 0.001 || Math.abs(from.x - to.x) < 0.001) {
        edges.push({ from: { ...from }, to: { ...to } })
      } else {
        const midX = (from.x + to.x) / 2
        edges.push(
          { from: { ...from }, to: { x: midX, y: from.y } },
          { from: { x: midX, y: from.y }, to: { x: midX, y: to.y } },
          { from: { x: midX, y: to.y }, to: { ...to } },
        )
      }
    }
    syntheticLocalTraces.push({
      type: "schematic_trace",
      schematic_trace_id: `schematic_trace_local_${syntheticLocalTraces.length}`,
      edges,
      junctions: [],
      subcircuit_connectivity_map_key: key,
      schematic_sheet_id: sheetIdByName.get(sheetName)!,
    })
  }
  const filteredResult = result.filter((element) => {
    if (element.type === "schematic_trace") {
      // Rebuild every within-sheet connection below. This makes the focused
      // sheets deterministic and orthogonal even when the flat autorouter
      // chose a path that was sensible only in the original overview.
      return false
    }
    if (element.type === "schematic_net_label") {
      const sourceNet = sourceNets.get(element.source_net_id)
      const sheetNames = sourceNet
        ? netSheetNames.get(sourceNet.subcircuit_connectivity_map_key)
        : undefined
      if (sheetNames?.size === 1) return false
    }
    return true
  })
  filteredResult.push(...syntheticLocalTraces)
  filteredResult.push({
    type: "schematic_text",
    schematic_text_id: "schematic_text_gpio_sheet_title",
    text: "GPIO HEADER / PIN ALLOCATION / FUSED 5 V ENTRY",
    position: { x: -12.8, y: 7.35 },
    anchor: "left",
    rotation: 0,
    color: "#006464",
    font_size: 0.28,
    schematic_sheet_id: sheetIdByName.get("gpio_header")!,
  })
  filteredResult.push({
    type: "schematic_text",
    schematic_text_id: "schematic_text_relay_outputs_title",
    text: "3B. FAIL-SAFE STEREO LINE + MONO GUITAR AMP OUTPUTS",
    position: { x: 9.2, y: 6.0 },
    anchor: "left",
    rotation: 0,
    color: "#006464",
    font_size: 0.28,
    schematic_sheet_id: sheetIdByName.get("relay_outputs")!,
  })
  filteredResult.push(...revBSheets.map((sheet) => ({
    type: "schematic_sheet",
    schematic_sheet_id: sheet.id,
    name: sheet.name,
    sheet_index: sheet.index,
  })))
  return filteredResult
}

async function writePdf(svgs: string[], path: URL) {
  const doc = new PDFDocument({ autoFirstPage: false, margin: 18 })
  const stream = createWriteStream(path)
  doc.pipe(stream)
  for (const svg of svgs) {
    doc.addPage({ size: "A3", layout: "landscape", margin: 18 })
    SVGtoPDF(doc, svg, 18, 18, { width: 1154, height: 806, preserveAspectRatio: "xMidYMid meet" })
  }
  doc.end()
  await new Promise<void>((resolve, reject) => {
    stream.on("finish", resolve)
    stream.on("error", reject)
  })
}

function stackSvgs(svgs: string[], labels: string[], width: number, height: number) {
  const labelHeight = 42
  const gap = 18
  const panelHeight = height + labelHeight
  const body = svgs.map((svg, index) => {
    const inner = svg.match(/<svg[^>]*>([\s\S]*)<\/svg>/)?.[1]
    if (!inner) throw new Error(`Could not compose SVG sheet ${labels[index]}`)
    const y = index * (panelHeight + gap)
    return `<g transform="translate(0 ${y})"><rect width="${width}" height="${panelHeight}" fill="white"/><text x="20" y="29" font-family="sans-serif" font-size="22" font-weight="600" fill="#202124">${index + 1}. ${labels[index]}</text><g transform="translate(0 ${labelHeight})">${inner}</g></g>`
  }).join("")
  const totalHeight = svgs.length * panelHeight + Math.max(0, svgs.length - 1) * gap
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${width}" height="${totalHeight}" viewBox="0 0 ${width} ${totalHeight}">${body}</svg>`
}

function cleanGeneratedSvg(svg: string) {
  return `${svg.split("\n").map((line) => line.trimEnd()).join("\n").trim()}\n`
}

export async function generateProject({
  integratedCodec = false,
  outputDirectory = new URL("./", import.meta.url),
  projectName = "control-io",
}: SchematicOptions & { outputDirectory?: URL; projectName?: string } = {}) {
  mkdirSync(outputDirectory, { recursive: true })
  const flatCircuit = new Circuit()
  addSchematic(flatCircuit, { integratedCodec })
  await flatCircuit.renderUntilSettled()
  const circuitJson = flatCircuit.getCircuitJson() as JsonElement[]

  const schematicJson = integratedCodec ? circuitJson : createRevBSheetJson(circuitJson)

  writeFileSync(
    new URL(`${projectName}.circuit.json`, outputDirectory),
    `${JSON.stringify(circuitJson, null, 2)}\n`,
  )
  if (!integratedCodec) {
    writeFileSync(
      new URL(`${projectName}.sheets.circuit.json`, outputDirectory),
      `${JSON.stringify(schematicJson, null, 2)}\n`,
    )
  }

  const converter = new CircuitJsonToKicadSchConverter(schematicJson)
  converter.runUntilFinished()
  for (const file of converter.getOutputFiles({ schematicFilename: `${projectName}.kicad_sch` })) {
    writeFileSync(new URL(file.filename, outputDirectory), file.content)
  }

  const libraryConverter = new CircuitJsonToKicadLibraryConverter(circuitJson, {
    libraryName: "tscircuit",
    footprintLibraryName: "tscircuit",
  })
  libraryConverter.runUntilFinished()
  const footprintDirectory = new URL("tscircuit.pretty/", outputDirectory)
  mkdirSync(footprintDirectory, { recursive: true })
  for (const footprint of libraryConverter.getFootprints()) {
    writeFileSync(
      new URL(`${footprint.footprintName}.kicad_mod`, footprintDirectory),
      footprint.kicadModString,
    )
  }
  writeFileSync(
    new URL("fp-lib-table", outputDirectory),
    libraryConverter.getFpLibTableString(),
  )
  writeFileSync(
    new URL("sym-lib-table", outputDirectory),
    libraryConverter.getSymLibTableString(),
  )
  writeFileSync(
    new URL("tscircuit.kicad_sym", outputDirectory),
    libraryConverter.getSymbolLibraryString(),
  )

  const projectConverter = new CircuitJsonToKicadProConverter(schematicJson, {
    projectName,
    schematicFilename: `${projectName}.kicad_sch`,
    schematicSheetPlan: converter.schematicSheetPlan,
  })
  projectConverter.runUntilFinished()
  const project = JSON.parse(projectConverter.getOutputString())
  project.head.created = "2026-08-22T00:00:00.000Z"
  project.head.modified = project.head.created
  writeFileSync(
    new URL(`${projectName}.kicad_pro`, outputDirectory),
    `${JSON.stringify(project, null, 2)}\n`,
  )

  const schematicSheets = schematicJson
    .filter((element) => element.type === "schematic_sheet")
    .sort((a, b) => (a.sheet_index ?? 0) - (b.sheet_index ?? 0))
  const svgOptions = {
    width: 1600,
    height: 1000,
    includeVersion: true,
    showErrorsInTextOverlay: false,
    css: `
      .sch-net-label { fill: transparent !important; stroke: none !important; }
      .sch-net-label-text { font-size: 9px !important; fill: #7b2d2d !important; }
      .sch-net-label-symbol-text { font-size: 9px !important; }
    `,
  }
  const sheetSvgs = schematicSheets.map((sheet) => {
    const focusedSheetJson = schematicJson.filter((element) =>
      !element.type.startsWith("schematic_") ||
      (element.type !== "schematic_sheet" && element.schematic_sheet_id === sheet.schematic_sheet_id),
    )
    return cleanGeneratedSvg(convertCircuitJsonToSchematicSvg(focusedSheetJson, svgOptions))
  })
  const svg = schematicSheets.length > 0
    ? cleanGeneratedSvg(stackSvgs(sheetSvgs, schematicSheets.map((sheet) => sheet.name), svgOptions.width, svgOptions.height))
    : cleanGeneratedSvg(convertCircuitJsonToSchematicSvg(schematicJson, svgOptions))
  writeFileSync(new URL(`${projectName}.svg`, outputDirectory), svg)
  for (const [index, sheet] of schematicSheets.entries()) {
    writeFileSync(
      new URL(`${projectName}-${String(index + 1).padStart(2, "0")}-${sheet.name}.svg`, outputDirectory),
      sheetSvgs[index],
    )
  }
  await writePdf(sheetSvgs.length > 0 ? sheetSvgs : [svg], new URL(`${projectName}.pdf`, outputDirectory))
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  generateProject().catch((error) => {
    console.error(error)
    process.exitCode = 1
  })
}
