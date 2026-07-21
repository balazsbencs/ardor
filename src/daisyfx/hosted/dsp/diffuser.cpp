#include "diffuser.h"

namespace pedal {

// Out-of-class definition required by C++14 for ODR-used static constexpr members.
constexpr size_t Diffuser::kDelays[Diffuser::STAGES];

} // namespace pedal
