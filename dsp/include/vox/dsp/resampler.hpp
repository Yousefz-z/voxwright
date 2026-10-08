#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace vox::dsp {

enum class ResamplerQuality {
    Fast,   ///< libsamplerate SINC_FASTEST: 80 % bandwidth, 97 dB SNR; for live device streams.
    Medium, ///< SINC_MEDIUM_QUALITY: 90 % bandwidth, 121 dB SNR.
    Best,   ///< SINC_BEST_QUALITY: 96 % bandwidth, 145 dB SNR; for importing clips.
};

/// Streaming mono sample-rate converter (libsamplerate). The ratio can change
/// between calls without clicks, which the engine uses for clock-drift
/// compensation. process() never allocates.
class StreamResampler {
public:
    StreamResampler();
    ~StreamResampler();
    StreamResampler(const StreamResampler&) = delete;
    StreamResampler& operator=(const StreamResampler&) = delete;
    StreamResampler(StreamResampler&&) noexcept;
    StreamResampler& operator=(StreamResampler&&) noexcept;

    /// ratio = output rate / input rate.
    void prepare(ResamplerQuality quality, double ratio);
    void reset() noexcept;
    void setRatio(double ratio) noexcept { ratio_ = ratio; }
    [[nodiscard]] double ratio() const noexcept { return ratio_; }

    struct Counts {
        std::size_t consumed = 0;
        std::size_t produced = 0;
    };
    /// Converts as much of `input` as fits into `output`.
    Counts process(std::span<const float> input, std::span<float> output) noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
    double ratio_ = 1.0;
};

/// Frames of output a freshly prepared resampler holds back (its streaming
/// latency), measured by feeding it silence; the resampler is reset after.
/// Control thread only.
[[nodiscard]] std::size_t measureHoldBack(StreamResampler& resampler);

/// One-shot conversion of a whole signal (control thread only; allocates).
[[nodiscard]] std::vector<float> resample(std::span<const float> input, double fromRate,
                                          double toRate,
                                          ResamplerQuality quality = ResamplerQuality::Best);

} // namespace vox::dsp
