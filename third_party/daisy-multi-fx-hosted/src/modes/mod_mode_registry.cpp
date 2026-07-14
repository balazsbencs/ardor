#include "mod_mode_registry.h"
#include "chorus_mode.h"
#include "flanger_mode.h"
#include "rotary_mode.h"
#include "vibe_mode.h"
#include "phaser_mode.h"
#include "vintage_trem_mode.h"
#include "poly_octave_mode.h"

namespace pedal {

static ChorusMode      s_chorus;
static FlangerMode     s_flanger;
static RotaryMode      s_rotary;
static VibeMode        s_vibe;
static PhaserMode      s_phaser;
static VintageTremMode s_vint_trem;
static PolyOctaveMode  s_poly_octave;

void ModModeRegistry::Init() {
    modes_[static_cast<uint8_t>(ModModeId::Chorus)]     = &s_chorus;
    modes_[static_cast<uint8_t>(ModModeId::Flanger)]    = &s_flanger;
    modes_[static_cast<uint8_t>(ModModeId::Rotary)]     = &s_rotary;
    modes_[static_cast<uint8_t>(ModModeId::Vibe)]       = &s_vibe;
    modes_[static_cast<uint8_t>(ModModeId::Phaser)]     = &s_phaser;
    modes_[static_cast<uint8_t>(ModModeId::VintTrem)]   = &s_vint_trem;
    modes_[static_cast<uint8_t>(ModModeId::PolyOctave)] = &s_poly_octave;

    for (int i = 0; i < NUM_MOD_MODES; ++i) {
        modes_[i]->Init();
    }
}

ModMode* ModModeRegistry::get(ModModeId id) {
    return modes_[static_cast<uint8_t>(id)];
}

void ModModeRegistry::Reset(ModModeId id) {
    modes_[static_cast<uint8_t>(id)]->Reset();
}

} // namespace pedal
