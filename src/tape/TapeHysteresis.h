#pragma once

namespace ardor {

// The Langevin function L(x) = coth(x) - 1/x and its derivative, which set the
// shape of the anhysteretic magnetisation curve.
//
// Both terms of L diverge at the origin, so their difference loses every
// significant bit there — and the origin is exactly where a quiet passage
// sits. Below kLangevinTaylorLimit both functions switch to their Taylor
// series, which is correct there and faster besides.
//
// The threshold is 0.1 rather than something smaller because the cancellation
// reaches much further out than it looks. In float, coth(x) - 1/x returns
// exactly zero at x = 1e-4, and is still 10% wrong at 1e-3, because 1/x has
// grown to 1000 and the difference being sought is 3.3e-4. Measured relative
// error of the two branches against a double-precision reference:
//
//   x       closed form (float)   Taylor
//   1e-4    1.0    (returns 0)    1.2e-8
//   1e-3    9.9e-2                2.3e-11
//   1e-2    2.2e-4                6.3e-11
//   1e-1    7.3e-6                6.4e-7
//
// They cross near 0.1, so that is where the seam belongs. Both branches are
// better than 1e-5 there, which is what makes the switch inaudible.
inline constexpr float kLangevinTaylorLimit = 0.1f;

float langevin(float x);
float langevinPrime(float x);

// Jiles-Atherton magnetic hysteresis.
//
// Magnetisation M lags behind an anhysteretic curve M_an, which is a Langevin
// function of the effective field H_e = H + alpha*M. How far M is allowed to
// lag is set by the coercivity k:
//
//   dM/dH = ( (1-c) * delta_M * (M_an - M) )
//           / ( (1-c) * delta * k - alpha * (M_an - M) )
//         + c * dM_an/dH_e
//
// That lag is the whole reason this class exists. It gives the block memory,
// so the harmonics depend on where the signal has recently been and not only
// on where it is now. A memoryless waveshaper cannot produce that, which is
// why the soft-clip Saturation helper in the tape delay was not extended.
//
// Solved with RK4. RK4 is a fixed four derivative evaluations per sample
// whatever the signal, so unlike the wah's DK solver its worst case equals its
// average case — which is what makes it safe to run beside NAM.
//
// This class knows nothing about controls, stereo, or any sample rate other
// than the one it is given. It is fed the already-oversampled rate.
class TapeHysteresis {
public:
  struct Parameters {
    float saturationMagnetisation = 3.5e5f; // M_s, A/m
    float anhystereticShape = 2.2e4f;       // a, A/m — lower gives a harder knee
    float interdomainCoupling = 1.6e-3f;    // alpha
    float coercivity = 2.7e4f;              // k, A/m — sets the loop width
    float reversibility = 0.17f;            // c
  };

  // A starting fit for a modern studio machine, tuned in this repository
  // against the harmonic and loop tests rather than measured off hardware.
  static Parameters defaultParameters() { return Parameters{}; }

  // `oversampledRate` is the rate this core actually runs at — 384000 for the
  // tape block's 8x oversampling, not the host's 48000.
  void configure(const Parameters& parameters, float oversampledRate);
  void reset();

  // Takes the applied field H in A/m, returns M / M_s, nominally within +/-1.
  float process(float field);

private:
  float derivative(float magnetisation, float field, float fieldRate, float direction) const;
  float trackDirection(float field);

  // How far the field must retrace, as a fraction of its own recent peak,
  // before the solver accepts that the material has changed branch.
  //
  // Without this the branch direction comes from the sign of a one-sample
  // difference, which is not robust. The signal reaching the solver has been
  // upsampled eight times, and the halfband images sit about 60 dB down but
  // near 96 kHz — so their dH/dt is 1e-3 * 96000 / 45, about twenty times
  // larger than that of a 45 Hz note at the same amplitude. The images
  // therefore decide the sign of dH/dt near a low note's turning points, and
  // each spurious flip switches the hysteresis branch. Measured, that lifted
  // 45 Hz by 0.6 dB relative to 1 kHz with everything else already fixed.
  //
  // A real material does not change branch because the field wobbled by a
  // thousandth of its swing, so the deadband is physical as well as practical.
  static constexpr float kDirectionDeadband = 5.0e-3f;

  Parameters parameters_{};
  float period_ = 1.0f / 384000.0f;
  float magnetisation_ = 0.0f;
  float previousField_ = 0.0f;

  float direction_ = 1.0f;
  float extremeField_ = 0.0f;
  float fieldEnvelope_ = 0.0f;
  float envelopeRelease_ = 0.0f;
};

} // namespace ardor
