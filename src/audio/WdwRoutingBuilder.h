#pragma once

#include "audio/EngineLoader.h"
#include "dsp/WdwRoutingProgram.h"

#include <cstddef>
#include <memory>
#include <string>

namespace ardor {

// Control-thread inputs for constructing the fixed two-lane wet/dry/wet
// program.  This is deliberately a plan-level API rather than a preset/UI
// schema: callers can validate and prepare a candidate before deciding how it
// should be represented or activated.
struct WdwRoutingBuildOptions {
  EngineLoadOptions engine;
  WdwRoutingProgramOptions program;
  int dryWorkerCpu = -1;
  int wetWorkerCpu = -1;

  // First-arrival probing is part of preparation so the final mixer can align
  // a short dry path with a longer cab/effect path.  Disabling it is intended
  // only for deterministic tools that provide declared latency metadata in
  // program.dryLatencyFrames/wetLatencyFrames.
  bool calibrateLatencies = true;
  std::size_t calibrationBlocks = 256;
  float calibrationThreshold = 1.0e-7f;

  WdwRoutingBuildOptions()
  {
    // A production candidate should use the dedicated lane workers.  Tests
    // may select Direct explicitly when they need a scalar/reference path.
    program.executor.mode = WdwPairExecutionMode::Pipelined;
  }
};

struct WdwRoutingBuildReport {
  std::size_t dryLatencyFrames = 0;
  std::size_t wetLatencyFrames = 0;
  std::size_t totalLatencyFrames = 0;
  bool latencyCalibrated = false;
};

// Validates the fixed product topology, prepares both RuntimeChains, probes
// their first-arrival latency, and returns an immutable WdwRoutingProgram.
// No audio-thread state is touched by this function.  On failure `program`
// remains null and the error explains which admission rule rejected the plan.
bool buildWdwRoutingProgram(const ChainPlan& dryPlan, const ChainPlan& wetPlan,
                           const WdwRoutingBuildOptions& options,
                           std::unique_ptr<WdwRoutingProgram>& program,
                           WdwRoutingBuildReport& report, std::string& error);

// Convenience activation helper matching applyChainPlan.  The candidate is
// fully prepared in a temporary PedalEngine and published only after all WDW
// validation, model/IR loading, latency calibration, and worker setup pass.
bool applyWdwRouting(PedalEngine& engine, const ChainPlan& dryPlan,
                     const ChainPlan& wetPlan,
                     const WdwRoutingBuildOptions& options,
                     WdwRoutingBuildReport& report, std::string& error);

} // namespace ardor
