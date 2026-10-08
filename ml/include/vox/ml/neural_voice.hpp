#pragma once

#include "vox/ml/neural_model.hpp"

#include <vox/core/ring_buffer.hpp>
#include <vox/plugins/effect.hpp>
#include <vox/plugins/registry.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace vox::ml {

/// The model every neural voice block uses, chosen in the app. Swapping it
/// takes effect at the next window; blocks hold no model of their own.
class ModelHost {
public:
    void set(std::shared_ptr<NeuralModel> model);
    [[nodiscard]] std::shared_ptr<NeuralModel> get() const;

private:
    mutable std::mutex mutex_;
    std::shared_ptr<NeuralModel> model_;
};

/// How the voice is cut into windows for the model.
struct StreamingConfig {
    double hopMs = 100.0;      ///< New audio per model run.
    double contextMs = 100.0;  ///< Earlier audio the model sees with it.
    double crossfadeMs = 10.0; ///< Overlap between consecutive outputs.
    double lookaheadMs = 10.0; ///< Newest audio kept back (resampler edges).
    double budgetMs = 60.0;    ///< Time a run may take before audio is late.
};

/// An effect block that converts the voice with the host's model.
///
/// The audio thread only moves samples through two lock-free rings; a
/// worker thread cuts the input into overlapping windows, runs the model,
/// and crossfades the outputs. The block has a fixed latency
/// (hop + crossfade + lookahead + budget). When a run is late, or no model
/// is loaded, the block plays the input instead, delayed by the same
/// latency, so the voice never drops out.
class NeuralVoiceNode final : public plugins::EffectNode {
public:
    enum Param : std::size_t { kMix = 0, kPitch = 1 };

    /// With `threaded` false no worker starts; the owner calls pump()
    /// (tests use this to run deterministically).
    NeuralVoiceNode(std::shared_ptr<ModelHost> host, StreamingConfig config, bool threaded = true);
    NeuralVoiceNode(const NeuralVoiceNode&) = delete;
    NeuralVoiceNode& operator=(const NeuralVoiceNode&) = delete;
    NeuralVoiceNode(NeuralVoiceNode&&) = delete;
    NeuralVoiceNode& operator=(NeuralVoiceNode&&) = delete;
    ~NeuralVoiceNode() override;

    void prepare(const plugins::PrepareContext& context) override;
    void reset() noexcept override;
    void setParameter(std::size_t index, float value) noexcept override;
    [[nodiscard]] float parameter(std::size_t index) const noexcept override;
    void process(std::span<float> block) noexcept override;
    [[nodiscard]] std::size_t latencySamples() const noexcept override { return latency_; }

    /// Runs every window that has enough input; returns how many ran.
    std::size_t pump();
    /// Output samples that had to fall back to the plain voice.
    [[nodiscard]] std::uint64_t lateSamples() const noexcept {
        return late_.load(std::memory_order_relaxed);
    }

private:
    bool runWindow();
    void stopWorker();

    std::shared_ptr<ModelHost> host_;
    StreamingConfig config_;
    bool threaded_;
    double rate_ = 48000.0;
    std::size_t hop_ = 0;
    std::size_t context_ = 0;
    std::size_t crossfade_ = 0;
    std::size_t lookahead_ = 0;
    std::size_t latency_ = 0;

    // Audio thread.
    std::unique_ptr<SpscRingBuffer<float>> input_;
    std::unique_ptr<SpscRingBuffer<float>> output_;
    std::vector<float> dry_; ///< Delay line for the fallback.
    std::size_t dryPos_ = 0;
    std::int64_t written_ = 0; ///< Input samples received.
    std::int64_t wetRead_ = 0; ///< Output samples taken from the ring.
    float mix_ = 1.0F;
    float mixTarget_ = 1.0F;
    float wetGain_ = 0.0F;
    float gainStep_ = 0.0F;
    std::atomic<float> pitch_{0.0F};
    std::atomic<std::uint64_t> late_{0};

    // Worker.
    std::vector<float> history_; ///< context + hop + lookahead at the block rate.
    std::vector<float> tail_;    ///< Last crossfade of the previous output.
    std::vector<float> fresh_;
    std::vector<float> emit_;
    std::atomic<bool> stop_{false};
    std::thread worker_;
};

/// Adds the "neural" effect (mix, pitch shift) to `registry`; its blocks use
/// `host`'s model.
void registerNeuralEffect(plugins::EffectRegistry& registry, std::shared_ptr<ModelHost> host,
                          StreamingConfig config = {});

} // namespace vox::ml
