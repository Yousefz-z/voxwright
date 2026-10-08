#include "vox/ml/neural_voice.hpp"

#include <vox/dsp/resampler.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace vox::ml {
namespace {

std::size_t samplesOf(double ms, double rate) {
    return static_cast<std::size_t>(std::lround(ms * rate / 1000.0));
}

std::vector<float> convert(const std::vector<float>& x, double from, double to) {
    if (from == to) {
        return x;
    }
    return dsp::resample(x, from, to, dsp::ResamplerQuality::Medium);
}

} // namespace

void ModelHost::set(std::shared_ptr<NeuralModel> model) {
    const std::scoped_lock lock(mutex_);
    model_ = std::move(model);
}

std::shared_ptr<NeuralModel> ModelHost::get() const {
    const std::scoped_lock lock(mutex_);
    return model_;
}

NeuralVoiceNode::NeuralVoiceNode(std::shared_ptr<ModelHost> host, StreamingConfig config,
                                 bool threaded)
    : host_(std::move(host))
    , config_(config)
    , threaded_(threaded) {}

NeuralVoiceNode::~NeuralVoiceNode() {
    stopWorker();
}

void NeuralVoiceNode::stopWorker() {
    stop_.store(true, std::memory_order_relaxed);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void NeuralVoiceNode::prepare(const plugins::PrepareContext& context) {
    stopWorker();
    rate_ = context.sampleRate;
    hop_ = std::max<std::size_t>(64, samplesOf(config_.hopMs, rate_));
    context_ = samplesOf(config_.contextMs, rate_);
    crossfade_ = std::min(hop_ / 2, samplesOf(config_.crossfadeMs, rate_));
    lookahead_ = std::min(hop_ / 2, samplesOf(config_.lookaheadMs, rate_));
    const std::size_t budget = samplesOf(config_.budgetMs, rate_);
    latency_ = hop_ + crossfade_ + lookahead_ + budget;

    input_ = std::make_unique<SpscRingBuffer<float>>(8 * hop_ + 4 * context.maxBlockSize);
    output_ = std::make_unique<SpscRingBuffer<float>>(4 * hop_ + latency_);
    dry_.assign(latency_ + 1, 0.0F);
    dryPos_ = 0;
    written_ = 0;
    wetRead_ = 0;
    wetGain_ = 0.0F;
    gainStep_ = static_cast<float>(1.0 / (0.005 * rate_)); // 5 ms
    mix_ = mixTarget_;
    late_.store(0, std::memory_order_relaxed);

    history_.assign(context_ + hop_, 0.0F);
    tail_.assign(crossfade_, 0.0F);
    fresh_.assign(hop_, 0.0F);
    emit_.assign(hop_, 0.0F);

    if (threaded_) {
        stop_.store(false, std::memory_order_relaxed);
        worker_ = std::thread([this] {
            while (!stop_.load(std::memory_order_relaxed)) {
                if (!runWindow()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        });
    }
}

void NeuralVoiceNode::reset() noexcept {
    // The pipeline keeps running (its two threads own their halves of it);
    // only the smoothing jumps to its targets.
    mix_ = mixTarget_;
}

void NeuralVoiceNode::setParameter(std::size_t index, float value) noexcept {
    if (index == kMix) {
        mixTarget_ = std::clamp(value, 0.0F, 1.0F);
    } else if (index == kPitch) {
        pitch_.store(std::clamp(value, -12.0F, 12.0F), std::memory_order_relaxed);
    }
}

float NeuralVoiceNode::parameter(std::size_t index) const noexcept {
    if (index == kMix) {
        return mixTarget_;
    }
    return index == kPitch ? pitch_.load(std::memory_order_relaxed) : 0.0F;
}

void NeuralVoiceNode::process(std::span<float> block) noexcept {
    if (!input_) {
        return;
    }
    static_cast<void>(input_->write(block));
    // Model output index k carries input index k - crossfade - lookahead, so
    // the output for input n - latency is model output n - (hop + budget).
    const auto lead = static_cast<std::int64_t>(latency_ - crossfade_ - lookahead_);
    for (float& sample : block) {
        dry_[dryPos_] = sample;
        dryPos_ = dryPos_ + 1 == dry_.size() ? 0 : dryPos_ + 1;
        const float delayed = dry_[dryPos_]; // written latency_ samples ago

        const std::int64_t wanted = written_ - lead;
        ++written_;
        float wet = 0.0F;
        bool valid = false;
        if (wanted >= 0) {
            float value = 0.0F;
            while (wetRead_ < wanted && output_->read(std::span<float>(&value, 1)) == 1) {
                ++wetRead_; // stale: it arrived after its time
            }
            if (wetRead_ == wanted && output_->read(std::span<float>(&value, 1)) == 1) {
                ++wetRead_;
                wet = value;
                valid = true;
            } else {
                late_.fetch_add(1, std::memory_order_relaxed);
            }
        }
        wetGain_ =
            valid ? std::min(1.0F, wetGain_ + gainStep_) : std::max(0.0F, wetGain_ - gainStep_);
        mix_ += (mixTarget_ - mix_) * 0.002F;
        const float g = mix_ * wetGain_;
        sample = delayed * (1.0F - g) + wet * g;
    }
}

bool NeuralVoiceNode::runWindow() {
    if (!input_ || input_->availableToRead() < hop_ || output_->availableToWrite() < hop_) {
        return false;
    }
    static_cast<void>(input_->read(fresh_));
    std::copy(history_.begin() + static_cast<std::ptrdiff_t>(hop_), history_.end(),
              history_.begin());
    std::copy(fresh_.begin(), fresh_.end(), history_.end() - static_cast<std::ptrdiff_t>(hop_));

    std::vector<float> out;
    if (const std::shared_ptr<NeuralModel> model = host_->get()) {
        const ModelInfo& info = model->info();
        auto converted = model->run(convert(history_, rate_, info.inputRate),
                                    pitch_.load(std::memory_order_relaxed));
        if (converted) {
            out = convert(converted.value(), info.outputRate, rate_);
        }
    }
    const std::size_t needed = hop_ + crossfade_ + lookahead_;
    if (out.size() < needed) {
        out = history_; // no model, or it failed: the plain voice, same timing
    }
    const std::size_t start = out.size() - needed;
    for (std::size_t t = 0; t < crossfade_; ++t) {
        const float w = (static_cast<float>(t) + 0.5F) / static_cast<float>(crossfade_);
        emit_[t] = tail_[t] * (1.0F - w) + out[start + t] * w;
    }
    std::copy(out.begin() + static_cast<std::ptrdiff_t>(start + crossfade_),
              out.begin() + static_cast<std::ptrdiff_t>(start + hop_),
              emit_.begin() + static_cast<std::ptrdiff_t>(crossfade_));
    std::copy(out.begin() + static_cast<std::ptrdiff_t>(start + hop_),
              out.begin() + static_cast<std::ptrdiff_t>(start + hop_ + crossfade_), tail_.begin());
    static_cast<void>(output_->write(emit_));
    return true;
}

std::size_t NeuralVoiceNode::pump() {
    std::size_t runs = 0;
    while (runWindow()) {
        ++runs;
    }
    return runs;
}

void registerNeuralEffect(plugins::EffectRegistry& registry, std::shared_ptr<ModelHost> host,
                          StreamingConfig config) {
    plugins::ParamSpec mix;
    mix.id = "mix";
    mix.name = "Mix";
    mix.defaultValue = 1.0F;
    plugins::ParamSpec pitch;
    pitch.id = "pitch";
    pitch.name = "Pitch";
    pitch.unit = "st";
    pitch.min = -12.0F;
    pitch.max = 12.0F;
    plugins::EffectDescriptor descriptor{
        "neural",
        "Neural Voice",
        "Neural",
        "Converts your voice with the neural model chosen in Settings (experimental).",
        {mix, pitch}};
    registry.add(std::move(descriptor), [host = std::move(host), config] {
        return std::make_unique<NeuralVoiceNode>(host, config);
    });
}

} // namespace vox::ml
