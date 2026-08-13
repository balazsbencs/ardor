#include "dsp/RuntimeChain.h"
#include "wah/WahProcessor.h"

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#ifndef ARDOR_SOURCE_DIR
#error "ARDOR_SOURCE_DIR must name the source tree"
#endif

namespace {

void require(bool condition, const std::string& message)
{
  if (!condition) throw std::runtime_error(message);
}

} // namespace

int main()
{
  const auto table = std::filesystem::path{ARDOR_SOURCE_DIR} / "assets/wah/gcb95.wahtable";
  ardor::WahProcessor wah;
  std::string error;
  require(wah.configure({{"mode", "gcb95"}, {"position", 0.0f}, {"level", 0.0f}},
                        48000.0f, table, error), error);
  require(wah.setParameterTarget("position", 0.75f), "position should be live-settable");
  require(wah.setParameterTarget("level", -3.0f), "level should be live-settable");
  require(!wah.setParameterTarget("postion", 0.5f), "unknown parameters should be rejected");

  for (int frame = 0; frame < 4096; ++frame) {
    const float input = 0.25f * std::sin(static_cast<float>(frame) * 0.05f);
    const auto output = wah.process({input, input});
    require(std::isfinite(output.left) && std::isfinite(output.right),
            "wah output should remain finite");
    require(output.left == output.right, "the mono wah should feed both output channels");
  }
  require(wah.latencyFrames() > 0 && wah.latencyFrames() < 64,
          "oversampling latency should stay within one audio block");

  ardor::RuntimeChain chain;
  chain.addWah("wah-1", std::move(wah));
  require(chain.setWahParameter("wah-1", "position", 0.25f),
          "runtime chain should route wah parameters by block id");
  require(!chain.setWahParameter("missing", "position", 0.25f),
          "runtime chain should reject an unknown wah id");
  const auto chained = chain.process({0.2f, 0.2f});
  require(std::isfinite(chained.left) && std::isfinite(chained.right),
          "runtime chain should process the wah block");
  require(chain.setBlockEnabled("wah-1", false), "runtime wah should be bypassable");
  const auto bypassed = chain.process({0.2f, -0.1f});
  require(bypassed.left == 0.2f && bypassed.right == -0.1f,
          "a bypassed wah should preserve both input channels");

  ardor::WahProcessor unsupported;
  require(!unsupported.configure({{"mode", "unknown"}}, 48000.0f, table, error),
          "unknown wah modes should fail configuration");
}
