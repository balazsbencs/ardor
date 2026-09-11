#include "audio/WdwRoutingBuilder.h"
#include "audio/PresetActivation.h"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef ARDOR_NAM_EXAMPLE_MODEL
#error "ARDOR_NAM_EXAMPLE_MODEL must point at a loadable NAM model"
#endif

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

void writeMonoWav(const std::filesystem::path& path)
{
  const float impulse[] = {1.0f, 0.25f, -0.1f, 0.05f};
  ma_encoder_config config =
    ma_encoder_config_init(ma_encoding_format_wav, ma_format_f32, 1, 48000);
  ma_encoder encoder;
  require(ma_encoder_init_file(path.string().c_str(), &config, &encoder) == MA_SUCCESS,
          "could not create builder smoke cabinet");
  ma_encoder_write_pcm_frames(&encoder, impulse, 4, nullptr);
  ma_encoder_uninit(&encoder);
}

ardor::ChainBlockPlan namBlock(const char* id, const std::filesystem::path& modelPath)
{
  ardor::ChainBlockPlan block;
  block.id = id;
  block.type = "nam";
  block.status = ardor::ChainBlockStatus::Ready;
  block.assetPath = modelPath;
  return block;
}

ardor::ChainBlockPlan cabBlock(const char* id, const std::filesystem::path& path)
{
  ardor::ChainBlockPlan block;
  block.id = id;
  block.type = "cab";
  block.status = ardor::ChainBlockStatus::Ready;
  block.assetPath = path;
  return block;
}

} // namespace

int main(int argc, char** argv)
{
  try {
    const auto root = std::filesystem::temp_directory_path()
      / ("ardor-wdw-builder-"
         + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto cabPath = root / "cab.wav";
    writeMonoWav(cabPath);
    const std::filesystem::path modelPath = argc > 1
      ? std::filesystem::path(argv[1])
      : std::filesystem::path(ARDOR_NAM_EXAMPLE_MODEL);

    ardor::ChainPlan dry;
    dry.blocks.push_back(namBlock("dry-nam", modelPath));
    dry.blocks.push_back(cabBlock("dry-cab", cabPath));
    ardor::ChainBlockPlan dryDistortion;
    dryDistortion.id = "dry-rat";
    dryDistortion.type = "distortion";
    dryDistortion.status = ardor::ChainBlockStatus::Ready;
    dryDistortion.params = {{"mode", "rat"}};
    dry.blocks.push_back(std::move(dryDistortion));
    ardor::ChainPlan wet;
    wet.blocks.push_back(namBlock("wet-nam", modelPath));
    wet.blocks.push_back(cabBlock("wet-cab", cabPath));
    ardor::ChainBlockPlan delay;
    delay.id = "wet-delay";
    delay.type = "delay";
    delay.status = ardor::ChainBlockStatus::Ready;
    delay.params = {{"mode", "digital"}};
    wet.blocks.push_back(std::move(delay));

    ardor::WdwRoutingBuildOptions options;
    options.engine.blockSize = 16;
    options.engine.sampleRate = 48000;
    options.engine.irSamples = 8192;
    options.program.executor.mode = ardor::WdwPairExecutionMode::Direct;
    options.program.executor.requireWorkerSetup = false;
    options.program.executor.requireRealtimeScheduling = false;
    options.program.executor.requireAffinity = false;
    options.program.mix = {1.0f, 0.0f, true, 0.0f, 1.0f, false};
    options.calibrationBlocks = 64;
    options.calibrationThreshold = 1.0e-10f;

    std::unique_ptr<ardor::WdwRoutingProgram> program;
    ardor::WdwRoutingBuildReport report;
    std::string error;
    require(ardor::buildWdwRoutingProgram(dry, wet, options, program, report, error), error);
    require(program && program->prepared(), "builder did not return a prepared program");
    require(report.latencyCalibrated
              && report.totalLatencyFrames == std::max(report.dryLatencyFrames,
                                                       report.wetLatencyFrames)
              && program->latencyFrames() == report.totalLatencyFrames,
            "direct builder report should contain calibrated lane latency");

    std::vector<float> input(options.engine.blockSize, 0.25f);
    std::vector<float> left(input.size(), 0.0f);
    std::vector<float> right(input.size(), 0.0f);
    ardor::WdwRoutingProcessResult processResult;
    require(program->processBlock(input.data(), left.data(), right.data(), input.size(),
                                  processResult),
            "built WDW program rejected its prepared block");
    require(processResult.outputReady && processResult.pairReady,
            "direct builder did not publish a complete pair");
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
      require(std::isfinite(left[frame]) && std::isfinite(right[frame]),
              "built WDW output is non-finite");
    }

    // The admission policy catches accidental cross-lane ID reuse before any
    // model worker is started.
    auto duplicateWet = wet;
    duplicateWet.blocks[0].id = "dry-nam";
    program.reset();
    require(!ardor::buildWdwRoutingProgram(dry, duplicateWet, options, program, report, error)
              && error.find("globally unique block IDs") != std::string::npos,
            "builder accepted duplicate cross-lane block IDs");

    // Time effects are intentionally confined to the wet lane in this first
    // product topology.
    auto invalidWet = wet;
    auto distortion = namBlock("wet-distortion", modelPath);
    distortion.type = "distortion";
    invalidWet.blocks.push_back(std::move(distortion));
    require(!ardor::buildWdwRoutingProgram(dry, invalidWet, options, program, report, error)
              && error.find("wet lane does not admit distortion") != std::string::npos,
            "builder admitted a dry-only effect in the wet lane");

    auto disabledNam = dry;
    disabledNam.blocks[0].status = ardor::ChainBlockStatus::Disabled;
    disabledNam.blocks[0].enabled = false;
    require(!ardor::buildWdwRoutingProgram(disabledNam, wet, options, program, report, error)
              && error.find("cannot disable its required nam") != std::string::npos,
            "builder accepted a disabled required NAM");

    auto disabledCab = wet;
    disabledCab.blocks[1].status = ardor::ChainBlockStatus::Disabled;
    disabledCab.blocks[1].enabled = false;
    require(ardor::buildWdwRoutingProgram(dry, disabledCab, options, program, report, error),
            "builder rejected an optional disabled cab: " + error);

    // Full-chain NAM captures already contain the cabinet response.  Both
    // lanes must therefore be runnable with NAM-only chains; a separate cab
    // remains an optional stage for head-only captures.
    auto dryNamOnly = dry;
    dryNamOnly.blocks.erase(dryNamOnly.blocks.begin() + 1);
    auto wetNamOnly = wet;
    wetNamOnly.blocks.erase(wetNamOnly.blocks.begin() + 1);
    program.reset();
    require(ardor::buildWdwRoutingProgram(dryNamOnly, wetNamOnly, options,
                                           program, report, error),
            "builder rejected full-chain NAM-only WDW lanes: " + error);
    require(program && program->prepared(), "NAM-only WDW program was not prepared");

    // The production worker contract requires two explicit pinned CPUs when
    // affinity checks are enabled.
    auto productionOptions = options;
    productionOptions.program.executor.mode = ardor::WdwPairExecutionMode::Pipelined;
    productionOptions.program.executor.requireAffinity = true;
    productionOptions.dryWorkerCpu = -1;
    productionOptions.wetWorkerCpu = -1;
    require(!ardor::buildWdwRoutingProgram(dry, wet, productionOptions,
                                             program, report, error)
              && error.find("two pinned worker CPUs") != std::string::npos,
            "builder admitted pipelined WDW without pinned workers");

    ardor::PedalEngine engine;
    require(ardor::applyWdwRouting(engine, dry, wet, options, report, error),
            "applyWdwRouting failed: " + error);
    require(engine.wdwRoutingEnabled(), "applyWdwRouting did not publish WDW");

    std::unique_ptr<ardor::PedalEngine> liveEngine = std::make_unique<ardor::PedalEngine>();
    const auto activation = ardor::prepareAndActivateWdwDraft(
      liveEngine, dry, wet, options, 0.5f,
      [](ardor::PedalEngine& candidate) {
        return candidate.wdwRoutingEnabled()
          ? ardor::EngineReplaceResult::Activated
          : ardor::EngineReplaceResult::DeviceStopped;
      }, &report);
    require(activation.activated() && liveEngine && liveEngine->wdwRoutingEnabled(),
            "WDW draft activation did not publish the prepared candidate");

    auto* previousEngine = liveEngine.get();
    const auto rejected = ardor::prepareAndActivateWdwDraft(
      liveEngine, dry, wet, options, 0.5f,
      [](ardor::PedalEngine&) { return ardor::EngineReplaceResult::Busy; });
    require(rejected.status == ardor::PresetActivationStatus::BackendRejected
              && liveEngine.get() == previousEngine,
            "backend rejection replaced the live WDW engine");

    std::filesystem::remove_all(root);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "wdw_routing_builder_smoke failed: " << error.what() << '\n';
    return 1;
  }
}
