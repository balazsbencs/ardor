import type { PresetBlock } from "../api/types";
import { createBlockFromDefinition } from "../effects/catalog";

/** A catalog block with a fixed id, for tests. */
export const blockOf = (definitionId: string, id: string): PresetBlock => ({ ...createBlockFromDefinition(definitionId, []), id });
