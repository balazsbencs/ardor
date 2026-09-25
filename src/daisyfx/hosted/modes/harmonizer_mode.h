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
    // 8192 covers an octave up (ratio 2) against the grain size below, with
    // history left for the slowest downward read.
    static constexpr size_t kBufSize = 8192;
    static constexpr size_t kGrainSize = 1024;
    // A new note is taken once the pitch is 35 cents past the boundary to it,
    // so vibrato and bends up to that far keep the harmony still.
    static constexpr float kNoteHysteresis = 0.85f;
    // Semitones from the tracked note to the harmony note, for one interval
    // in the current key and scale.
    float semitonesForNote(int midiNote, int interval) const;

    // One harmony voice: a pitch shifter per channel, so a stereo source keeps
    // its image, and its own glide toward the interval of the tracked note.
    struct Channel {
        float        buf[kBufSize];
        PitchShifter shifter;
        ToneFilter   tone;
        DcBlocker    dc;
    };
    struct Voice {
        Channel channels[2];
        int     interval = 5;       // index into the interval table
        bool    active = true;
        float   semitones = 0.0f;
        float   semitoneTarget = 0.0f;
        float   ratio = 1.0f;
        float   ratioStep = 0.0f;
        float   level = 1.0f;
        float   gain[2] = {1.0f, 1.0f};  // pan: L and R gain
    };
    void PrepareVoice(Voice& voice, float tone);

    Voice        voices_[2];
    PitchTracker tracker_;
    // The tracker follows mid, or side when a stereo source carries the note
    // in anti-phase and mid cancels.
    float        env_mid_ = 0.0f;
    float        env_side_ = 0.0f;

    int   key_ = 0;          // 0 = C
    int   scale_ = 0;        // 0 = major
    int   lastNote_ = -1;    // MIDI note the harmony was computed for
    float glide_ = 0.4f;
    bool  seeded_ = false;
};

} // namespace pedal
