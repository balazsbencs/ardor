// SPDX-License-Identifier: MIT
// Parametric A2 kernels adapted from Gene Ko's NAM core fork; see LICENSE-Gene-Ko.
#include "film_wavenet.h"
#include "NAM/compiler.h"
#include "NAM/dsp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <Eigen/Dense>
#define NAM_A2_RING_MODE 1
namespace nam::ardor_film {
namespace {
constexpr int kNumLayers = 23, kHeadKernelSize = 16;
constexpr float kLeakySlope = 0.01f;
constexpr std::array<int, 23> kKernelSizes = {6,6,6,6,6,6,6,6,6,6,6,6,6,6,15,15,6,6,6,6,6,6,6};
constexpr std::array<int, 23> kDilations = {1,3,7,17,41,101,239,1,3,7,17,41,101,239,1,13,1,3,7,17,41,101,239};
int next_pow2(int v) { int n = 1; while (n < v) n <<= 1; return n; }
template <int Channels>
class ParametricA2FastModel : public DSP, public IParametricControl
{
public:
  static constexpr int kChannels = Channels;
  static constexpr int kMaxParams = 16;

  ParametricA2FastModel(std::vector<float> weights, double expected_sample_rate,
                        int num_params, std::vector<ParamSpec> param_defs);
  ~ParametricA2FastModel() override = default;

  void SetParams(std::span<const float> values) override;
  std::span<const float> GetParams() const override { return _values; }
  int ParamDim() const override { return _num_params; }
  const std::vector<ParamSpec>& GetParamSpecs() const override { return _param_defs; }

  void process(NAM_SAMPLE** input, NAM_SAMPLE** output, int num_frames) override;
  int GetPrewarmSamples() override { return _prewarm_samples; }
  void Reset(double sampleRate, int maxBufferSize) override;

protected:
  void SetMaxBufferSize(int maxBufferSize) override;

private:
  struct Layer
  {
    int kernel_size = 0;
    int dilation = 0;
    int max_lookback = 0;
    std::vector<float> conv_w; // K × Channels × Channels, col-major per tap
    std::array<float, Channels> conv_b{};
    std::array<float, Channels> mixin_w{};
    std::array<float, Channels * Channels> l1x1_w{};
    std::array<float, Channels> l1x1_b{};
    std::vector<float> history;
  #if NAM_A2_RING_MODE == 1
    int pow2_size = 0;
    int pow2_mask = 0;
    int write_pos = 0;
  #else
    int history_cols = 0;
    int write_pos = 0;
  #endif
  };

  std::array<Layer, kNumLayers> _layers;

  // Per-layer FiLM network weights.
  // _film_net_w[li]: (2*Channels rows × _num_params cols), row-major.
  // _film_net_b[li]: 2*Channels floats.
  struct FilmLayer
  {
    std::vector<float> net_w; // 2*Channels × _num_params, row-major
    std::vector<float> net_b; // 2*Channels
  };
  std::array<FilmLayer, kNumLayers> _film_layers;

  // Per-layer precomputed gamma/beta (rebuilt each buffer from _values).
  std::array<std::array<float, Channels>, kNumLayers> _film_gamma{};
  std::array<std::array<float, Channels>, kNumLayers> _film_beta{};

  int _num_params = 0;
  std::vector<ParamSpec> _param_defs;
  std::vector<float> _values;
  // [Fork] Per-buffer one-pole smoother — eliminates zipper noise when UI sliders move.
  // Audio thread only; no atomic needed. Initialized to default knob value (0.5).
  std::array<float, kMaxParams> _knob_smoothed{};
  float _knob_smooth_coeff{0.3f}; // recomputed in SetMaxBufferSize(); default covers startup before first Reset

  // Rechannel (1 → Channels), no bias.
  std::array<float, Channels> _rechannel_w{};

  // Head conv (Channels → 1, kernel=16, bias). Col-major per tap.
  std::array<std::array<float, Channels>, kHeadKernelSize> _head_w{};
  float _head_b = 0.0f;
  float _head_scale = 1.0f;

  // Head ring buffer. Reads from _head_sum (the skip-connection accumulator),
  // not from _layer_in.
  std::vector<float> _head_history;
  #if NAM_A2_RING_MODE == 1
  int _head_pow2_size = 0;
  int _head_pow2_mask = 0;
  int _head_write_pos = 0;
  #else
  int _head_history_cols = 0;
  int _head_write_pos = 0;
  #endif

  // Working buffers (Channels × maxBufferSize, col-major).
  // Eigen::aligned_allocator guarantees 16-byte alignment for the Eigen::Map<..., Eigen::Aligned16>
  // views below -- see A2FastModel's identical comment above for why this matters.
  std::vector<float, Eigen::aligned_allocator<float>> _layer_in; // layer input / residual accumulator
  // Skip accumulator: sum of every layer's post-activation (fed to the head).
  std::vector<float, Eigen::aligned_allocator<float>> _head_sum;
  std::vector<float, Eigen::aligned_allocator<float>> _z;        // per-layer conv accumulator
  std::vector<float> _cond;     // float32 copy of input audio
  std::vector<float> _head_out; // float32 head output

  // First-order IIR high-pass DC blocker (fc ≈ 20 Hz).
  float _dc_alpha = 0.99738f; // 1 - 2π·20/48000; recomputed in Reset()
  float _dc_x_prev = 0.0f;
  float _dc_y_prev = 0.0f;

  int _prewarm_samples = 0;

  void _load_weights(std::vector<float>& weights);
  // Rebuild _film_gamma/_film_beta for all layers from _values. Audio thread only.
  void _recompute_film();
  void _ring_write(Layer& L, int num_frames);
  // Write _head_sum (the skip-connection accumulator) into _head_history ring.
  void _head_ring_write(int num_frames);

  template <int KernelSize>
  void _layer_forward_k(Layer& L, int li, const float* cond, int num_frames);
  void _layer_forward(int li, const float* cond, int num_frames);
  void _head_forward(float* output, int num_frames);
};

// --- Construction ---
template <int Channels>
ParametricA2FastModel<Channels>::ParametricA2FastModel(std::vector<float> weights,
                                                        double expected_sample_rate,
                                                        int num_params,
                                                        std::vector<ParamSpec> param_defs)
: DSP(/*in_channels=*/1, /*out_channels=*/1, expected_sample_rate)
, _num_params(num_params)
, _param_defs(std::move(param_defs))
, _values(num_params)
{
  for (int i = 0; i < kNumLayers; i++)
  {
    _layers[i].kernel_size = kKernelSizes[i];
    _layers[i].dilation = kDilations[i];
    _layers[i].max_lookback = (kKernelSizes[i] - 1) * kDilations[i];
    _layers[i].conv_w.assign(static_cast<size_t>(kKernelSizes[i]) * Channels * Channels, 0.0f);
    _film_layers[i].net_w.assign(static_cast<size_t>(2 * Channels) * num_params, 0.0f);
    _film_layers[i].net_b.assign(static_cast<size_t>(2 * Channels), 0.0f);
  }

  // Use the exported defaults, before any host restoration.
  for (int k = 0; k < num_params && k < kMaxParams; k++) {
    _values[k] = _param_defs[k].defaultValue;
    _knob_smoothed[k] = _values[k]; // smoother starts at the same default so there's no initial transient
  }

  _load_weights(weights);

  int prewarm = 0;
  for (int i = 0; i < kNumLayers; i++)
    prewarm += _layers[i].max_lookback;
  prewarm += kHeadKernelSize - 1;
  _prewarm_samples = prewarm;
}

// --- Weight loader ---
// Order (must match Layer::set_weights_ with activation_pre_film active):
//   rechannel.weight (C floats)
//   per layer × 23: conv.w + conv.b + mixin.w + l1x1.w + l1x1.b + film.net.w + film.net.b
//   head.w + head.b + head_scale
template <int Channels>
void ParametricA2FastModel<Channels>::_load_weights(std::vector<float>& weights)
{
  auto it = weights.begin();
  const auto end = weights.end();
  auto take = [&]() -> float {
    if (it == end)
      throw std::runtime_error("ParametricA2FastModel: weight stream exhausted");
    return *it++;
  };

  // Rechannel: 1 → Channels, no bias.
  for (int i = 0; i < Channels; i++)
    _rechannel_w[i] = take();

  for (int li = 0; li < kNumLayers; li++)
  {
    Layer& L = _layers[li];
    const int K = L.kernel_size;

    // Conv1D: Channels → Channels, kernel=K, bias. Col-major per tap.
    for (int i = 0; i < Channels; i++)
      for (int j = 0; j < Channels; j++)
        for (int k = 0; k < K; k++)
          L.conv_w[k * Channels * Channels + j * Channels + i] = take();
    for (int i = 0; i < Channels; i++)
      L.conv_b[i] = take();

    // Input mixin: 1 → Channels, no bias.
    for (int i = 0; i < Channels; i++)
      L.mixin_w[i] = take();

    // layer1x1: Channels → Channels, with bias. Col-major.
    for (int i = 0; i < Channels; i++)
      for (int j = 0; j < Channels; j++)
        L.l1x1_w[j * Channels + i] = take();
    for (int i = 0; i < Channels; i++)
      L.l1x1_b[i] = take();

    // FiLM net: Conv1x1(num_params → 2*Channels, bias).
    // Read order: for i in 2*Channels: for j in num_params.
    for (int i = 0; i < 2 * Channels; i++)
      for (int j = 0; j < _num_params; j++)
        _film_layers[li].net_w[i * _num_params + j] = take();
    for (int i = 0; i < 2 * Channels; i++)
      _film_layers[li].net_b[i] = take();
  }

  // Head rechannel: Channels → 1, kernel=16, bias. Col-major per tap.
  for (int j = 0; j < Channels; j++)
    for (int k = 0; k < kHeadKernelSize; k++)
      _head_w[k][j] = take();
  _head_b = take();
  _head_scale = take();

  if (it != end)
    throw std::runtime_error("ParametricA2FastModel: " + std::to_string(std::distance(it, end))
                             + " trailing weights");
}

// --- SetKnobValues ---
template <int Channels>
void ParametricA2FastModel<Channels>::SetParams(std::span<const float> values)
{
  if (values.size() != _values.size()) throw std::invalid_argument("FiLM parameter count mismatch");
  for (size_t k = 0; k < values.size(); ++k)
    if (!std::isfinite(values[k]) || values[k] < _param_defs[k].min || values[k] > _param_defs[k].max)
      throw std::invalid_argument("FiLM parameter out of range");
  std::copy(values.begin(), values.end(), _values.begin());
}

// --- _recompute_film: rebuild gamma/beta from knob atomics (audio thread, once per buffer) ---
template <int Channels>
void ParametricA2FastModel<Channels>::_recompute_film()
{
  // [Fork] One-pole smooth toward the atomic target each buffer.
  // Eliminates zipper noise when UI sliders move; uses a 100 ms time constant.
  float knobs[kMaxParams];
  for (int j = 0; j < _num_params; j++) {
    float target = _values[j];
    _knob_smoothed[j] += _knob_smooth_coeff * (target - _knob_smoothed[j]);
    knobs[j] = _knob_smoothed[j];
  }

  for (int li = 0; li < kNumLayers; li++)
  {
    const float* w = _film_layers[li].net_w.data(); // (2C × num_params) row-major
    const float* b = _film_layers[li].net_b.data();
    for (int i = 0; i < Channels; i++)
    {
      float g = b[i];
      float beta = b[Channels + i];
      for (int j = 0; j < _num_params; j++)
      {
        g    += w[i           * _num_params + j] * knobs[j];
        beta += w[(Channels + i) * _num_params + j] * knobs[j];
      }
      _film_gamma[li][i] = g;
      _film_beta[li][i]  = beta;
    }
  }
}

// --- Buffer sizing ---
template <int Channels>
void ParametricA2FastModel<Channels>::SetMaxBufferSize(int maxBufferSize)
{
  DSP::SetMaxBufferSize(maxBufferSize);

  // Reset sizes reusable work/ring storage. Smoothing uses actual frames in process().
  _layer_in.assign(static_cast<size_t>(Channels) * maxBufferSize, 0.0f);
  _head_sum.assign(static_cast<size_t>(Channels) * maxBufferSize, 0.0f);
  _z.assign(static_cast<size_t>(Channels) * maxBufferSize, 0.0f);
  _cond.assign(static_cast<size_t>(maxBufferSize), 0.0f);
  _head_out.assign(static_cast<size_t>(maxBufferSize), 0.0f);

  for (auto& L : _layers)
  {
  #if NAM_A2_RING_MODE == 1
    L.pow2_size = next_pow2(L.max_lookback + maxBufferSize);
    L.pow2_mask = L.pow2_size - 1;
    L.history.assign(static_cast<size_t>(Channels) * (L.pow2_size + maxBufferSize), 0.0f);
    L.write_pos = L.max_lookback;
  #else
    L.history_cols = 2 * L.max_lookback + maxBufferSize;
    L.history.assign(static_cast<size_t>(Channels) * L.history_cols, 0.0f);
    L.write_pos = L.max_lookback;
  #endif
  }

  const int head_lookback = kHeadKernelSize - 1;
  #if NAM_A2_RING_MODE == 1
  _head_pow2_size = next_pow2(head_lookback + maxBufferSize);
  _head_pow2_mask = _head_pow2_size - 1;
  _head_history.assign(static_cast<size_t>(Channels) * (_head_pow2_size + maxBufferSize), 0.0f);
  _head_write_pos = head_lookback;
  #else
  _head_history_cols = 2 * head_lookback + maxBufferSize;
  _head_history.assign(static_cast<size_t>(Channels) * _head_history_cols, 0.0f);
  _head_write_pos = head_lookback;
  #endif
}

// --- Reset (recomputes DC blocker coefficient for new sample rate) ---
template <int Channels>
void ParametricA2FastModel<Channels>::Reset(double sampleRate, int maxBufferSize)
{
  _dc_alpha = 1.0f - 2.0f * 3.14159265358979323846f * 20.0f / static_cast<float>(sampleRate);
  _dc_x_prev = 0.0f;
  _dc_y_prev = 0.0f;
  std::copy(_values.begin(), _values.end(), _knob_smoothed.begin());
  DSP::Reset(sampleRate, maxBufferSize); // calls SetMaxBufferSize then prewarm()
}

// --- Ring write (identical to A2FastModel) ---
template <int Channels>
void ParametricA2FastModel<Channels>::_ring_write(Layer& L, int num_frames)
{
  #if NAM_A2_RING_MODE == 1
  const int mbs = GetMaxBufferSize();
  float* const hist = L.history.data();
  const float* const src = _layer_in.data();
  const int wp = L.write_pos;
  const int first = std::min(num_frames, L.pow2_size - wp);
  std::memcpy(hist + static_cast<size_t>(wp) * Channels, src, static_cast<size_t>(first) * Channels * sizeof(float));
  if (first < num_frames)
    std::memcpy(hist, src + static_cast<size_t>(first) * Channels,
                static_cast<size_t>(num_frames - first) * Channels * sizeof(float));
  std::memcpy(hist + static_cast<size_t>(L.pow2_size) * Channels, hist,
              static_cast<size_t>(mbs) * Channels * sizeof(float));
  L.write_pos = (wp + num_frames) & L.pow2_mask;
  #else
  if (L.write_pos + num_frames > L.history_cols)
  {
    const int keep = L.max_lookback;
    std::memmove(L.history.data(), L.history.data() + static_cast<size_t>(L.write_pos - keep) * Channels,
                 static_cast<size_t>(keep) * Channels * sizeof(float));
    L.write_pos = keep;
  }
  std::memcpy(L.history.data() + static_cast<size_t>(L.write_pos) * Channels, _layer_in.data(),
              static_cast<size_t>(num_frames) * Channels * sizeof(float));
  L.write_pos += num_frames;
  #endif
}

// --- Head ring write: source is _head_sum (the skip-connection accumulator) ---
template <int Channels>
void ParametricA2FastModel<Channels>::_head_ring_write(int num_frames)
{
  // Head input: the sum of every layer's post-activation (standard WaveNet skip connections).
  const float* const src = _head_sum.data();

  #if NAM_A2_RING_MODE == 1
  const int mbs = GetMaxBufferSize();
  float* const hist = _head_history.data();
  const int wp = _head_write_pos;
  const int first = std::min(num_frames, _head_pow2_size - wp);
  std::memcpy(hist + static_cast<size_t>(wp) * Channels, src, static_cast<size_t>(first) * Channels * sizeof(float));
  if (first < num_frames)
    std::memcpy(hist, src + static_cast<size_t>(first) * Channels,
                static_cast<size_t>(num_frames - first) * Channels * sizeof(float));
  std::memcpy(hist + static_cast<size_t>(_head_pow2_size) * Channels, hist,
              static_cast<size_t>(mbs) * Channels * sizeof(float));
  _head_write_pos = (wp + num_frames) & _head_pow2_mask;
  #else
  const int keep = kHeadKernelSize - 1;
  if (_head_write_pos + num_frames > _head_history_cols)
  {
    std::memmove(_head_history.data(),
                 _head_history.data() + static_cast<size_t>(_head_write_pos - keep) * Channels,
                 static_cast<size_t>(keep) * Channels * sizeof(float));
    _head_write_pos = keep;
  }
  std::memcpy(_head_history.data() + static_cast<size_t>(_head_write_pos) * Channels, src,
              static_cast<size_t>(num_frames) * Channels * sizeof(float));
  _head_write_pos += num_frames;
  #endif
}

// --- Per-layer forward: FiLM applied; head_sum accumulated (skip connections) ---
template <int Channels>
template <int KernelSize>
void ParametricA2FastModel<Channels>::_layer_forward_k(Layer& L, int li, const float* cond, int num_frames)
{
  constexpr int K = KernelSize;
  const int D = L.dilation;
  #if NAM_A2_RING_MODE == 1
  const int mask = L.pow2_mask;
  auto tap_base_phys = [&](int taps_back) { return (L.write_pos - num_frames - taps_back * D) & mask; };
  #else
  const int base = L.write_pos - num_frames;
  auto tap_base_phys = [&](int taps_back) { return base - taps_back * D; };
  #endif

  if constexpr (Channels == 3)
  {
    // Preload per-layer FiLM coefficients (constant across all frames in this buffer).
    const float g0 = _film_gamma[li][0], g1 = _film_gamma[li][1], g2 = _film_gamma[li][2];
    const float bt0 = _film_beta[li][0], bt1 = _film_beta[li][1], bt2 = _film_beta[li][2];

    float* z = _z.data();

    // Tap 0: seed z with conv_b.
    {
      const float* wk = &L.conv_w[0];
      const int tap_base = tap_base_phys(K - 1);
      const float w0=wk[0],w1=wk[1],w2=wk[2],w3=wk[3],w4=wk[4],w5=wk[5],w6=wk[6],w7=wk[7],w8=wk[8];
      const float cb0=L.conv_b[0],cb1=L.conv_b[1],cb2=L.conv_b[2];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 3];
        float* zf = z + static_cast<size_t>(f) * 3;
        zf[0] = cb0 + w0*src[0] + w3*src[1] + w6*src[2];
        zf[1] = cb1 + w1*src[0] + w4*src[1] + w7*src[2];
        zf[2] = cb2 + w2*src[0] + w5*src[1] + w8*src[2];
      }
    }

    // Taps 1..K-2: accumulate.
    for (int k = 1; k < K - 1; k++)
    {
      const float* wk = &L.conv_w[static_cast<size_t>(k) * 9];
      const int tap_base = tap_base_phys(K - 1 - k);
      const float w0=wk[0],w1=wk[1],w2=wk[2],w3=wk[3],w4=wk[4],w5=wk[5],w6=wk[6],w7=wk[7],w8=wk[8];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 3];
        float* zf = z + static_cast<size_t>(f) * 3;
        zf[0] += w0*src[0] + w3*src[1] + w6*src[2];
        zf[1] += w1*src[0] + w4*src[1] + w7*src[2];
        zf[2] += w2*src[0] + w5*src[1] + w8*src[2];
      }
    }

    // Final tap + mixin + FiLM + LeakyReLU + [head_sum accumulate] + l1x1 residual.
    const float* wk_last = &L.conv_w[static_cast<size_t>(K - 1) * 9];
    const int tap_base_last = tap_base_phys(0);
    const float cw0=wk_last[0],cw1=wk_last[1],cw2=wk_last[2];
    const float cw3=wk_last[3],cw4=wk_last[4],cw5=wk_last[5];
    const float cw6=wk_last[6],cw7=wk_last[7],cw8=wk_last[8];
    const float mw0=L.mixin_w[0],mw1=L.mixin_w[1],mw2=L.mixin_w[2];
    const float* NAM_RESTRICT l1x1_src = L.l1x1_w.data();
    const float lw00=l1x1_src[0],lw01=l1x1_src[1],lw02=l1x1_src[2];
    const float lw10=l1x1_src[3],lw11=l1x1_src[4],lw12=l1x1_src[5];
    const float lw20=l1x1_src[6],lw21=l1x1_src[7],lw22=l1x1_src[8];
    const float lb0=L.l1x1_b[0],lb1=L.l1x1_b[1],lb2=L.l1x1_b[2];
    for (int f = 0; f < num_frames; f++)
    {
      const float* src = &L.history[static_cast<size_t>(tap_base_last + f) * 3];
      const float* zf = z + static_cast<size_t>(f) * 3;
      // Final tap GEMV.
      float a0 = zf[0] + cw0*src[0] + cw3*src[1] + cw6*src[2];
      float a1 = zf[1] + cw1*src[0] + cw4*src[1] + cw7*src[2];
      float a2 = zf[2] + cw2*src[0] + cw5*src[1] + cw8*src[2];
      // Mixin.
      const float cf = cond[f];
      a0 += mw0 * cf;
      a1 += mw1 * cf;
      a2 += mw2 * cf;
      // FiLM: gamma * (conv+mixin) + beta — constant per buffer, 6 FMAs.
      a0 = g0 * a0 + bt0;
      a1 = g1 * a1 + bt1;
      a2 = g2 * a2 + bt2;
      // LeakyReLU.
      a0 = (a0 >= 0.0f) ? a0 : a0 * kLeakySlope;
      a1 = (a1 >= 0.0f) ? a1 : a1 * kLeakySlope;
      a2 = (a2 >= 0.0f) ? a2 : a2 * kLeakySlope;
      // Skip connections: accumulate this layer's post-activation into the head sum
      // (same site the static A2FastModel uses -- after FiLM+LeakyReLU, BEFORE the
      // l1x1 residual).
      float* NAM_RESTRICT hsum = &_head_sum[static_cast<size_t>(f) * 3];
      hsum[0] += a0;
      hsum[1] += a1;
      hsum[2] += a2;
      // layer1x1 residual.
      float* lin = &_layer_in[static_cast<size_t>(f) * 3];
      lin[0] += lb0 + lw00*a0 + lw10*a1 + lw20*a2;
      lin[1] += lb1 + lw01*a0 + lw11*a1 + lw21*a2;
      lin[2] += lb2 + lw02*a0 + lw12*a1 + lw22*a2;
    }
  }
  // [Fork] BEGIN: hand-unrolled Channels==4 kernel, mirrors the Channels==3 branch
  // above (adds FiLM gamma/beta vs. the non-parametric A2FastModel version) widened from a
  // 3x3 GEMV (9 weights/tap) to a 4x4 GEMV (16 weights/tap, 4 accumulators). Same
  // column-major weight convention: w[j*Channels + i] for output row i, input col j.
  // Verified numerically against the generic ParametricWaveNet reference by
  // test_parametric_matches_generic_medium.
  else if constexpr (Channels == 4)
  {
    // Preload per-layer FiLM coefficients (constant across all frames in this buffer).
    const float g0 = _film_gamma[li][0], g1 = _film_gamma[li][1], g2 = _film_gamma[li][2], g3 = _film_gamma[li][3];
    const float bt0 = _film_beta[li][0], bt1 = _film_beta[li][1], bt2 = _film_beta[li][2], bt3 = _film_beta[li][3];

    float* z = _z.data();

    // Tap 0: seed z with conv_b.
    {
      const float* wk = &L.conv_w[0];
      const int tap_base = tap_base_phys(K - 1);
      const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3];
      const float w4 = wk[4], w5 = wk[5], w6 = wk[6], w7 = wk[7];
      const float w8 = wk[8], w9 = wk[9], w10 = wk[10], w11 = wk[11];
      const float w12 = wk[12], w13 = wk[13], w14 = wk[14], w15 = wk[15];
      const float cb0 = L.conv_b[0], cb1 = L.conv_b[1], cb2 = L.conv_b[2], cb3 = L.conv_b[3];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 4];
        float* zf = z + static_cast<size_t>(f) * 4;
        zf[0] = cb0 + w0 * src[0] + w4 * src[1] + w8 * src[2] + w12 * src[3];
        zf[1] = cb1 + w1 * src[0] + w5 * src[1] + w9 * src[2] + w13 * src[3];
        zf[2] = cb2 + w2 * src[0] + w6 * src[1] + w10 * src[2] + w14 * src[3];
        zf[3] = cb3 + w3 * src[0] + w7 * src[1] + w11 * src[2] + w15 * src[3];
      }
    }

    // Taps 1..K-2: accumulate.
    for (int k = 1; k < K - 1; k++)
    {
      const float* wk = &L.conv_w[static_cast<size_t>(k) * 16];
      const int tap_base = tap_base_phys(K - 1 - k);
      const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3];
      const float w4 = wk[4], w5 = wk[5], w6 = wk[6], w7 = wk[7];
      const float w8 = wk[8], w9 = wk[9], w10 = wk[10], w11 = wk[11];
      const float w12 = wk[12], w13 = wk[13], w14 = wk[14], w15 = wk[15];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 4];
        float* zf = z + static_cast<size_t>(f) * 4;
        zf[0] += w0 * src[0] + w4 * src[1] + w8 * src[2] + w12 * src[3];
        zf[1] += w1 * src[0] + w5 * src[1] + w9 * src[2] + w13 * src[3];
        zf[2] += w2 * src[0] + w6 * src[1] + w10 * src[2] + w14 * src[3];
        zf[3] += w3 * src[0] + w7 * src[1] + w11 * src[2] + w15 * src[3];
      }
    }

    // Final tap + mixin + FiLM + LeakyReLU + [head_sum accumulate] + l1x1 residual.
    const float* wk_last = &L.conv_w[static_cast<size_t>(K - 1) * 16];
    const int tap_base_last = tap_base_phys(0);
    const float cw0 = wk_last[0], cw1 = wk_last[1], cw2 = wk_last[2], cw3 = wk_last[3];
    const float cw4 = wk_last[4], cw5 = wk_last[5], cw6 = wk_last[6], cw7 = wk_last[7];
    const float cw8 = wk_last[8], cw9 = wk_last[9], cw10 = wk_last[10], cw11 = wk_last[11];
    const float cw12 = wk_last[12], cw13 = wk_last[13], cw14 = wk_last[14], cw15 = wk_last[15];
    const float mw0 = L.mixin_w[0], mw1 = L.mixin_w[1], mw2 = L.mixin_w[2], mw3 = L.mixin_w[3];
    const float* NAM_RESTRICT l1x1_src = L.l1x1_w.data();
    const float lw00 = l1x1_src[0], lw01 = l1x1_src[1], lw02 = l1x1_src[2], lw03 = l1x1_src[3];
    const float lw10 = l1x1_src[4], lw11 = l1x1_src[5], lw12 = l1x1_src[6], lw13 = l1x1_src[7];
    const float lw20 = l1x1_src[8], lw21 = l1x1_src[9], lw22 = l1x1_src[10], lw23 = l1x1_src[11];
    const float lw30 = l1x1_src[12], lw31 = l1x1_src[13], lw32 = l1x1_src[14], lw33 = l1x1_src[15];
    const float lb0 = L.l1x1_b[0], lb1 = L.l1x1_b[1], lb2 = L.l1x1_b[2], lb3 = L.l1x1_b[3];
    for (int f = 0; f < num_frames; f++)
    {
      const float* src = &L.history[static_cast<size_t>(tap_base_last + f) * 4];
      const float* zf = z + static_cast<size_t>(f) * 4;
      // Final tap GEMV.
      float a0 = zf[0] + cw0 * src[0] + cw4 * src[1] + cw8 * src[2] + cw12 * src[3];
      float a1 = zf[1] + cw1 * src[0] + cw5 * src[1] + cw9 * src[2] + cw13 * src[3];
      float a2 = zf[2] + cw2 * src[0] + cw6 * src[1] + cw10 * src[2] + cw14 * src[3];
      float a3 = zf[3] + cw3 * src[0] + cw7 * src[1] + cw11 * src[2] + cw15 * src[3];
      // Mixin.
      const float cf = cond[f];
      a0 += mw0 * cf;
      a1 += mw1 * cf;
      a2 += mw2 * cf;
      a3 += mw3 * cf;
      // FiLM: gamma * (conv+mixin) + beta.
      a0 = g0 * a0 + bt0;
      a1 = g1 * a1 + bt1;
      a2 = g2 * a2 + bt2;
      a3 = g3 * a3 + bt3;
      // LeakyReLU.
      a0 = (a0 >= 0.0f) ? a0 : a0 * kLeakySlope;
      a1 = (a1 >= 0.0f) ? a1 : a1 * kLeakySlope;
      a2 = (a2 >= 0.0f) ? a2 : a2 * kLeakySlope;
      a3 = (a3 >= 0.0f) ? a3 : a3 * kLeakySlope;
      // Skip connections: accumulate this layer's post-activation into the head sum
      // (same site the static A2FastModel uses -- after FiLM+LeakyReLU, BEFORE the
      // l1x1 residual).
      float* NAM_RESTRICT hsum = &_head_sum[static_cast<size_t>(f) * 4];
      hsum[0] += a0;
      hsum[1] += a1;
      hsum[2] += a2;
      hsum[3] += a3;
      // layer1x1 residual.
      float* lin = &_layer_in[static_cast<size_t>(f) * 4];
      lin[0] += lb0 + lw00 * a0 + lw10 * a1 + lw20 * a2 + lw30 * a3;
      lin[1] += lb1 + lw01 * a0 + lw11 * a1 + lw21 * a2 + lw31 * a3;
      lin[2] += lb2 + lw02 * a0 + lw12 * a1 + lw22 * a2 + lw32 * a3;
      lin[3] += lb3 + lw03 * a0 + lw13 * a1 + lw23 * a2 + lw33 * a3;
    }
  }
  // [Fork] END: hand-unrolled Channels==4 kernel
  else if constexpr (Channels == 5)
  {
    // Preload per-layer FiLM coefficients (constant across all frames in this buffer).
    const float g0 = _film_gamma[li][0], g1 = _film_gamma[li][1], g2 = _film_gamma[li][2], g3 = _film_gamma[li][3], g4 = _film_gamma[li][4];
    const float bt0 = _film_beta[li][0], bt1 = _film_beta[li][1], bt2 = _film_beta[li][2], bt3 = _film_beta[li][3], bt4 = _film_beta[li][4];

    float* z = _z.data();

    // Tap 0: seed z with conv_b.
    {
      const float* wk = &L.conv_w[0];
      const int tap_base = tap_base_phys(K - 1);
      const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3], w4 = wk[4];
      const float w5 = wk[5], w6 = wk[6], w7 = wk[7], w8 = wk[8], w9 = wk[9];
      const float w10 = wk[10], w11 = wk[11], w12 = wk[12], w13 = wk[13], w14 = wk[14];
      const float w15 = wk[15], w16 = wk[16], w17 = wk[17], w18 = wk[18], w19 = wk[19];
      const float w20 = wk[20], w21 = wk[21], w22 = wk[22], w23 = wk[23], w24 = wk[24];
      const float cb0 = L.conv_b[0], cb1 = L.conv_b[1], cb2 = L.conv_b[2], cb3 = L.conv_b[3], cb4 = L.conv_b[4];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 5];
        float* zf = z + static_cast<size_t>(f) * 5;
        zf[0] = cb0 + w0 * src[0] + w5 * src[1] + w10 * src[2] + w15 * src[3] + w20 * src[4];
        zf[1] = cb1 + w1 * src[0] + w6 * src[1] + w11 * src[2] + w16 * src[3] + w21 * src[4];
        zf[2] = cb2 + w2 * src[0] + w7 * src[1] + w12 * src[2] + w17 * src[3] + w22 * src[4];
        zf[3] = cb3 + w3 * src[0] + w8 * src[1] + w13 * src[2] + w18 * src[3] + w23 * src[4];
        zf[4] = cb4 + w4 * src[0] + w9 * src[1] + w14 * src[2] + w19 * src[3] + w24 * src[4];
      }
    }

    // Taps 1..K-2: accumulate.
    for (int k = 1; k < K - 1; k++)
    {
      const float* wk = &L.conv_w[static_cast<size_t>(k) * 25];
      const int tap_base = tap_base_phys(K - 1 - k);
      const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3], w4 = wk[4];
      const float w5 = wk[5], w6 = wk[6], w7 = wk[7], w8 = wk[8], w9 = wk[9];
      const float w10 = wk[10], w11 = wk[11], w12 = wk[12], w13 = wk[13], w14 = wk[14];
      const float w15 = wk[15], w16 = wk[16], w17 = wk[17], w18 = wk[18], w19 = wk[19];
      const float w20 = wk[20], w21 = wk[21], w22 = wk[22], w23 = wk[23], w24 = wk[24];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 5];
        float* zf = z + static_cast<size_t>(f) * 5;
        zf[0] += w0 * src[0] + w5 * src[1] + w10 * src[2] + w15 * src[3] + w20 * src[4];
        zf[1] += w1 * src[0] + w6 * src[1] + w11 * src[2] + w16 * src[3] + w21 * src[4];
        zf[2] += w2 * src[0] + w7 * src[1] + w12 * src[2] + w17 * src[3] + w22 * src[4];
        zf[3] += w3 * src[0] + w8 * src[1] + w13 * src[2] + w18 * src[3] + w23 * src[4];
        zf[4] += w4 * src[0] + w9 * src[1] + w14 * src[2] + w19 * src[3] + w24 * src[4];
      }
    }

    // Final tap + mixin + FiLM + LeakyReLU + [head_sum accumulate] + l1x1 residual.
    const float* wk_last = &L.conv_w[static_cast<size_t>(K - 1) * 25];
    const int tap_base_last = tap_base_phys(0);
    const float cw0 = wk_last[0], cw1 = wk_last[1], cw2 = wk_last[2], cw3 = wk_last[3], cw4 = wk_last[4];
    const float cw5 = wk_last[5], cw6 = wk_last[6], cw7 = wk_last[7], cw8 = wk_last[8], cw9 = wk_last[9];
    const float cw10 = wk_last[10], cw11 = wk_last[11], cw12 = wk_last[12], cw13 = wk_last[13], cw14 = wk_last[14];
    const float cw15 = wk_last[15], cw16 = wk_last[16], cw17 = wk_last[17], cw18 = wk_last[18], cw19 = wk_last[19];
    const float cw20 = wk_last[20], cw21 = wk_last[21], cw22 = wk_last[22], cw23 = wk_last[23], cw24 = wk_last[24];
    const float mw0 = L.mixin_w[0], mw1 = L.mixin_w[1], mw2 = L.mixin_w[2], mw3 = L.mixin_w[3], mw4 = L.mixin_w[4];
    const float* NAM_RESTRICT l1x1_src = L.l1x1_w.data();
    const float lw00 = l1x1_src[0], lw01 = l1x1_src[1], lw02 = l1x1_src[2], lw03 = l1x1_src[3], lw04 = l1x1_src[4];
    const float lw10 = l1x1_src[5], lw11 = l1x1_src[6], lw12 = l1x1_src[7], lw13 = l1x1_src[8], lw14 = l1x1_src[9];
    const float lw20 = l1x1_src[10], lw21 = l1x1_src[11], lw22 = l1x1_src[12], lw23 = l1x1_src[13], lw24 = l1x1_src[14];
    const float lw30 = l1x1_src[15], lw31 = l1x1_src[16], lw32 = l1x1_src[17], lw33 = l1x1_src[18], lw34 = l1x1_src[19];
    const float lw40 = l1x1_src[20], lw41 = l1x1_src[21], lw42 = l1x1_src[22], lw43 = l1x1_src[23], lw44 = l1x1_src[24];
    const float lb0 = L.l1x1_b[0], lb1 = L.l1x1_b[1], lb2 = L.l1x1_b[2], lb3 = L.l1x1_b[3], lb4 = L.l1x1_b[4];
    for (int f = 0; f < num_frames; f++)
    {
      const float* src = &L.history[static_cast<size_t>(tap_base_last + f) * 5];
      const float* zf = z + static_cast<size_t>(f) * 5;
      // Final tap GEMV.
      float a0 = zf[0] + cw0 * src[0] + cw5 * src[1] + cw10 * src[2] + cw15 * src[3] + cw20 * src[4];
      float a1 = zf[1] + cw1 * src[0] + cw6 * src[1] + cw11 * src[2] + cw16 * src[3] + cw21 * src[4];
      float a2 = zf[2] + cw2 * src[0] + cw7 * src[1] + cw12 * src[2] + cw17 * src[3] + cw22 * src[4];
      float a3 = zf[3] + cw3 * src[0] + cw8 * src[1] + cw13 * src[2] + cw18 * src[3] + cw23 * src[4];
      float a4 = zf[4] + cw4 * src[0] + cw9 * src[1] + cw14 * src[2] + cw19 * src[3] + cw24 * src[4];
      // Mixin.
      const float cf = cond[f];
      a0 += mw0 * cf;
      a1 += mw1 * cf;
      a2 += mw2 * cf;
      a3 += mw3 * cf;
      a4 += mw4 * cf;
      // FiLM: gamma * (conv+mixin) + beta.
      a0 = g0 * a0 + bt0;
      a1 = g1 * a1 + bt1;
      a2 = g2 * a2 + bt2;
      a3 = g3 * a3 + bt3;
      a4 = g4 * a4 + bt4;
      // LeakyReLU.
      a0 = (a0 >= 0.0f) ? a0 : a0 * kLeakySlope;
      a1 = (a1 >= 0.0f) ? a1 : a1 * kLeakySlope;
      a2 = (a2 >= 0.0f) ? a2 : a2 * kLeakySlope;
      a3 = (a3 >= 0.0f) ? a3 : a3 * kLeakySlope;
      a4 = (a4 >= 0.0f) ? a4 : a4 * kLeakySlope;
      // Skip connections: accumulate this layer's post-activation into the head sum
      // (same site the static A2FastModel uses -- after FiLM+LeakyReLU, BEFORE the
      // l1x1 residual).
      float* NAM_RESTRICT hsum = &_head_sum[static_cast<size_t>(f) * 5];
      hsum[0] += a0;
      hsum[1] += a1;
      hsum[2] += a2;
      hsum[3] += a3;
      hsum[4] += a4;
      // layer1x1 residual.
      float* lin = &_layer_in[static_cast<size_t>(f) * 5];
      lin[0] += lb0 + lw00 * a0 + lw10 * a1 + lw20 * a2 + lw30 * a3 + lw40 * a4;
      lin[1] += lb1 + lw01 * a0 + lw11 * a1 + lw21 * a2 + lw31 * a3 + lw41 * a4;
      lin[2] += lb2 + lw02 * a0 + lw12 * a1 + lw22 * a2 + lw32 * a3 + lw42 * a4;
      lin[3] += lb3 + lw03 * a0 + lw13 * a1 + lw23 * a2 + lw33 * a3 + lw43 * a4;
      lin[4] += lb4 + lw04 * a0 + lw14 * a1 + lw24 * a2 + lw34 * a3 + lw44 * a4;
    }
  }
  // [Fork] END: hand-unrolled Channels==5 kernel
  else if constexpr (Channels == 6)
  {
    // Preload per-layer FiLM coefficients (constant across all frames in this buffer).
    const float g0 = _film_gamma[li][0], g1 = _film_gamma[li][1], g2 = _film_gamma[li][2], g3 = _film_gamma[li][3], g4 = _film_gamma[li][4], g5 = _film_gamma[li][5];
    const float bt0 = _film_beta[li][0], bt1 = _film_beta[li][1], bt2 = _film_beta[li][2], bt3 = _film_beta[li][3], bt4 = _film_beta[li][4], bt5 = _film_beta[li][5];

    float* z = _z.data();

    // Tap 0: seed z with conv_b.
    {
      const float* wk = &L.conv_w[0];
      const int tap_base = tap_base_phys(K - 1);
      const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3], w4 = wk[4], w5 = wk[5];
      const float w6 = wk[6], w7 = wk[7], w8 = wk[8], w9 = wk[9], w10 = wk[10], w11 = wk[11];
      const float w12 = wk[12], w13 = wk[13], w14 = wk[14], w15 = wk[15], w16 = wk[16], w17 = wk[17];
      const float w18 = wk[18], w19 = wk[19], w20 = wk[20], w21 = wk[21], w22 = wk[22], w23 = wk[23];
      const float w24 = wk[24], w25 = wk[25], w26 = wk[26], w27 = wk[27], w28 = wk[28], w29 = wk[29];
      const float w30 = wk[30], w31 = wk[31], w32 = wk[32], w33 = wk[33], w34 = wk[34], w35 = wk[35];
      const float cb0 = L.conv_b[0], cb1 = L.conv_b[1], cb2 = L.conv_b[2], cb3 = L.conv_b[3], cb4 = L.conv_b[4], cb5 = L.conv_b[5];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 6];
        float* zf = z + static_cast<size_t>(f) * 6;
        zf[0] = cb0 + w0 * src[0] + w6 * src[1] + w12 * src[2] + w18 * src[3] + w24 * src[4] + w30 * src[5];
        zf[1] = cb1 + w1 * src[0] + w7 * src[1] + w13 * src[2] + w19 * src[3] + w25 * src[4] + w31 * src[5];
        zf[2] = cb2 + w2 * src[0] + w8 * src[1] + w14 * src[2] + w20 * src[3] + w26 * src[4] + w32 * src[5];
        zf[3] = cb3 + w3 * src[0] + w9 * src[1] + w15 * src[2] + w21 * src[3] + w27 * src[4] + w33 * src[5];
        zf[4] = cb4 + w4 * src[0] + w10 * src[1] + w16 * src[2] + w22 * src[3] + w28 * src[4] + w34 * src[5];
        zf[5] = cb5 + w5 * src[0] + w11 * src[1] + w17 * src[2] + w23 * src[3] + w29 * src[4] + w35 * src[5];
      }
    }

    // Taps 1..K-2: accumulate.
    for (int k = 1; k < K - 1; k++)
    {
      const float* wk = &L.conv_w[static_cast<size_t>(k) * 36];
      const int tap_base = tap_base_phys(K - 1 - k);
      const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3], w4 = wk[4], w5 = wk[5];
      const float w6 = wk[6], w7 = wk[7], w8 = wk[8], w9 = wk[9], w10 = wk[10], w11 = wk[11];
      const float w12 = wk[12], w13 = wk[13], w14 = wk[14], w15 = wk[15], w16 = wk[16], w17 = wk[17];
      const float w18 = wk[18], w19 = wk[19], w20 = wk[20], w21 = wk[21], w22 = wk[22], w23 = wk[23];
      const float w24 = wk[24], w25 = wk[25], w26 = wk[26], w27 = wk[27], w28 = wk[28], w29 = wk[29];
      const float w30 = wk[30], w31 = wk[31], w32 = wk[32], w33 = wk[33], w34 = wk[34], w35 = wk[35];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 6];
        float* zf = z + static_cast<size_t>(f) * 6;
        zf[0] += w0 * src[0] + w6 * src[1] + w12 * src[2] + w18 * src[3] + w24 * src[4] + w30 * src[5];
        zf[1] += w1 * src[0] + w7 * src[1] + w13 * src[2] + w19 * src[3] + w25 * src[4] + w31 * src[5];
        zf[2] += w2 * src[0] + w8 * src[1] + w14 * src[2] + w20 * src[3] + w26 * src[4] + w32 * src[5];
        zf[3] += w3 * src[0] + w9 * src[1] + w15 * src[2] + w21 * src[3] + w27 * src[4] + w33 * src[5];
        zf[4] += w4 * src[0] + w10 * src[1] + w16 * src[2] + w22 * src[3] + w28 * src[4] + w34 * src[5];
        zf[5] += w5 * src[0] + w11 * src[1] + w17 * src[2] + w23 * src[3] + w29 * src[4] + w35 * src[5];
      }
    }

    // Final tap + mixin + FiLM + LeakyReLU + [head_sum accumulate] + l1x1 residual.
    const float* wk_last = &L.conv_w[static_cast<size_t>(K - 1) * 36];
    const int tap_base_last = tap_base_phys(0);
    const float cw0 = wk_last[0], cw1 = wk_last[1], cw2 = wk_last[2], cw3 = wk_last[3], cw4 = wk_last[4], cw5 = wk_last[5];
    const float cw6 = wk_last[6], cw7 = wk_last[7], cw8 = wk_last[8], cw9 = wk_last[9], cw10 = wk_last[10], cw11 = wk_last[11];
    const float cw12 = wk_last[12], cw13 = wk_last[13], cw14 = wk_last[14], cw15 = wk_last[15], cw16 = wk_last[16], cw17 = wk_last[17];
    const float cw18 = wk_last[18], cw19 = wk_last[19], cw20 = wk_last[20], cw21 = wk_last[21], cw22 = wk_last[22], cw23 = wk_last[23];
    const float cw24 = wk_last[24], cw25 = wk_last[25], cw26 = wk_last[26], cw27 = wk_last[27], cw28 = wk_last[28], cw29 = wk_last[29];
    const float cw30 = wk_last[30], cw31 = wk_last[31], cw32 = wk_last[32], cw33 = wk_last[33], cw34 = wk_last[34], cw35 = wk_last[35];
    const float mw0 = L.mixin_w[0], mw1 = L.mixin_w[1], mw2 = L.mixin_w[2], mw3 = L.mixin_w[3], mw4 = L.mixin_w[4], mw5 = L.mixin_w[5];
    const float* NAM_RESTRICT l1x1_src = L.l1x1_w.data();
    const float lw00 = l1x1_src[0], lw01 = l1x1_src[1], lw02 = l1x1_src[2], lw03 = l1x1_src[3], lw04 = l1x1_src[4], lw05 = l1x1_src[5];
    const float lw10 = l1x1_src[6], lw11 = l1x1_src[7], lw12 = l1x1_src[8], lw13 = l1x1_src[9], lw14 = l1x1_src[10], lw15 = l1x1_src[11];
    const float lw20 = l1x1_src[12], lw21 = l1x1_src[13], lw22 = l1x1_src[14], lw23 = l1x1_src[15], lw24 = l1x1_src[16], lw25 = l1x1_src[17];
    const float lw30 = l1x1_src[18], lw31 = l1x1_src[19], lw32 = l1x1_src[20], lw33 = l1x1_src[21], lw34 = l1x1_src[22], lw35 = l1x1_src[23];
    const float lw40 = l1x1_src[24], lw41 = l1x1_src[25], lw42 = l1x1_src[26], lw43 = l1x1_src[27], lw44 = l1x1_src[28], lw45 = l1x1_src[29];
    const float lw50 = l1x1_src[30], lw51 = l1x1_src[31], lw52 = l1x1_src[32], lw53 = l1x1_src[33], lw54 = l1x1_src[34], lw55 = l1x1_src[35];
    const float lb0 = L.l1x1_b[0], lb1 = L.l1x1_b[1], lb2 = L.l1x1_b[2], lb3 = L.l1x1_b[3], lb4 = L.l1x1_b[4], lb5 = L.l1x1_b[5];
    for (int f = 0; f < num_frames; f++)
    {
      const float* src = &L.history[static_cast<size_t>(tap_base_last + f) * 6];
      const float* zf = z + static_cast<size_t>(f) * 6;
      // Final tap GEMV.
      float a0 = zf[0] + cw0 * src[0] + cw6 * src[1] + cw12 * src[2] + cw18 * src[3] + cw24 * src[4] + cw30 * src[5];
      float a1 = zf[1] + cw1 * src[0] + cw7 * src[1] + cw13 * src[2] + cw19 * src[3] + cw25 * src[4] + cw31 * src[5];
      float a2 = zf[2] + cw2 * src[0] + cw8 * src[1] + cw14 * src[2] + cw20 * src[3] + cw26 * src[4] + cw32 * src[5];
      float a3 = zf[3] + cw3 * src[0] + cw9 * src[1] + cw15 * src[2] + cw21 * src[3] + cw27 * src[4] + cw33 * src[5];
      float a4 = zf[4] + cw4 * src[0] + cw10 * src[1] + cw16 * src[2] + cw22 * src[3] + cw28 * src[4] + cw34 * src[5];
      float a5 = zf[5] + cw5 * src[0] + cw11 * src[1] + cw17 * src[2] + cw23 * src[3] + cw29 * src[4] + cw35 * src[5];
      // Mixin.
      const float cf = cond[f];
      a0 += mw0 * cf;
      a1 += mw1 * cf;
      a2 += mw2 * cf;
      a3 += mw3 * cf;
      a4 += mw4 * cf;
      a5 += mw5 * cf;
      // FiLM: gamma * (conv+mixin) + beta.
      a0 = g0 * a0 + bt0;
      a1 = g1 * a1 + bt1;
      a2 = g2 * a2 + bt2;
      a3 = g3 * a3 + bt3;
      a4 = g4 * a4 + bt4;
      a5 = g5 * a5 + bt5;
      // LeakyReLU.
      a0 = (a0 >= 0.0f) ? a0 : a0 * kLeakySlope;
      a1 = (a1 >= 0.0f) ? a1 : a1 * kLeakySlope;
      a2 = (a2 >= 0.0f) ? a2 : a2 * kLeakySlope;
      a3 = (a3 >= 0.0f) ? a3 : a3 * kLeakySlope;
      a4 = (a4 >= 0.0f) ? a4 : a4 * kLeakySlope;
      a5 = (a5 >= 0.0f) ? a5 : a5 * kLeakySlope;
      // Skip connections: accumulate this layer's post-activation into the head sum
      // (same site the static A2FastModel uses -- after FiLM+LeakyReLU, BEFORE the
      // l1x1 residual).
      float* NAM_RESTRICT hsum = &_head_sum[static_cast<size_t>(f) * 6];
      hsum[0] += a0;
      hsum[1] += a1;
      hsum[2] += a2;
      hsum[3] += a3;
      hsum[4] += a4;
      hsum[5] += a5;
      // layer1x1 residual.
      float* lin = &_layer_in[static_cast<size_t>(f) * 6];
      lin[0] += lb0 + lw00 * a0 + lw10 * a1 + lw20 * a2 + lw30 * a3 + lw40 * a4 + lw50 * a5;
      lin[1] += lb1 + lw01 * a0 + lw11 * a1 + lw21 * a2 + lw31 * a3 + lw41 * a4 + lw51 * a5;
      lin[2] += lb2 + lw02 * a0 + lw12 * a1 + lw22 * a2 + lw32 * a3 + lw42 * a4 + lw52 * a5;
      lin[3] += lb3 + lw03 * a0 + lw13 * a1 + lw23 * a2 + lw33 * a3 + lw43 * a4 + lw53 * a5;
      lin[4] += lb4 + lw04 * a0 + lw14 * a1 + lw24 * a2 + lw34 * a3 + lw44 * a4 + lw54 * a5;
      lin[5] += lb5 + lw05 * a0 + lw15 * a1 + lw25 * a2 + lw35 * a3 + lw45 * a4 + lw55 * a5;
    }
  }
  // [Fork] END: hand-unrolled Channels==6 kernel
  else if constexpr (Channels == 7)
  {
    // Preload per-layer FiLM coefficients (constant across all frames in this buffer).
    const float g0 = _film_gamma[li][0], g1 = _film_gamma[li][1], g2 = _film_gamma[li][2], g3 = _film_gamma[li][3], g4 = _film_gamma[li][4], g5 = _film_gamma[li][5], g6 = _film_gamma[li][6];
    const float bt0 = _film_beta[li][0], bt1 = _film_beta[li][1], bt2 = _film_beta[li][2], bt3 = _film_beta[li][3], bt4 = _film_beta[li][4], bt5 = _film_beta[li][5], bt6 = _film_beta[li][6];

    float* z = _z.data();

    // Tap 0: seed z with conv_b.
    {
      const float* wk = &L.conv_w[0];
      const int tap_base = tap_base_phys(K - 1);
      const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3], w4 = wk[4], w5 = wk[5], w6 = wk[6];
      const float w7 = wk[7], w8 = wk[8], w9 = wk[9], w10 = wk[10], w11 = wk[11], w12 = wk[12], w13 = wk[13];
      const float w14 = wk[14], w15 = wk[15], w16 = wk[16], w17 = wk[17], w18 = wk[18], w19 = wk[19], w20 = wk[20];
      const float w21 = wk[21], w22 = wk[22], w23 = wk[23], w24 = wk[24], w25 = wk[25], w26 = wk[26], w27 = wk[27];
      const float w28 = wk[28], w29 = wk[29], w30 = wk[30], w31 = wk[31], w32 = wk[32], w33 = wk[33], w34 = wk[34];
      const float w35 = wk[35], w36 = wk[36], w37 = wk[37], w38 = wk[38], w39 = wk[39], w40 = wk[40], w41 = wk[41];
      const float w42 = wk[42], w43 = wk[43], w44 = wk[44], w45 = wk[45], w46 = wk[46], w47 = wk[47], w48 = wk[48];
      const float cb0 = L.conv_b[0], cb1 = L.conv_b[1], cb2 = L.conv_b[2], cb3 = L.conv_b[3], cb4 = L.conv_b[4], cb5 = L.conv_b[5], cb6 = L.conv_b[6];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 7];
        float* zf = z + static_cast<size_t>(f) * 7;
        zf[0] = cb0 + w0 * src[0] + w7 * src[1] + w14 * src[2] + w21 * src[3] + w28 * src[4] + w35 * src[5] + w42 * src[6];
        zf[1] = cb1 + w1 * src[0] + w8 * src[1] + w15 * src[2] + w22 * src[3] + w29 * src[4] + w36 * src[5] + w43 * src[6];
        zf[2] = cb2 + w2 * src[0] + w9 * src[1] + w16 * src[2] + w23 * src[3] + w30 * src[4] + w37 * src[5] + w44 * src[6];
        zf[3] = cb3 + w3 * src[0] + w10 * src[1] + w17 * src[2] + w24 * src[3] + w31 * src[4] + w38 * src[5] + w45 * src[6];
        zf[4] = cb4 + w4 * src[0] + w11 * src[1] + w18 * src[2] + w25 * src[3] + w32 * src[4] + w39 * src[5] + w46 * src[6];
        zf[5] = cb5 + w5 * src[0] + w12 * src[1] + w19 * src[2] + w26 * src[3] + w33 * src[4] + w40 * src[5] + w47 * src[6];
        zf[6] = cb6 + w6 * src[0] + w13 * src[1] + w20 * src[2] + w27 * src[3] + w34 * src[4] + w41 * src[5] + w48 * src[6];
      }
    }

    // Taps 1..K-2: accumulate.
    for (int k = 1; k < K - 1; k++)
    {
      const float* wk = &L.conv_w[static_cast<size_t>(k) * 49];
      const int tap_base = tap_base_phys(K - 1 - k);
      const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3], w4 = wk[4], w5 = wk[5], w6 = wk[6];
      const float w7 = wk[7], w8 = wk[8], w9 = wk[9], w10 = wk[10], w11 = wk[11], w12 = wk[12], w13 = wk[13];
      const float w14 = wk[14], w15 = wk[15], w16 = wk[16], w17 = wk[17], w18 = wk[18], w19 = wk[19], w20 = wk[20];
      const float w21 = wk[21], w22 = wk[22], w23 = wk[23], w24 = wk[24], w25 = wk[25], w26 = wk[26], w27 = wk[27];
      const float w28 = wk[28], w29 = wk[29], w30 = wk[30], w31 = wk[31], w32 = wk[32], w33 = wk[33], w34 = wk[34];
      const float w35 = wk[35], w36 = wk[36], w37 = wk[37], w38 = wk[38], w39 = wk[39], w40 = wk[40], w41 = wk[41];
      const float w42 = wk[42], w43 = wk[43], w44 = wk[44], w45 = wk[45], w46 = wk[46], w47 = wk[47], w48 = wk[48];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 7];
        float* zf = z + static_cast<size_t>(f) * 7;
        zf[0] += w0 * src[0] + w7 * src[1] + w14 * src[2] + w21 * src[3] + w28 * src[4] + w35 * src[5] + w42 * src[6];
        zf[1] += w1 * src[0] + w8 * src[1] + w15 * src[2] + w22 * src[3] + w29 * src[4] + w36 * src[5] + w43 * src[6];
        zf[2] += w2 * src[0] + w9 * src[1] + w16 * src[2] + w23 * src[3] + w30 * src[4] + w37 * src[5] + w44 * src[6];
        zf[3] += w3 * src[0] + w10 * src[1] + w17 * src[2] + w24 * src[3] + w31 * src[4] + w38 * src[5] + w45 * src[6];
        zf[4] += w4 * src[0] + w11 * src[1] + w18 * src[2] + w25 * src[3] + w32 * src[4] + w39 * src[5] + w46 * src[6];
        zf[5] += w5 * src[0] + w12 * src[1] + w19 * src[2] + w26 * src[3] + w33 * src[4] + w40 * src[5] + w47 * src[6];
        zf[6] += w6 * src[0] + w13 * src[1] + w20 * src[2] + w27 * src[3] + w34 * src[4] + w41 * src[5] + w48 * src[6];
      }
    }

    // Final tap + mixin + FiLM + LeakyReLU + [head_sum accumulate] + l1x1 residual.
    const float* wk_last = &L.conv_w[static_cast<size_t>(K - 1) * 49];
    const int tap_base_last = tap_base_phys(0);
    const float cw0 = wk_last[0], cw1 = wk_last[1], cw2 = wk_last[2], cw3 = wk_last[3], cw4 = wk_last[4], cw5 = wk_last[5], cw6 = wk_last[6];
    const float cw7 = wk_last[7], cw8 = wk_last[8], cw9 = wk_last[9], cw10 = wk_last[10], cw11 = wk_last[11], cw12 = wk_last[12], cw13 = wk_last[13];
    const float cw14 = wk_last[14], cw15 = wk_last[15], cw16 = wk_last[16], cw17 = wk_last[17], cw18 = wk_last[18], cw19 = wk_last[19], cw20 = wk_last[20];
    const float cw21 = wk_last[21], cw22 = wk_last[22], cw23 = wk_last[23], cw24 = wk_last[24], cw25 = wk_last[25], cw26 = wk_last[26], cw27 = wk_last[27];
    const float cw28 = wk_last[28], cw29 = wk_last[29], cw30 = wk_last[30], cw31 = wk_last[31], cw32 = wk_last[32], cw33 = wk_last[33], cw34 = wk_last[34];
    const float cw35 = wk_last[35], cw36 = wk_last[36], cw37 = wk_last[37], cw38 = wk_last[38], cw39 = wk_last[39], cw40 = wk_last[40], cw41 = wk_last[41];
    const float cw42 = wk_last[42], cw43 = wk_last[43], cw44 = wk_last[44], cw45 = wk_last[45], cw46 = wk_last[46], cw47 = wk_last[47], cw48 = wk_last[48];
    const float mw0 = L.mixin_w[0], mw1 = L.mixin_w[1], mw2 = L.mixin_w[2], mw3 = L.mixin_w[3], mw4 = L.mixin_w[4], mw5 = L.mixin_w[5], mw6 = L.mixin_w[6];
    const float* NAM_RESTRICT l1x1_src = L.l1x1_w.data();
    const float lw00 = l1x1_src[0], lw01 = l1x1_src[1], lw02 = l1x1_src[2], lw03 = l1x1_src[3], lw04 = l1x1_src[4], lw05 = l1x1_src[5], lw06 = l1x1_src[6];
    const float lw10 = l1x1_src[7], lw11 = l1x1_src[8], lw12 = l1x1_src[9], lw13 = l1x1_src[10], lw14 = l1x1_src[11], lw15 = l1x1_src[12], lw16 = l1x1_src[13];
    const float lw20 = l1x1_src[14], lw21 = l1x1_src[15], lw22 = l1x1_src[16], lw23 = l1x1_src[17], lw24 = l1x1_src[18], lw25 = l1x1_src[19], lw26 = l1x1_src[20];
    const float lw30 = l1x1_src[21], lw31 = l1x1_src[22], lw32 = l1x1_src[23], lw33 = l1x1_src[24], lw34 = l1x1_src[25], lw35 = l1x1_src[26], lw36 = l1x1_src[27];
    const float lw40 = l1x1_src[28], lw41 = l1x1_src[29], lw42 = l1x1_src[30], lw43 = l1x1_src[31], lw44 = l1x1_src[32], lw45 = l1x1_src[33], lw46 = l1x1_src[34];
    const float lw50 = l1x1_src[35], lw51 = l1x1_src[36], lw52 = l1x1_src[37], lw53 = l1x1_src[38], lw54 = l1x1_src[39], lw55 = l1x1_src[40], lw56 = l1x1_src[41];
    const float lw60 = l1x1_src[42], lw61 = l1x1_src[43], lw62 = l1x1_src[44], lw63 = l1x1_src[45], lw64 = l1x1_src[46], lw65 = l1x1_src[47], lw66 = l1x1_src[48];
    const float lb0 = L.l1x1_b[0], lb1 = L.l1x1_b[1], lb2 = L.l1x1_b[2], lb3 = L.l1x1_b[3], lb4 = L.l1x1_b[4], lb5 = L.l1x1_b[5], lb6 = L.l1x1_b[6];
    for (int f = 0; f < num_frames; f++)
    {
      const float* src = &L.history[static_cast<size_t>(tap_base_last + f) * 7];
      const float* zf = z + static_cast<size_t>(f) * 7;
      // Final tap GEMV.
      float a0 = zf[0] + cw0 * src[0] + cw7 * src[1] + cw14 * src[2] + cw21 * src[3] + cw28 * src[4] + cw35 * src[5] + cw42 * src[6];
      float a1 = zf[1] + cw1 * src[0] + cw8 * src[1] + cw15 * src[2] + cw22 * src[3] + cw29 * src[4] + cw36 * src[5] + cw43 * src[6];
      float a2 = zf[2] + cw2 * src[0] + cw9 * src[1] + cw16 * src[2] + cw23 * src[3] + cw30 * src[4] + cw37 * src[5] + cw44 * src[6];
      float a3 = zf[3] + cw3 * src[0] + cw10 * src[1] + cw17 * src[2] + cw24 * src[3] + cw31 * src[4] + cw38 * src[5] + cw45 * src[6];
      float a4 = zf[4] + cw4 * src[0] + cw11 * src[1] + cw18 * src[2] + cw25 * src[3] + cw32 * src[4] + cw39 * src[5] + cw46 * src[6];
      float a5 = zf[5] + cw5 * src[0] + cw12 * src[1] + cw19 * src[2] + cw26 * src[3] + cw33 * src[4] + cw40 * src[5] + cw47 * src[6];
      float a6 = zf[6] + cw6 * src[0] + cw13 * src[1] + cw20 * src[2] + cw27 * src[3] + cw34 * src[4] + cw41 * src[5] + cw48 * src[6];
      // Mixin.
      const float cf = cond[f];
      a0 += mw0 * cf;
      a1 += mw1 * cf;
      a2 += mw2 * cf;
      a3 += mw3 * cf;
      a4 += mw4 * cf;
      a5 += mw5 * cf;
      a6 += mw6 * cf;
      // FiLM: gamma * (conv+mixin) + beta.
      a0 = g0 * a0 + bt0;
      a1 = g1 * a1 + bt1;
      a2 = g2 * a2 + bt2;
      a3 = g3 * a3 + bt3;
      a4 = g4 * a4 + bt4;
      a5 = g5 * a5 + bt5;
      a6 = g6 * a6 + bt6;
      // LeakyReLU.
      a0 = (a0 >= 0.0f) ? a0 : a0 * kLeakySlope;
      a1 = (a1 >= 0.0f) ? a1 : a1 * kLeakySlope;
      a2 = (a2 >= 0.0f) ? a2 : a2 * kLeakySlope;
      a3 = (a3 >= 0.0f) ? a3 : a3 * kLeakySlope;
      a4 = (a4 >= 0.0f) ? a4 : a4 * kLeakySlope;
      a5 = (a5 >= 0.0f) ? a5 : a5 * kLeakySlope;
      a6 = (a6 >= 0.0f) ? a6 : a6 * kLeakySlope;
      // Skip connections: accumulate this layer's post-activation into the head sum
      // (same site the static A2FastModel uses -- after FiLM+LeakyReLU, BEFORE the
      // l1x1 residual).
      float* NAM_RESTRICT hsum = &_head_sum[static_cast<size_t>(f) * 7];
      hsum[0] += a0;
      hsum[1] += a1;
      hsum[2] += a2;
      hsum[3] += a3;
      hsum[4] += a4;
      hsum[5] += a5;
      hsum[6] += a6;
      // layer1x1 residual.
      float* lin = &_layer_in[static_cast<size_t>(f) * 7];
      lin[0] += lb0 + lw00 * a0 + lw10 * a1 + lw20 * a2 + lw30 * a3 + lw40 * a4 + lw50 * a5 + lw60 * a6;
      lin[1] += lb1 + lw01 * a0 + lw11 * a1 + lw21 * a2 + lw31 * a3 + lw41 * a4 + lw51 * a5 + lw61 * a6;
      lin[2] += lb2 + lw02 * a0 + lw12 * a1 + lw22 * a2 + lw32 * a3 + lw42 * a4 + lw52 * a5 + lw62 * a6;
      lin[3] += lb3 + lw03 * a0 + lw13 * a1 + lw23 * a2 + lw33 * a3 + lw43 * a4 + lw53 * a5 + lw63 * a6;
      lin[4] += lb4 + lw04 * a0 + lw14 * a1 + lw24 * a2 + lw34 * a3 + lw44 * a4 + lw54 * a5 + lw64 * a6;
      lin[5] += lb5 + lw05 * a0 + lw15 * a1 + lw25 * a2 + lw35 * a3 + lw45 * a4 + lw55 * a5 + lw65 * a6;
      lin[6] += lb6 + lw06 * a0 + lw16 * a1 + lw26 * a2 + lw36 * a3 + lw46 * a4 + lw56 * a5 + lw66 * a6;
    }
  }
  // [Fork] END: hand-unrolled Channels==7 kernel

  // [Fork] Channels==8 hand-unrolled kernel (restored -- the Eigen-batched version below
  // measured faster in a synthetic standalone benchmark but did NOT reduce real in-app DSP%
  // (Instruments still showed ~34%, even after removing a heap allocation from the Eigen
  // path). Reverting Channels==8 to hand-unrolled,
  // mirroring Channels==3..7 above, so the real app can be re-measured against a path that's
  // at least architecturally consistent with the other widths pending further investigation.
  else if constexpr (Channels == 8)
  {
    // Preload per-layer FiLM coefficients (constant across all frames in this buffer).
    const float g0 = _film_gamma[li][0], g1 = _film_gamma[li][1], g2 = _film_gamma[li][2], g3 = _film_gamma[li][3], g4 = _film_gamma[li][4], g5 = _film_gamma[li][5], g6 = _film_gamma[li][6], g7 = _film_gamma[li][7];
    const float bt0 = _film_beta[li][0], bt1 = _film_beta[li][1], bt2 = _film_beta[li][2], bt3 = _film_beta[li][3], bt4 = _film_beta[li][4], bt5 = _film_beta[li][5], bt6 = _film_beta[li][6], bt7 = _film_beta[li][7];

    float* z = _z.data();

    // Tap 0: seed z with conv_b.
    {
      const float* wk = &L.conv_w[0];
      const int tap_base = tap_base_phys(K - 1);
      const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3], w4 = wk[4], w5 = wk[5], w6 = wk[6], w7 = wk[7];
      const float w8 = wk[8], w9 = wk[9], w10 = wk[10], w11 = wk[11], w12 = wk[12], w13 = wk[13], w14 = wk[14], w15 = wk[15];
      const float w16 = wk[16], w17 = wk[17], w18 = wk[18], w19 = wk[19], w20 = wk[20], w21 = wk[21], w22 = wk[22], w23 = wk[23];
      const float w24 = wk[24], w25 = wk[25], w26 = wk[26], w27 = wk[27], w28 = wk[28], w29 = wk[29], w30 = wk[30], w31 = wk[31];
      const float w32 = wk[32], w33 = wk[33], w34 = wk[34], w35 = wk[35], w36 = wk[36], w37 = wk[37], w38 = wk[38], w39 = wk[39];
      const float w40 = wk[40], w41 = wk[41], w42 = wk[42], w43 = wk[43], w44 = wk[44], w45 = wk[45], w46 = wk[46], w47 = wk[47];
      const float w48 = wk[48], w49 = wk[49], w50 = wk[50], w51 = wk[51], w52 = wk[52], w53 = wk[53], w54 = wk[54], w55 = wk[55];
      const float w56 = wk[56], w57 = wk[57], w58 = wk[58], w59 = wk[59], w60 = wk[60], w61 = wk[61], w62 = wk[62], w63 = wk[63];
      const float cb0 = L.conv_b[0], cb1 = L.conv_b[1], cb2 = L.conv_b[2], cb3 = L.conv_b[3], cb4 = L.conv_b[4], cb5 = L.conv_b[5], cb6 = L.conv_b[6], cb7 = L.conv_b[7];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 8];
        float* zf = z + static_cast<size_t>(f) * 8;
        zf[0] = cb0 + w0 * src[0] + w8 * src[1] + w16 * src[2] + w24 * src[3] + w32 * src[4] + w40 * src[5] + w48 * src[6] + w56 * src[7];
        zf[1] = cb1 + w1 * src[0] + w9 * src[1] + w17 * src[2] + w25 * src[3] + w33 * src[4] + w41 * src[5] + w49 * src[6] + w57 * src[7];
        zf[2] = cb2 + w2 * src[0] + w10 * src[1] + w18 * src[2] + w26 * src[3] + w34 * src[4] + w42 * src[5] + w50 * src[6] + w58 * src[7];
        zf[3] = cb3 + w3 * src[0] + w11 * src[1] + w19 * src[2] + w27 * src[3] + w35 * src[4] + w43 * src[5] + w51 * src[6] + w59 * src[7];
        zf[4] = cb4 + w4 * src[0] + w12 * src[1] + w20 * src[2] + w28 * src[3] + w36 * src[4] + w44 * src[5] + w52 * src[6] + w60 * src[7];
        zf[5] = cb5 + w5 * src[0] + w13 * src[1] + w21 * src[2] + w29 * src[3] + w37 * src[4] + w45 * src[5] + w53 * src[6] + w61 * src[7];
        zf[6] = cb6 + w6 * src[0] + w14 * src[1] + w22 * src[2] + w30 * src[3] + w38 * src[4] + w46 * src[5] + w54 * src[6] + w62 * src[7];
        zf[7] = cb7 + w7 * src[0] + w15 * src[1] + w23 * src[2] + w31 * src[3] + w39 * src[4] + w47 * src[5] + w55 * src[6] + w63 * src[7];
      }
    }

    // Taps 1..K-2: accumulate.
    for (int k = 1; k < K - 1; k++)
    {
      const float* wk = &L.conv_w[static_cast<size_t>(k) * 64];
      const int tap_base = tap_base_phys(K - 1 - k);
      const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3], w4 = wk[4], w5 = wk[5], w6 = wk[6], w7 = wk[7];
      const float w8 = wk[8], w9 = wk[9], w10 = wk[10], w11 = wk[11], w12 = wk[12], w13 = wk[13], w14 = wk[14], w15 = wk[15];
      const float w16 = wk[16], w17 = wk[17], w18 = wk[18], w19 = wk[19], w20 = wk[20], w21 = wk[21], w22 = wk[22], w23 = wk[23];
      const float w24 = wk[24], w25 = wk[25], w26 = wk[26], w27 = wk[27], w28 = wk[28], w29 = wk[29], w30 = wk[30], w31 = wk[31];
      const float w32 = wk[32], w33 = wk[33], w34 = wk[34], w35 = wk[35], w36 = wk[36], w37 = wk[37], w38 = wk[38], w39 = wk[39];
      const float w40 = wk[40], w41 = wk[41], w42 = wk[42], w43 = wk[43], w44 = wk[44], w45 = wk[45], w46 = wk[46], w47 = wk[47];
      const float w48 = wk[48], w49 = wk[49], w50 = wk[50], w51 = wk[51], w52 = wk[52], w53 = wk[53], w54 = wk[54], w55 = wk[55];
      const float w56 = wk[56], w57 = wk[57], w58 = wk[58], w59 = wk[59], w60 = wk[60], w61 = wk[61], w62 = wk[62], w63 = wk[63];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 8];
        float* zf = z + static_cast<size_t>(f) * 8;
        zf[0] += w0 * src[0] + w8 * src[1] + w16 * src[2] + w24 * src[3] + w32 * src[4] + w40 * src[5] + w48 * src[6] + w56 * src[7];
        zf[1] += w1 * src[0] + w9 * src[1] + w17 * src[2] + w25 * src[3] + w33 * src[4] + w41 * src[5] + w49 * src[6] + w57 * src[7];
        zf[2] += w2 * src[0] + w10 * src[1] + w18 * src[2] + w26 * src[3] + w34 * src[4] + w42 * src[5] + w50 * src[6] + w58 * src[7];
        zf[3] += w3 * src[0] + w11 * src[1] + w19 * src[2] + w27 * src[3] + w35 * src[4] + w43 * src[5] + w51 * src[6] + w59 * src[7];
        zf[4] += w4 * src[0] + w12 * src[1] + w20 * src[2] + w28 * src[3] + w36 * src[4] + w44 * src[5] + w52 * src[6] + w60 * src[7];
        zf[5] += w5 * src[0] + w13 * src[1] + w21 * src[2] + w29 * src[3] + w37 * src[4] + w45 * src[5] + w53 * src[6] + w61 * src[7];
        zf[6] += w6 * src[0] + w14 * src[1] + w22 * src[2] + w30 * src[3] + w38 * src[4] + w46 * src[5] + w54 * src[6] + w62 * src[7];
        zf[7] += w7 * src[0] + w15 * src[1] + w23 * src[2] + w31 * src[3] + w39 * src[4] + w47 * src[5] + w55 * src[6] + w63 * src[7];
      }
    }

    // Final tap + mixin + FiLM + LeakyReLU + [head_sum accumulate] + l1x1 residual.
    const float* wk_last = &L.conv_w[static_cast<size_t>(K - 1) * 64];
    const int tap_base_last = tap_base_phys(0);
    const float cw0 = wk_last[0], cw1 = wk_last[1], cw2 = wk_last[2], cw3 = wk_last[3], cw4 = wk_last[4], cw5 = wk_last[5], cw6 = wk_last[6], cw7 = wk_last[7];
    const float cw8 = wk_last[8], cw9 = wk_last[9], cw10 = wk_last[10], cw11 = wk_last[11], cw12 = wk_last[12], cw13 = wk_last[13], cw14 = wk_last[14], cw15 = wk_last[15];
    const float cw16 = wk_last[16], cw17 = wk_last[17], cw18 = wk_last[18], cw19 = wk_last[19], cw20 = wk_last[20], cw21 = wk_last[21], cw22 = wk_last[22], cw23 = wk_last[23];
    const float cw24 = wk_last[24], cw25 = wk_last[25], cw26 = wk_last[26], cw27 = wk_last[27], cw28 = wk_last[28], cw29 = wk_last[29], cw30 = wk_last[30], cw31 = wk_last[31];
    const float cw32 = wk_last[32], cw33 = wk_last[33], cw34 = wk_last[34], cw35 = wk_last[35], cw36 = wk_last[36], cw37 = wk_last[37], cw38 = wk_last[38], cw39 = wk_last[39];
    const float cw40 = wk_last[40], cw41 = wk_last[41], cw42 = wk_last[42], cw43 = wk_last[43], cw44 = wk_last[44], cw45 = wk_last[45], cw46 = wk_last[46], cw47 = wk_last[47];
    const float cw48 = wk_last[48], cw49 = wk_last[49], cw50 = wk_last[50], cw51 = wk_last[51], cw52 = wk_last[52], cw53 = wk_last[53], cw54 = wk_last[54], cw55 = wk_last[55];
    const float cw56 = wk_last[56], cw57 = wk_last[57], cw58 = wk_last[58], cw59 = wk_last[59], cw60 = wk_last[60], cw61 = wk_last[61], cw62 = wk_last[62], cw63 = wk_last[63];
    const float mw0 = L.mixin_w[0], mw1 = L.mixin_w[1], mw2 = L.mixin_w[2], mw3 = L.mixin_w[3], mw4 = L.mixin_w[4], mw5 = L.mixin_w[5], mw6 = L.mixin_w[6], mw7 = L.mixin_w[7];
    const float* NAM_RESTRICT l1x1_src = L.l1x1_w.data();
    const float lw00 = l1x1_src[0], lw01 = l1x1_src[1], lw02 = l1x1_src[2], lw03 = l1x1_src[3], lw04 = l1x1_src[4], lw05 = l1x1_src[5], lw06 = l1x1_src[6], lw07 = l1x1_src[7];
    const float lw10 = l1x1_src[8], lw11 = l1x1_src[9], lw12 = l1x1_src[10], lw13 = l1x1_src[11], lw14 = l1x1_src[12], lw15 = l1x1_src[13], lw16 = l1x1_src[14], lw17 = l1x1_src[15];
    const float lw20 = l1x1_src[16], lw21 = l1x1_src[17], lw22 = l1x1_src[18], lw23 = l1x1_src[19], lw24 = l1x1_src[20], lw25 = l1x1_src[21], lw26 = l1x1_src[22], lw27 = l1x1_src[23];
    const float lw30 = l1x1_src[24], lw31 = l1x1_src[25], lw32 = l1x1_src[26], lw33 = l1x1_src[27], lw34 = l1x1_src[28], lw35 = l1x1_src[29], lw36 = l1x1_src[30], lw37 = l1x1_src[31];
    const float lw40 = l1x1_src[32], lw41 = l1x1_src[33], lw42 = l1x1_src[34], lw43 = l1x1_src[35], lw44 = l1x1_src[36], lw45 = l1x1_src[37], lw46 = l1x1_src[38], lw47 = l1x1_src[39];
    const float lw50 = l1x1_src[40], lw51 = l1x1_src[41], lw52 = l1x1_src[42], lw53 = l1x1_src[43], lw54 = l1x1_src[44], lw55 = l1x1_src[45], lw56 = l1x1_src[46], lw57 = l1x1_src[47];
    const float lw60 = l1x1_src[48], lw61 = l1x1_src[49], lw62 = l1x1_src[50], lw63 = l1x1_src[51], lw64 = l1x1_src[52], lw65 = l1x1_src[53], lw66 = l1x1_src[54], lw67 = l1x1_src[55];
    const float lw70 = l1x1_src[56], lw71 = l1x1_src[57], lw72 = l1x1_src[58], lw73 = l1x1_src[59], lw74 = l1x1_src[60], lw75 = l1x1_src[61], lw76 = l1x1_src[62], lw77 = l1x1_src[63];
    const float lb0 = L.l1x1_b[0], lb1 = L.l1x1_b[1], lb2 = L.l1x1_b[2], lb3 = L.l1x1_b[3], lb4 = L.l1x1_b[4], lb5 = L.l1x1_b[5], lb6 = L.l1x1_b[6], lb7 = L.l1x1_b[7];
    for (int f = 0; f < num_frames; f++)
    {
      const float* src = &L.history[static_cast<size_t>(tap_base_last + f) * 8];
      const float* zf = z + static_cast<size_t>(f) * 8;
      // Final tap GEMV.
      float a0 = zf[0] + cw0 * src[0] + cw8 * src[1] + cw16 * src[2] + cw24 * src[3] + cw32 * src[4] + cw40 * src[5] + cw48 * src[6] + cw56 * src[7];
      float a1 = zf[1] + cw1 * src[0] + cw9 * src[1] + cw17 * src[2] + cw25 * src[3] + cw33 * src[4] + cw41 * src[5] + cw49 * src[6] + cw57 * src[7];
      float a2 = zf[2] + cw2 * src[0] + cw10 * src[1] + cw18 * src[2] + cw26 * src[3] + cw34 * src[4] + cw42 * src[5] + cw50 * src[6] + cw58 * src[7];
      float a3 = zf[3] + cw3 * src[0] + cw11 * src[1] + cw19 * src[2] + cw27 * src[3] + cw35 * src[4] + cw43 * src[5] + cw51 * src[6] + cw59 * src[7];
      float a4 = zf[4] + cw4 * src[0] + cw12 * src[1] + cw20 * src[2] + cw28 * src[3] + cw36 * src[4] + cw44 * src[5] + cw52 * src[6] + cw60 * src[7];
      float a5 = zf[5] + cw5 * src[0] + cw13 * src[1] + cw21 * src[2] + cw29 * src[3] + cw37 * src[4] + cw45 * src[5] + cw53 * src[6] + cw61 * src[7];
      float a6 = zf[6] + cw6 * src[0] + cw14 * src[1] + cw22 * src[2] + cw30 * src[3] + cw38 * src[4] + cw46 * src[5] + cw54 * src[6] + cw62 * src[7];
      float a7 = zf[7] + cw7 * src[0] + cw15 * src[1] + cw23 * src[2] + cw31 * src[3] + cw39 * src[4] + cw47 * src[5] + cw55 * src[6] + cw63 * src[7];
      // Mixin.
      const float cf = cond[f];
      a0 += mw0 * cf;
      a1 += mw1 * cf;
      a2 += mw2 * cf;
      a3 += mw3 * cf;
      a4 += mw4 * cf;
      a5 += mw5 * cf;
      a6 += mw6 * cf;
      a7 += mw7 * cf;
      // FiLM: gamma * (conv+mixin) + beta.
      a0 = g0 * a0 + bt0;
      a1 = g1 * a1 + bt1;
      a2 = g2 * a2 + bt2;
      a3 = g3 * a3 + bt3;
      a4 = g4 * a4 + bt4;
      a5 = g5 * a5 + bt5;
      a6 = g6 * a6 + bt6;
      a7 = g7 * a7 + bt7;
      // LeakyReLU.
      a0 = (a0 >= 0.0f) ? a0 : a0 * kLeakySlope;
      a1 = (a1 >= 0.0f) ? a1 : a1 * kLeakySlope;
      a2 = (a2 >= 0.0f) ? a2 : a2 * kLeakySlope;
      a3 = (a3 >= 0.0f) ? a3 : a3 * kLeakySlope;
      a4 = (a4 >= 0.0f) ? a4 : a4 * kLeakySlope;
      a5 = (a5 >= 0.0f) ? a5 : a5 * kLeakySlope;
      a6 = (a6 >= 0.0f) ? a6 : a6 * kLeakySlope;
      a7 = (a7 >= 0.0f) ? a7 : a7 * kLeakySlope;
      // Skip connections: accumulate this layer's post-activation into the head sum
      // (same site the static A2FastModel uses -- after FiLM+LeakyReLU, BEFORE the
      // l1x1 residual).
      float* NAM_RESTRICT hsum = &_head_sum[static_cast<size_t>(f) * 8];
      hsum[0] += a0;
      hsum[1] += a1;
      hsum[2] += a2;
      hsum[3] += a3;
      hsum[4] += a4;
      hsum[5] += a5;
      hsum[6] += a6;
      hsum[7] += a7;
      // layer1x1 residual.
      float* lin = &_layer_in[static_cast<size_t>(f) * 8];
      lin[0] += lb0 + lw00 * a0 + lw10 * a1 + lw20 * a2 + lw30 * a3 + lw40 * a4 + lw50 * a5 + lw60 * a6 + lw70 * a7;
      lin[1] += lb1 + lw01 * a0 + lw11 * a1 + lw21 * a2 + lw31 * a3 + lw41 * a4 + lw51 * a5 + lw61 * a6 + lw71 * a7;
      lin[2] += lb2 + lw02 * a0 + lw12 * a1 + lw22 * a2 + lw32 * a3 + lw42 * a4 + lw52 * a5 + lw62 * a6 + lw72 * a7;
      lin[3] += lb3 + lw03 * a0 + lw13 * a1 + lw23 * a2 + lw33 * a3 + lw43 * a4 + lw53 * a5 + lw63 * a6 + lw73 * a7;
      lin[4] += lb4 + lw04 * a0 + lw14 * a1 + lw24 * a2 + lw34 * a3 + lw44 * a4 + lw54 * a5 + lw64 * a6 + lw74 * a7;
      lin[5] += lb5 + lw05 * a0 + lw15 * a1 + lw25 * a2 + lw35 * a3 + lw45 * a4 + lw55 * a5 + lw65 * a6 + lw75 * a7;
      lin[6] += lb6 + lw06 * a0 + lw16 * a1 + lw26 * a2 + lw36 * a3 + lw46 * a4 + lw56 * a5 + lw66 * a6 + lw76 * a7;
      lin[7] += lb7 + lw07 * a0 + lw17 * a1 + lw27 * a2 + lw37 * a3 + lw47 * a4 + lw57 * a5 + lw67 * a6 + lw77 * a7;
    }
  }
  // [Fork] END: hand-unrolled Channels==8 kernel

  else if constexpr (Channels == 9)
  {
    // Preload per-layer FiLM coefficients (constant across all frames in this buffer).
    const float g0 = _film_gamma[li][0], g1 = _film_gamma[li][1], g2 = _film_gamma[li][2], g3 = _film_gamma[li][3], g4 = _film_gamma[li][4], g5 = _film_gamma[li][5], g6 = _film_gamma[li][6], g7 = _film_gamma[li][7], g8 = _film_gamma[li][8];
    const float bt0 = _film_beta[li][0], bt1 = _film_beta[li][1], bt2 = _film_beta[li][2], bt3 = _film_beta[li][3], bt4 = _film_beta[li][4], bt5 = _film_beta[li][5], bt6 = _film_beta[li][6], bt7 = _film_beta[li][7], bt8 = _film_beta[li][8];

    float* z = _z.data();

    // Tap 0: seed z with conv_b.
    {
      const float* wk = &L.conv_w[0];
      const int tap_base = tap_base_phys(K - 1);
      const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3], w4 = wk[4], w5 = wk[5], w6 = wk[6], w7 = wk[7], w8 = wk[8];
      const float w9 = wk[9], w10 = wk[10], w11 = wk[11], w12 = wk[12], w13 = wk[13], w14 = wk[14], w15 = wk[15], w16 = wk[16], w17 = wk[17];
      const float w18 = wk[18], w19 = wk[19], w20 = wk[20], w21 = wk[21], w22 = wk[22], w23 = wk[23], w24 = wk[24], w25 = wk[25], w26 = wk[26];
      const float w27 = wk[27], w28 = wk[28], w29 = wk[29], w30 = wk[30], w31 = wk[31], w32 = wk[32], w33 = wk[33], w34 = wk[34], w35 = wk[35];
      const float w36 = wk[36], w37 = wk[37], w38 = wk[38], w39 = wk[39], w40 = wk[40], w41 = wk[41], w42 = wk[42], w43 = wk[43], w44 = wk[44];
      const float w45 = wk[45], w46 = wk[46], w47 = wk[47], w48 = wk[48], w49 = wk[49], w50 = wk[50], w51 = wk[51], w52 = wk[52], w53 = wk[53];
      const float w54 = wk[54], w55 = wk[55], w56 = wk[56], w57 = wk[57], w58 = wk[58], w59 = wk[59], w60 = wk[60], w61 = wk[61], w62 = wk[62];
      const float w63 = wk[63], w64 = wk[64], w65 = wk[65], w66 = wk[66], w67 = wk[67], w68 = wk[68], w69 = wk[69], w70 = wk[70], w71 = wk[71];
      const float w72 = wk[72], w73 = wk[73], w74 = wk[74], w75 = wk[75], w76 = wk[76], w77 = wk[77], w78 = wk[78], w79 = wk[79], w80 = wk[80];
      const float cb0 = L.conv_b[0], cb1 = L.conv_b[1], cb2 = L.conv_b[2], cb3 = L.conv_b[3], cb4 = L.conv_b[4], cb5 = L.conv_b[5], cb6 = L.conv_b[6], cb7 = L.conv_b[7], cb8 = L.conv_b[8];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 9];
        float* zf = z + static_cast<size_t>(f) * 9;
        zf[0] = cb0 + w0 * src[0] + w9 * src[1] + w18 * src[2] + w27 * src[3] + w36 * src[4] + w45 * src[5] + w54 * src[6] + w63 * src[7] + w72 * src[8];
        zf[1] = cb1 + w1 * src[0] + w10 * src[1] + w19 * src[2] + w28 * src[3] + w37 * src[4] + w46 * src[5] + w55 * src[6] + w64 * src[7] + w73 * src[8];
        zf[2] = cb2 + w2 * src[0] + w11 * src[1] + w20 * src[2] + w29 * src[3] + w38 * src[4] + w47 * src[5] + w56 * src[6] + w65 * src[7] + w74 * src[8];
        zf[3] = cb3 + w3 * src[0] + w12 * src[1] + w21 * src[2] + w30 * src[3] + w39 * src[4] + w48 * src[5] + w57 * src[6] + w66 * src[7] + w75 * src[8];
        zf[4] = cb4 + w4 * src[0] + w13 * src[1] + w22 * src[2] + w31 * src[3] + w40 * src[4] + w49 * src[5] + w58 * src[6] + w67 * src[7] + w76 * src[8];
        zf[5] = cb5 + w5 * src[0] + w14 * src[1] + w23 * src[2] + w32 * src[3] + w41 * src[4] + w50 * src[5] + w59 * src[6] + w68 * src[7] + w77 * src[8];
        zf[6] = cb6 + w6 * src[0] + w15 * src[1] + w24 * src[2] + w33 * src[3] + w42 * src[4] + w51 * src[5] + w60 * src[6] + w69 * src[7] + w78 * src[8];
        zf[7] = cb7 + w7 * src[0] + w16 * src[1] + w25 * src[2] + w34 * src[3] + w43 * src[4] + w52 * src[5] + w61 * src[6] + w70 * src[7] + w79 * src[8];
        zf[8] = cb8 + w8 * src[0] + w17 * src[1] + w26 * src[2] + w35 * src[3] + w44 * src[4] + w53 * src[5] + w62 * src[6] + w71 * src[7] + w80 * src[8];
      }
    }

    // Taps 1..K-2: accumulate.
    for (int k = 1; k < K - 1; k++)
    {
      const float* wk = &L.conv_w[static_cast<size_t>(k) * 81];
      const int tap_base = tap_base_phys(K - 1 - k);
      const float w0 = wk[0], w1 = wk[1], w2 = wk[2], w3 = wk[3], w4 = wk[4], w5 = wk[5], w6 = wk[6], w7 = wk[7], w8 = wk[8];
      const float w9 = wk[9], w10 = wk[10], w11 = wk[11], w12 = wk[12], w13 = wk[13], w14 = wk[14], w15 = wk[15], w16 = wk[16], w17 = wk[17];
      const float w18 = wk[18], w19 = wk[19], w20 = wk[20], w21 = wk[21], w22 = wk[22], w23 = wk[23], w24 = wk[24], w25 = wk[25], w26 = wk[26];
      const float w27 = wk[27], w28 = wk[28], w29 = wk[29], w30 = wk[30], w31 = wk[31], w32 = wk[32], w33 = wk[33], w34 = wk[34], w35 = wk[35];
      const float w36 = wk[36], w37 = wk[37], w38 = wk[38], w39 = wk[39], w40 = wk[40], w41 = wk[41], w42 = wk[42], w43 = wk[43], w44 = wk[44];
      const float w45 = wk[45], w46 = wk[46], w47 = wk[47], w48 = wk[48], w49 = wk[49], w50 = wk[50], w51 = wk[51], w52 = wk[52], w53 = wk[53];
      const float w54 = wk[54], w55 = wk[55], w56 = wk[56], w57 = wk[57], w58 = wk[58], w59 = wk[59], w60 = wk[60], w61 = wk[61], w62 = wk[62];
      const float w63 = wk[63], w64 = wk[64], w65 = wk[65], w66 = wk[66], w67 = wk[67], w68 = wk[68], w69 = wk[69], w70 = wk[70], w71 = wk[71];
      const float w72 = wk[72], w73 = wk[73], w74 = wk[74], w75 = wk[75], w76 = wk[76], w77 = wk[77], w78 = wk[78], w79 = wk[79], w80 = wk[80];
      for (int f = 0; f < num_frames; f++)
      {
        const float* src = &L.history[static_cast<size_t>(tap_base + f) * 9];
        float* zf = z + static_cast<size_t>(f) * 9;
        zf[0] += w0 * src[0] + w9 * src[1] + w18 * src[2] + w27 * src[3] + w36 * src[4] + w45 * src[5] + w54 * src[6] + w63 * src[7] + w72 * src[8];
        zf[1] += w1 * src[0] + w10 * src[1] + w19 * src[2] + w28 * src[3] + w37 * src[4] + w46 * src[5] + w55 * src[6] + w64 * src[7] + w73 * src[8];
        zf[2] += w2 * src[0] + w11 * src[1] + w20 * src[2] + w29 * src[3] + w38 * src[4] + w47 * src[5] + w56 * src[6] + w65 * src[7] + w74 * src[8];
        zf[3] += w3 * src[0] + w12 * src[1] + w21 * src[2] + w30 * src[3] + w39 * src[4] + w48 * src[5] + w57 * src[6] + w66 * src[7] + w75 * src[8];
        zf[4] += w4 * src[0] + w13 * src[1] + w22 * src[2] + w31 * src[3] + w40 * src[4] + w49 * src[5] + w58 * src[6] + w67 * src[7] + w76 * src[8];
        zf[5] += w5 * src[0] + w14 * src[1] + w23 * src[2] + w32 * src[3] + w41 * src[4] + w50 * src[5] + w59 * src[6] + w68 * src[7] + w77 * src[8];
        zf[6] += w6 * src[0] + w15 * src[1] + w24 * src[2] + w33 * src[3] + w42 * src[4] + w51 * src[5] + w60 * src[6] + w69 * src[7] + w78 * src[8];
        zf[7] += w7 * src[0] + w16 * src[1] + w25 * src[2] + w34 * src[3] + w43 * src[4] + w52 * src[5] + w61 * src[6] + w70 * src[7] + w79 * src[8];
        zf[8] += w8 * src[0] + w17 * src[1] + w26 * src[2] + w35 * src[3] + w44 * src[4] + w53 * src[5] + w62 * src[6] + w71 * src[7] + w80 * src[8];
      }
    }

    // Final tap + mixin + FiLM + LeakyReLU + [head_sum accumulate] + l1x1 residual.
    const float* wk_last = &L.conv_w[static_cast<size_t>(K - 1) * 81];
    const int tap_base_last = tap_base_phys(0);
    const float cw0 = wk_last[0], cw1 = wk_last[1], cw2 = wk_last[2], cw3 = wk_last[3], cw4 = wk_last[4], cw5 = wk_last[5], cw6 = wk_last[6], cw7 = wk_last[7], cw8 = wk_last[8];
    const float cw9 = wk_last[9], cw10 = wk_last[10], cw11 = wk_last[11], cw12 = wk_last[12], cw13 = wk_last[13], cw14 = wk_last[14], cw15 = wk_last[15], cw16 = wk_last[16], cw17 = wk_last[17];
    const float cw18 = wk_last[18], cw19 = wk_last[19], cw20 = wk_last[20], cw21 = wk_last[21], cw22 = wk_last[22], cw23 = wk_last[23], cw24 = wk_last[24], cw25 = wk_last[25], cw26 = wk_last[26];
    const float cw27 = wk_last[27], cw28 = wk_last[28], cw29 = wk_last[29], cw30 = wk_last[30], cw31 = wk_last[31], cw32 = wk_last[32], cw33 = wk_last[33], cw34 = wk_last[34], cw35 = wk_last[35];
    const float cw36 = wk_last[36], cw37 = wk_last[37], cw38 = wk_last[38], cw39 = wk_last[39], cw40 = wk_last[40], cw41 = wk_last[41], cw42 = wk_last[42], cw43 = wk_last[43], cw44 = wk_last[44];
    const float cw45 = wk_last[45], cw46 = wk_last[46], cw47 = wk_last[47], cw48 = wk_last[48], cw49 = wk_last[49], cw50 = wk_last[50], cw51 = wk_last[51], cw52 = wk_last[52], cw53 = wk_last[53];
    const float cw54 = wk_last[54], cw55 = wk_last[55], cw56 = wk_last[56], cw57 = wk_last[57], cw58 = wk_last[58], cw59 = wk_last[59], cw60 = wk_last[60], cw61 = wk_last[61], cw62 = wk_last[62];
    const float cw63 = wk_last[63], cw64 = wk_last[64], cw65 = wk_last[65], cw66 = wk_last[66], cw67 = wk_last[67], cw68 = wk_last[68], cw69 = wk_last[69], cw70 = wk_last[70], cw71 = wk_last[71];
    const float cw72 = wk_last[72], cw73 = wk_last[73], cw74 = wk_last[74], cw75 = wk_last[75], cw76 = wk_last[76], cw77 = wk_last[77], cw78 = wk_last[78], cw79 = wk_last[79], cw80 = wk_last[80];
    const float mw0 = L.mixin_w[0], mw1 = L.mixin_w[1], mw2 = L.mixin_w[2], mw3 = L.mixin_w[3], mw4 = L.mixin_w[4], mw5 = L.mixin_w[5], mw6 = L.mixin_w[6], mw7 = L.mixin_w[7], mw8 = L.mixin_w[8];
    const float* NAM_RESTRICT l1x1_src = L.l1x1_w.data();
    const float lw00 = l1x1_src[0], lw01 = l1x1_src[1], lw02 = l1x1_src[2], lw03 = l1x1_src[3], lw04 = l1x1_src[4], lw05 = l1x1_src[5], lw06 = l1x1_src[6], lw07 = l1x1_src[7], lw08 = l1x1_src[8];
    const float lw10 = l1x1_src[9], lw11 = l1x1_src[10], lw12 = l1x1_src[11], lw13 = l1x1_src[12], lw14 = l1x1_src[13], lw15 = l1x1_src[14], lw16 = l1x1_src[15], lw17 = l1x1_src[16], lw18 = l1x1_src[17];
    const float lw20 = l1x1_src[18], lw21 = l1x1_src[19], lw22 = l1x1_src[20], lw23 = l1x1_src[21], lw24 = l1x1_src[22], lw25 = l1x1_src[23], lw26 = l1x1_src[24], lw27 = l1x1_src[25], lw28 = l1x1_src[26];
    const float lw30 = l1x1_src[27], lw31 = l1x1_src[28], lw32 = l1x1_src[29], lw33 = l1x1_src[30], lw34 = l1x1_src[31], lw35 = l1x1_src[32], lw36 = l1x1_src[33], lw37 = l1x1_src[34], lw38 = l1x1_src[35];
    const float lw40 = l1x1_src[36], lw41 = l1x1_src[37], lw42 = l1x1_src[38], lw43 = l1x1_src[39], lw44 = l1x1_src[40], lw45 = l1x1_src[41], lw46 = l1x1_src[42], lw47 = l1x1_src[43], lw48 = l1x1_src[44];
    const float lw50 = l1x1_src[45], lw51 = l1x1_src[46], lw52 = l1x1_src[47], lw53 = l1x1_src[48], lw54 = l1x1_src[49], lw55 = l1x1_src[50], lw56 = l1x1_src[51], lw57 = l1x1_src[52], lw58 = l1x1_src[53];
    const float lw60 = l1x1_src[54], lw61 = l1x1_src[55], lw62 = l1x1_src[56], lw63 = l1x1_src[57], lw64 = l1x1_src[58], lw65 = l1x1_src[59], lw66 = l1x1_src[60], lw67 = l1x1_src[61], lw68 = l1x1_src[62];
    const float lw70 = l1x1_src[63], lw71 = l1x1_src[64], lw72 = l1x1_src[65], lw73 = l1x1_src[66], lw74 = l1x1_src[67], lw75 = l1x1_src[68], lw76 = l1x1_src[69], lw77 = l1x1_src[70], lw78 = l1x1_src[71];
    const float lw80 = l1x1_src[72], lw81 = l1x1_src[73], lw82 = l1x1_src[74], lw83 = l1x1_src[75], lw84 = l1x1_src[76], lw85 = l1x1_src[77], lw86 = l1x1_src[78], lw87 = l1x1_src[79], lw88 = l1x1_src[80];
    const float lb0 = L.l1x1_b[0], lb1 = L.l1x1_b[1], lb2 = L.l1x1_b[2], lb3 = L.l1x1_b[3], lb4 = L.l1x1_b[4], lb5 = L.l1x1_b[5], lb6 = L.l1x1_b[6], lb7 = L.l1x1_b[7], lb8 = L.l1x1_b[8];
    for (int f = 0; f < num_frames; f++)
    {
      const float* src = &L.history[static_cast<size_t>(tap_base_last + f) * 9];
      const float* zf = z + static_cast<size_t>(f) * 9;
      // Final tap GEMV.
      float a0 = zf[0] + cw0 * src[0] + cw9 * src[1] + cw18 * src[2] + cw27 * src[3] + cw36 * src[4] + cw45 * src[5] + cw54 * src[6] + cw63 * src[7] + cw72 * src[8];
      float a1 = zf[1] + cw1 * src[0] + cw10 * src[1] + cw19 * src[2] + cw28 * src[3] + cw37 * src[4] + cw46 * src[5] + cw55 * src[6] + cw64 * src[7] + cw73 * src[8];
      float a2 = zf[2] + cw2 * src[0] + cw11 * src[1] + cw20 * src[2] + cw29 * src[3] + cw38 * src[4] + cw47 * src[5] + cw56 * src[6] + cw65 * src[7] + cw74 * src[8];
      float a3 = zf[3] + cw3 * src[0] + cw12 * src[1] + cw21 * src[2] + cw30 * src[3] + cw39 * src[4] + cw48 * src[5] + cw57 * src[6] + cw66 * src[7] + cw75 * src[8];
      float a4 = zf[4] + cw4 * src[0] + cw13 * src[1] + cw22 * src[2] + cw31 * src[3] + cw40 * src[4] + cw49 * src[5] + cw58 * src[6] + cw67 * src[7] + cw76 * src[8];
      float a5 = zf[5] + cw5 * src[0] + cw14 * src[1] + cw23 * src[2] + cw32 * src[3] + cw41 * src[4] + cw50 * src[5] + cw59 * src[6] + cw68 * src[7] + cw77 * src[8];
      float a6 = zf[6] + cw6 * src[0] + cw15 * src[1] + cw24 * src[2] + cw33 * src[3] + cw42 * src[4] + cw51 * src[5] + cw60 * src[6] + cw69 * src[7] + cw78 * src[8];
      float a7 = zf[7] + cw7 * src[0] + cw16 * src[1] + cw25 * src[2] + cw34 * src[3] + cw43 * src[4] + cw52 * src[5] + cw61 * src[6] + cw70 * src[7] + cw79 * src[8];
      float a8 = zf[8] + cw8 * src[0] + cw17 * src[1] + cw26 * src[2] + cw35 * src[3] + cw44 * src[4] + cw53 * src[5] + cw62 * src[6] + cw71 * src[7] + cw80 * src[8];
      // Mixin.
      const float cf = cond[f];
      a0 += mw0 * cf;
      a1 += mw1 * cf;
      a2 += mw2 * cf;
      a3 += mw3 * cf;
      a4 += mw4 * cf;
      a5 += mw5 * cf;
      a6 += mw6 * cf;
      a7 += mw7 * cf;
      a8 += mw8 * cf;
      // FiLM: gamma * (conv+mixin) + beta.
      a0 = g0 * a0 + bt0;
      a1 = g1 * a1 + bt1;
      a2 = g2 * a2 + bt2;
      a3 = g3 * a3 + bt3;
      a4 = g4 * a4 + bt4;
      a5 = g5 * a5 + bt5;
      a6 = g6 * a6 + bt6;
      a7 = g7 * a7 + bt7;
      a8 = g8 * a8 + bt8;
      // LeakyReLU.
      a0 = (a0 >= 0.0f) ? a0 : a0 * kLeakySlope;
      a1 = (a1 >= 0.0f) ? a1 : a1 * kLeakySlope;
      a2 = (a2 >= 0.0f) ? a2 : a2 * kLeakySlope;
      a3 = (a3 >= 0.0f) ? a3 : a3 * kLeakySlope;
      a4 = (a4 >= 0.0f) ? a4 : a4 * kLeakySlope;
      a5 = (a5 >= 0.0f) ? a5 : a5 * kLeakySlope;
      a6 = (a6 >= 0.0f) ? a6 : a6 * kLeakySlope;
      a7 = (a7 >= 0.0f) ? a7 : a7 * kLeakySlope;
      a8 = (a8 >= 0.0f) ? a8 : a8 * kLeakySlope;
      // Skip connections: accumulate this layer's post-activation into the head sum
      // (same site the static A2FastModel uses -- after FiLM+LeakyReLU, BEFORE the
      // l1x1 residual).
      float* NAM_RESTRICT hsum = &_head_sum[static_cast<size_t>(f) * 9];
      hsum[0] += a0;
      hsum[1] += a1;
      hsum[2] += a2;
      hsum[3] += a3;
      hsum[4] += a4;
      hsum[5] += a5;
      hsum[6] += a6;
      hsum[7] += a7;
      hsum[8] += a8;
      // layer1x1 residual.
      float* lin = &_layer_in[static_cast<size_t>(f) * 9];
      lin[0] += lb0 + lw00 * a0 + lw10 * a1 + lw20 * a2 + lw30 * a3 + lw40 * a4 + lw50 * a5 + lw60 * a6 + lw70 * a7 + lw80 * a8;
      lin[1] += lb1 + lw01 * a0 + lw11 * a1 + lw21 * a2 + lw31 * a3 + lw41 * a4 + lw51 * a5 + lw61 * a6 + lw71 * a7 + lw81 * a8;
      lin[2] += lb2 + lw02 * a0 + lw12 * a1 + lw22 * a2 + lw32 * a3 + lw42 * a4 + lw52 * a5 + lw62 * a6 + lw72 * a7 + lw82 * a8;
      lin[3] += lb3 + lw03 * a0 + lw13 * a1 + lw23 * a2 + lw33 * a3 + lw43 * a4 + lw53 * a5 + lw63 * a6 + lw73 * a7 + lw83 * a8;
      lin[4] += lb4 + lw04 * a0 + lw14 * a1 + lw24 * a2 + lw34 * a3 + lw44 * a4 + lw54 * a5 + lw64 * a6 + lw74 * a7 + lw84 * a8;
      lin[5] += lb5 + lw05 * a0 + lw15 * a1 + lw25 * a2 + lw35 * a3 + lw45 * a4 + lw55 * a5 + lw65 * a6 + lw75 * a7 + lw85 * a8;
      lin[6] += lb6 + lw06 * a0 + lw16 * a1 + lw26 * a2 + lw36 * a3 + lw46 * a4 + lw56 * a5 + lw66 * a6 + lw76 * a7 + lw86 * a8;
      lin[7] += lb7 + lw07 * a0 + lw17 * a1 + lw27 * a2 + lw37 * a3 + lw47 * a4 + lw57 * a5 + lw67 * a6 + lw77 * a7 + lw87 * a8;
      lin[8] += lb8 + lw08 * a0 + lw18 * a1 + lw28 * a2 + lw38 * a3 + lw48 * a4 + lw58 * a5 + lw68 * a6 + lw78 * a7 + lw88 * a8;
    }
  }
  // [Fork] END: hand-unrolled Channels==9 kernel

  // [Fork] Fallback for any width outside 3-9 -- never reached in practice, since
  // is_parametric_a2_shape only accepts 3-9 and all of those now have hand-unrolled
  // branches above. Kept as a dead-but-correct safety net (and as a reference/rollback
  // point): this batches everything -- conv, FiLM, LeakyReLU, skip-sum, l1x1 -- as
  // GEMM/elementwise ops over the whole (Channels x num_frames) tile at once, matching how
  // NAM/film.h processes the generic path. A synthetic standalone benchmark
  // measured this ~1.15-1.3x faster than generic for the shipped 8-channel model
  // (vs. ~0.6x, i.e. slower, for the hand-unrolled version) -- but real in-app Instruments
  // measurement showed the opposite (34% DSP, unchanged even after removing a heap
  // allocation from this path). Channels==8 was
  // reverted to hand-unrolled above pending that discrepancy being understood. Channels==9
  // (added later, matching the same hand-unrolled + _build_effective_l1x1 pattern) never
  // had the batched-fallback path tried against it at all.
  else
  {
    using MatCC  = Eigen::Matrix<float, Channels, Channels>;
    using MatCDyn= Eigen::Matrix<float, Channels, Eigen::Dynamic>;
    using VecC   = Eigen::Matrix<float, Channels, 1>;
    using RowDyn = Eigen::Matrix<float, 1, Eigen::Dynamic>;

    Eigen::Map<const VecC>   conv_b_vec(L.conv_b.data());
    Eigen::Map<const VecC>   mixin_vec(L.mixin_w.data());
    Eigen::Map<const MatCC>  l1x1_mat(L.l1x1_w.data());
    Eigen::Map<const VecC>   l1x1_b_vec(L.l1x1_b.data());
    Eigen::Map<const RowDyn> cond_row(cond, 1, num_frames);
    Eigen::Map<const VecC>   gamma_vec(_film_gamma[li].data());
    Eigen::Map<const VecC>   beta_vec(_film_beta[li].data());

    // Aligned16: _z/_layer_in use Eigen::aligned_allocator (guaranteed 16-byte aligned).
    Eigen::Map<MatCDyn, Eigen::Aligned16> ztile(_z.data(), Channels, num_frames);
    Eigen::Map<MatCDyn, Eigen::Aligned16> lin_block(_layer_in.data(), Channels, num_frames);

    ztile.setZero();

    // Conv: one Channels×Channels × Channels×N GEMM per tap.
    for (int k = 0; k < K; k++)
    {
      const int tap_base = tap_base_phys(K - 1 - k);
      Eigen::Map<const MatCC>  W(&L.conv_w[static_cast<size_t>(k) * Channels * Channels]);
      Eigen::Map<const MatCDyn> input_block(&L.history[static_cast<size_t>(tap_base) * Channels], Channels, num_frames);
      ztile.noalias() += W * input_block;
    }

    // Post-conv: bias, mixin, FiLM, LeakyReLU, 1x1 residual.
    ztile.colwise() += conv_b_vec;
    ztile.noalias() += mixin_vec * cond_row;
    // FiLM: scale each row c by gamma[c], then add beta[c].
    ztile = gamma_vec.asDiagonal() * ztile; // O(C×N) — diagonal×dense
    ztile.colwise() += beta_vec;
    // LeakyReLU.
    ztile = (ztile.array() < 0.0f).select(ztile.array() * kLeakySlope, ztile.array());
    // Skip connections: accumulate post-activation into the head sum.
    Eigen::Map<MatCDyn, Eigen::Aligned16> hsum_block(_head_sum.data(), Channels, num_frames);
    hsum_block.noalias() += ztile;
    // layer1x1 residual.
    lin_block.noalias() += l1x1_mat * ztile;
    lin_block.colwise() += l1x1_b_vec;
  }
}

template <int Channels>
void ParametricA2FastModel<Channels>::_layer_forward(int li, const float* cond, int num_frames)
{
  Layer& L = _layers[li];
  _ring_write(L, num_frames);
  switch (L.kernel_size)
  {
    case 6:  _layer_forward_k<6>(L, li, cond, num_frames); break;
    case 15: _layer_forward_k<15>(L, li, cond, num_frames); break;
    default: throw std::runtime_error("ParametricA2FastModel: unexpected kernel_size "
                                      + std::to_string(L.kernel_size));
  }
}

// --- Head (identical to A2FastModel) ---
template <int Channels>
void ParametricA2FastModel<Channels>::_head_forward(float* output, int num_frames)
{
  _head_ring_write(num_frames);
  #if NAM_A2_RING_MODE == 1
  const int mask = _head_pow2_mask;
  auto col_of = [&](int f, int k) { return (_head_write_pos - num_frames + f - (kHeadKernelSize - 1 - k)) & mask; };
  #else
  const int base = _head_write_pos - num_frames;
  auto col_of = [&](int f, int k) { return base + f - (kHeadKernelSize - 1 - k); };
  #endif

  for (int f = 0; f < num_frames; f++)
  {
    float y = _head_b;
    for (int k = 0; k < kHeadKernelSize; k++)
    {
      const int col = col_of(f, k);
      const float* src = &_head_history[static_cast<size_t>(col) * Channels];
      const float* wk = _head_w[k].data();
      for (int b = 0; b < Channels; b++)
        y += wk[b] * src[b];
    }
    output[f] = y * _head_scale;
  }
}

// --- process ---
template <int Channels>
void ParametricA2FastModel<Channels>::process(NAM_SAMPLE** input, NAM_SAMPLE** output, int num_frames)
{
  if (num_frames < 0 || num_frames > GetMaxBufferSize())
    throw std::invalid_argument("FiLM block exceeds prepared buffer");

  // Rebuild per-layer gamma/beta from knob atomics (cheap: 23 × num_params × 2C FMAs).
  _knob_smooth_coeff = 1.0f - std::exp(-static_cast<float>(num_frames) / (static_cast<float>(mExternalSampleRate) * 0.100f));
  _recompute_film();

  const NAM_SAMPLE* in0 = input[0];
  NAM_SAMPLE* out0 = output[0];

  // Rechannel + float cond copy.
  float* cond = _cond.data();
  for (int f = 0; f < num_frames; f++)
  {
    const float x = static_cast<float>(in0[f]);
    cond[f] = x;
    float* lin = &_layer_in[static_cast<size_t>(f) * Channels];
    for (int c = 0; c < Channels; c++)
      lin[c] = _rechannel_w[c] * x;
  }

  // Zero the skip accumulator for this buffer.
  std::memset(_head_sum.data(), 0, static_cast<size_t>(num_frames) * Channels * sizeof(float));

  // Run all 23 layers. Afterwards _layer_in holds the final layer's l1x1+residual output
  // (feeds the next layer, not the head), and _head_sum holds the sum of every layer's
  // post-activation -- parametric models are SKIP-ONLY, so this is the only path.
  for (int li = 0; li < kNumLayers; li++)
    _layer_forward(li, cond, num_frames);

  // Head: reads _head_sum (skip accumulator) via _head_ring_write.
  float* head_out = _head_out.data();
  _head_forward(head_out, num_frames);

  // DC blocker: first-order IIR HP, fc ≈ 20 Hz. Matches ParametricWaveNet::process().
  float xp = _dc_x_prev, yp = _dc_y_prev;
  const float alpha = _dc_alpha;
  for (int f = 0; f < num_frames; f++)
  {
    const float xi = head_out[f];
    const float yi = xi - xp + alpha * yp;
    xp = xi;
    yp = yi;
    out0[f] = static_cast<NAM_SAMPLE>(yi);
  }
  _dc_x_prev = xp;
  _dc_y_prev = yp;
}

// [Fork] END: ParametricA2FastModel<Channels>

// --- ParametricA2FastConfig ---
struct ParametricA2FastConfig : public ModelConfig
{
  int channels = 0;
  int num_params = 0;
  std::vector<ParamSpec> param_defs;

  template <int C>
  std::unique_ptr<DSP> _make(std::vector<float>&& weights, double sampleRate)
  {
    return std::make_unique<ParametricA2FastModel<C>>(std::move(weights), sampleRate,
                                                      num_params, param_defs);
  }

  std::unique_ptr<DSP> create(std::vector<float> weights, double sampleRate) override
  {
    if (channels == 3)
      return _make<3>(std::move(weights), sampleRate);
    if (channels == 4) // [Fork] BEGIN: intermediate widths
      return _make<4>(std::move(weights), sampleRate);
    if (channels == 5)
      return _make<5>(std::move(weights), sampleRate);
    if (channels == 6)
      return _make<6>(std::move(weights), sampleRate);
    if (channels == 7) // [Fork] END
      return _make<7>(std::move(weights), sampleRate);
    if (channels == 8)
      return _make<8>(std::move(weights), sampleRate);
    if (channels == 9)
      return _make<9>(std::move(weights), sampleRate);
    throw std::runtime_error("ParametricA2FastConfig: unsupported channel count "
                             + std::to_string(channels));
  }
};

} // namespace
std::unique_ptr<DSP> create(const nlohmann::json& document) {
  const auto& config = document.at("config");
  auto parsed = ParametricA2FastConfig{};
  parsed.channels = config.at("layers").get<int>();
  parsed.param_defs = parse_param_specs({{"params", config.at("parametric").at("parameters")}}, "ParametricWaveNet");
  parsed.num_params = static_cast<int>(parsed.param_defs.size());
  auto weights = document.at("weights").get<std::vector<float>>();
  if (!std::all_of(weights.begin(), weights.end(), [](float v) {return std::isfinite(v);}))
    throw std::runtime_error("FiLM weights must be finite");
  return parsed.create(std::move(weights), document.at("sample_rate").get<double>());
}
} // namespace nam::ardor_film
