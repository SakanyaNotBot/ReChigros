#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace rechigros::audio {

/// One interleaved stereo frame.
struct StereoSample {
  float left{};
  float right{};
};

namespace mixer {

inline constexpr float kMinPlaybackSpeed = 0.05F;
inline constexpr float kMaxPlaybackSpeed = 4.0F;
inline constexpr float kMaxGain = 5.0F;

/// Soft clip that keeps the mixed signal inside the representable range.
inline float clampSample(float value) noexcept {
  return value > 1.0F ? 1.0F : (value < -1.0F ? -1.0F : value);
}

inline float clampGain(float value) noexcept {
  return value < 0.0F ? 0.0F : (value > kMaxGain ? kMaxGain : value);
}

inline float clampSpeed(float value) noexcept {
  return value < kMinPlaybackSpeed ? kMinPlaybackSpeed
                                   : (value > kMaxPlaybackSpeed ? kMaxPlaybackSpeed : value);
}

/// Maps a frame index onto `[0, frameCount)` for any input value.
inline std::int64_t wrapFrameIndex(std::int64_t index, std::int64_t frameCount) noexcept {
  if (frameCount <= 0) return 0;
  const std::int64_t wrapped = index % frameCount;
  return wrapped < 0 ? wrapped + frameCount : wrapped;
}

/// Maps a fractional read position onto `[0, frameCount)` for any input value.
inline double wrapPosition(double positionFrames, std::int64_t frameCount) noexcept {
  if (frameCount <= 0) return 0.0;
  const double span = static_cast<double>(frameCount);
  double wrapped = std::fmod(positionFrames, span);
  if (wrapped < 0.0) wrapped += span;
  return wrapped;
}

/// Reads one stereo frame, interpolating linearly between neighbours so that
/// fractional playback speeds stay smooth. Positions outside the buffer read as
/// silence unless looping is enabled, in which case they wrap into the buffer.
inline StereoSample sampleMusic(const float* frames, std::int64_t frameCount,
    double positionFrames, bool looping) noexcept {
  StereoSample out;
  if (frames == nullptr || frameCount <= 0) return out;
  if (!std::isfinite(positionFrames)) return out;
  if (!looping && (positionFrames < 0.0 || positionFrames >= static_cast<double>(frameCount))) {
    return out;
  }

  const double position = looping ? wrapPosition(positionFrames, frameCount) : positionFrames;
  const auto index0 = static_cast<std::int64_t>(position);
  const auto fraction = static_cast<float>(position - static_cast<double>(index0));
  std::int64_t index1 = index0 + 1;
  if (index1 >= frameCount) index1 = looping ? 0 : index0;

  const float* first = frames + index0 * 2;
  const float* second = frames + index1 * 2;
  out.left = first[0] + (second[0] - first[0]) * fraction;
  out.right = first[1] + (second[1] - first[1]) * fraction;
  return out;
}

/// Moves the music read position forward by one output frame.
inline double advancePosition(double positionFrames, float speed) noexcept {
  return positionFrames + static_cast<double>(speed);
}

/// Sums one frame of a voice into `target`. Frames outside the voice read as silence.
inline void accumulate(StereoSample& target, const float* frames, std::int64_t frameCount,
    std::int64_t frameIndex, float gain) noexcept {
  if (frames == nullptr || frameIndex < 0 || frameIndex >= frameCount) return;
  const float* source = frames + frameIndex * 2;
  target.left += source[0] * gain;
  target.right += source[1] * gain;
}

/// Smooths the playhead between audio callbacks. The result is the read position
/// `elapsedSeconds` after a callback that started at `startFrames`, bounded by the
/// frames that callback produces. The buffer being mixed is already committed to the
/// device, so dead-reckoning to its end is valid even though the playhead counter is
/// only published once the mix returns. Bounding by the last published counter instead
/// would pin the playhead to the callback's start for its whole duration, which turns a
/// large buffer into a visible staircase.
inline double estimatePlayheadFrames(double startFrames, std::int32_t callbackFrames,
    float speed, double elapsedSeconds, double sampleRate) noexcept {
  if (callbackFrames <= 0 || sampleRate <= 0.0) return startFrames;
  const float boundedSpeed = clampSpeed(speed);
  double generated = elapsedSeconds * sampleRate;
  if (!std::isfinite(generated) || generated < 0.0) generated = 0.0;
  if (generated > static_cast<double>(callbackFrames)) generated = static_cast<double>(callbackFrames);

  double estimate = startFrames + generated * static_cast<double>(boundedSpeed);
  const double maximum =
      startFrames + static_cast<double>(callbackFrames) * static_cast<double>(boundedSpeed);
  if (estimate > maximum) estimate = maximum;
  if (estimate < 0.0) estimate = 0.0;
  return estimate;
}

/// Linear resampling of interleaved stereo frames between two sample rates.
inline std::vector<float> resampleStereo(const float* frames, std::int64_t frameCount,
    std::uint32_t fromRate, std::uint32_t toRate) {
  std::vector<float> out;
  if (frames == nullptr || frameCount <= 0 || fromRate == 0 || toRate == 0) return out;
  if (fromRate == toRate) return std::vector<float>(frames, frames + frameCount * 2);

  const double ratio = static_cast<double>(toRate) / static_cast<double>(fromRate);
  const auto outFrames = static_cast<std::int64_t>(std::llround(static_cast<double>(frameCount) * ratio));
  if (outFrames <= 0) return out;
  out.resize(static_cast<std::size_t>(outFrames) * 2);

  for (std::int64_t index = 0; index < outFrames; ++index) {
    const double sourcePosition = static_cast<double>(index) / ratio;
    auto index0 = static_cast<std::int64_t>(sourcePosition);
    if (index0 < 0) index0 = 0;
    if (index0 >= frameCount) index0 = frameCount - 1;
    std::int64_t index1 = index0 + 1;
    if (index1 >= frameCount) index1 = frameCount - 1;
    const auto fraction = static_cast<float>(sourcePosition - static_cast<double>(index0));

    const float* first = frames + index0 * 2;
    const float* second = frames + index1 * 2;
    out[static_cast<std::size_t>(index) * 2 + 0] = first[0] + (second[0] - first[0]) * fraction;
    out[static_cast<std::size_t>(index) * 2 + 1] = first[1] + (second[1] - first[1]) * fraction;
  }
  return out;
}

} // namespace mixer

/// Second-order Butterworth low-pass with a linearly ramped cutoff, applied to the
/// music path only. Coefficients follow the RBJ audio EQ cookbook.
class LowPassFilter final {
public:
  static constexpr float kOpenCutoffHz = 22000.0F;
  static constexpr float kClosedCutoffHz = 1500.0F;
  static constexpr float kRampSeconds = 0.05F;

  void reset() noexcept {
    x1_[0] = x2_[0] = y1_[0] = y2_[0] = 0.0;
    x1_[1] = x2_[1] = y1_[1] = y2_[1] = 0.0;
    currentCutoff_ = kOpenCutoffHz;
    rampStart_ = kOpenCutoffHz;
    targetCutoff_ = kOpenCutoffHz;
    rampElapsed_ = kRampSeconds;
    coefficientCutoff_ = -1.0F;
    coefficientsValid_ = false;
  }

  void setSampleRate(double sampleRate) noexcept {
    if (sampleRate <= 0.0 || sampleRate == sampleRate_) return;
    sampleRate_ = sampleRate;
    coefficientsValid_ = false;
  }

  /// Starts a new ramp towards `cutoffHz` from the current cutoff when it differs
  /// from the cutoff already being targeted.
  void setTargetCutoff(float cutoffHz) noexcept {
    if (cutoffHz == targetCutoff_) return;
    rampStart_ = currentCutoff_;
    rampElapsed_ = 0.0F;
    targetCutoff_ = cutoffHz;
  }

  /// Advances the cutoff ramp by one output sample.
  void advance(float deltaSeconds) noexcept {
    if (currentCutoff_ == targetCutoff_) return;
    rampElapsed_ += deltaSeconds;
    float progress = kRampSeconds > 0.0F ? rampElapsed_ / kRampSeconds : 1.0F;
    if (progress < 0.0F) progress = 0.0F;
    if (progress > 1.0F) progress = 1.0F;
    currentCutoff_ = rampStart_ + (targetCutoff_ - rampStart_) * progress;
    if (progress >= 1.0F) currentCutoff_ = targetCutoff_;
  }

  void process(float& left, float& right) noexcept {
    if (!coefficientsValid_ || std::abs(currentCutoff_ - coefficientCutoff_) >= 1.0F) {
      updateCoefficients();
    }

    const double inLeft = static_cast<double>(left);
    const double outLeft = b0_ * inLeft + b1_ * x1_[0] + b2_ * x2_[0]
        - a1_ * y1_[0] - a2_ * y2_[0];
    x2_[0] = x1_[0];
    x1_[0] = inLeft;
    y2_[0] = y1_[0];
    y1_[0] = outLeft;

    const double inRight = static_cast<double>(right);
    const double outRight = b0_ * inRight + b1_ * x1_[1] + b2_ * x2_[1]
        - a1_ * y1_[1] - a2_ * y2_[1];
    x2_[1] = x1_[1];
    x1_[1] = inRight;
    y2_[1] = y1_[1];
    y1_[1] = outRight;

    left = static_cast<float>(outLeft);
    right = static_cast<float>(outRight);
  }

  [[nodiscard]] float cutoff() const noexcept { return currentCutoff_; }

private:
  void updateCoefficients() noexcept {
    if (sampleRate_ <= 0.0) return;
    constexpr double kQ = 0.7071067811865476;
    constexpr double kTwoPi = 6.283185307179586;

    double cutoff = static_cast<double>(currentCutoff_);
    if (cutoff < 20.0) cutoff = 20.0;
    const double nyquist = sampleRate_ * 0.5;
    if (cutoff > nyquist * 0.98) cutoff = nyquist * 0.98;

    const double omega = kTwoPi * cutoff / sampleRate_;
    const double cosine = std::cos(omega);
    const double alpha = std::sin(omega) / (2.0 * kQ);
    const double a0 = 1.0 + alpha;
    b0_ = ((1.0 - cosine) * 0.5) / a0;
    b1_ = (1.0 - cosine) / a0;
    b2_ = b0_;
    a1_ = (-2.0 * cosine) / a0;
    a2_ = (1.0 - alpha) / a0;

    coefficientCutoff_ = currentCutoff_;
    coefficientsValid_ = true;
  }

  double sampleRate_{48000.0};
  double b0_{1.0}, b1_{0.0}, b2_{0.0}, a1_{0.0}, a2_{0.0};
  double x1_[2]{}, x2_[2]{}, y1_[2]{}, y2_[2]{};
  float currentCutoff_{kOpenCutoffHz};
  float rampStart_{kOpenCutoffHz};
  float targetCutoff_{kOpenCutoffHz};
  float rampElapsed_{kRampSeconds};
  float coefficientCutoff_{-1.0F};
  bool coefficientsValid_{};
};

} // namespace rechigros::audio
