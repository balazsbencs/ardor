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

expectPart("U1", "OPA4377AIPWR")
expectPart("U3", "TLE2426IDR")
expectPart("U5", "ADS1115IDGSR (0x48)")
for (const diode of ["D1", "D3", "D4", "D5"]) {
  expectPart(diode, "PESD5V0U1BA-Q")
}
expectPart("D18", "SMBJ5.0CA")

console.log(
  `Connectivity checks passed: ${components.size} components, ${ports.length} pins, MIDI isolation intact.`,
)
