#include "dsp/IrReverbProcessor.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace ardor {

struct IrReverbKernel {
  NonUniformConvolver::PreparedImpulse left, right;
  IrReverbKernel* retiredNext = nullptr;
};

static_assert(std::atomic<IrReverbKernel*>::is_always_lock_free);

struct IrReverbLiveParameters {
  // One control producer and one audio consumer. Superseded pending kernels
  // and the retired list are destroyed only by the control thread.
  std::atomic<IrReverbKernel*> pendingKernel{nullptr};
  std::atomic<IrReverbKernel*> retiredKernels{nullptr};

  void reclaimRetired() noexcept {
    auto* kernel = retiredKernels.exchange(nullptr, std::memory_order_acquire);
    while (kernel) {
      auto* next = kernel->retiredNext;
      delete kernel;
      kernel = next;
    }
  }
  ~IrReverbLiveParameters() {
    delete pendingKernel.load();
    reclaimRetired();
  }

  std::atomic<float> mix{0.35f};
  std::atomic<float> levelDb{0.0f};
  std::atomic<float> preDelayMs{0.0f};
  std::atomic<float> lowCutHz{IrReverbProcessor::LOW_CUT_MIN_HZ};
  std::atomic<float> highCutHz{IrReverbProcessor::HIGH_CUT_MAX_HZ};
  std::atomic<std::uint64_t> revision{0};
};

namespace {

// A target may arrive midway through a scheduled partition. Three tail
// periods plus alignment flush incomplete overlap before the audible fade.
constexpr std::size_t kKernelWarmupFrames =
    3 * NonUniformConvolver::TAIL_PARTITION_FRAMES
    + NonUniformConvolver::EARLY_IMPULSE_FRAMES
    + NonUniformConvolver::EARLY_PARTITION_FRAMES;
constexpr std::size_t kKernelFadeFrames = 4800; // 100 ms at the 48 kHz host rate
constexpr double kLn1000 = 6.907755278982137;

// Schroeder backwards energy integration, with a -5 to -35 dB T30 fit.
// One stereo estimate/envelope preserves the balance between the channels.
std::optional<float> estimateRt60(const std::vector<float>& left,
                                const std::vector<float>& right,
                                float sampleRate, std::size_t& onset)
{
  const std::size_t frames = std::max(left.size(), right.size());
  if (frames < static_cast<std::size_t>(0.1f * sampleRate)) return std::nullopt;
  std::vector<double> energy(frames);
  double sum = 0.0, peak = 0.0;
  for (std::size_t i = frames; i-- > 0;) {
    const double l = i < left.size() ? left[i] : 0.0;
    const double r = i < right.size() ? right[i] : 0.0;
    const double e = l * l + r * r;
    if (!std::isfinite(e)) return std::nullopt;
    peak = std::max(peak, e);
    sum += e;
    energy[i] = sum;
  }
  if (!(sum > 0.0)) return std::nullopt;
  onset = 0;
  while (onset + 1 < frames && energy[onset] - energy[onset + 1] < peak * 1e-8)
    ++onset;

  double sx = 0, sy = 0, sxx = 0, sxy = 0, syy = 0;
  std::size_t count = 0, first = 0, last = 0;
  for (std::size_t i = onset; i < frames; ++i) {
    if (energy[i] <= 0.0) break;
    const double db = 10.0 * std::log10(energy[i] / sum);
    if (db > -5.0) continue;
    if (db < -35.0) break;
    const double t = static_cast<double>(i - onset) / sampleRate;
    if (count++ == 0) first = i;
    last = i;
    sx += t; sy += db; sxx += t * t; sxy += t * db; syy += db * db;
  }
  if (count < 2 || last - first < static_cast<std::size_t>(0.025f * sampleRate))
    return std::nullopt;
  const double vx = count * sxx - sx * sx;
  const double vy = count * syy - sy * sy;
  const double cov = count * sxy - sx * sy;
  if (!(vx > 0 && vy > 0 && cov < 0) || cov * cov / (vx * vy) < 0.98)
    return std::nullopt;
  const double rt60 = -60.0 * vx / cov;
  if (!std::isfinite(rt60) || rt60 < 0.05 || rt60 > 60.0) return std::nullopt;
  return static_cast<float>(rt60);
}

constexpr float kTwoPi = 6.28318530718f;
constexpr float kMixSmoothing = 0.0005f;
constexpr std::size_t kPreDelayFadeFrames = 960; // 20 ms at the 48 kHz host rate
constexpr float kFilterBypassStep = 1.0f / 480.0f;
constexpr float kFilterCoeffSmoothing = 1.0f / 240.0f;

float onePoleCoeff(float cutoffHz, float sampleRate)
{
  if (!(cutoffHz > 0.0f) || !(sampleRate > 0.0f)) return 1.0f;
  const float k = 1.0f - std::exp(-kTwoPi * cutoffHz / sampleRate);
  return std::clamp(k, 0.00001f, 1.0f);
}

} // namespace

IrReverbProcessor::IrReverbProcessor() = default;
IrReverbProcessor::~IrReverbProcessor() = default;

bool IrReverbProcessor::load(std::vector<float> left, std::vector<float> right,
                             float sampleRate, std::string& error, float reverbTimeRatio)
{
  if (!(sampleRate > 0.0f) || !std::isfinite(sampleRate)) {
    error = "convolution reverb needs a positive sample rate";
    return false;
  }
  if (left.empty()) {
    error = "convolution reverb needs a non-empty impulse";
    return false;
  }
  sampleRate_ = sampleRate;

  const std::size_t maxFrames =
      static_cast<std::size_t>(MAX_IMPULSE_SECONDS * sampleRate);
  if (left.size() > maxFrames) left.resize(maxFrames);
  if (right.size() > maxFrames) right.resize(maxFrames);
  // A mono impulse drives both channels; the reverb is still stereo because the
  // two convolvers see different input.
  if (right.empty()) right = left;

  // Loading and destruction happen after the caller has stopped processing.
  activeKernel_.reset();
  nextKernel_.reset();
  kernelWarmupRemaining_ = kernelFadeFrame_ = 0;
  requestedReverbTimeRatio_ = 1.0f;
  impulseOnset_ = 0;
  originalRt60_ = estimateRt60(left, right, sampleRate_, impulseOnset_);
  originalLeft_ = left;
  originalRight_ = right;
  impulseFrames_ = std::max(left.size(), right.size());
  left_.load(std::move(left));
  right_.load(std::move(right));

  // Room for the largest pre-delay the control offers, plus a guard sample.
  const std::size_t preDelayCapacity =
      static_cast<std::size_t>(0.5f * sampleRate_) + 2;
  preLeft_.assign(preDelayCapacity, 0.0f);
  preRight_.assign(preDelayCapacity, 0.0f);
  preWrite_ = 0;
  preDelaySamples_ = preDelayCurrent_ = preDelayTarget_ = 0;
  preDelayFadeRemaining_ = 0;

  liveParameters_ = std::make_shared<IrReverbLiveParameters>();
  liveRevision_ = 1;
  liveParameters_->revision.store(liveRevision_, std::memory_order_release);

  mixTarget_ = 0.35f;
  levelTarget_ = 1.0f;
  lowCutHz_ = LOW_CUT_MIN_HZ;
  highCutHz_ = HIGH_CUT_MAX_HZ;

  updateFilters();
  lowCutL_.coeff = lowCutR_.coeff = lowCutCoeffTarget_;
  highCutL_.coeff = highCutR_.coeff = highCutCoeffTarget_;
  lowCutL_.reset(); lowCutR_.reset();
  highCutL_.reset(); highCutR_.reset();
  mix_ = mixTarget_;
  level_ = levelTarget_;
  lowCutMix_ = lowCutActive_ ? 1.0f : 0.0f;
  highCutMix_ = highCutActive_ ? 1.0f : 0.0f;
  loaded_ = true;
  setReverbTimeRatio(reverbTimeRatio);
  reset();
  error.clear();
  return true;
}

void IrReverbProcessor::reset()
{
  refreshLiveParameters();
  // reset also applies the newest prepared load-time value immediately. This
  // path can run on audio; retiring kernels must never free them here.
  if (nextKernel_) finishKernelTransition();
  beginKernelTransition();
  if (nextKernel_) finishKernelTransition();
  left_.reset();
  right_.reset();
  std::fill(preLeft_.begin(), preLeft_.end(), 0.0f);
  std::fill(preRight_.begin(), preRight_.end(), 0.0f);
  preWrite_ = 0;
  preDelayCurrent_ = preDelayTarget_ = preDelaySamples_;
  preDelayFadeRemaining_ = 0;
  lowCutL_.coeff = lowCutR_.coeff = lowCutCoeffTarget_;
  highCutL_.coeff = highCutR_.coeff = highCutCoeffTarget_;
  lowCutL_.reset(); lowCutR_.reset();
  highCutL_.reset(); highCutR_.reset();
  mix_ = mixTarget_;
  level_ = levelTarget_;
  lowCutMix_ = lowCutActive_ ? 1.0f : 0.0f;
  highCutMix_ = highCutActive_ ? 1.0f : 0.0f;
}

void IrReverbProcessor::setMix(float mix)
{
  if (!liveParameters_) return;
  liveParameters_->mix.store(
    std::isfinite(mix) ? std::clamp(mix, 0.0f, 1.0f) : 0.0f,
    std::memory_order_relaxed);
  liveParameters_->revision.fetch_add(1, std::memory_order_release);
}

void IrReverbProcessor::setLevelDb(float levelDb)
{
  if (!liveParameters_) return;
  if (!std::isfinite(levelDb)) levelDb = 0.0f;
  liveParameters_->levelDb.store(std::clamp(levelDb, -60.0f, 12.0f),
                                 std::memory_order_relaxed);
  liveParameters_->revision.fetch_add(1, std::memory_order_release);
}

void IrReverbProcessor::setPreDelayMs(float milliseconds)
{
  if (!liveParameters_) return;
  if (!std::isfinite(milliseconds) || milliseconds < 0.0f) milliseconds = 0.0f;
  liveParameters_->preDelayMs.store(std::min(milliseconds, 500.0f),
                                    std::memory_order_relaxed);
  liveParameters_->revision.fetch_add(1, std::memory_order_release);
}

void IrReverbProcessor::setLowCutHz(float hz)
{
  if (!liveParameters_) return;
  liveParameters_->lowCutHz.store(
    std::isfinite(hz) ? std::clamp(hz, LOW_CUT_MIN_HZ, LOW_CUT_MAX_HZ) : LOW_CUT_MIN_HZ,
    std::memory_order_relaxed);
  liveParameters_->revision.fetch_add(1, std::memory_order_release);
}

void IrReverbProcessor::setHighCutHz(float hz)
{
  if (!liveParameters_) return;
  liveParameters_->highCutHz.store(
    std::isfinite(hz) ? std::clamp(hz, HIGH_CUT_MIN_HZ, HIGH_CUT_MAX_HZ) : HIGH_CUT_MAX_HZ,
    std::memory_order_relaxed);
  liveParameters_->revision.fetch_add(1, std::memory_order_release);
}

void IrReverbProcessor::setReverbTimeRatio(float ratio)
{
  if (!liveParameters_) return;
  liveParameters_->reclaimRetired();
  ratio = std::isfinite(ratio) ? std::clamp(ratio, 0.25f, 1.0f) : 1.0f;
  if (ratio == requestedReverbTimeRatio_ || !originalRt60_) return;
  auto left = originalLeft_;
  auto right = originalRight_;
  if (ratio < 1.0f) {
    const double slope = -kLn1000 * (1.0 / ratio - 1.0) / *originalRt60_;
    const auto shape = [&](std::vector<float>& impulse) {
      for (std::size_t i = impulseOnset_; i < impulse.size(); ++i)
        impulse[i] *= static_cast<float>(std::exp(slope * (i - impulseOnset_) / sampleRate_));
    };
    shape(left);
    shape(right);
  }
  auto kernel = std::make_unique<IrReverbKernel>();
  kernel->left = left_.prepareImpulse(left);
  kernel->right = right_.prepareImpulse(right);
  // Exchange transfers ownership. Only an unconsumed pending kernel can be
  // deleted here; the audio thread retains every kernel it has taken.
  delete liveParameters_->pendingKernel.exchange(kernel.release(), std::memory_order_acq_rel);
  requestedReverbTimeRatio_ = ratio;
}

void IrReverbProcessor::retireKernel(IrReverbKernel* kernel) noexcept
{
  if (!kernel) return;
  auto& retired = liveParameters_->retiredKernels;
  auto* head = retired.load(std::memory_order_relaxed);
  do {
    kernel->retiredNext = head;
  } while (!retired.compare_exchange_strong(head, kernel, std::memory_order_release,
                                         std::memory_order_relaxed));
}

void IrReverbProcessor::beginKernelTransition() noexcept
{
  if (nextKernel_ || !liveParameters_) return;
  if (!liveParameters_->pendingKernel.load(std::memory_order_relaxed)) return;
  nextKernel_.reset(liveParameters_->pendingKernel.exchange(nullptr, std::memory_order_acquire));
  if (!nextKernel_) return;
  kernelWarmupRemaining_ = kKernelWarmupFrames;
  kernelFadeFrame_ = 0;
}

void IrReverbProcessor::finishKernelTransition() noexcept
{
  left_.finishTransition();
  right_.finishTransition();
  retireKernel(activeKernel_.release());
  activeKernel_.reset(nextKernel_.release());
  kernelWarmupRemaining_ = kernelFadeFrame_ = 0;
}

void IrReverbProcessor::refreshLiveParameters() noexcept
{
  if (!liveParameters_) return;
  const auto revision = liveParameters_->revision.load(std::memory_order_acquire);
  if (revision == liveRevision_) return;

  mixTarget_ = liveParameters_->mix.load(std::memory_order_relaxed);
  const float levelDb = liveParameters_->levelDb.load(std::memory_order_relaxed);
  levelTarget_ = levelDb <= -60.0f ? 0.0f : std::pow(10.0f, levelDb / 20.0f);
  const float milliseconds = liveParameters_->preDelayMs.load(std::memory_order_relaxed);
  const std::size_t samples = static_cast<std::size_t>(milliseconds * 0.001f * sampleRate_);
  preDelaySamples_ = preLeft_.empty() ? 0 : std::min(samples, preLeft_.size() - 1);
  if (preDelayFadeRemaining_ == 0 && preDelaySamples_ != preDelayCurrent_) {
    preDelayTarget_ = preDelaySamples_;
    preDelayFadeRemaining_ = kPreDelayFadeFrames;
  }

  const float lowCut = liveParameters_->lowCutHz.load(std::memory_order_relaxed);
  const float highCut = liveParameters_->highCutHz.load(std::memory_order_relaxed);
  if (lowCut != lowCutHz_ || highCut != highCutHz_) {
    lowCutHz_ = lowCut;
    highCutHz_ = highCut;
    updateFilters();
  }
  liveRevision_ = revision;
}

void IrReverbProcessor::updateFilters()
{
  lowCutActive_ = lowCutHz_ > LOW_CUT_MIN_HZ;
  highCutActive_ = highCutHz_ < HIGH_CUT_MAX_HZ;
  lowCutCoeffTarget_ = onePoleCoeff(lowCutHz_, sampleRate_);
  highCutCoeffTarget_ = onePoleCoeff(highCutHz_, sampleRate_);
}

std::size_t IrReverbProcessor::tailFrames() const noexcept
{
  return loaded_ ? impulseFrames_ + PARTITION_FRAMES
                   + std::max({preDelaySamples_, preDelayCurrent_, preDelayTarget_}) : 0;
}

StereoSample IrReverbProcessor::process(StereoSample input)
{
  return processFrame(input).mixed;
}

IrReverbFrame IrReverbProcessor::processFrame(StereoSample input)
{
  refreshLiveParameters();
  if (!loaded_) return {input, {}};

  // Pre-delay ahead of the convolver, so its buffer only spans the extra delay.
  // Always run the line, even at zero delay: skipping the write would leave
  // stale audio behind for the control to uncover when it is raised again.
  preLeft_[preWrite_] = input.left;
  preRight_[preWrite_] = input.right;
  const auto readTap = [this](std::size_t delay) {
    return (preWrite_ + preLeft_.size() - delay) % preLeft_.size();
  };
  const std::size_t currentRead = readTap(preDelayCurrent_);
  float sendL = preLeft_[currentRead];
  float sendR = preRight_[currentRead];
  if (preDelayFadeRemaining_ > 0) {
    const std::size_t targetRead = readTap(preDelayTarget_);
    const float blend = float(kPreDelayFadeFrames - preDelayFadeRemaining_ + 1)
                      / float(kPreDelayFadeFrames);
    sendL += blend * (preLeft_[targetRead] - sendL);
    sendR += blend * (preRight_[targetRead] - sendR);
    if (--preDelayFadeRemaining_ == 0) {
      preDelayCurrent_ = preDelayTarget_;
      if (preDelaySamples_ != preDelayCurrent_) {
        preDelayTarget_ = preDelaySamples_;
        preDelayFadeRemaining_ = kPreDelayFadeFrames;
      }
    }
  }
  preWrite_ = (preWrite_ + 1) % preLeft_.size();

  // The convolver buffers internally and spreads its own work, so this is a
  // plain per-sample call from here.
  beginKernelTransition();
  const auto l = left_.processFrame(sendL, activeKernel_ ? &activeKernel_->left : nullptr,
                                   nextKernel_ ? &nextKernel_->left : nullptr);
  const auto r = right_.processFrame(sendR, activeKernel_ ? &activeKernel_->right : nullptr,
                                    nextKernel_ ? &nextKernel_->right : nullptr);
  float wetL = l.current, wetR = r.current;
  if (nextKernel_) {
    if (kernelWarmupRemaining_ > 0) --kernelWarmupRemaining_;
    else {
      const float blend = static_cast<float>(++kernelFadeFrame_) / kKernelFadeFrames;
      wetL += blend * (l.next - wetL);
      wetR += blend * (r.next - wetR);
      if (kernelFadeFrame_ == kKernelFadeFrames) finishKernelTransition();
    }
  }
  // Keep a malformed impulse or upstream non-finite sample out of the
  // continuously running filter histories.
  if (!std::isfinite(wetL)) wetL = 0.0f;
  if (!std::isfinite(wetR)) wetR = 0.0f;

  // Shape the tail, not the dry signal.
  lowCutMix_ += std::clamp((lowCutActive_ ? 1.0f : 0.0f) - lowCutMix_,
                          -kFilterBypassStep, kFilterBypassStep);
  highCutMix_ += std::clamp((highCutActive_ ? 1.0f : 0.0f) - highCutMix_,
                           -kFilterBypassStep, kFilterBypassStep);
  lowCutL_.coeff += kFilterCoeffSmoothing * (lowCutCoeffTarget_ - lowCutL_.coeff);
  lowCutR_.coeff = lowCutL_.coeff;
  highCutL_.coeff += kFilterCoeffSmoothing * (highCutCoeffTarget_ - highCutL_.coeff);
  highCutR_.coeff = highCutL_.coeff;
  const float highPassedL = lowCutL_.highPass(wetL);
  const float highPassedR = lowCutR_.highPass(wetR);
  wetL += lowCutMix_ * (highPassedL - wetL);
  wetR += lowCutMix_ * (highPassedR - wetR);
  const float lowPassedL = highCutL_.lowPass(wetL);
  const float lowPassedR = highCutR_.lowPass(wetR);
  wetL += highCutMix_ * (lowPassedL - wetL);
  wetR += highCutMix_ * (lowPassedR - wetR);

  mix_ += kMixSmoothing * (mixTarget_ - mix_);
  level_ += kMixSmoothing * (levelTarget_ - level_);

  if (!std::isfinite(wetL)) wetL = 0.0f;
  if (!std::isfinite(wetR)) wetR = 0.0f;

  const float dry = 1.0f - mix_;
  const StereoSample wet{
      wetL * mix_ * level_,
      wetR * mix_ * level_,
  };
  return {
    {
      input.left * dry * level_ + wet.left,
      input.right * dry * level_ + wet.right,
    },
    wet,
  };
}

} // namespace ardor
