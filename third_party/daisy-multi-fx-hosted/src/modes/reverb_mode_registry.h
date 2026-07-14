#pragma once
#include "reverb_mode.h"
#include "../config/reverb_mode_id.h"
#include "../config/constants.h"

namespace pedal {

class ReverbModeRegistry {
public:
    void Init();
    ReverbMode* get(ReverbModeId id);
    void Reset(ReverbModeId id);

private:
    ReverbMode* modes_[static_cast<size_t>(ReverbModeId::COUNT)]{};
};

} // namespace pedal
