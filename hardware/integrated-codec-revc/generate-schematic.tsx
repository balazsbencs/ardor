import { generateProject } from "../control-io/generate-schematic.js"

generateProject({
  integratedCodec: true,
  outputDirectory: new URL("./", import.meta.url),
  projectName: "ardor-revc",
}).catch((error) => {
  console.error(error)
  process.exitCode = 1
})
