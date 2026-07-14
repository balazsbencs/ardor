#pragma once
#include "mod_mode.h"
#include "../config/mod_mode_id.h"
#include "../config/constants.h"

namespace pedal {

class ModModeRegistry {
public:
    void Init();
    ModMode* get(ModModeId id);
    void Reset(ModModeId id);

private:
    ModMode* modes_[static_cast<size_t>(ModModeId::COUNT)]{};
};

} // namespace pedal
