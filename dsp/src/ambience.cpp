#include "vox/dsp/ambience.hpp"

#include "vox/dsp/math.hpp"

#include <algorithm>
#include <cmath>

namespace vox::dsp {

void Ambience::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    for (auto& f : filters_) {
        f.prepare(sampleRate);
    }
    for (auto& l : lfos_) {
        l.prepare(sampleRate);
    }
    for (auto& o : oscillators_) {
        o.prepare(sampleRate);
    }
    level_.prepare(sampleRate, 50.0F);
    level_.setImmediate(-18.0F);
    enabled_.prepare(sampleRate, 200.0F);
    enabled_.setImmediate(enabledFlag_ ? 1.0F : 0.0F);
    lowCut_.setCoefficients(designBiquad(BiquadType::Highpass, sampleRate, 30.0, 0.707));
    setToneHz(8000.0F);
    setKind(kind_);
    reset();
}

void Ambience::reset() noexcept {
    for (auto& f : filters_) {
        f.reset();
    }
    for (std::size_t i = 0; i < lfos_.size(); ++i) {
        lfos_[i].reset(0.31 * static_cast<double>(i));
    }
    for (auto& o : oscillators_) {
        o.reset();
    }
    for (auto& p : pings_) {
        p = Ping{};
    }
    tone_.reset();
    lowCut_.reset();
    envelope_ = 0.0F;
    burst_ = 0.0F;
    counter_ = 0;
}

void Ambience::setToneHz(float hz) noexcept {
    tone_.setCoefficients(designBiquad(BiquadType::Lowpass, sampleRate_,
                                       static_cast<double>(std::clamp(hz, 300.0F, 20000.0F)),
                                       0.707));
}

void Ambience::setKind(Kind kind) noexcept {
    kind_ = kind;
    // Per-kind modulation rates and oscillator setup.
    for (auto& l : lfos_) {
        l.setShape(LfoShape::SmoothRandom);
    }
    switch (kind) {
    case Kind::Wind:
        lfos_[0].setFrequency(0.15F);
        lfos_[1].setFrequency(0.4F);
        break;
    case Kind::Crowd:
        lfos_[0].setFrequency(4.1F);
        lfos_[1].setFrequency(5.3F);
        lfos_[2].setFrequency(3.2F);
        lfos_[3].setFrequency(0.2F);
        break;
    case Kind::EngineHum:
        oscillators_[0].setWaveform(Waveform::Saw);
        oscillators_[0].setFrequency(50.0F);
        oscillators_[1].setWaveform(Waveform::Square);
        oscillators_[1].setFrequency(100.3F);
        lfos_[0].setFrequency(0.3F);
        break;
    case Kind::SpaceDrone:
        oscillators_[0].setWaveform(Waveform::Saw);
        oscillators_[0].setFrequency(55.0F);
        oscillators_[1].setWaveform(Waveform::Saw);
        oscillators_[1].setFrequency(55.4F);
        oscillators_[2].setWaveform(Waveform::Sine);
        oscillators_[2].setFrequency(82.4F);
        lfos_[0].setShape(LfoShape::Sine);
        lfos_[0].setFrequency(0.05F);
        break;
    case Kind::RadioStatic:
        lfos_[0].setFrequency(0.7F);
        break;
    case Kind::Traffic:
        lfos_[0].setFrequency(0.08F);
        lfos_[1].setFrequency(0.13F);
        break;
    default:
        lfos_[0].setFrequency(0.3F);
        break;
    }
}

bool Ambience::chance(float perSecond) noexcept {
    return random_.nextUnipolar() < perSecond / static_cast<float>(sampleRate_);
}

float Ambience::triggerPing(float minHz, float maxHz, float decayMs, float glide,
                            float amplitude) noexcept {
    for (Ping& p : pings_) {
        if (p.amplitude < 1.0e-4F) {
            const float hz = minHz * std::pow(maxHz / minHz, random_.nextUnipolar());
            p.phase = 0.0F;
            p.increment = hz / static_cast<float>(sampleRate_);
            p.glide = glide;
            p.amplitude = amplitude * (0.5F + 0.5F * random_.nextUnipolar());
            p.decay = onePoleCoefficient(decayMs, sampleRate_);
            return hz;
        }
    }
    return 0.0F;
}

float Ambience::pings() noexcept {
    float out = 0.0F;
    for (Ping& p : pings_) {
        if (p.amplitude < 1.0e-4F) {
            continue;
        }
        out += p.amplitude * std::sin(kTwoPi * p.phase);
        p.phase += p.increment;
        p.phase -= std::floor(p.phase);
        p.increment *= p.glide;
        p.amplitude *= p.decay;
    }
    return out;
}

float Ambience::generate() noexcept {
    const bool update = counter_-- <= 0;
    if (update) {
        counter_ = 16;
    }
    float x = 0.0F;
    switch (kind_) {
    case Kind::Rain: {
        if (update) {
            filters_[0].setParameters(3500.0F, 0.5F);
        }
        x = filters_[0].processSample(pink_.next()).low * 0.8F;
        if (chance(40.0F)) {
            static_cast<void>(triggerPing(2500.0F, 6000.0F, 6.0F, 0.9995F, 0.25F));
        }
        x += pings();
        break;
    }
    case Kind::Wind: {
        const float gust = 0.5F + 0.5F * lfos_[0].next();
        const float swirl = lfos_[1].next();
        if (update) {
            filters_[0].setParameters(250.0F + 900.0F * gust + 150.0F * swirl, 2.5F);
            filters_[1].setParameters(1800.0F + 600.0F * swirl, 6.0F);
        }
        const float n = pink_.next();
        x = (filters_[0].processSample(n).band * 1.6F + filters_[1].processSample(n).band * 0.3F) *
            (0.3F + 0.7F * gust);
        break;
    }
    case Kind::Crowd: {
        // Several "talkers": noise through vowel-like formants, syllable-rate gating.
        if (update) {
            filters_[0].setParameters(550.0F, 6.0F);
            filters_[1].setParameters(1200.0F, 7.0F);
            filters_[2].setParameters(2400.0F, 8.0F);
            filters_[3].setParameters(800.0F, 5.0F);
        }
        const float n = pink_.next();
        for (std::size_t i = 0; i < 4; ++i) {
            const float gate = std::max(0.0F, lfos_[i].next());
            x += filters_[i].processSample(n).band * gate;
        }
        x *= 1.4F;
        break;
    }
    case Kind::EngineHum: {
        const float wobble = 1.0F + 0.01F * lfos_[0].next();
        oscillators_[0].setFrequency(50.0F * wobble);
        if (update) {
            filters_[0].setParameters(400.0F, 0.8F);
        }
        x = filters_[0]
                .processSample(oscillators_[0].next() * 0.5F + oscillators_[1].next() * 0.2F +
                               brown_.next() * 0.4F)
                .low;
        break;
    }
    case Kind::RadioStatic: {
        if (update) {
            filters_[0].setParameters(1500.0F, 0.6F);
        }
        float n = random_.nextBipolar() * (0.25F + 0.15F * lfos_[0].next());
        if (chance(25.0F)) {
            burst_ = 1.0F;
        }
        burst_ *= 0.995F;
        n += random_.nextBipolar() * burst_ * 0.8F;
        x = filters_[0].processSample(n).band * 1.2F;
        break;
    }
    case Kind::SpaceDrone: {
        const float sweep = 0.5F + 0.5F * lfos_[0].next();
        if (update) {
            filters_[0].setParameters(200.0F + 1200.0F * sweep, 4.0F);
        }
        const float tones = oscillators_[0].next() * 0.3F + oscillators_[1].next() * 0.3F +
                            oscillators_[2].next() * 0.25F;
        x = filters_[0].processSample(tones + pink_.next() * 0.2F).low;
        break;
    }
    case Kind::CaveDrips: {
        if (chance(1.2F)) {
            static_cast<void>(triggerPing(900.0F, 2600.0F, 70.0F, 1.00003F, 0.5F));
        }
        if (update) {
            filters_[0].setParameters(120.0F, 0.7F);
        }
        x = pings() + filters_[0].processSample(brown_.next()).low * 0.15F;
        break;
    }
    case Kind::Underwater: {
        if (chance(6.0F)) {
            static_cast<void>(triggerPing(250.0F, 700.0F, 40.0F, 1.00015F, 0.35F));
        }
        if (update) {
            filters_[0].setParameters(300.0F, 1.2F);
        }
        x = filters_[0].processSample(brown_.next()).low * 0.9F + pings();
        break;
    }
    case Kind::Fire: {
        if (update) {
            filters_[0].setParameters(500.0F, 0.6F);
            filters_[1].setParameters(3000.0F, 0.7F);
        }
        if (chance(18.0F)) {
            burst_ = 0.6F + 0.4F * random_.nextUnipolar();
        }
        burst_ *= 0.992F;
        x = filters_[0].processSample(brown_.next()).low * 0.6F +
            filters_[1].processSample(random_.nextBipolar()).high * burst_;
        break;
    }
    case Kind::Traffic: {
        const float passA = std::max(0.0F, lfos_[0].next());
        const float passB = std::max(0.0F, lfos_[1].next());
        if (update) {
            filters_[0].setParameters(180.0F, 0.7F);
            filters_[1].setParameters(400.0F + 1400.0F * passA, 1.5F);
        }
        const float rumble = filters_[0].processSample(brown_.next()).low;
        const float whoosh = filters_[1].processSample(pink_.next()).band * (passA + passB);
        x = rumble * 0.7F + whoosh * 0.9F;
        break;
    }
    case Kind::Computer: {
        if (chance(7.0F)) {
            constexpr std::array<float, 6> kNotes{880.0F,  988.0F,  1175.0F,
                                                  1319.0F, 1568.0F, 1760.0F};
            const auto idx = static_cast<std::size_t>(random_.nextUnipolar() * 5.99F);
            oscillators_[0].setWaveform(Waveform::Square);
            oscillators_[0].setFrequency(kNotes[idx]);
            envelope_ = 0.25F;
        }
        envelope_ *= 0.9993F;
        if (update) {
            filters_[0].setParameters(90.0F, 0.7F);
        }
        x = oscillators_[0].next() * envelope_ +
            filters_[0].processSample(brown_.next()).low * 0.1F;
        break;
    }
    }
    return lowCut_.processSample(tone_.processSample(x));
}

void Ambience::process(std::span<float> block) noexcept {
    for (float& sample : block) {
        const float on = enabled_.next();
        const float gain = dbToGain(level_.next()) * on;
        const float bed = on > 0.0F ? generate() : 0.0F;
        sample += bed * gain;
    }
}

// ---------------------------------------------------------------- Stutter

void Stutter::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    fade_ = static_cast<std::size_t>(0.002 * sampleRate);
    buffer_.assign(kBufferSize, 0.0F);
    setSliceMs(90.0F);
    reset();
}

void Stutter::reset() noexcept {
    std::fill(buffer_.begin(), buffer_.end(), 0.0F);
    written_ = 0;
    sinceBoundary_ = 0;
    repeatsLeft_ = 0;
    repeatOffset_ = 0;
    liveGain_ = 1.0F;
}

void Stutter::setSliceMs(float ms) noexcept {
    slice_ = std::clamp<std::size_t>(
        static_cast<std::size_t>(static_cast<double>(ms) * 0.001 * sampleRate_), 4 * fade_,
        kBufferSize / 4);
}

void Stutter::process(std::span<float> block) noexcept {
    constexpr std::size_t kMask = kBufferSize - 1;
    for (float& sample : block) {
        buffer_[written_ & kMask] = sample;
        ++written_;
        if (++sinceBoundary_ >= slice_) {
            sinceBoundary_ = 0;
            if (repeatsLeft_ > 0) {
                --repeatsLeft_;
                repeatOffset_ = 0;
            } else if (written_ > 2 * slice_ && random_.nextUnipolar() < probability_) {
                repeatsLeft_ =
                    1 + static_cast<int>(random_.nextUnipolar() * static_cast<float>(maxRepeats_));
                repeatStart_ = written_ - slice_;
                repeatOffset_ = 0;
            }
        }
        float out = sample;
        if (repeatsLeft_ > 0) {
            const float replay = buffer_[(repeatStart_ + repeatOffset_) & kMask];
            // Short fades at both ends of every repeated slice.
            const std::size_t fromEnd = slice_ - repeatOffset_;
            const float w =
                std::min({1.0F, static_cast<float>(repeatOffset_) / static_cast<float>(fade_),
                          static_cast<float>(fromEnd) / static_cast<float>(fade_)});
            out = replay * w;
            ++repeatOffset_;
            liveGain_ = 0.0F;
        } else if (liveGain_ < 1.0F) {
            liveGain_ = std::min(1.0F, liveGain_ + 1.0F / static_cast<float>(fade_));
            out = sample * liveGain_;
        }
        sample = out;
    }
}

} // namespace vox::dsp
