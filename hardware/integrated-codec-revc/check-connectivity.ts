import { readFileSync } from "node:fs"

type Element = Record<string, unknown> & { type: string }
type Port = Element & {
  source_component_id: string
  name: string
  port_hints?: string[]
  do_not_connect?: boolean
  subcircuit_connectivity_map_key?: string
}

const circuit = JSON.parse(
  readFileSync(new URL("ardor-revc.circuit.json", import.meta.url), "utf8"),
) as Element[]
const components = circuit.filter((element) => element.type === "source_component")
const ports = circuit.filter((element): element is Port => element.type === "source_port")
const pcbComponents = circuit.filter((element) => element.type === "pcb_component")
const pcbPads = circuit.filter((element) => element.type === "pcb_smtpad")

function component(name: string) {
  const found = components.find((candidate) => candidate.name === name)
  if (!found) throw new Error(`Missing component ${name}`)
  return found
}

function port(selector: string) {
  const [componentName, portName] = selector.split(".")
  const source = component(componentName)
  const found = ports.find(
    (candidate) => candidate.source_component_id === source.source_component_id &&
      (candidate.name === portName || candidate.port_hints?.includes(portName)),
  )
  if (!found) throw new Error(`Missing port ${selector}`)
  return found
}

function sameNet(...selectors: string[]) {
  const keys = new Set(selectors.map((selector) => port(selector).subcircuit_connectivity_map_key))
  if (keys.size !== 1 || keys.has(undefined)) {
    throw new Error(`Expected same net: ${selectors.join(", ")}`)
  }
}

const errors = circuit.filter((element) => element.type.endsWith("_error"))
if (errors.length) throw new Error(`tscircuit errors: ${[...new Set(errors.map((error) => error.type))].join(", ")}`)

if (component("U6").manufacturer_part_number !== "DA7212-01UM2") {
  throw new Error("U6 must be DA7212-01UM2")
}
sameNet("U6.AUX_L", "J2.LEFT")
sameNet("U6.AUX_R", "J2.RIGHT")
sameNet("U6.HP_L", "J3.LEFT")
sameNet("U6.HP_R", "J3.RIGHT")
sameNet("U6.BCLK", "R40.pin1")
sameNet("R40.pin2", "J8.GPIO18_I2S_CLK")
sameNet("U6.WCLK", "R41.pin1")
sameNet("R41.pin2", "J8.GPIO19_I2S_FS")
sameNet("U6.DATOUT", "R42.pin1")
sameNet("R42.pin2", "J8.GPIO20_I2S_DIN")
sameNet("J8.GPIO21_I2S_DOUT", "R43.pin1")
sameNet("R43.pin2", "U6.DATIN")
sameNet("U6.SDA", "J8.GPIO2_SDA")
sameNet("U6.SCL", "J8.GPIO3_SCL")
sameNet("U6.MCLK", "R39.pin2")
sameNet("U6.VDD_A", "U7.OUT")
sameNet("U6.VDD_IO", "J8.3V3")
sameNet("U6.VDD_MIC", "C41.pin1")
sameNet("U6.GND_A", "U6.GND_CP", "U6.GND_SENSE", "J8.GND")
if (!port("U6.VDD_SP").do_not_connect) throw new Error("Unused speaker supply must be NC")

const u6 = component("U6")
const u6Pcb = pcbComponents.find((candidate) => candidate.source_component_id === u6.source_component_id)
if (!u6Pcb) throw new Error("Missing U6 PCB component")
const u6Pads = pcbPads.filter((candidate) => candidate.pcb_component_id === u6Pcb.pcb_component_id)
if (u6Pads.length !== 34) throw new Error(`U6 footprint must have 34 pads, found ${u6Pads.length}`)
const expectedBalls = [
  "A1", "C1", "B2", "D2", "A3", "C3", "B4", "D4", "A5", "C5", "B6", "D6",
  "A7", "C7", "B8", "D8", "A9", "C9", "B10", "D10", "A11", "C11", "B12", "D12",
  "A13", "C13", "B14", "D14", "A15", "C15", "B16", "D16", "A17", "C17",
]
for (const [index, ball] of expectedBalls.entries()) {
  const pad = u6Pads.find((candidate) =>
    Array.isArray(candidate.port_hints) && candidate.port_hints.includes(String(index + 1)))
  if (!pad || !(pad.port_hints as unknown[]).includes(ball) || pad.radius !== 0.125) {
    throw new Error(`U6 pad ${index + 1} must map to ${ball} with 0.125 mm radius`)
  }
}

for (const name of ["J2", "J3", "J10", "R31"]) {
  const source = component(name)
  const pcb = pcbComponents.find((candidate) => candidate.source_component_id === source.source_component_id)
  if (!pcb?.do_not_place) throw new Error(`${name} must be marked DNP`)
}

console.log(`Rev C checks passed: ${components.length} components, integrated DA7212 I2S/audio/power paths verified.`)
