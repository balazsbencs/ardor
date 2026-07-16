#pragma once
#include "waveshaper.h"

namespace pedal {
// Backward-compatible alias. Existing Init() / SetDrive() / Process() calls
// work unchanged; default curve is SoftClip (same behavior as before).
using Saturation = WaveShaper;
} // namespace pedal
