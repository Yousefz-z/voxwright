#pragma once

#include <vox/core/ring_buffer.hpp>
#include <vox/dsp/resampler.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace vox::engine {

/// Carries engine-rate audio to one output device that runs on its own clock.
///
/// The producer (the processing thread) converts to the device rate and
/// writes into a ring buffer; the device callback reads from it. The two
/// clocks always differ slightly, so a PI controller trims the conversion
/// ratio by up to +-0.5 % to hold the buffer at its target. It acquires
/// quickly for 20 s after a start or refill, then tracks slowly so that
/// scheduling jitter does not turn into phase noise.
///
/// The controlled quantity is the fill the buffer would have if the device
/// consumed continuously instead of a period at a time: the ring fill minus
/// the frames the device will have played since its last read. Unlike the
/// raw fill it does not jump when a read moves past a write, so the
/// controller does not chase the beat between the two callback rates. It is
/// measured once per producer callback, just before its first write, which
/// is the low point of the cycle: target = scheduling safety margin.
///
/// At start and after an underrun the stage outputs silence until it holds
/// the target plus one device period plus the producer's last burst, so the
/// first reads cannot run dry before the next producer callback; then the
/// controller settles the fill. This beats crackling.
class OutputStage {
public:
    /// `targetFrames` is the continuous-model fill the controller holds
    /// before each producer callback, in device frames.
    void prepare(double engineRate, double deviceRate, std::size_t targetFrames,
                 std::size_t maxEngineBlock);
    /// For an output whose own callback produces the audio (pull mode). It
    /// shares the processing clock, so there is no drift to correct (a trim
    /// would only shift the pitch), and it renders before every read, so it
    /// needs no refill margin.
    void setSynchronous(bool synchronous) noexcept { synchronous_ = synchronous; }

    /// Producer side. `now` is monotonic time in seconds; every push made in
    /// one producer callback passes the same value.
    void push(std::span<const float> engineAudio, double now) noexcept;

    /// Consumer side: fills `frames` frames of `channels`-channel interleaved
    /// output. `now` is monotonic time in seconds on the same clock as push().
    void pull(float* interleaved, std::size_t frames, std::uint32_t channels, double now) noexcept;

    [[nodiscard]] std::uint64_t underruns() const noexcept {
        return underruns_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t overruns() const noexcept {
        return overruns_.load(std::memory_order_relaxed);
    }
    /// Current ratio trim (producer thread only).
    [[nodiscard]] double ratioTrim() const noexcept { return trim_; }
    [[nodiscard]] std::size_t fill() const noexcept { return ring_ ? ring_->availableToRead() : 0; }
    [[nodiscard]] std::size_t targetFrames() const noexcept { return target_; }
    /// Frames the resampler holds back between calls (measured at prepare).
    [[nodiscard]] std::size_t resamplerDelayFrames() const noexcept { return resamplerDelay_; }
    [[nodiscard]] double deviceRate() const noexcept { return deviceRate_; }

private:
    void control(double now) noexcept;

    double engineRate_ = 48000.0;
    double deviceRate_ = 48000.0;
    double nominalRatio_ = 1.0;
    std::size_t target_ = 256;
    bool synchronous_ = false;
    std::size_t resamplerDelay_ = 0;
    dsp::StreamResampler resampler_;
    std::unique_ptr<SpscRingBuffer<float>> ring_;
    std::vector<float> converted_;
    std::vector<float> mono_;
    // Controller state (producer thread).
    double lastPushTime_ = -1.0;
    std::size_t burstFrames_ = 0;
    double smoothedError_ = 0.0;
    bool haveError_ = false;
    std::uint64_t seenRefills_ = 0;
    double controlledSeconds_ = 0.0;
    double integralTrim_ = 0.0;
    double trim_ = 1.0;
    // Shared.
    std::atomic<double> lastPullTime_{-1.0};
    std::atomic<std::size_t> lastBurst_{0};
    std::atomic<bool> refilling_{true};
    std::atomic<std::uint64_t> refills_{0};
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<std::uint64_t> overruns_{0};
};

} // namespace vox::engine
