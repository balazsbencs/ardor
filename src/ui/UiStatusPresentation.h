#pragma once

#include "ui/UiModel.h"

#include <cstdint>
#include <string>

namespace ardor {

struct UiTelemetryPresentation {
  std::string text;
  std::uint32_t color = 0;
};

UiTelemetryPresentation makeTelemetryPresentation(const UiState& state,
                                                  std::uint32_t accentColor);

} // namespace ardor
