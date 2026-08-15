#pragma once
#include "mod_mode.h"
#include "../dsp/dc_blocker.h"
#include "../dsp/pitch_shifter.h"
#include "../dsp/pitch_tracker.h"
#include "../dsp/tone_filter.h"

namespace pedal {

/// Key-aware harmoniser.
///
/// The Whammy shifts by a fixed number of semitones, so a "third" stays major
/// whatever the underlying chord is doing. This one tracks the played note,
/// places it on a scale, and moves it by scale degrees instead — a third comes
/// out major or minor according to where it sits in the key.
///
/// Interval, Key and Scale are the three controls that decide the harmony;
/// Speed sets how quickly the voice follows a new note.
class HarmonizerMode : public ModMode {
public:
    static constexpr int INTERVAL_COUNT = 10;
    static constexpr int KEY_COUNT = 12;
    static constexpr int SCALE_COUNT = 5;
    static constexpr int SCALE_DEGREES = 7;

    void Init() override;
    void Reset() override;
    void Prepare(const mod_fx::ParamSet& params) override;
    StereoFrame Process(StereoFrame input, const mod_fx::ParamSet& params) override;
    const char* Name() const override { return "Harmonizer"; }

private:
    // 4096 covers an octave up (ratio 2) against the grain size below, with
    // history left for the slowest downward read.
    static constexpr size_t kBufSize = 8192;
    static constexpr size_t kGrainSize = 1024;
    // Semitones from the tracked note to the harmony note, for the current
    // interval and key. Recomputed only when the played note changes.
    float semitonesForNote(int midiNote) const;

    float buf_[kBufSize];
    PitchShifter shifter_;
    PitchTracker tracker_;
    ToneFilter   tone_;
    DcBlocker    dc_;

    int   interval_ = 5;     // index into the interval table
    int   key_ = 0;          // 0 = C
    int   scale_ = 0;        // 0 = major
    int   lastNote_ = -1;    // MIDI note the harmony was computed for

    // Glide in the pitch domain; slewing the ratio would make the move between
    // notes accelerate towards the top of its travel.
    float semitones_ = 0.0f;
    float semitoneTarget_ = 0.0f;
    float glide_ = 0.4f;
    float ratio_ = 1.0f;
    float ratioStep_ = 0.0f;
    bool  seeded_ = false;
};

} // namespace pedal
