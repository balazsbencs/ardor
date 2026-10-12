#include "daisyfx/DaisyFxProcessor.h"
#include "daisyfx/pog3/Pog3Parameters.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <atomic>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sched.h>
#include <stdexcept>
#include <thread>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" void pog3_global_malloc_begin() __attribute__((weak));
extern "C" void pog3_global_malloc_end(std::size_t*, std::size_t*) __attribute__((weak));

namespace {
void require(bool value, const char* message);
std::atomic<bool> held{false}, entered{false};
static_assert(std::atomic<bool>::is_always_lock_free);
void pauseWorker(int) {
  entered.store(true, std::memory_order_release);
  while (held.load(std::memory_order_acquire)) {}
  entered.store(false, std::memory_order_release);
}
// A deterministic test-only stall: deliver a signal to the actual worker,
// leave the audio caller running, then release it. No production delay hook.
struct WorkerGate {
  struct sigaction previous{};
  WorkerGate() {
    struct sigaction action{};
    action.sa_handler = pauseWorker;
    sigemptyset(&action.sa_mask);
    require(sigaction(SIGUSR1, &action, &previous) == 0, "install worker gate");
  }
  ~WorkerGate() {
    release();
    sigaction(SIGUSR1, &previous, nullptr);
  }
  void block() {
    int tid = -1;
    for (const auto& task : std::filesystem::directory_iterator("/proc/self/task")) {
      std::ifstream name(task.path() / "comm");
      std::string value;
      std::getline(name, value);
      if (value == "ardor-stereo") {
        require(tid == -1, "one production stereo worker");
        tid = std::stoi(task.path().filename().string());
      }
    }
    require(tid >= 0, "production worker thread found");
    held.store(true, std::memory_order_release);
    require(syscall(SYS_tgkill, getpid(), tid, SIGUSR1) == 0, "pause production worker");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!entered.load(std::memory_order_acquire)) {
      require(std::chrono::steady_clock::now() < deadline, "worker gate timeout");
      std::this_thread::yield();
    }
  }
  void release() {
    held.store(false, std::memory_order_release);
    while (entered.load(std::memory_order_acquire)) std::this_thread::yield();
  }
};
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void wait(const ardor::DaisyFxProcessor& worker, std::uint64_t generation) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (worker.workerCompletedGeneration() < generation) {
    require(std::chrono::steady_clock::now() < deadline, "worker completion timeout");
    std::this_thread::yield();
  }
}
void run(unsigned frames, int cpu, bool allocationCheck) {
  constexpr unsigned blocks = 256;
  const unsigned delay = 256 / frames;
  ardor::DaisyFxProcessor direct, worker;
  const nlohmann::json params{{"mode", "pog3"}, {"master_level", .35}};
  std::string error;
  require(direct.configure("mod", params, 48000, error), "direct factory configuration");
  require(worker.configure("mod", params, 48000, error, frames, cpu, false), "worker factory configuration");
  require(worker.isPog3() && worker.latencyFrames() == 256, "common fixed latency");
  require(worker.setParameterTarget(32, 1) && !worker.setParameterTarget(33, 1), "33-index publication");
  require(!worker.setParameterTarget(13, std::numeric_limits<float>::quiet_NaN()), "nonfinite control");
  std::array<float, 128> left{}, right{}, outLeft{}, outRight{};
  std::array<std::array<float, 128>, 8> historyLeft{}, historyRight{};
  float worst = 0;
  double energy = 0;
  for (unsigned pass = 0; pass < 2; ++pass) {
    require(worker.setParameterTarget("master_level", .35f)
         && direct.setParameterTarget("master_level", .35f), "equal reset targets");
    require(worker.setParameterTarget("focus", 0) && direct.setParameterTarget("focus", 0), "reset Focus");
    direct.reset(); worker.reset();
    for (auto& data : historyLeft) data.fill(0);
    for (auto& data : historyRight) data.fill(0);
    for (unsigned b = 0; b < blocks + delay; ++b) {
      // Key/index paths must address the same POG3 slots, independent of the
      // legacy modulation table. Input-dated changes cross 48-sample cadence.
      const float master = b % 43 == 0 ? 0 : .2f + b % 7 * .025f;
      require(worker.setParameterTarget(13, master) && direct.setParameterTarget("master_level", master), "master routing");
      require(worker.setParameterTarget("focus", (b / 16) % 2)
           && direct.setParameterTarget(27, (b / 16) % 2), "Focus routing");
      for (unsigned i = 0; i < frames; ++i) {
        const double t = (b * frames + i) / 48000.0;
        left[i] = b < blocks - 64 ? .1f * (std::sin(517.772 * t) + std::sin(1231.505 * t)) : 0;
        right[i] = b < blocks - 64 ? .08f * (std::sin(693.002 * t) - std::sin(1553.316 * t)) : 0;
      }
      if (allocationCheck) pog3_global_malloc_begin();
      worker.processBlock(left.data(), right.data(), outLeft.data(), outRight.data(), frames);
      for (unsigned i = 0; i < frames; ++i) {
        worst = std::max({worst, std::fabs(outLeft[i] - historyLeft[b % delay][i]),
                                 std::fabs(outRight[i] - historyRight[b % delay][i])});
        require(std::isfinite(outLeft[i]) && std::isfinite(outRight[i]), "finite worker output");
      }
      if (b < blocks) {
        direct.processBlock(left.data(), right.data(), historyLeft[b % delay].data(), historyRight[b % delay].data(), frames);
        for (unsigned i = 0; i < frames; ++i)
          energy += std::fabs(historyLeft[b % delay][i]) + std::fabs(historyRight[b % delay][i]);
      }
      wait(worker, b + 1); // Offline pacing, outside the realtime exchange.
      if (allocationCheck) {
        std::size_t allocations = 0, releases = 0;
        pog3_global_malloc_end(&allocations, &releases);
        require(allocations == 0 && releases == 0, "hosted callback/worker allocated or freed");
      }
      require(worker.workerUnderflowCount() == 0, "no concealed pipeline failures");
    }
  }
  require(worst < 1e-7f && energy > 1, "direct audio plus exactly 256 samples");
  worker.processBlock(left.data(), right.data(), outLeft.data(), outRight.data(), frames - 1);
  require(worker.workerUnderflowCount() == 1, "wrong quantum is reported");
  for (unsigned b = 0; b <= 128 / frames; ++b)
    worker.processBlock(left.data(), right.data(), outLeft.data(), outRight.data(), frames);
  require(std::all_of(outLeft.begin(), outLeft.begin() + frames, [](float x) { return x == 0; })
            && worker.pog3Recovery().state == ardor::pog3::RecoveryState::Bypassed,
          "wrong quantum fades to delayed dry without replaying stale blocks");
  worker.reset();
  worker.processBlock(left.data(), right.data(), outLeft.data(), outRight.data(), frames);
  wait(worker, 1);
  require(worker.workerUnderflowCount() == 0, "reset recovers pipeline ownership");
  std::cout << "frames=" << frames << " delay=256 worst_error=" << worst
            << " key/index/automation/startup/drain/reset/fault=pass\n";
}
void runRecovery(unsigned frames, int cpu, bool allocationCheck) {
  ardor::DaisyFxProcessor worker;
  std::string error;
  require(worker.configure("mod", {{"mode", "pog3"}, {"master_level", .35f},
                                   {"down1_level", 0}, {"up1_level", 0}},
                           48000, error, frames, cpu, false),
          "recovery factory");
  WorkerGate gate;
  std::array<float, 128> left{}, right{}, outLeft{}, outRight{};
  left.fill(.1f); right.fill(-.08f);
  const unsigned delay = 256 / frames;
  std::array<float, 256> dryHistoryLeft{}, dryHistoryRight{};
  std::array<float, 128> expectedDryLeft{}, expectedDryRight{};
  unsigned dryCursor = 0;
  auto process = [&] {
    for (unsigned i = 0; i < frames; ++i) {
      expectedDryLeft[i] = dryHistoryLeft[dryCursor];
      expectedDryRight[i] = dryHistoryRight[dryCursor];
      dryHistoryLeft[dryCursor] = left[i]; dryHistoryRight[dryCursor] = right[i];
      dryCursor = (dryCursor + 1) % 256;
    }
    worker.processBlock(left.data(), right.data(), outLeft.data(), outRight.data(), frames);
  };
  // Warm the core and establish a known nonzero dry fallback history.
  unsigned generation = 0;
  for (unsigned b = 0; b < 32; ++b) { process(); wait(worker, ++generation); }
  gate.block();
  if (allocationCheck) pog3_global_malloc_begin();
  for (unsigned b = 0; b <= delay; ++b) process();
  generation += delay;
  require(worker.workerUnderflowCount() == 1
            && worker.pog3Recovery().state == ardor::pog3::RecoveryState::Waiting,
          "late worker is exposed and one recovery is pending");
  for (unsigned b = 0; b < 16; ++b) {
    left.fill(.1f + b * .001f); right.fill(-.08f - b * .002f);
    process();
    if ((b + 1) * frames >= 128) {
      for (unsigned i = 0; i < frames; ++i)
        require(std::fabs(outLeft[i] - expectedDryLeft[i]) < 1e-7f
                  && std::fabs(outRight[i] - expectedDryRight[i]) < 1e-7f,
                "stalled-worker dry output retains exactly 256 samples of delay");
    }
  }
  left.fill(.1f); right.fill(-.08f);
  require(worker.setParameterTarget("master_level", .25f), "publish controls during recovery");
  gate.release(); wait(worker, generation);
  for (unsigned b = 0; b < delay + 960 / frames + 2; ++b) {
    process(); wait(worker, ++generation);
    for (unsigned i = 0; i < frames; ++i)
      require(std::isfinite(outLeft[i]) && std::isfinite(outRight[i]), "finite recovery output");
  }
  require(worker.pog3Recovery().recoveries == 1
            && worker.pog3Recovery().state == ardor::pog3::RecoveryState::Healthy
            && worker.workerUnderflowCount() == 1,
          "recovery resumes processed output without erasing fault history");
  require(std::fabs(outLeft[frames - 1] - .05f) < 1e-6f
            && std::fabs(outRight[frames - 1] + .04f) < 1e-6f,
          "recovered audio uses the new master target, not dry or old publications");
  if (allocationCheck) {
    std::size_t allocations = 0, releases = 0;
    pog3_global_malloc_end(&allocations, &releases);
    require(allocations == 0 && releases == 0, "worker recovery allocated or freed");
  }
  // A second deadline failure stays dry instead of entering a restart loop.
  gate.block();
  for (unsigned b = 0; b <= delay; ++b) process();
  generation += delay;
  require(worker.pog3Recovery().state == ardor::pog3::RecoveryState::Bypassed
            && worker.workerUnderflowCount() == 2,
          "repeated worker failures latch an explicit block bypass");
  gate.release(); wait(worker, generation);
  for (unsigned b = 0; b < 16; ++b) process();
  require(worker.workerCompletedGeneration() == generation
            && std::fabs(outLeft[frames - 1] - .1f) < 1e-7f,
          "terminal bypass keeps audio and dispatches no more work");
  worker.reset();
  process(); wait(worker, 1);
  require(worker.pog3Recovery().faults == 0 && worker.pog3Recovery().recoveries == 0
            && worker.pog3Recovery().state == ardor::pog3::RecoveryState::Healthy,
          "explicit reset rearms recovery and clears the bypass");
  std::cout << "frames=" << frames
            << " stalled-worker/delayed-dry/worker-reset/recovery/repeated-fault/reset=pass\n";
}
}
int main(int argc, char** argv) {
  try {
    const bool allocationCheck = argc == 2 && std::string(argv[1]) == "--allocation";
    require(argc == 1 || allocationCheck, "usage: quality [--allocation]");
    if (allocationCheck) require(pog3_global_malloc_begin && pog3_global_malloc_end, "global C allocation interposer missing");
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    require(sched_getaffinity(0, sizeof(allowed), &allowed) == 0, "host CPU set");
    int cpu = -1;
    for (int i = 0; i < CPU_SETSIZE; ++i) if (CPU_ISSET(i, &allowed)) cpu = i;
    require(cpu >= 0, "available worker CPU");
    for (unsigned frames : {32, 64, 128}) {
      run(frames, cpu, allocationCheck);
      runRecovery(frames, cpu, allocationCheck);
    }
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
