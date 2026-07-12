#pragma once

namespace pedal {

class DcBlocker {
public:
    void Init() { x1_ = 0.0f; y1_ = 0.0f; }

    inline float Process(float x) {
        // y[n] = x[n] - x[n-1] + 0.9993 * y[n-1]
        // R = 0.9993 → fc ≈ 5 Hz at 48 kHz; safe margin below lowest guitar note (E1 = 41 Hz).
        float y = x - x1_ + 0.9993f * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    float x1_ = 0.0f;
    float y1_ = 0.0f;
};

} // namespace pedal
