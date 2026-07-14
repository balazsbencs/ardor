#include "reverb_mode_registry.h"
#include "room_reverb.h"
#include "hall_reverb.h"
#include "plate_reverb.h"
#include "spring_reverb.h"
#include "bloom_reverb.h"
#include "cloud_reverb.h"
#include "shimmer_reverb.h"
#include "chorale_reverb.h"
#include "nonlinear_reverb.h"
#include "swell_reverb.h"
#include "magneto_reverb.h"
#include "reflections_reverb.h"

namespace pedal {

static RoomReverb        s_room;
static HallReverb        s_hall;
static PlateReverb       s_plate;
static SpringReverb      s_spring;
static BloomReverb       s_bloom;
static CloudReverb       s_cloud;
static ShimmerReverb     s_shimmer;
static ChoraleReverb     s_chorale;
static NonlinearReverb   s_nonlinear;
static SwellReverb       s_swell;
static MagnetoReverb     s_magneto;
static ReflectionsReverb s_reflections;

void ReverbModeRegistry::Init() {
    modes_[static_cast<uint8_t>(ReverbModeId::Room)]        = &s_room;
    modes_[static_cast<uint8_t>(ReverbModeId::Hall)]        = &s_hall;
    modes_[static_cast<uint8_t>(ReverbModeId::Plate)]       = &s_plate;
    modes_[static_cast<uint8_t>(ReverbModeId::Spring)]      = &s_spring;
    modes_[static_cast<uint8_t>(ReverbModeId::Bloom)]       = &s_bloom;
    modes_[static_cast<uint8_t>(ReverbModeId::Cloud)]       = &s_cloud;
    modes_[static_cast<uint8_t>(ReverbModeId::Shimmer)]     = &s_shimmer;
    modes_[static_cast<uint8_t>(ReverbModeId::Chorale)]     = &s_chorale;
    modes_[static_cast<uint8_t>(ReverbModeId::Nonlinear)]   = &s_nonlinear;
    modes_[static_cast<uint8_t>(ReverbModeId::Swell)]       = &s_swell;
    modes_[static_cast<uint8_t>(ReverbModeId::Magneto)]     = &s_magneto;
    modes_[static_cast<uint8_t>(ReverbModeId::Reflections)] = &s_reflections;

    for (int i = 0; i < NUM_REVERB_MODES; ++i) {
        modes_[i]->Init();
    }
}

ReverbMode* ReverbModeRegistry::get(ReverbModeId id) {
    return modes_[static_cast<uint8_t>(id)];
}

void ReverbModeRegistry::Reset(ReverbModeId id) {
    modes_[static_cast<uint8_t>(id)]->Reset();
}

} // namespace pedal
