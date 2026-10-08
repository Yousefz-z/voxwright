#include "vox/dsp/vocoder.hpp"

#include "vox/dsp/math.hpp"

#include <algorithm>
#include <cmath>

namespace vox::dsp {

// ---------------------------------------------------------------- ChannelVocoder

void ChannelVocoder::prepare(double sampleRate, std::size_t /*maxBlockSize*/) {
    sampleRate_ = sampleRate;
    for (auto& f : analysis_) {
        f.prepare(sampleRate);
    }
    for (auto& f : synthesis_) {
        f.prepare(sampleRate);
    }
    for (auto& e : envelopes_) {
        e.prepare(sampleRate);
        e.setTimes(2.0F, 25.0F);
    }
    for (auto& o : oscillators_) {
        o.prepare(sampleRate);
    }
    tracker_.prepare({sampleRate, 70.0F, 700.0F});
    frequency_.setCutoff(sampleRate, 30.0);
    sibilanceFilter_.setCoefficients(designBiquad(BiquadType::Highpass, sampleRate, 5000.0, 0.707));
    mix_.prepare(sampleRate, 30.0F);
    mix_.setImmediate(1.0F);
    updateBands();
    reset();
}

void ChannelVocoder::reset() noexcept {
    for (auto& f : analysis_) {
        f.reset();
    }
    for (auto& f : synthesis_) {
        f.reset();
    }
    for (auto& e : envelopes_) {
        e.reset();
    }
    for (auto& o : oscillators_) {
        o.reset();
    }
    tracker_.reset();
    frequency_.reset(carrierHz_);
    sibilanceFilter_.reset();
}

void ChannelVocoder::setBands(int bands) noexcept {
    bands_ = std::clamp(bands, 4, kMaxBands);
    updateBands();
}

void ChannelVocoder::setFollowPitch(bool follow, float semitones) noexcept {
    followPitch_ = follow;
    followRatio_ = semitonesToRatio(semitones);
}

void ChannelVocoder::setFormantSemitones(float semitones) noexcept {
    formantRatio_ = semitonesToRatio(std::clamp(semitones, -12.0F, 12.0F));
    updateBands();
}

void ChannelVocoder::updateBands() noexcept {
    constexpr float kLow = 120.0F;
    constexpr float kHigh = 7500.0F;
    const float ratio = std::pow(kHigh / kLow, 1.0F / static_cast<float>(bands_ - 1));
    // Q so that neighbouring bands cross near -3 dB.
    const float q = std::sqrt(ratio) / (ratio - 1.0F);
    for (int b = 0; b < bands_; ++b) {
        const float centre = kLow * std::pow(ratio, static_cast<float>(b));
        analysis_[static_cast<std::size_t>(b)].setParameters(centre, q);
        synthesis_[static_cast<std::size_t>(b)].setParameters(
            std::min(centre * formantRatio_, 0.45F * static_cast<float>(sampleRate_)), q);
    }
    // Rough loudness normalization: more, narrower bands pass less energy each.
    gain_ = 2.5F * std::sqrt(static_cast<float>(bands_) / 20.0F);
}

float ChannelVocoder::nextCarrier() noexcept {
    switch (carrier_) {
    case Carrier::Noise:
        return noise_.nextBipolar();
    case Carrier::Chord:
        return (oscillators_[0].next() + oscillators_[1].next() + oscillators_[2].next()) * 0.45F;
    case Carrier::Saw:
    case Carrier::Square:
    case Carrier::Pulse:
        return oscillators_[0].next();
    }
    return 0.0F;
}

void ChannelVocoder::process(std::span<float> block) noexcept {
    if (followPitch_) {
        tracker_.push(block);
    }
    Waveform wave = Waveform::Saw;
    if (carrier_ == Carrier::Square) {
        wave = Waveform::Square;
    } else if (carrier_ == Carrier::Pulse) {
        wave = Waveform::Pulse25;
    }
    const float target =
        followPitch_ ? static_cast<float>(tracker_.frequencyHz()) * followRatio_ : carrierHz_;
    for (auto& o : oscillators_) {
        o.setWaveform(wave);
    }
    for (float& sample : block) {
        const float f = frequency_.processSample(target);
        oscillators_[0].setFrequency(f);
        oscillators_[1].setFrequency(f * 1.25992F); // major third
        oscillators_[2].setFrequency(f * 1.49831F); // fifth
        const float carrier = nextCarrier();
        float wet = 0.0F;
        for (int b = 0; b < bands_; ++b) {
            const auto i = static_cast<std::size_t>(b);
            const float band = analysis_[i].processBandNormalized(sample);
            const float env = envelopes_[i].processSample(band);
            wet += synthesis_[i].processBandNormalized(carrier) * env;
        }
        wet = wet * gain_ + sibilance_ * sibilanceFilter_.processSample(sample);
        const float m = mix_.next();
        sample = sample + m * (wet - sample);
    }
}

// ---------------------------------------------------------------- Harmonizer

void Harmonizer::prepare(double sampleRate, std::size_t maxBlockSize) {
    maxBlock_ = maxBlockSize;
    analyzer_.prepare({sampleRate, 75.0F, 800.0F, maxBlockSize});
    for (auto& v : voices_) {
        v.voice.prepare(analyzer_);
        v.gainDb.prepare(sampleRate, 30.0F);
        v.gainDb.setImmediate(-3.0F);
    }
    dryDelay_.assign(static_cast<std::size_t>(voices_[0].voice.latency()), 0.0F);
    scratch_.assign(maxBlockSize, 0.0F);
    dry_.prepare(sampleRate, 30.0F);
    dry_.setImmediate(0.0F);
    reset();
}

void Harmonizer::reset() noexcept {
    analyzer_.reset();
    for (auto& v : voices_) {
        v.voice.reset();
    }
    std::fill(dryDelay_.begin(), dryDelay_.end(), 0.0F);
    dryPos_ = 0;
}

void Harmonizer::setVoice(int index, bool enabled, float semitones, float gainDb,
                          float formantSemitones) noexcept {
    if (index < 0 || index >= kMaxVoices) {
        return;
    }
    VoiceSlot& v = voices_[static_cast<std::size_t>(index)];
    v.enabled = enabled;
    v.voice.setPitchRatio(semitonesToRatio(semitones));
    v.voice.setFormantRatio(semitonesToRatio(formantSemitones));
    v.gainDb.setTarget(gainDb);
}

std::size_t Harmonizer::latencySamples() const noexcept {
    return static_cast<std::size_t>(voices_[0].voice.latency());
}

void Harmonizer::process(std::span<float> block) noexcept {
    for (std::size_t pos = 0; pos < block.size(); pos += maxBlock_) {
        const auto chunk = block.subspan(pos, std::min(maxBlock_, block.size() - pos));
        analyzer_.write(chunk);
        const auto latency = static_cast<std::int64_t>(latencySamples());
        const std::int64_t start =
            analyzer_.written() - static_cast<std::int64_t>(chunk.size()) - latency;
        const auto horizon = static_cast<double>(start + static_cast<std::int64_t>(chunk.size())) +
                             analyzer_.maxPeriod();
        // Dry path through a delay equal to the PSOLA latency.
        auto out = std::span<float>(scratch_).first(chunk.size());
        for (std::size_t i = 0; i < chunk.size(); ++i) {
            const float delayed = dryDelay_[dryPos_];
            dryDelay_[dryPos_] = chunk[i];
            dryPos_ = (dryPos_ + 1) % dryDelay_.size();
            out[i] = delayed * dbToGain(dry_.next());
        }
        for (auto& v : voices_) {
            v.voice.render(analyzer_, horizon);
            const float gain =
                v.enabled ? dbToGain(v.gainDb.skip(static_cast<int>(chunk.size()))) : 0.0F;
            v.voice.emit(out, start, gain, true);
        }
        std::copy(out.begin(), out.end(), chunk.begin());
    }
}

} // namespace vox::dsp
