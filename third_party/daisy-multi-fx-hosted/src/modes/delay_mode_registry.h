#pragma once
#include "delay_mode.h"
#include "../config/delay_mode_id.h"
#include "../config/constants.h"

namespace pedal {

class DelayModeRegistry {
public:
    void Init();
    DelayMode* get(DelayModeId id);
    void Reset(DelayModeId id);

private:
    DelayMode* modes_[static_cast<size_t>(DelayModeId::COUNT)]{};
};

} // namespace pedal
