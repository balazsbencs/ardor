import { readFileSync } from "node:fs"

type Element = Record<string, unknown> & { type: string }
type Port = Element & {
  source_port_id: string
  source_component_id: string
  name: string
  do_not_connect?: boolean
  port_hints?: string[]
  subcircuit_connectivity_map_key?: string
}

const circuit = JSON.parse(
  readFileSync(new URL("control-io.circuit.json", import.meta.url), "utf8"),
) as Element[]
const sheetCircuit = JSON.parse(
  readFileSync(new URL("control-io.sheets.circuit.json", import.meta.url), "utf8"),
) as Element[]

const components = new Map(
  circuit
    .filter((element) => element.type === "source_component")
    .map((component) => [component.source_component_id as string, component]),
)

const ports = circuit.filter(
  (element): element is Port => element.type === "source_port",
)

function port(selector: string): Port {
  const [componentName, portName] = selector.split(".")
  const component = [...components.values()].find(
    (candidate) => candidate.name === componentName,
  )
  if (!component) throw new Error(`Missing component ${componentName}`)
  const result = ports.find(
    (candidate) =>
      candidate.source_component_id === component.source_component_id &&
      (candidate.name === portName || candidate.port_hints?.includes(portName)),
  )
  if (!result) throw new Error(`Missing port ${selector}`)
  return result
}

function sameNet(...selectors: string[]) {
  const selected = selectors.map(port)
  const keys = new Set(
    selected.map((candidate) => candidate.subcircuit_connectivity_map_key),
  )
  if (keys.size !== 1 || keys.has(undefined)) {
    throw new Error(`Expected same net: ${selectors.join(", ")}`)
  }
}

function differentNet(a: string, b: string) {
  const pa = port(a)
  const pb = port(b)
  if (
    !pa.subcircuit_connectivity_map_key ||
    pa.subcircuit_connectivity_map_key === pb.subcircuit_connectivity_map_key
  ) {
    throw new Error(`Expected isolation between ${a} and ${b}`)
  }
}

function expectPart(name: string, manufacturerPartNumber: string) {
  const component = [...components.values()].find(
    (candidate) => candidate.name === name,
  )
  if (!component) throw new Error(`Missing component ${name}`)
  if (component.manufacturer_part_number !== manufacturerPartNumber) {
    throw new Error(
      `Expected ${name} to be ${manufacturerPartNumber}, got ${String(component.manufacturer_part_number)}`,
    )
  }
}

const unexpectedErrors = circuit.filter(
  (element) => element.type.endsWith("_error"),
)
if (unexpectedErrors.length > 0) {
  throw new Error(
    `Unexpected tscircuit errors: ${[...new Set(unexpectedErrors.map((e) => e.type))].join(", ")}`,
  )
}

const dangling = ports.filter(
  (candidate) =>
    !candidate.do_not_connect && !candidate.subcircuit_connectivity_map_key,
)
if (dangling.length > 0) {
  throw new Error(
    `Unconnected ports: ${dangling
      .map((candidate) => `${components.get(candidate.source_component_id)?.name}.${candidate.name}`)
      .join(", ")}`,
  )
}

const connectedPortGroups = Object.groupBy(
  ports.filter((candidate) => !candidate.do_not_connect),
  (candidate) => candidate.subcircuit_connectivity_map_key ?? "missing",
)
const singletonNets = Object.entries(connectedPortGroups).filter(
  ([key, members]) => key !== "missing" && members.length < 2,
)
if (singletonNets.length > 0) {
  throw new Error(`Singleton connectivity nets: ${singletonNets.map(([key]) => key).join(", ")}`)
}

for (const component of components.values()) {
  if (!["simple_resistor", "simple_capacitor", "simple_diode", "simple_fuse"].includes(String(component.ftype))) continue
  const componentPorts = ports.filter(
    (candidate) => candidate.source_component_id === component.source_component_id && !candidate.do_not_connect,
  )
  if (componentPorts.length !== 2) {
    throw new Error(`Expected two active pins on ${String(component.name)}`)
  }
  if (
    componentPorts[0].subcircuit_connectivity_map_key ===
    componentPorts[1].subcircuit_connectivity_map_key
  ) {
    throw new Error(`Passive component ${String(component.name)} is shorted across one net`)
  }
}

sameNet("J8.5V", "F1.pin1")
sameNet("J8.GPIO10_MUTE", "R34.pin1")
if (!port("J8.GPIO4").do_not_connect) {
  throw new Error("GPIO4 must remain unconnected; it can boot high on Raspberry Pi 4")
}
sameNet("J8.GPIO9_UART4_RX", "R21.pin2")
sameNet("U4.VO", "R20.pin2", "R21.pin1")
sameNet("J8.GPIO2_SDA", "R25.pin1")
sameNet("R25.pin2", "U5.SDA")
sameNet("J8.GPIO3_SCL", "R26.pin1")
sameNet("R26.pin2", "U5.SCL")
sameNet("J8.GPIO25_ADC_ALERT", "R33.pin2")

sameNet("J1.TIP", "D1.pin2", "R1.pin1")
sameNet("J2.LEFT", "R4.pin2", "R5.pin1")
sameNet("J2.RIGHT", "R6.pin2", "R7.pin1")
sameNet("J3.LEFT", "C5.pin1")
sameNet("J3.RIGHT", "C6.pin1")
sameNet("J4.TIP_L", "K1.NO_L", "R16.pin1", "D3.pin2")
sameNet("J4.RING_R", "K1.NO_R", "R17.pin1", "D4.pin2")
sameNet("J5.TIP", "K2.NO_A", "K2.NO_B", "R18.pin1", "D5.pin2")
sameNet("F2.pin2", "K1.COIL_P", "K2.COIL_P", "D12.pin1")
sameNet("K1.pin1", "K1.COIL_P")
sameNet("K1.pin10", "K1.COIL_N")
sameNet("K1.pin3", "K1.COM_L")
sameNet("K1.pin4", "K1.NO_L")
sameNet("K1.pin8", "K1.COM_R")
sameNet("K1.pin7", "K1.NO_R")
sameNet("K1.COM_L", "R13.pin2", "R36.pin1")
sameNet("K1.COM_R", "R14.pin2", "R37.pin1")
sameNet("K2.COM_A", "K2.COM_B", "R15.pin2", "R38.pin1")
sameNet("C9.pin2", "R15.pin1")
differentNet("R15.pin1", "R15.pin2")

sameNet("J6.TIP", "D13.pin1", "D15.pin2", "D6.pin1")
sameNet("J6.RING", "D14.pin1", "D16.pin2", "D6.pin2")
differentNet("J6.TIP", "J8.GND")
differentNet("J6.RING", "J8.GND")
differentNet("J6.SLEEVE", "J8.GND")
differentNet("U4.LED_A", "U4.GND")

sameNet("J7.TIP", "SW1.COM_A", "D8.pin2")
sameNet("J7.RING", "SW1.COM_B", "D9.pin2")
sameNet("J7.TIP", "SW1.pin3")
sameNet("J7.RING", "SW1.pin6")
sameNet("U5.AIN0", "D10.IO", "C13.pin1", "R24.pin2")
sameNet("D17.pin1", "J8.3V3")
sameNet("D17.pin2", "R23.pin1")
sameNet("D18.pin1", "J9.CHASSIS", "R30.pin1", "C27.pin1")
sameNet("D18.pin2", "R30.pin2", "C27.pin2")

sameNet("U3.pin8", "U3.NOISE_REDUCTION", "C25.pin1")
sameNet("U3.pin2", "U3.COMMON", "C25.pin2")
if (!port("U3.pin5").do_not_connect) {
  throw new Error("TLE2426 physical pin 5 must be marked no-connect")
}

expectPart("U1", "OPA4377AIPWR")
expectPart("U3", "TLE2426IDR")
expectPart("U5", "ADS1115IDGSR (0x48)")
for (const diode of ["D1", "D3", "D4", "D5"]) {
  expectPart(diode, "PESD5V0U1BA-Q")
}
expectPart("D18", "SMBJ5.0CA")
expectPart("D7", "1N4148WS")

const expectedSheets = [
  "gpio_header", "power_reference", "guitar_input", "codec_output_processing",
  "relay_outputs", "midi_input", "expression_input", "output_mute",
]
const sheets = sheetCircuit.filter((element) => element.type === "schematic_sheet")
if (sheets.map((sheet) => sheet.name).join(",") !== expectedSheets.join(",")) {
  throw new Error(`Unexpected Rev B sheet plan: ${sheets.map((sheet) => String(sheet.name)).join(", ")}`)
}
const sheetIds = new Set(sheets.map((sheet) => sheet.schematic_sheet_id))
const sheetOwnedTypes = new Set([
  "schematic_component", "schematic_port", "schematic_trace", "schematic_net_label", "schematic_text",
])
const unownedSchematicElements = sheetCircuit.filter(
  (element) => sheetOwnedTypes.has(element.type) && !sheetIds.has(element.schematic_sheet_id),
)
if (unownedSchematicElements.length > 0) {
  throw new Error(`Schematic elements without a valid sheet: ${unownedSchematicElements.length}`)
}
const diagonalTraces = sheetCircuit.filter(
  (element) => element.type === "schematic_trace" &&
    (element.edges as Array<{ from: { x: number; y: number }; to: { x: number; y: number } }>).some(
      (edge) => Math.abs(edge.from.x - edge.to.x) > 0.001 && Math.abs(edge.from.y - edge.to.y) > 0.001,
    ),
)
if (diagonalTraces.length > 0) {
  throw new Error(`Non-orthogonal documentation traces: ${diagonalTraces.length}`)
}
const r15SourceId = [...components.values()].find((component) => component.name === "R15")?.source_component_id
const c9SourceId = [...components.values()].find((component) => component.name === "C9")?.source_component_id
const sheetComponents = sheetCircuit.filter((element) => element.type === "schematic_component")
const r15SheetId = sheetComponents.find((component) => component.source_component_id === r15SourceId)?.schematic_sheet_id
const c9SheetId = sheetComponents.find((component) => component.source_component_id === c9SourceId)?.schematic_sheet_id
if (!r15SheetId || r15SheetId !== c9SheetId) {
  throw new Error("C9 and R15 must be drawn on the same schematic sheet")
}
const c9Pin2 = port("C9.pin2")
const r15Pin1 = port("R15.pin1")
const sheetPortPosition = (sourcePortId: string) => {
  const candidate = sheetCircuit.find(
    (element) => element.type === "schematic_port" && element.source_port_id === sourcePortId,
  )
  return candidate?.center as { x: number; y: number } | undefined
}
const c9Pin2Position = sheetPortPosition(c9Pin2.source_port_id)
const r15Pin1Position = sheetPortPosition(r15Pin1.source_port_id)
const samePoint = (
  a: { x: number; y: number },
  b: { x: number; y: number },
) => Math.abs(a.x - b.x) < 0.001 && Math.abs(a.y - b.y) < 0.001
if (!c9Pin2Position || !r15Pin1Position || !sheetCircuit.some(
  (element) => element.type === "schematic_trace" &&
    element.schematic_sheet_id === r15SheetId &&
    (element.edges as Array<{ from: { x: number; y: number }; to: { x: number; y: number } }>).some(
      (edge) =>
        (samePoint(edge.from, c9Pin2Position) && samePoint(edge.to, r15Pin1Position)) ||
        (samePoint(edge.to, c9Pin2Position) && samePoint(edge.from, r15Pin1Position)),
    ),
)) {
  throw new Error("The visible C9-to-R15 wire is missing from the Rev B documentation")
}

console.log(
  `Connectivity checks passed: ${components.size} components, ${ports.length} pins, ${sheets.length} readable sheets, MIDI isolation intact.`,
)
