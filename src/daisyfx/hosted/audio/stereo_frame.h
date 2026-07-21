#pragma once

namespace pedal {

struct StereoFrame {
    float left  = 0.0f;
    float right = 0.0f;

    float mono() const { return (left + right) * 0.5f; }
};

inline StereoFrame make_mono_frame(float sample) {
    return StereoFrame{sample, sample};
}

inline StereoFrame mix_frames(const StereoFrame& dry, const StereoFrame& wet, float mix) {
    return StereoFrame{
        dry.left  * (1.0f - mix) + wet.left  * mix,
        dry.right * (1.0f - mix) + wet.right * mix,
    };
}

} // namespace pedal
