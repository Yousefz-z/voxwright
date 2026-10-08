#include "vox/testing/signals.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

namespace vox::testing {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi;

/// Two-pole resonator with unity gain at DC.
class Resonator {
public:
    Resonator(double freqHz, double bandwidthHz, double sampleRate) {
        const double r = std::exp(-std::numbers::pi * bandwidthHz / sampleRate);
        const double c = 2.0 * r * std::cos(kTwoPi * freqHz / sampleRate);
        a1_ = c;
        a2_ = -r * r;
        gain_ = 1.0 - c + r * r;
    }
    double process(double x) {
        const double y = gain_ * x + a1_ * y1_ + a2_ * y2_;
        y2_ = y1_;
        y1_ = y;
        return y;
    }

private:
    double a1_ = 0.0;
    double a2_ = 0.0;
    double gain_ = 1.0;
    double y1_ = 0.0;
    double y2_ = 0.0;
};

/// Rosenberg glottal flow over one period, phase in [0, 1).
double glottalFlow(double phase) {
    constexpr double kOpen = 0.40;
    constexpr double kClose = 0.16;
    if (phase < kOpen) {
        return 0.5 * (1.0 - std::cos(std::numbers::pi * phase / kOpen));
    }
    if (phase < kOpen + kClose) {
        return std::cos(0.5 * std::numbers::pi * (phase - kOpen) / kClose);
    }
    return 0.0;
}

void normalizePeak(std::vector<float>& x, float peak) {
    float maxAbs = 0.0F;
    for (const float v : x) {
        maxAbs = std::max(maxAbs, std::abs(v));
    }
    if (maxAbs > 0.0F) {
        const float g = peak / maxAbs;
        for (float& v : x) {
            v *= g;
        }
    }
}

void applyFades(std::vector<float>& x, std::size_t fadeSamples) {
    const std::size_t n = std::min(fadeSamples, x.size() / 2);
    for (std::size_t i = 0; i < n; ++i) {
        const auto w = static_cast<float>(
            0.5 *
            (1.0 - std::cos(std::numbers::pi * static_cast<double>(i) / static_cast<double>(n))));
        x[i] *= w;
        x[x.size() - 1 - i] *= w;
    }
}

/// Band-passed noise burst standing in for a fricative.
std::vector<float> fricative(double seconds, double lowHz, double highHz, double sampleRate,
                             std::uint32_t seed) {
    const auto n = static_cast<std::size_t>(seconds * sampleRate);
    std::vector<float> out(n);
    std::mt19937 rng(seed);
    std::normal_distribution<double> gauss(0.0, 1.0);
    const double centre = std::sqrt(lowHz * highHz);
    Resonator a(centre, highHz - lowHz, sampleRate);
    Resonator b(centre * 1.15, (highHz - lowHz) * 0.8, sampleRate);
    double prev = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double white = gauss(rng);
        const double hp = white - prev; // tilt toward high frequencies
        prev = white;
        out[i] = static_cast<float>(b.process(a.process(hp)));
    }
    applyFades(out, static_cast<std::size_t>(0.015 * sampleRate));
    return out;
}

} // namespace

std::vector<float> sine(double freqHz, double seconds, double sampleRate, float amplitude) {
    const auto n = static_cast<std::size_t>(seconds * sampleRate);
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = amplitude * static_cast<float>(
                                 std::sin(kTwoPi * freqHz * static_cast<double>(i) / sampleRate));
    }
    return out;
}

std::vector<float> logSweep(double startHz, double endHz, double seconds, double sampleRate,
                            float amplitude) {
    const auto n = static_cast<std::size_t>(seconds * sampleRate);
    std::vector<float> out(n);
    const double k = std::log(endHz / startHz);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double phase = kTwoPi * startHz * seconds / k * (std::exp(t / seconds * k) - 1.0);
        out[i] = amplitude * static_cast<float>(std::sin(phase));
    }
    return out;
}

std::vector<float> whiteNoise(double seconds, double sampleRate, float amplitude,
                              std::uint32_t seed) {
    const auto n = static_cast<std::size_t>(seconds * sampleRate);
    std::vector<float> out(n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> uni(-1.0F, 1.0F);
    for (float& v : out) {
        v = amplitude * uni(rng);
    }
    return out;
}

std::vector<float> silence(double seconds, double sampleRate) {
    return std::vector<float>(static_cast<std::size_t>(seconds * sampleRate), 0.0F);
}

Vowel vowelA() {
    return {{730, 1090, 2440, 3400, 4500}, {90, 110, 160, 200, 250}};
}
Vowel vowelE() {
    return {{530, 1840, 2480, 3500, 4500}, {70, 100, 160, 200, 250}};
}
Vowel vowelI() {
    return {{270, 2290, 3010, 3600, 4500}, {60, 100, 140, 200, 250}};
}
Vowel vowelO() {
    return {{570, 840, 2410, 3400, 4500}, {80, 90, 150, 200, 250}};
}
Vowel vowelU() {
    return {{300, 870, 2240, 3400, 4500}, {60, 90, 140, 200, 250}};
}

std::vector<float> synthVoice(const VoiceSpec& spec, double sampleRate) {
    const auto n = static_cast<std::size_t>(spec.seconds * sampleRate);
    std::vector<float> out(n);
    std::array<Resonator, 5> formants{
        Resonator(spec.vowel.formantsHz[0], spec.vowel.bandwidthsHz[0], sampleRate),
        Resonator(spec.vowel.formantsHz[1], spec.vowel.bandwidthsHz[1], sampleRate),
        Resonator(spec.vowel.formantsHz[2], spec.vowel.bandwidthsHz[2], sampleRate),
        Resonator(spec.vowel.formantsHz[3], spec.vowel.bandwidthsHz[3], sampleRate),
        Resonator(spec.vowel.formantsHz[4], spec.vowel.bandwidthsHz[4], sampleRate)};
    std::mt19937 rng(spec.seed);
    std::normal_distribution<double> gauss(0.0, 1.0);

    const double endF0 = spec.f0EndHz > 0.0 ? spec.f0EndHz : spec.f0Hz;
    double phase = 0.0;
    double periodScale = 1.0;
    double prevFlow = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double glide = spec.f0Hz * std::pow(endF0 / spec.f0Hz, t / spec.seconds);
        const double vib =
            std::exp2(spec.vibratoCents / 1200.0 * std::sin(kTwoPi * spec.vibratoHz * t));
        const double f0 = glide * vib / periodScale;
        phase += f0 / sampleRate;
        if (phase >= 1.0) {
            phase -= 1.0;
            periodScale = 1.0 + spec.jitter * gauss(rng);
        }
        const double flow = glottalFlow(phase);
        double excitation = flow - prevFlow; // lip radiation: differentiated flow
        prevFlow = flow;
        excitation += spec.breathiness * 0.02 * flow * gauss(rng);
        double y = excitation;
        for (Resonator& r : formants) {
            y = r.process(y);
        }
        out[i] = static_cast<float>(y);
    }
    normalizePeak(out, spec.peak);
    applyFades(out, static_cast<std::size_t>(0.02 * sampleRate));
    return out;
}

Phrase synthPhrase(double baseF0Hz, double sampleRate, std::uint32_t seed) {
    struct Segment {
        enum class Kind { Vowel, Fricative, Pause } kind;
        double seconds;
        Vowel vowel;
        double f0Start;
        double f0End;
        double lowHz;
        double highHz;
    };
    const std::array<Segment, 9> segments{{
        {Segment::Kind::Pause, 0.15, {}, 0, 0, 0, 0},
        {Segment::Kind::Vowel, 0.35, vowelA(), 1.00, 1.15, 0, 0},
        {Segment::Kind::Fricative, 0.12, {}, 0, 0, 4500, 8000},
        {Segment::Kind::Vowel, 0.30, vowelI(), 1.10, 0.95, 0, 0},
        {Segment::Kind::Pause, 0.10, {}, 0, 0, 0, 0},
        {Segment::Kind::Vowel, 0.30, vowelO(), 0.90, 1.00, 0, 0},
        {Segment::Kind::Fricative, 0.10, {}, 0, 0, 2500, 5000},
        {Segment::Kind::Vowel, 0.35, vowelU(), 1.00, 0.85, 0, 0},
        {Segment::Kind::Pause, 0.15, {}, 0, 0, 0, 0},
    }};
    Phrase phrase;
    std::uint32_t s = seed;
    for (const Segment& seg : segments) {
        const auto n = static_cast<std::size_t>(seg.seconds * sampleRate);
        std::vector<float> audio;
        std::vector<float> f0(n, 0.0F);
        if (seg.kind == Segment::Kind::Vowel) {
            VoiceSpec spec;
            spec.f0Hz = baseF0Hz * seg.f0Start;
            spec.f0EndHz = baseF0Hz * seg.f0End;
            spec.vowel = seg.vowel;
            spec.seconds = seg.seconds;
            spec.jitter = 0.003;
            spec.breathiness = 0.2;
            spec.peak = 0.5F;
            spec.seed = ++s;
            audio = synthVoice(spec, sampleRate);
            for (std::size_t i = 0; i < n; ++i) {
                const double t = static_cast<double>(i) / sampleRate;
                f0[i] = static_cast<float>(spec.f0Hz *
                                           std::pow(spec.f0EndHz / spec.f0Hz, t / spec.seconds));
            }
        } else if (seg.kind == Segment::Kind::Fricative) {
            audio = fricative(seg.seconds, seg.lowHz, seg.highHz, sampleRate, ++s);
            normalizePeak(audio, 0.15F);
        } else {
            audio = silence(seg.seconds, sampleRate);
        }
        audio.resize(n, 0.0F);
        phrase.audio.insert(phrase.audio.end(), audio.begin(), audio.end());
        phrase.f0.insert(phrase.f0.end(), f0.begin(), f0.end());
    }
    return phrase;
}

} // namespace vox::testing
