// SPDX-License-Identifier: MIT
// A2 topology/weight layout follow the pinned NAM core (see ../LICENSE).
#include "mutable_a2.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#ifdef __aarch64__
#include <arm_neon.h>
#endif

namespace nam::wavenet {
namespace {
constexpr int C = 16, Layers = 23, HeadKernel = 16;
constexpr std::array<int, Layers> kernels = {6, 6, 6,  6,  6, 6, 6, 6, 6, 6, 6, 6,
                                             6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6};
constexpr std::array<int, Layers> dilations = {1,   3,   7, 17, 41, 101, 239, 1,  3,  7,   17, 41,
                                               101, 239, 1, 13, 1,  3,   7,   17, 41, 101, 239};
constexpr size_t weightCount = [] {
  size_t n = C + HeadKernel * C + 2;
  for (int k : kernels)
    n += C * C * k + 3 * C + C * C;
  return n;
}();
int nextPower2(int n) {
  int p = 1;
  while (p < n)
    p *= 2;
  return p;
}

#ifdef __aarch64__
// Four frames by sixteen output channels stay in sixteen NEON accumulators.
// One column of weights is reused across all four frames. Only floating-point
// reduction order changes; no weights, channels, layers or activations are removed.
struct Tile {
  float32x4_t a00, a01, a02, a03, a10, a11, a12, a13, a20, a21, a22, a23, a30, a31, a32, a33;
  inline __attribute__((always_inline)) void zero() {
    a00 = a01 = a02 = a03 = a10 = a11 = a12 = a13 = a20 = a21 = a22 = a23 = a30 = a31 = a32 = a33 =
        vdupq_n_f32(0);
  }
  template <int Lane>
  inline __attribute__((always_inline)) void lane(const float *w, float32x4_t x0, float32x4_t x1,
                                                  float32x4_t x2, float32x4_t x3) {
    const auto w0 = vld1q_f32(w), w1 = vld1q_f32(w + 4), w2 = vld1q_f32(w + 8), w3 = vld1q_f32(w + 12);
    a00 = vfmaq_laneq_f32(a00, w0, x0, Lane);
    a01 = vfmaq_laneq_f32(a01, w1, x0, Lane);
    a02 = vfmaq_laneq_f32(a02, w2, x0, Lane);
    a03 = vfmaq_laneq_f32(a03, w3, x0, Lane);
    a10 = vfmaq_laneq_f32(a10, w0, x1, Lane);
    a11 = vfmaq_laneq_f32(a11, w1, x1, Lane);
    a12 = vfmaq_laneq_f32(a12, w2, x1, Lane);
    a13 = vfmaq_laneq_f32(a13, w3, x1, Lane);
    a20 = vfmaq_laneq_f32(a20, w0, x2, Lane);
    a21 = vfmaq_laneq_f32(a21, w1, x2, Lane);
    a22 = vfmaq_laneq_f32(a22, w2, x2, Lane);
    a23 = vfmaq_laneq_f32(a23, w3, x2, Lane);
    a30 = vfmaq_laneq_f32(a30, w0, x3, Lane);
    a31 = vfmaq_laneq_f32(a31, w1, x3, Lane);
    a32 = vfmaq_laneq_f32(a32, w2, x3, Lane);
    a33 = vfmaq_laneq_f32(a33, w3, x3, Lane);
  }
  inline __attribute__((always_inline)) void add(const float *w, const float *x) {
    for (int c = 0; c < C; c += 4) {
      const auto x0 = vld1q_f32(x + c), x1 = vld1q_f32(x + C + c), x2 = vld1q_f32(x + 2 * C + c),
                 x3 = vld1q_f32(x + 3 * C + c);
      lane<0>(w + c * C, x0, x1, x2, x3);
      lane<1>(w + (c + 1) * C, x0, x1, x2, x3);
      lane<2>(w + (c + 2) * C, x0, x1, x2, x3);
      lane<3>(w + (c + 3) * C, x0, x1, x2, x3);
    }
  }
  inline __attribute__((always_inline)) void store(float *z) const {
    vst1q_f32(z, a00);
    vst1q_f32(z + 4, a01);
    vst1q_f32(z + 8, a02);
    vst1q_f32(z + 12, a03);
    vst1q_f32(z + C, a10);
    vst1q_f32(z + C + 4, a11);
    vst1q_f32(z + C + 8, a12);
    vst1q_f32(z + C + 12, a13);
    vst1q_f32(z + 2 * C, a20);
    vst1q_f32(z + 2 * C + 4, a21);
    vst1q_f32(z + 2 * C + 8, a22);
    vst1q_f32(z + 2 * C + 12, a23);
    vst1q_f32(z + 3 * C, a30);
    vst1q_f32(z + 3 * C + 4, a31);
    vst1q_f32(z + 3 * C + 8, a32);
    vst1q_f32(z + 3 * C + 12, a33);
  }
};
#endif
} // namespace

struct MutableA2::Impl {
  struct Layer {
    std::vector<float> conv, history;
    std::array<float, C> bias, mixin, residualBias;
    std::array<float, C * C> residual;
    int size = 0, mask = 0, write = 0;
  };
  std::array<Layer, Layers> layers;
  std::array<float, C> rechannel;
  std::array<float, HeadKernel * C> head;
  float headBias = 0, scale = 1;
  std::vector<float> input, skip, headHistory;
  int headSize = 0, headMask = 0, headWrite = 0;

  Impl() {
    for (int l = 0; l < Layers; ++l)
      layers[l].conv.resize(kernels[l] * C * C);
  }
  static void writeRing(std::vector<float> &history, int size, int pos, const float *input, int frames) {
    const int n = std::min(frames, size - pos);
    std::memcpy(history.data() + pos * C, input, n * C * sizeof(float));
    if (n < frames)
      std::memcpy(history.data(), input + n * C, (frames - n) * C * sizeof(float));
    // A four-frame tile can cross the ring end by at most three frames.
    // Mirroring only those frames keeps the histories small enough for cache.
    std::memcpy(history.data() + size * C, history.data(), 3 * C * sizeof(float));
  }
  void prepare(int frames) {
    input.assign(C * frames, 0);
    skip.assign(C * frames, 0);
    for (int l = 0; l < Layers; ++l) {
      auto &layer = layers[l];
      layer.size = nextPower2((kernels[l] - 1) * dilations[l] + frames);
      layer.mask = layer.size - 1;
      layer.write = 0;
      layer.history.assign((layer.size + 3) * C, 0);
    }
    headSize = nextPower2(HeadKernel - 1 + frames);
    headMask = headSize - 1;
    headWrite = 0;
    headHistory.assign((headSize + 3) * C, 0);
  }
  void layer(int li, const NAM_SAMPLE *condition, int frames) {
    auto &l = layers[li];
    const int K = kernels[li], D = dilations[li];
    writeRing(l.history, l.size, l.write, input.data(), frames);
    int f = 0;
#ifdef __aarch64__
    for (; f + 4 <= frames; f += 4) {
      Tile sum;
      sum.zero();
      for (int k = 0; k < K; ++k) {
        const int p = (l.write + f - (K - 1 - k) * D) & l.mask;
        sum.add(l.conv.data() + k * C * C, l.history.data() + p * C);
      }
      alignas(16) float activation[4 * C], residual[4 * C];
      sum.store(activation);
      for (int j = 0; j < 4; ++j)
        for (int c = 0; c < C; c += 4) {
          auto z = vaddq_f32(vld1q_f32(activation + j * C + c), vld1q_f32(l.bias.data() + c));
          // Preserve generic evaluation order: conv+bias, then input mixin.
          z = vaddq_f32(z, vmulq_n_f32(vld1q_f32(l.mixin.data() + c), condition[f + j]));
          z = vbslq_f32(vcltq_f32(z, vdupq_n_f32(0)), vmulq_n_f32(z, 0.01f), z);
          vst1q_f32(activation + j * C + c, z);
          auto *s = skip.data() + (f + j) * C + c;
          vst1q_f32(s, vaddq_f32(vld1q_f32(s), z));
        }
      Tile r;
      r.zero();
      r.add(l.residual.data(), activation);
      r.store(residual);
      for (int j = 0; j < 4; ++j)
        for (int c = 0; c < C; c += 4) {
          const auto delta = vaddq_f32(vld1q_f32(residual + j * C + c), vld1q_f32(l.residualBias.data() + c));
          auto *dest = input.data() + (f + j) * C + c;
          vst1q_f32(dest, vaddq_f32(vld1q_f32(dest), delta));
        }
    }
#endif
    for (; f < frames; ++f) {
      std::array<float, C> z{};
      for (int k = 0; k < K; ++k) {
        const int p = (l.write + f - (K - 1 - k) * D) & l.mask;
        const auto *x = l.history.data() + p * C;
        for (int c = 0; c < C; ++c)
          for (int o = 0; o < C; ++o)
            z[o] += l.conv[k * C * C + c * C + o] * x[c];
      }
      for (int c = 0; c < C; ++c) {
        z[c] += l.bias[c];
        z[c] += l.mixin[c] * condition[f];
        if (z[c] < 0)
          z[c] *= 0.01f;
        skip[f * C + c] += z[c];
      }
      for (int o = 0; o < C; ++o) {
        float v = 0;
        for (int c = 0; c < C; ++c)
          v += l.residual[c * C + o] * z[c];
        input[f * C + o] += v + l.residualBias[o];
      }
    }
    l.write = (l.write + frames) & l.mask;
  }
};

bool MutableA2::supports(const WaveNetConfig &config) {
  if (config.in_channels != 1 || config.with_head || config.head_params || config.condition_dsp ||
      config.layer_array_params.size() != 1)
    return false;
  const auto &a = config.layer_array_params[0];
  if (a.channels != C || a.bottleneck != C || a.input_size != 1 || a.condition_size != 1 ||
      a.head_size != 1 || a.head_dilation != 1 || a.head_kernel_size != HeadKernel || !a.head_bias ||
      !a.layer1x1_params.active || a.layer1x1_params.groups != 1 || a.head1x1_params.active ||
      a.groups_input != 1 || a.groups_input_mixin != 1 || a.conv_pre_film_params.active ||
      a.conv_post_film_params.active || a.input_mixin_pre_film_params.active ||
      a.input_mixin_post_film_params.active || a.activation_pre_film_params.active ||
      a.activation_post_film_params.active || a._layer1x1_post_film_params.active ||
      a.head1x1_post_film_params.active || a.kernel_sizes.size() != Layers || a.dilations.size() != Layers ||
      a.activation_configs.size() != Layers || a.gating_modes.size() != Layers)
    return false;
  for (int i = 0; i < Layers; ++i)
    if (a.kernel_sizes[i] != kernels[i] || a.dilations[i] != dilations[i] ||
        a.gating_modes[i] != GatingMode::NONE ||
        a.activation_configs[i].type != activations::ActivationType::LeakyReLU ||
        a.activation_configs[i].negative_slope.value_or(-1) != 0.01f)
      return false;
  return true;
}
MutableA2::MutableA2(std::span<const float> weights, double rate)
    : DSP(1, 1, rate), impl_(std::make_unique<Impl>()) {
  setWeights(weights);
}
MutableA2::~MutableA2() = default;
bool MutableA2::overlapsConvolution(size_t offset, size_t count) noexcept {
  size_t n = C;
  for (int k : kernels) {
    const size_t end = n + k * C * C;
    // Subtraction avoids overflow even for direct callers with oversized ranges.
    if (offset < end && (offset >= n || count > n - offset))
      return count > 0;
    n = end + 3 * C + C * C;
  }
  return false;
}
void MutableA2::setWeights(std::span<const float> weights, bool updateConvolutions) {
  if (weights.size() != weightCount)
    throw std::invalid_argument("Mutable A2 weight count mismatch");
  size_t n = 0;
  auto take = [&] { return weights[n++]; };
  auto &p = *impl_;
  for (auto &w : p.rechannel)
    w = take();
  for (int i = 0; i < Layers; ++i) {
    auto &l = p.layers[i];
    if (updateConvolutions)
      for (int o = 0; o < C; ++o)
        for (int c = 0; c < C; ++c)
          for (int k = 0; k < kernels[i]; ++k)
            l.conv[k * C * C + c * C + o] = take();
    else
      n += kernels[i] * C * C;
    for (auto &w : l.bias)
      w = take();
    for (auto &w : l.mixin)
      w = take();
    for (int o = 0; o < C; ++o)
      for (int c = 0; c < C; ++c)
        l.residual[c * C + o] = take();
    for (auto &w : l.residualBias)
      w = take();
  }
  for (int c = 0; c < C; ++c)
    for (int k = 0; k < HeadKernel; ++k)
      p.head[k * C + c] = take();
  p.headBias = take();
  p.scale = take();
}
void MutableA2::SetMaxBufferSize(int frames) {
  if (frames < 1 || frames > 65536)
    throw std::invalid_argument("Mutable A2 buffer size out of range");
  DSP::SetMaxBufferSize(frames);
  impl_->prepare(frames);
}
int MutableA2::GetPrewarmSamples() {
  int n = 1 + HeadKernel - 1;
  for (int l = 0; l < Layers; ++l)
    n += (kernels[l] - 1) * dilations[l];
  return n;
}
void MutableA2::process(NAM_SAMPLE **input, NAM_SAMPLE **output, int frames) {
  if (frames < 0 || frames > GetMaxBufferSize())
    throw std::invalid_argument("Mutable A2 block exceeds prepared capacity");
  auto &p = *impl_;
  const auto *x = input[0];
  for (int f = 0; f < frames; ++f)
    for (int c = 0; c < C; ++c)
      p.input[f * C + c] = p.rechannel[c] * x[f];
  std::fill_n(p.skip.data(), frames * C, 0.f);
  for (int l = 0; l < Layers; ++l)
    p.layer(l, x, frames);
  Impl::writeRing(p.headHistory, p.headSize, p.headWrite, p.skip.data(), frames);
  for (int f = 0; f < frames; ++f) {
    float y = p.headBias;
    for (int k = 0; k < HeadKernel; ++k) {
      const int pos = (p.headWrite + f - (HeadKernel - 1 - k)) & p.headMask;
      for (int c = 0; c < C; ++c)
        y += p.head[k * C + c] * p.headHistory[pos * C + c];
    }
    output[0][f] = y * p.scale;
  }
  p.headWrite = (p.headWrite + frames) & p.headMask;
}
} // namespace nam::wavenet
