import type { PresetBlock } from "../api/types";
import { findEffectDefinition } from "../effects/catalog";
import { fileStem } from "../ui/format";

// Module codes, as on the pedal's module drawer and chain strip.
export const MODULE_CODES: Record<string, string> = {
  nam: "NAM", dualAmp: "DAMP", dualRig: "RIG", cab: "CAB", irreverb: "IRV",
  "dynamics:compressor": "CMP", "dynamics:noise_gate": "GATE", "dynamics:transient_shaper": "TRN", "eq:parametric_eq_5": "EQ",
  "stereo:widener": "WIDE", "wah:gcb95": "WAH", "distortion:rat": "RAT", "distortion:big_cheese": "FUZZ", "distortion:tape": "TMC",
  "mod:chorus": "CHO", "mod:flanger": "FLG", "mod:rotary": "ROT", "mod:vibe": "VIBE", "mod:phaser": "PHS", "mod:vintage_trem": "TREM",
  "mod:poly_octave": "OCT", "mod:pattern_trem": "PTRM", "mod:auto_swell": "SWL", "mod:filter": "FLT", "mod:ladder_sweep": "LADR",
  "mod:formant": "FORM", "mod:quadrature": "QUAD", "mod:destroyer": "DSTR", "mod:whammy": "WHAM", "mod:harmonizer": "HARM",
  "delay:digital": "DIG", "delay:tape": "TAPE", "delay:dual": "DUAL", "delay:filter": "FDLY", "delay:lofi": "LOFI", "delay:dbucket": "BBD",
  "delay:duck": "DUCK", "delay:pattern": "PAT", "delay:swell": "SWDL", "delay:trem": "TRDL",
  "reverb:room": "ROOM", "reverb:hall": "HALL", "reverb:plate": "PLATE", "reverb:spring": "SPRG", "reverb:bloom": "BLOOM", "reverb:cloud": "CLOUD",
  "reverb:shimmer": "SHIM", "reverb:chorale": "CHRL", "reverb:nonlinear": "NLIN", "reverb:swell": "SWRV", "reverb:magneto": "MAG", "reverb:reflections": "REFL",
};

export function stripCode(block: PresetBlock): string {
  const name = block.asset ? fileStem(block.asset.split("/").pop() ?? block.asset) : "";
  if (block.type === "cab" && name) return (name.split(" ").find((word) => /\d+x\d+/i.test(word)) ?? name.split(" ")[0]).toUpperCase().slice(0, 6);
  if (block.type === "nam" && name) return name.split(" ")[0].toUpperCase().slice(0, 6);
  const definition = findEffectDefinition(block);
  return (definition && MODULE_CODES[definition.id]) ?? block.type.toUpperCase().slice(0, 4);
}
