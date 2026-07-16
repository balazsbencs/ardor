#include "reverb_param_map.h"

namespace pedal {
namespace reverb_fx {

const ParamRange& get_param_range(ReverbModeId mode, ParamId param) {
    // Mode-specific Decay overrides
    if (param == ParamId::Decay) {
        switch (mode) {
            case ReverbModeId::Room:        return default_ranges::DECAY_ROOM;
            case ReverbModeId::Spring:      return default_ranges::DECAY_SPRING;
            case ReverbModeId::Cloud:       return default_ranges::DECAY_CLOUD;
            case ReverbModeId::Magneto:     return default_ranges::DECAY_MAGNETO;
            case ReverbModeId::Nonlinear:   return default_ranges::DECAY_NONLIN;
            case ReverbModeId::Reflections: return default_ranges::DECAY_REFL;
            case ReverbModeId::Bloom:       return default_ranges::DECAY_BLOOM;
            case ReverbModeId::Swell:       return default_ranges::DECAY_SWELL_R;
            default:                        return default_ranges::DECAY;
        }
    }

    // Mode-specific Param1 overrides
    if (param == ParamId::Param1) {
        switch (mode) {
            case ReverbModeId::Swell:       return default_ranges::PARAM1_SWELL;
            case ReverbModeId::Shimmer:     return default_ranges::PARAM1_SHIMMER;
            case ReverbModeId::Chorale:     return default_ranges::PARAM1_CHORALE;
            case ReverbModeId::Nonlinear:   return default_ranges::PARAM1_NONLIN;
            case ReverbModeId::Magneto:     return default_ranges::PARAM1_MAGNETO;
            case ReverbModeId::Reflections: return default_ranges::PARAM1_REFL;
            case ReverbModeId::Bloom:       return default_ranges::PARAM1_BLOOM;
            default:                        return default_ranges::PARAM1;
        }
    }

    // Mode-specific Param2 overrides
    if (param == ParamId::Param2) {
        switch (mode) {
            case ReverbModeId::Shimmer:     return default_ranges::PARAM2_SHIMMER;
            case ReverbModeId::Chorale:     return default_ranges::PARAM2_CHORALE;
            case ReverbModeId::Nonlinear:   return default_ranges::PARAM2_NONLIN;
            case ReverbModeId::Magneto:     return default_ranges::PARAM2_MAGNETO;
            case ReverbModeId::Reflections: return default_ranges::PARAM2_REFL;
            case ReverbModeId::Bloom:       return default_ranges::PARAM2_BLOOM;
            default:                        return default_ranges::PARAM2;
        }
    }

    switch (param) {
        case ParamId::PreDelay:
            return mode == ReverbModeId::Magneto ? default_ranges::FEEDBACK : default_ranges::PRE_DELAY;
        case ParamId::Mix:      return default_ranges::MIX;
        case ParamId::Tone:     return default_ranges::TONE;
        case ParamId::Mod:      return default_ranges::MOD;
        default:                return default_ranges::MIX;
    }
}

const AlgoParamDescriptor& get_algo_param_descriptor(ReverbModeId mode) {
    static const AlgoParamDescriptor kDescriptors[] = {
        {"Size",       "Diffusion"},   // Room        = 0
        {"Size",       "Mid EQ"},      // Hall        = 1
        {"Size",       ""},            // Plate       = 2
        {"Dwell",      "Springs"},     // Spring      = 3
        {"Bloom Time", "Feedback"},    // Bloom       = 4
        {"Diffusion",  "Darkness"},    // Cloud       = 5
        {"Pitch 1",    "Pitch 2"},     // Shimmer     = 6
        {"Vowel",      "Resonance"},   // Chorale     = 7
        {"Shape",      "Diffusion"},   // Nonlinear   = 8
        {"Rise Time",  "Direction"},   // Swell       = 9
        {"Heads",      "Spacing"},     // Magneto     = 10
        {"Depth",      "Width"},       // Reflections = 11
    };
    static constexpr int kCount = static_cast<int>(sizeof(kDescriptors) / sizeof(kDescriptors[0]));
    const int idx = static_cast<int>(mode);
    return kDescriptors[idx >= 0 && idx < kCount ? idx : 0];
}

} // namespace reverb_fx
} // namespace pedal
