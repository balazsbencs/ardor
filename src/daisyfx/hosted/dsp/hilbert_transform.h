#pragma once
namespace pedal {

/// IIR allpass approximation of a Hilbert transformer.
///
/// Uses cascaded z^-2 allpass half-band sections plus a one-sample path
/// delay. The two paths form a quadrature pair suitable for single-sideband
/// frequency shifting.
///
/// Path A → "real"  output (constant group delay)
/// Path B → "imaginary" output (~90° relative to A)
///
/// Coefficients: Niemitalo-style 4-section polyphase Hilbert pair.
class HilbertTransform {
public:
    struct Frame { float re; float im; };

    void Init() {
        Reset();
    }

    void Reset() {
        for (int i = 0; i < 4; ++i) {
            path_a_[i].Reset();
            path_b_[i].Reset();
        }
        re_delay_ = 0.0f;
    }

    /// Returns {re, im} where im ≈ Hilbert-transform of re.
    Frame Process(float x) {
        float re = x;
        float im = x;
        for (int i = 0; i < 4; ++i) {
            re = path_a_[i].Process(re);
            im = path_b_[i].Process(im);
        }
        const float delayed_re = re_delay_;
        re_delay_ = re;
        return {delayed_re, im};
    }

private:
    class Allpass2 {
    public:
        explicit Allpass2(float a = 0.0f) : a_(a) {}
        void SetCoeff(float a) { a_ = a; }
        void Reset() { x1_ = x2_ = y1_ = y2_ = 0.0f; }
        float Process(float x) {
            const float y = a_ * x + x2_ - a_ * y2_;
            x2_ = x1_;
            x1_ = x;
            y2_ = y1_;
            y1_ = y;
            return y;
        }

    private:
        float a_  = 0.0f;
        float x1_ = 0.0f;
        float x2_ = 0.0f;
        float y1_ = 0.0f;
        float y2_ = 0.0f;
    };

    Allpass2 path_a_[4] = {
        Allpass2(0.161758f),
        Allpass2(0.733029f),
        Allpass2(0.945350f),
        Allpass2(0.990598f),
    };
    Allpass2 path_b_[4] = {
        Allpass2(0.479401f),
        Allpass2(0.876218f),
        Allpass2(0.976599f),
        Allpass2(0.997500f),
    };
    float re_delay_ = 0.0f;
};

} // namespace pedal
