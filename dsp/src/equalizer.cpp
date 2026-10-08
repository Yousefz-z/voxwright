#include "vox/dsp/equalizer.hpp"

#include <algorithm>
#include <cmath>

namespace vox::dsp {
namespace {

constexpr std::size_t kControlInterval = 32;
constexpr std::array<float, Equalizer::BandCount> kDefaultHz{80.0F,   200.0F,  500.0F,  1500.0F,
                                                             3500.0F, 6000.0F, 12000.0F};

} // namespace

BiquadType Equalizer::typeOf(std::size_t index) noexcept {
    switch (index) {
    case LowCut:
        return BiquadType::Highpass;
    case LowShelf:
        return BiquadType::LowShelf;
    case HighShelf:
        return BiquadType::HighShelf;
    case HighCut:
        return BiquadType::Lowpass;
    default:
        return BiquadType::Peak;
    }
}

void Equalizer::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (std::size_t i = 0; i < BandCount; ++i) {
        BandState& b = bands_[i];
        b.frequency.prepare(sampleRate, 30.0F);
        b.gainDb.prepare(sampleRate, 30.0F);
        b.frequency.setImmediate(kDefaultHz[i]);
        b.gainDb.setImmediate(0.0F);
        b.enabled = (i != LowCut && i != HighCut);
        b.q = (i == LowCut || i == HighCut) ? 0.707F : 0.9F;
        b.dirty = true;
        updateCoefficients(b, i);
    }
    reset();
}

void Equalizer::reset() noexcept {
    for (BandState& b : bands_) {
        b.filter.reset();
    }
}

void Equalizer::setFrequency(Band band, float hz) noexcept {
    bands_[band].frequency.setTarget(std::clamp(hz, 20.0F, 20000.0F));
    bands_[band].dirty = true;
}

void Equalizer::setGainDb(Band band, float db) noexcept {
    bands_[band].gainDb.setTarget(std::clamp(db, -30.0F, 30.0F));
    bands_[band].dirty = true;
}

void Equalizer::setQ(Band band, float q) noexcept {
    bands_[band].q = std::clamp(q, 0.1F, 20.0F);
    bands_[band].dirty = true;
}

void Equalizer::setEnabled(Band band, bool enabled) noexcept {
    bands_[band].enabled = enabled;
}

void Equalizer::updateCoefficients(BandState& band, std::size_t index) const noexcept {
    band.filter.setCoefficients(
        designBiquad(typeOf(index), sampleRate_, static_cast<double>(band.frequency.current()),
                     static_cast<double>(band.q), static_cast<double>(band.gainDb.current())));
    band.dirty = band.frequency.isSmoothing() || band.gainDb.isSmoothing();
}

void Equalizer::process(std::span<float> block) noexcept {
    for (std::size_t pos = 0; pos < block.size(); pos += kControlInterval) {
        const auto chunk = block.subspan(pos, std::min(kControlInterval, block.size() - pos));
        for (std::size_t i = 0; i < BandCount; ++i) {
            BandState& b = bands_[i];
            if (b.dirty) {
                b.frequency.skip(static_cast<int>(chunk.size()));
                b.gainDb.skip(static_cast<int>(chunk.size()));
                updateCoefficients(b, i);
            }
            if (!b.enabled) {
                continue;
            }
            const bool neutral =
                (typeOf(i) == BiquadType::Peak || typeOf(i) == BiquadType::LowShelf ||
                 typeOf(i) == BiquadType::HighShelf) &&
                std::abs(b.gainDb.current()) < 1.0e-3F && !b.dirty;
            if (!neutral) {
                b.filter.process(chunk);
            }
        }
    }
}

double Equalizer::magnitudeDb(double hz) const noexcept {
    double total = 0.0;
    for (const BandState& b : bands_) {
        if (b.enabled) {
            total += biquadMagnitudeDb(b.filter.coefficients(), sampleRate_, hz);
        }
    }
    return total;
}

} // namespace vox::dsp
