export type Family = "amp" | "cab" | "util" | "mod" | "dly" | "rev" | "unknown";

const familyByType: Record<string, Family> = {
  nam: "amp", dualAmp: "amp", dualRig: "amp", distortion: "amp",
  cab: "cab",
  dynamics: "util", eq: "util", wah: "util", stereo: "util",
  mod: "mod", delay: "dly", reverb: "rev", irreverb: "rev",
};

// Mirrors labelForBlockType in src/ui/UiModel.cpp, so a card cap reads like the pedal.
const capByType: Record<string, string> = {
  nam: "Neural Amp", cab: "Cab", dualAmp: "Dual Amp", dualRig: "Dual Rig", mod: "Modulation",
  delay: "Delay", reverb: "Reverb", dynamics: "Dynamics", eq: "EQ", wah: "Wah",
  distortion: "Drive", irreverb: "Reverb", stereo: "Stereo",
};

export const familyOf = (blockType: string): Family => familyByType[blockType] ?? "unknown";
export const capFor = (blockType: string): string => capByType[blockType] ?? blockType;
