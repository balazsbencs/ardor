#pragma once

#include <cmath>

namespace pedal {

// Click-free dual-head transition for clean delay modes. A new target arriving
// during a fade is queued rather than replacing the active anchors and
// restarting the fade every control block. This gives continuous MIDI or
// expression gestures a sequence of complete, continuous tap transitions.
class DelayTapTransition {
public:
    explicit DelayTapTransition(int duration_samples = 2400)
        : duration_(duration_samples > 0 ? duration_samples : 1) {}

    void Reset() {
        seeded_ = false;
        from_ = to_ = pending_ = 0.0f;
        remaining_ = 0;
    }

    void SetTarget(float target) {
        if (!std::isfinite(target)) return;
        if (!seeded_) {
            seeded_ = true;
            from_ = to_ = pending_ = target;
            return;
        }
        pending_ = target;
        if (remaining_ == 0 && std::fabs(pending_ - to_) > 0.01f) {
            beginPending();
        }
    }

    bool active() const { return remaining_ > 0; }
    float from() const { return from_; }
    float to() const { return to_; }
    float target() const { return pending_; }
    float mix() const {
        return active() ? 1.0f - static_cast<float>(remaining_) /
                                  static_cast<float>(duration_) : 1.0f;
    }

    void Advance() {
        if (remaining_ == 0) return;
        --remaining_;
        if (remaining_ == 0) {
            from_ = to_;
            if (std::fabs(pending_ - to_) > 0.01f) beginPending();
        }
    }

private:
    void beginPending() {
        from_ = to_;
        to_ = pending_;
        remaining_ = duration_;
    }

    int duration_ = 2400;
    int remaining_ = 0;
    bool seeded_ = false;
    float from_ = 0.0f;
    float to_ = 0.0f;
    float pending_ = 0.0f;
};

} // namespace pedal
