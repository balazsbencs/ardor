#pragma once

#include <span>

namespace ardor {

// The 1073 EQ drive stage. A tanh curve with a makeup gain runs at twice the
// host rate; only its distortion residue (curve minus input) goes through
// first-order antiderivative anti-aliasing, so a light drive keeps the exact
// linear path. The Console character adds two musical approximations:
//   - a Class-A style bias for even harmonics, with the residue DC-blocked;
//   - an output-transformer stage that compresses the low band first.
// Both scale with the drive, so at zero saturation the stage is exactly linear.
class ConsoleSaturation {
public:
  struct State {
    double previousInput = 0, previousIntegral = 0;
    double residueIn = 0, residueOut = 0; // residue DC blocker
    double lowBand = 0;                   // transformer low-band follower
  };

  void configure(float hostRate);
  // Control rate. saturation is 0..1, console blends the character in, 0..1.
  // The ADAA history depends on the curve, so it is resynchronised here.
  void setAmount(float saturation, float console, std::span<State> states);
  // Oversampled rate.
  double shape(State& state, double x) const;
  // Host rate, after the EQ, so a low-shelf boost drives it harder.
  float transformer(State& state, float x) const;

private:
  double residue(double x) const;
  double residueIntegral(double x) const;

  double drive_ = 0, makeup_ = 1, bias_ = 0, biasOffset_ = 0;
  double transformerDrive_ = 0;
  double residueBlock_ = 0.9997, lowBandStep_ = 0.013;
};

} // namespace ardor
