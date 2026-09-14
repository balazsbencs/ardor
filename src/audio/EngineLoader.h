#pragma once

#include "dsp/PedalEngine.h"
#include "preset/ChainPlan.h"
#include "preset/PresetStore.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ardor {

struct EngineLoadOptions {
  uint32_t sampleRate = 48000;
  uint32_t blockSize = 64;
  size_t irSamples = 8192;
  bool parallelRigs = false;
  int rigWorkerCpu = -1;

  // WDW uses two independent lane workers when parallelRigs is enabled.  The
  // legacy single-worker fields above remain unchanged for Dual Rig.
  int wdwAudioCpu = -1;
  int wdwDryWorkerCpu = -1;
  int wdwWetWorkerCpu = -1;
  std::size_t wdwPipelineSlots = 3;

  // When a preset is loaded, the caller supplies the directory that owns its
  // model, cabinet, reverb, and wah assets.  An empty root preserves the
  // lower-level plan APIs used by offline tools and tests; applyPreset()
  // always fills this from its dataRoot before loading user-selected assets.
  std::filesystem::path assetRoot;

  // Analog dBu RMS at the ADC that corresponds to a 0 dBFS-peak 1 kHz sine.
  std::optional<float> inputReferenceLevelDbU;
};

bool applyChainPlan(PedalEngine& engine, const ChainPlan& plan, const EngineLoadOptions& options, std::string& error);

// Prepares one already-validated RuntimeChain on the control thread.  This is
// shared by the fixed WDW builder and the legacy Dual Rig loader; it never
// performs work from the realtime callback.
bool prepareRuntimeChain(RuntimeChain& chain, const std::vector<ChainBlockPlan>& blocks,
                         const EngineLoadOptions& options, std::string& error);

bool preflightPreset(const Preset& preset, const std::filesystem::path& dataRoot,
                     const EngineLoadOptions& options, std::string& error);

// Checks a stored preset's structural constraints and cabinet asset before a
// caller changes the live audio program. Daisy processor state is owned by
// each instance, so preparing a replacement is safe while another runs.
bool preflightPresetSlot(const PresetStore& store, PresetSlot slot,
                         const std::filesystem::path& dataRoot,
                         const EngineLoadOptions& options, std::string& error);

bool applyPreset(PedalEngine& engine, const Preset& preset, const std::filesystem::path& dataRoot,
                 const EngineLoadOptions& options, std::string& error);
bool applyPresetSlot(PedalEngine& engine, const PresetStore& store, PresetSlot slot,
                     const std::filesystem::path& dataRoot, const EngineLoadOptions& options, std::string& error);

} // namespace ardor
