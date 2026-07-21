#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ardor {

// These stages are signal boundaries, not claims about a processor's internal
// nonlinear behavior. A stage overload means its output exceeded digital full
// scale (0 dBFS) before the final safety limiter.
enum class SignalStageKind {
  Input,
  Nam,
  Cab,
  Daisy,
  Compressor,
  Equalizer,
  Output,
};

struct ClipStageSnapshot {
  SignalStageKind kind = SignalStageKind::Input;
  std::string id;
  float peak = 0.0f;
  uint64_t overloadFrames = 0;
};

struct ClipDiagnosticsSnapshot {
  std::vector<ClipStageSnapshot> stages;
  uint64_t limiterFrames = 0;
};

const char* signalStageKindName(SignalStageKind kind) noexcept;
std::string formatClipDiagnostics(const ClipDiagnosticsSnapshot& diagnostics);

} // namespace ardor
