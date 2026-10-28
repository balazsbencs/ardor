// SPDX-License-Identifier: MIT
#include "ModelInfo.h"
#include "NAM/get_dsp.h"
#include "NAM/slimmable.h"

namespace ardor::nam_clap {
// Keep NAM core's YES/NO enum identifiers outside Objective-C++ headers.
std::vector<double> nativeSizeBreakpoints(const Json& document) {
  auto probe=nam::get_dsp(document);
  if(const auto* slim=dynamic_cast<const nam::SlimmableModel*>(probe.get()))
    return slim->GetSlimmableSizeBreakpoints();
  return {};
}
}
