import { readFileSync, writeFileSync } from "node:fs"

const source = new URL("../../docs/assets/gpio-controls.svg", import.meta.url)
const destination = new URL("gpio-wiring-revc.svg", import.meta.url)

let svg = readFileSync(source, "utf8")

const replacements: Array<[string, string]> = [
  ["Ardor Raspberry Pi, Codec Zero, audio and control wiring", "Ardor Rev C Raspberry Pi and integrated DA7212 wiring"],
  ["Complete wiring reference showing every Raspberry Pi 40-pin header function, the companion audio and control board GPIO connections, Codec Zero P1 and P2 pin order, external guitar, line, amplifier, MIDI and expression jacks, footswitches, encoder, grounds and chassis bond.", "Complete Rev C wiring reference showing every Raspberry Pi 40-pin header function, onboard DA7212 I2S and I2C connections, external guitar, line, amplifier, MIDI and expression jacks, footswitches, encoder, grounds and chassis bond."],
  ["Raspberry Pi 4B + Codec Zero + audio/control companion board · header view from above, USB/Ethernet ports to the right", "Raspberry Pi + Ardor Rev C integrated DA7212 audio/control HAT · header view from above, USB/Ethernet ports to the right"],
  ["13 GPIO27 / Codec SW", "13 GPIO27 / FREE"],
  ["Codec LED / GPIO23 16", "FREE / GPIO23 16"],
  ["Codec LED / GPIO24 18", "FREE / GPIO24 18"],
  ["<circle cx=\"286\" cy=\"0\" r=\"9\" class=\"encoder\"/><circle cx=\"340\" cy=\"0\" r=\"9\" class=\"reserved\"/><text x=\"364\" y=\"4\" class=\"pin-text\">I2S CLK / GPIO18 12</text>", "<circle cx=\"286\" cy=\"0\" r=\"9\" class=\"encoder\"/><circle cx=\"340\" cy=\"0\" r=\"9\" class=\"audio\"/><text x=\"364\" y=\"4\" class=\"pin-text\">I2S BCLK / GPIO18 12</text>"],
  ["<text x=\"262\" y=\"4\" text-anchor=\"end\" class=\"pin-text\">13 GPIO27 / FREE</text><circle cx=\"286\" cy=\"0\" r=\"9\" class=\"reserved\"/>", "<text x=\"262\" y=\"4\" text-anchor=\"end\" class=\"pin-text\">13 GPIO27 / FREE</text><circle cx=\"286\" cy=\"0\" r=\"9\" class=\"free\"/>"],
  ["<circle cx=\"340\" cy=\"0\" r=\"9\" class=\"reserved\"/><text x=\"364\" y=\"4\" class=\"pin-text\">FREE / GPIO23 16</text>", "<circle cx=\"340\" cy=\"0\" r=\"9\" class=\"free\"/><text x=\"364\" y=\"4\" class=\"pin-text\">FREE / GPIO23 16</text>"],
  ["<circle cx=\"340\" cy=\"0\" r=\"9\" class=\"reserved\"/><text x=\"364\" y=\"4\" class=\"pin-text\">FREE / GPIO24 18</text>", "<circle cx=\"340\" cy=\"0\" r=\"9\" class=\"free\"/><text x=\"364\" y=\"4\" class=\"pin-text\">FREE / GPIO24 18</text>"],
  ["<text x=\"262\" y=\"4\" text-anchor=\"end\" class=\"pin-text\">35 GPIO19 / I2S FS</text><circle cx=\"286\" cy=\"0\" r=\"9\" class=\"reserved\"/>", "<text x=\"262\" y=\"4\" text-anchor=\"end\" class=\"pin-text\">35 GPIO19 / I2S WCLK</text><circle cx=\"286\" cy=\"0\" r=\"9\" class=\"audio\"/>"],
  ["<circle cx=\"340\" cy=\"0\" r=\"9\" class=\"reserved\"/><text x=\"364\" y=\"4\" class=\"pin-text\">I2S DIN / GPIO20 38</text>", "<circle cx=\"340\" cy=\"0\" r=\"9\" class=\"audio\"/><text x=\"364\" y=\"4\" class=\"pin-text\">I2S DIN / GPIO20 38</text>"],
  ["<circle cx=\"340\" cy=\"0\" r=\"9\" class=\"reserved\"/><text x=\"364\" y=\"4\" class=\"pin-text\">I2S DOUT / GPIO21 40</text>", "<circle cx=\"340\" cy=\"0\" r=\"9\" class=\"audio\"/><text x=\"364\" y=\"4\" class=\"pin-text\">I2S DOUT / GPIO21 40</text>"],
  ["Companion board J8 taps", "Rev C HAT header J8"],
  ["The stack-through header passes every pin to Codec Zero.", "J8 connects the Pi directly to the onboard codec and protected I/O."],
  ["Pi pin 24, GPIO8/UART4 TX reserved; no connection", "Pi 12/35/38/40 → DA7212 BCLK/WCLK/DATOUT/DATIN"],
  ["Reserved by Codec Zero / HAT", "Onboard codec and HAT reservations"],
  ["GPIO18/19/20/21 (pins 12/35/38/40): I2S audio · GPIO23/24 (pins 16/18): Codec LEDs", "GPIO18/19/20/21 (pins 12/35/38/40): DA7212 BCLK / WCLK / DATOUT / DATIN"],
  ["GPIO27 (pin 13): Codec button · GPIO0/1 (pins 27/28): HAT EEPROM ID bus", "GPIO0/1 (pins 27/28): reserve for optional HAT ID EEPROM · GPIO23/24/27 are free"],
  ["GPIO2/3 I2C is shared intentionally. Do not add another strong set of pull-ups on the companion board.", "GPIO2/3 I2C is shared by DA7212 and ADS1115. Pi pull-ups are used; do not fit another strong pair."],
  ["The board remains muted until software drives default-low GPIO10 HIGH after Codec Zero has settled.", "The board remains muted until software drives default-low GPIO10 HIGH after DA7212 clocks and bias settle."],
  ["Physical audio and control harnesses", "Physical audio and control connections"],
  ["Audio + control companion board", "Ardor Rev C audio + control HAT"],
  ["Codec input harness", "DA7212 AUX input"],
  ["J2 → Codec Zero P1", "U1A → 10k/10k → U6 AUX L/R"],
  ["Codec output harness", "DA7212 HP output"],
  ["Codec Zero P2 → J3", "U6 HP L/R → U1B/C/D"],
  ["Raspberry Pi Codec Zero", "Onboard DA7212 codec"],
  ["P1 AUX IN — from companion J2", "AUX INPUT — from protected guitar buffer"],
  ["P2 AUX OUT — to companion J3", "HEADPHONE DAC — to line/amp buffers"],
  ["Both AUX interfaces: maximum 1 V RMS.", "U7: quiet 1.8 V analogue rail · U8: 12.288 MHz MCLK"],
  ["1 LEFT", "AUX_L"],
  ["2 GND", "AGND"],
  ["3 RIGHT", "AUX_R"],
  ["4 GND", "AGND"],
  ["square pad", "J2 DNI"],
]

for (const [before, after] of replacements) {
  if (!svg.includes(before)) throw new Error(`Wiring SVG source changed; missing: ${before}`)
  svg = svg.replaceAll(before, after)
}

const hpBlockStart = svg.indexOf('<g transform="translate(1170,1021)">')
const hpBlockEnd = svg.indexOf("</g>", hpBlockStart)
if (hpBlockStart < 0 || hpBlockEnd < 0) throw new Error("Missing DA7212 output block")
const hpBlock = svg.slice(hpBlockStart, hpBlockEnd)
  .replaceAll("AUX_L", "HP_L")
  .replaceAll("AUX_R", "HP_R")
  .replaceAll("J2 DNI", "J3 DNI")
svg = `${svg.slice(0, hpBlockStart)}${hpBlock}${svg.slice(hpBlockEnd)}`

svg = svg.replace(
  '<circle cx="654" cy="490" r="6" class="reserved"/><text x="672" y="495">Pi 12/35/38/40 → DA7212 BCLK/WCLK/DATOUT/DATIN</text>',
  '<circle cx="654" cy="490" r="6" class="audio"/><text x="672" y="495">Pi 12/35/38/40 → DA7212 BCLK/WCLK/DATOUT/DATIN</text>',
)

svg = svg.replace(
  '<text x="1170" y="1083" class="small">U7: quiet 1.8 V analogue rail · U8: 12.288 MHz MCLK</text>',
  '<text x="1170" y="1083" class="small">U7: quiet 1.8 V analogue rail · U8: 12.288 MHz MCLK</text><text x="1170" y="1102" class="small">I2C: SDA pin 3 · SCL pin 5 · address 0x1A</text>',
)

writeFileSync(destination, svg)
