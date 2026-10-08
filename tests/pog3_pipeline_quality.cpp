#include "pog3_pipeline_probe.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void run(unsigned frames, unsigned delayBlocks, bool allocations) {
  constexpr unsigned blocks = 256;
  ardor::pog3::Pog3Processor direct, pipelined;
  std::string error;
  const nlohmann::json params{{"dry_level", .25}, {"down2_level", .25},
    {"down1_level", .25}, {"fifth_level", .25}, {"up1_level", .25},
    {"up2_level", .25}, {"dry_attack", 1}, {"detune", .2}, {"spread", .4}};
  require(direct.configure(params, 48000, error), error.c_str());
  require(pipelined.configure(params, 48000, error), error.c_str());
  const auto initial = direct.targetValues();
  std::atomic<bool> hold{false};
  pog3_probe::Worker worker(pipelined, blocks + delayBlocks);
  require(worker.configure(frames, -1, false, error, delayBlocks), error.c_str());
  worker.monitorAllocations = allocations;
  std::array<float, 128> left{}, right{}, outputLeft{}, outputRight{};
  std::array<std::array<float, 128>, 2> previousLeft{}, previousRight{};
  double energy = 0;
  std::size_t callbackAllocations = 0, callbackReleases = 0;
  for (unsigned pass = 0; pass < 2; ++pass) {
    // Reset adopts current targets. The drain submits one extra zero block,
    // so restore equal targets before comparing a fresh lifecycle.
    pog3_probe::apply(direct, initial); pog3_probe::apply(pipelined, initial);
    worker.reset(); direct.reset();
    for (auto& values : previousLeft) values.fill(0);
    for (auto& values : previousRight) values.fill(0);
    for (unsigned b = 0; b < blocks + delayBlocks; ++b) {
      auto targets = initial;
      using ardor::pog3::Parameter;
      targets[ardor::pog3::index(Parameter::Focus)] = (b / 16) % 2;
      targets[ardor::pog3::index(Parameter::Attack)] = b % 5 * .08f;
      targets[ardor::pog3::index(Parameter::MasterLevel)] = .25f + (b % 7) * .025f;
      targets[ardor::pog3::index(Parameter::Up1Pan)] = b % 9 / 8.0f;
      targets[ardor::pog3::index(Parameter::FilterFrequency)] = .6f + b % 4 * .1f;
      for (unsigned i = 0; i < frames; ++i) {
        const double t = (b * frames + i) / 48000.0;
        // Distinct stereo channels and a zero tail exercise delayed startup,
        // automation, control-cadence crossings, and complete draining.
        left[i] = b < blocks - 64 ? .1f * (std::sin(517.772 * t) + std::sin(1231.505 * t)) : 0;
        right[i] = b < blocks - 64 ? .08f * (std::sin(693.002 * t) - std::sin(1553.316 * t)) : 0;
      }
      require(worker.waitForIdle(), "offline worker completion timeout");
      std::size_t a = 0, f = 0;
      if (allocations) pog3_malloc_begin();
      const bool ok = worker.processBlock(left.data(), right.data(), outputLeft.data(),
                                          outputRight.data(), frames, targets);
      if (allocations) pog3_malloc_end(&a, &f);
      callbackAllocations += a; callbackReleases += f;
      require(ok, "generation/order failure");
      const bool exact = std::memcmp(outputLeft.data(), previousLeft[b % delayBlocks].data(), frames * sizeof(float)) == 0
           && std::memcmp(outputRight.data(), previousRight[b % delayBlocks].data(), frames * sizeof(float)) == 0;
      if (!exact) std::cerr << "mismatch pass=" << pass << " block=" << b << " worker="
        << outputLeft[0] << " direct=" << previousLeft[b % delayBlocks][0] << '\n';
      require(exact, "pipeline differs from direct plus fixed block delay");
      if (b < blocks) {
        ardor::ScopedDenormalGuard guard;
        pog3_probe::apply(direct, targets);
        for (unsigned i = 0; i < frames; ++i) {
          const auto y = direct.process({left[i], right[i]}).mixed;
          previousLeft[b % delayBlocks][i] = y.left; previousRight[b % delayBlocks][i] = y.right;
          energy += std::fabs(y.left) + std::fabs(y.right);
        }
      }
    }
    require(worker.waitForIdle(), "drain timeout");
    require(worker.submitted() == blocks + delayBlocks && worker.completed() == blocks + delayBlocks
         && worker.consumed() == blocks, "accepted/completed/consumed accounting");
    require(worker.late() == 0 && worker.submissionMisses() == 0 && worker.wrongOutput() == 0,
            "unexpected pipeline misses");
    require(pipelined.healthy() && pipelined.deadlineMisses() == 0, "worker DSP health");
    for (std::size_t g = 1; g <= worker.completed(); ++g) {
      const auto& job = worker.jobs()[g];
      require(job.startNs >= job.submittedNs && job.endNs >= job.startNs, "job timing order");
      require(job.allocations == 0 && job.releases == 0, "worker allocation/release");
    }
  }
  // A deliberately held first job must fail at its exact due callback, before
  // any new submission or overwrite. Release the gate before checking results.
  worker.reset();
  worker.testHold = &hold;
  hold.store(true, std::memory_order_release);
  const bool first = worker.processBlock(left.data(), right.data(), outputLeft.data(),
                                         outputRight.data(), frames, initial);
  bool startup = first;
  for (unsigned b = 1; b < delayBlocks; ++b)
    startup = worker.processBlock(left.data(), right.data(), outputLeft.data(),
                                   outputRight.data(), frames, initial) && startup;
  const bool late = worker.processBlock(left.data(), right.data(), outputLeft.data(),
                                        outputRight.data(), frames, initial);
  hold.store(false, std::memory_order_release);
  require(startup && !late && worker.late() == 1 && worker.submitted() == delayBlocks
       && worker.consumed() == 0, "late output was concealed or accepted input skipped");
  require(worker.waitForIdle(), "held worker release");
  worker.testHold = nullptr;
  worker.reset();
  require(worker.completed() == 0 && worker.submitted() == 0 && worker.late() == 0,
          "reset retained stale generations");
  require(worker.processBlock(left.data(), right.data(), outputLeft.data(), outputRight.data(),
                               frames, initial), "restart after miss");
  worker.clear(); // Join an in-flight job before destroying its DSP/context.
  require(callbackAllocations == 0 && callbackReleases == 0 && energy > 1,
          "callback allocations or silent fixture");
  std::cout << "frames=" << frames << " exact_delayed_blocks=" << 2 * (blocks + delayBlocks)
            << " delay_blocks=" << delayBlocks << " delay_ms=" << delayBlocks * frames / 48.0
            << " allocations_checked=" << allocations
            << " startup/drain/reset/late/shutdown=pass\n";
}
}
int main(int argc, char** argv) {
  try {
    const bool allocations = argc == 2 && std::string(argv[1]) == "--allocation";
    require(argc == 1 || allocations, "usage: quality [--allocation]");
    if (allocations) require(pog3_malloc_begin && pog3_malloc_end, "C allocation interposer missing");
    for (unsigned frames : {64, 128})
      for (unsigned delay : {1, 2}) run(frames, delay, allocations);
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
