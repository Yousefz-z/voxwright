#include "vox/dsp/spectral_shifter.hpp"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace vox::dsp {

struct SpectralShifter::Impl {
    // Fixed seed so renders are reproducible in tests.
    signalsmith::stretch::SignalsmithStretch<float> stretch{0x5EEDL};
};

SpectralShifter::SpectralShifter()
    : impl_(std::make_unique<Impl>()) {}
SpectralShifter::~SpectralShifter() = default;
SpectralShifter::SpectralShifter(SpectralShifter&&) noexcept = default;
SpectralShifter& SpectralShifter::operator=(SpectralShifter&&) noexcept = default;

void SpectralShifter::prepare(const Config& config) {
    double blockSeconds = 0.03;
    switch (config.quality) {
    case Quality::LowLatency:
        blockSeconds = 0.02;
        break;
    case Quality::Balanced:
        blockSeconds = 0.03;
        break;
    case Quality::High:
        blockSeconds = 0.0427;
        break;
    case Quality::Studio:
        blockSeconds = 0.12;
        break;
    }
    const auto block = static_cast<int>(std::lround(blockSeconds * config.sampleRate));
    impl_->stretch.configure(1, block, block / 4);
    scratch_.assign(config.maxBlockSize, 0.0F);
    applyFormants();
    reset();
}

void SpectralShifter::reset() noexcept {
    impl_->stretch.reset();
}

void SpectralShifter::setPitchSemitones(float semitones) noexcept {
    impl_->stretch.setTransposeSemitones(std::clamp(semitones, -24.0F, 24.0F));
}

void SpectralShifter::setFormantSemitones(float semitones) noexcept {
    formantSemitones_ = std::clamp(semitones, -12.0F, 12.0F);
    applyFormants();
}

void SpectralShifter::setPreserveFormants(bool preserve) noexcept {
    preserveFormants_ = preserve;
    applyFormants();
}

void SpectralShifter::applyFormants() noexcept {
    impl_->stretch.setFormantSemitones(formantSemitones_, preserveFormants_);
}

void SpectralShifter::process(std::span<float> block) noexcept {
    const std::size_t chunkSize = scratch_.size();
    for (std::size_t pos = 0; pos < block.size(); pos += chunkSize) {
        const std::size_t n = std::min(chunkSize, block.size() - pos);
        std::copy_n(block.begin() + static_cast<std::ptrdiff_t>(pos), n, scratch_.begin());
        std::array<float*, 1> in{scratch_.data()};
        std::array<float*, 1> out{block.data() + pos};
        impl_->stretch.process(in, static_cast<int>(n), out, static_cast<int>(n));
    }
}

std::size_t SpectralShifter::latencySamples() const noexcept {
    return static_cast<std::size_t>(impl_->stretch.inputLatency()) +
           static_cast<std::size_t>(impl_->stretch.outputLatency());
}

} // namespace vox::dsp
