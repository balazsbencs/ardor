#include "circuit/MnaMatrix.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ardor::circuit {

namespace {

// The largest exponent argument a junction is evaluated at. Beyond this the
// device is fully on as far as any surrounding network is concerned, and the
// clamp is what keeps a cold Newton start from overflowing.
constexpr double kMaxExponent = 40.0;

} // namespace

Mat multiply(const Mat& a, const Mat& b)
{
  Mat out(a.rows, b.cols);
  for (std::size_t i = 0; i < a.rows; ++i) {
    for (std::size_t k = 0; k < a.cols; ++k) {
      const double aik = a.at(i, k);
      if (aik == 0.0) continue;
      for (std::size_t j = 0; j < b.cols; ++j) {
        out.at(i, j) += aik * b.at(k, j);
      }
    }
  }
  return out;
}

Mat solve(Mat a, Mat b)
{
  const std::size_t n = a.rows;
  if (a.cols != n || b.rows != n) throw std::invalid_argument("solve: shape mismatch");

  for (std::size_t col = 0; col < n; ++col) {
    std::size_t pivot = col;
    double best = std::fabs(a.at(col, col));
    for (std::size_t r = col + 1; r < n; ++r) {
      const double candidate = std::fabs(a.at(r, col));
      if (candidate > best) {
        best = candidate;
        pivot = r;
      }
    }
    if (best < 1e-300) throw std::runtime_error("solve: singular matrix");
    if (pivot != col) {
      for (std::size_t j = 0; j < n; ++j) std::swap(a.at(col, j), a.at(pivot, j));
      for (std::size_t j = 0; j < b.cols; ++j) std::swap(b.at(col, j), b.at(pivot, j));
    }
    const double diagonal = a.at(col, col);
    for (std::size_t r = col + 1; r < n; ++r) {
      const double factor = a.at(r, col) / diagonal;
      if (factor == 0.0) continue;
      for (std::size_t j = col; j < n; ++j) a.at(r, j) -= factor * a.at(col, j);
      for (std::size_t j = 0; j < b.cols; ++j) b.at(r, j) -= factor * b.at(col, j);
    }
  }
  Mat x(n, b.cols);
  for (std::size_t j = 0; j < b.cols; ++j) {
    for (std::size_t i = n; i-- > 0;) {
      double sum = b.at(i, j);
      for (std::size_t c = i + 1; c < n; ++c) sum -= a.at(i, c) * x.at(c, j);
      x.at(i, j) = sum / a.at(i, i);
    }
  }
  return x;
}

void stampResistor(Mat& s, int a, int b, double ohms)
{
  const double g = 1.0 / ohms;
  if (a >= 0) s.at(a, a) += g;
  if (b >= 0) s.at(b, b) += g;
  if (a >= 0 && b >= 0) {
    s.at(a, b) -= g;
    s.at(b, a) -= g;
  }
}

void stampVccs(Mat& s, int from, int to, int cp, int cn, double gm)
{
  if (from >= 0 && cp >= 0) s.at(from, cp) += gm;
  if (from >= 0 && cn >= 0) s.at(from, cn) -= gm;
  if (to >= 0 && cp >= 0) s.at(to, cp) -= gm;
  if (to >= 0 && cn >= 0) s.at(to, cn) += gm;
}

double nodeVoltage(const std::vector<double>& v, int node)
{
  return node < 0 ? 0.0 : v[static_cast<std::size_t>(node)];
}

double junctionCurrent(double volts, double saturationCurrent, double thermalVolts)
{
  const double arg = std::min(volts / thermalVolts, kMaxExponent);
  return saturationCurrent * (std::exp(arg) - 1.0);
}

double junctionConductance(double volts, double saturationCurrent, double thermalVolts)
{
  const double arg = std::min(volts / thermalVolts, kMaxExponent);
  return (saturationCurrent / thermalVolts) * std::exp(arg);
}

} // namespace ardor::circuit
