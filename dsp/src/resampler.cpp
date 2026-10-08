#include "vox/dsp/resampler.hpp"

#include <samplerate.h>

#include <algorithm>
#include <cmath>

namespace vox::dsp {
namespace {

int converterType(ResamplerQuality quality) {
    switch (quality) {
    case ResamplerQuality::Fast:
        return SRC_SINC_FASTEST;
    case ResamplerQuality::Medium:
        return SRC_SINC_MEDIUM_QUALITY;
    case ResamplerQuality::Best:
        return SRC_SINC_BEST_QUALITY;
    }
    return SRC_SINC_FASTEST;
}

} // namespace

struct StreamResampler::State {
    SRC_STATE* src = nullptr;
    State() = default;
    State(const State&) = delete;
    State& operator=(const State&) = delete;
    State(State&&) = delete;
    State& operator=(State&&) = delete;
    ~State() {
        if (src != nullptr) {
            src_delete(src);
        }
    }
};

StreamResampler::StreamResampler()
    : state_(std::make_unique<State>()) {}
StreamResampler::~StreamResampler() = default;
StreamResampler::StreamResampler(StreamResampler&&) noexcept = default;
StreamResampler& StreamResampler::operator=(StreamResampler&&) noexcept = default;

void StreamResampler::prepare(ResamplerQuality quality, double ratio) {
    if (state_->src != nullptr) {
        src_delete(state_->src);
    }
    int error = 0;
    state_->src = src_new(converterType(quality), 1, &error);
    ratio_ = ratio;
}

void StreamResampler::reset() noexcept {
    if (state_->src != nullptr) {
        src_reset(state_->src);
    }
}

StreamResampler::Counts StreamResampler::process(std::span<const float> input,
                                                 std::span<float> output) noexcept {
    if (state_->src == nullptr || output.empty()) {
        return {};
    }
    SRC_DATA data{};
    data.data_in = input.data();
    data.input_frames = static_cast<long>(input.size());
    data.data_out = output.data();
    data.output_frames = static_cast<long>(output.size());
    data.src_ratio = ratio_;
    data.end_of_input = 0;
    if (src_process(state_->src, &data) != 0) {
        return {};
    }
    return {static_cast<std::size_t>(data.input_frames_used),
            static_cast<std::size_t>(data.output_frames_gen)};
}

std::size_t measureHoldBack(StreamResampler& resampler) {
    constexpr std::size_t kProbe = 4096;
    const std::vector<float> silence(kProbe, 0.0F);
    std::vector<float> out(
        static_cast<std::size_t>(std::ceil(static_cast<double>(kProbe) * resampler.ratio())) + 64);
    std::size_t produced = 0;
    std::size_t consumed = 0;
    while (consumed < kProbe) {
        const auto counts = resampler.process(std::span<const float>(silence).subspan(consumed),
                                              std::span<float>(out).subspan(produced));
        if (counts.consumed == 0 && counts.produced == 0) {
            break;
        }
        consumed += counts.consumed;
        produced += counts.produced;
    }
    resampler.reset();
    const auto expected =
        static_cast<std::size_t>(std::llround(static_cast<double>(kProbe) * resampler.ratio()));
    return expected > produced ? expected - produced : 0;
}

std::vector<float> resample(std::span<const float> input, double fromRate, double toRate,
                            ResamplerQuality quality) {
    if (fromRate == toRate) {
        return {input.begin(), input.end()};
    }
    const double ratio = toRate / fromRate;
    std::vector<float> out(
        static_cast<std::size_t>(std::ceil(static_cast<double>(input.size()) * ratio)) + 256);
    SRC_DATA data{};
    data.data_in = input.data();
    data.input_frames = static_cast<long>(input.size());
    data.data_out = out.data();
    data.output_frames = static_cast<long>(out.size());
    data.src_ratio = ratio;
    data.end_of_input = 1;
    if (src_simple(&data, converterType(quality), 1) != 0) {
        return {};
    }
    out.resize(static_cast<std::size_t>(data.output_frames_gen));
    return out;
}

} // namespace vox::dsp
