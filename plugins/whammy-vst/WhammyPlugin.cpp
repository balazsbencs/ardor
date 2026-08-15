// Test host for the pedal's Whammy mode.
//
// This wraps pedal::WhammyMode so the algorithm can be auditioned in a DAW. It
// deliberately drives the mode object directly rather than going through
// ardor::DaisyFxProcessor: that path hard-requires a 48 kHz host, and a plugin
// that silently bypasses at 44.1 kHz is worse than one that runs everywhere.
// The dry/wet and level laws below are copied from the engine so the result
// matches the pedal when the host does run at 48 kHz.

#include "DistrhoPlugin.hpp"

#include "daisyfx/hosted/modes/whammy_mode.h"
#include "daisyfx/hosted/params/mod_param_map.h"
#include "daisyfx/hosted/params/param_range.h"

#include <cmath>

START_NAMESPACE_DISTRHO

namespace {

enum ParamIndex {
    kParamPedal = 0,
    kParamPreset,
    kParamGlide,
    kParamHarmonyLevel,
    kParamTone,
    kParamMix,
    kParamLevel,
    kParamCount
};

// Must match kPresets in whammy_mode.cpp and kWhammyPresets in DaisyFxCatalog.cpp.
const char* const kPresetNames[pedal::WhammyMode::PRESET_COUNT] = {
    "2 Oct up", "1 Oct up", "5th up", "4th up", "2nd dn",
    "4th dn", "5th dn", "1 Oct dn", "2 Oct dn", "Dive bomb",
    "Oct dn / Oct up", "5th dn / 4th dn", "4th dn / 3rd dn",
    "5th up / 7th up", "5th up / 6th up", "4th up / 5th up",
    "3rd up / 4th up", "Min 3rd up / 3rd up", "2nd up / 3rd up",
};

} // namespace

class ArdorWhammyPlugin : public Plugin {
public:
    ArdorWhammyPlugin()
        : Plugin(kParamCount, 0, 0)
    {
        mode_.Init();
        for (uint32_t i = 0; i < kParamCount; ++i) {
            values_[i] = defaultOf(i);
        }
    }

protected:
    const char* getLabel()       const override { return "ArdorWhammy"; }
    const char* getDescription() const override
    {
        return "Pedal-swept pitch shifter from the Ardor pedal. Ten Whammy presets "
               "and nine Harmony presets on one selector.";
    }
    const char* getMaker()       const override { return "Ardor"; }
    const char* getHomePage()    const override { return "https://github.com/"; }
    const char* getLicense()     const override { return "MIT"; }
    uint32_t    getVersion()     const override { return d_version(0, 1, 0); }
    int64_t     getUniqueId()    const override { return d_cconst('A', 'w', 'h', 'm'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 1.0f;
        parameter.ranges.def = defaultOf(index);

        switch (index) {
        case kParamPedal:
            // The control worth automating: heel at 0, toe at 1.
            parameter.name = "Pedal";
            parameter.symbol = "pedal";
            break;
        case kParamPreset: {
            parameter.name = "Preset";
            parameter.symbol = "preset";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(pedal::WhammyMode::PRESET_COUNT - 1);
            parameter.ranges.def = 0.0f;
            auto* const names = new ParameterEnumerationValue[pedal::WhammyMode::PRESET_COUNT];
            for (int i = 0; i < pedal::WhammyMode::PRESET_COUNT; ++i) {
                names[i].value = static_cast<float>(i);
                names[i].label = kPresetNames[i];
            }
            parameter.enumValues.count = pedal::WhammyMode::PRESET_COUNT;
            parameter.enumValues.values = names;
            parameter.enumValues.restrictedMode = true;
            break;
        }
        case kParamGlide:
            parameter.name = "Glide";
            parameter.symbol = "glide";
            break;
        case kParamHarmonyLevel:
            parameter.name = "Harmony Level";
            parameter.symbol = "harmony_level";
            break;
        case kParamTone:
            parameter.name = "Tone";
            parameter.symbol = "tone";
            break;
        case kParamMix:
            parameter.name = "Mix";
            parameter.symbol = "mix";
            break;
        case kParamLevel:
            parameter.name = "Level";
            parameter.symbol = "level";
            break;
        default:
            break;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        return index < kParamCount ? values_[index] : 0.0f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index < kParamCount) values_[index] = value;
    }

    void activate() override
    {
        mode_.Reset();
        smoothedMix_ = normalized(kParamMix);
        smoothedLevel_ = pedal::map_param(normalized(kParamLevel),
                                          pedal::mod_fx::default_ranges::LEVEL);
        controlCountdown_ = 0;
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        // The mode expects its control update on the pedal's own cadence.
        const uint32_t interval = pedal::BLOCK_SIZE;

        for (uint32_t n = 0; n < frames; ++n) {
            if (controlCountdown_ == 0) {
                refreshParams();
                mode_.Prepare(params_);
                controlCountdown_ = interval;
            }
            --controlCountdown_;

            const pedal::StereoFrame in{inputs[0][n], inputs[1][n]};
            const pedal::StereoFrame wet = mode_.Process(in, params_);

            // Same law as the engine: crossfade then apply level, both smoothed
            // per sample so automation does not step.
            smoothedMix_ += kSmoothing * (params_.mix - smoothedMix_);
            smoothedLevel_ += kSmoothing * (params_.level - smoothedLevel_);

            const float dryGain = 1.0f - smoothedMix_;
            outputs[0][n] = (in.left * dryGain + finite(wet.left) * smoothedMix_) * smoothedLevel_;
            outputs[1][n] = (in.right * dryGain + finite(wet.right) * smoothedMix_) * smoothedLevel_;
        }
    }

private:
    static constexpr float kSmoothing = 0.002f;

    static float finite(float value) { return std::isfinite(value) ? value : 0.0f; }

    static float defaultOf(uint32_t index)
    {
        switch (index) {
        case kParamPedal:         return 0.0f;
        case kParamPreset:        return 0.0f;
        case kParamGlide:         return 0.35f;
        case kParamHarmonyLevel:  return 0.70f;
        case kParamTone:          return 0.50f;
        case kParamMix:           return 1.00f;
        case kParamLevel:         return 0.50f;   // unity on the 0..2 range
        default:                  return 0.0f;
        }
    }

    float normalized(uint32_t index) const { return values_[index]; }

    void refreshParams()
    {
        using namespace pedal::mod_fx;
        // Map exactly as the pedal's catalog does, so the plugin and the device
        // agree on what a given knob position means.
        params_.speed = pedal::map_param(values_[kParamGlide], default_ranges::SPEED);
        params_.depth = pedal::map_param(values_[kParamHarmonyLevel], default_ranges::DEPTH);
        params_.mix   = pedal::map_param(values_[kParamMix], default_ranges::MIX);
        params_.tone  = pedal::map_param(values_[kParamTone], default_ranges::TONE);
        params_.p1    = pedal::map_param(values_[kParamPedal], default_ranges::P1);
        params_.level = pedal::map_param(values_[kParamLevel], default_ranges::LEVEL);

        // Preset arrives as an integer index; hand the mode the centre of that
        // step so rounding cannot land it on a neighbour.
        const int preset = static_cast<int>(values_[kParamPreset] + 0.5f);
        const int clamped = preset < 0 ? 0
                          : (preset >= pedal::WhammyMode::PRESET_COUNT
                                 ? pedal::WhammyMode::PRESET_COUNT - 1 : preset);
        params_.p2 = (static_cast<float>(clamped) + 0.5f) /
                     static_cast<float>(pedal::WhammyMode::PRESET_COUNT);
    }

    pedal::WhammyMode        mode_;
    pedal::mod_fx::ParamSet  params_ = pedal::mod_fx::ParamSet::make_default();
    float                    values_[kParamCount] = {};
    float                    smoothedMix_ = 1.0f;
    float                    smoothedLevel_ = 1.0f;
    uint32_t                 controlCountdown_ = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArdorWhammyPlugin)
};

Plugin* createPlugin() { return new ArdorWhammyPlugin(); }

END_NAMESPACE_DISTRHO
