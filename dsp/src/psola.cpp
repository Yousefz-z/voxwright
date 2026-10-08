#include "vox/dsp/psola.hpp"

#include "vox/dsp/math.hpp"

#include <algorithm>
#include <cmath>

namespace vox::dsp {
namespace {

std::size_t nextPowerOfTwo(std::size_t n) {
    std::size_t p = 1;
    while (p < n) {
        p <<= 1U;
    }
    return p;
}

constexpr double kMinCorrelation = 0.3;
constexpr double kSearchRadius = 0.15;    // of a period, either side of the prediction
constexpr double kCorrelationHalf = 0.25; // half-length of the compared segments
constexpr double kUnvoicedSeconds = 0.005;

} // namespace

// ---------------------------------------------------------------------------
// PsolaAnalyzer

void PsolaAnalyzer::prepare(const Config& config) {
    config_ = config;
    tracker_.prepare({config.sampleRate, config.minHz, config.maxHz});
    maxPeriod_ = config.sampleRate / static_cast<double>(config.minHz);
    minPeriod_ = config.sampleRate / static_cast<double>(config.maxHz);
    unvoicedPeriod_ = std::round(kUnvoicedSeconds * config.sampleRate);
    const auto needed = static_cast<std::size_t>(12.0 * maxPeriod_) + 2 * config.maxBlockSize + 64;
    history_.assign(nextPowerOfTwo(needed), 0.0F);
    historyMask_ = history_.size() - 1;
    reset();
}

void PsolaAnalyzer::reset() noexcept {
    tracker_.reset();
    std::fill(history_.begin(), history_.end(), 0.0F);
    written_ = 0;
    markHead_ = 0;
    markCount_ = 0;
    lastMark_ = 0.0;
    lastVoiced_ = false;
}

void PsolaAnalyzer::write(std::span<const float> input) noexcept {
    for (const float x : input) {
        history_[static_cast<std::size_t>(written_) & historyMask_] = x;
        ++written_;
    }
    tracker_.push(input);
    placeMarks();
}

float PsolaAnalyzer::sampleAt(double position) const noexcept {
    const double floorPos = std::floor(position);
    const auto i = static_cast<std::int64_t>(floorPos);
    const auto t = static_cast<float>(position - floorPos);
    return hermite(at(i - 1), at(i), at(i + 1), at(i + 2), t);
}

void PsolaAnalyzer::placeMarks() noexcept {
    const auto last = static_cast<double>(written_ - 1);
    for (;;) {
        const bool voiced = tracker_.voiced();
        const double period =
            voiced ? std::clamp(tracker_.period(), minPeriod_, maxPeriod_) : unvoicedPeriod_;
        double position = 0.0;
        if (voiced && lastVoiced_) {
            const double predicted = lastMark_ + period;
            const int radius = static_cast<int>(std::lround(kSearchRadius * period));
            const int half = static_cast<int>(std::lround(kCorrelationHalf * period));
            if (predicted + radius + half + 3.0 > last) {
                break;
            }
            position = correlate(lastMark_, predicted, radius, half);
        } else if (voiced) {
            const double from = lastMark_ + 1.0;
            if (from + period + 3.0 > last) {
                break;
            }
            position = onsetPeak(from, period);
        } else {
            position = lastMark_ + unvoicedPeriod_;
            if (position + 3.0 > last) {
                break;
            }
        }
        position = std::max(position, lastMark_ + 1.0);
        marks_[markHead_] = Mark{position, period, voiced};
        markHead_ = (markHead_ + 1) % kMarkCapacity;
        markCount_ = std::min(markCount_ + 1, kMarkCapacity);
        lastMark_ = position;
        lastVoiced_ = voiced;
    }
}

double PsolaAnalyzer::correlate(double from, double predicted, int radius,
                                int half) const noexcept {
    const auto a = static_cast<std::int64_t>(std::lround(from));
    const auto p = static_cast<std::int64_t>(std::lround(predicted));
    double energyA = 0.0;
    for (int k = -half; k <= half; ++k) {
        const auto v = static_cast<double>(at(a + k));
        energyA += v * v;
    }
    double best = -2.0;
    int bestOffset = 0;
    std::array<double, 3> around{0.0, 0.0, 0.0};
    double previous = -2.0;
    for (int d = -radius; d <= radius; ++d) {
        double cross = 0.0;
        double energyB = 0.0;
        for (int k = -half; k <= half; ++k) {
            const auto va = static_cast<double>(at(a + k));
            const auto vb = static_cast<double>(at(p + d + k));
            cross += va * vb;
            energyB += vb * vb;
        }
        const double denom = std::sqrt(energyA * energyB);
        const double c = denom > 1e-12 ? cross / denom : 0.0;
        if (c > best) {
            best = c;
            bestOffset = d;
            around[0] = previous;
            around[1] = c;
            around[2] = -2.0; // filled on the next iteration if any
        } else if (d == bestOffset + 1) {
            around[2] = c;
        }
        previous = c;
    }
    if (best < kMinCorrelation) {
        return predicted;
    }
    double shift = 0.0;
    if (around[0] > -2.0 && around[2] > -2.0) {
        const double denom = around[0] - 2.0 * around[1] + around[2];
        if (std::abs(denom) > 1e-12) {
            shift = std::clamp(0.5 * (around[0] - around[2]) / denom, -0.5, 0.5);
        }
    }
    return static_cast<double>(p + bestOffset) + shift;
}

double PsolaAnalyzer::onsetPeak(double from, double period) const noexcept {
    const auto start = static_cast<std::int64_t>(std::ceil(from));
    const auto end = static_cast<std::int64_t>(std::floor(from + period));
    std::int64_t best = start;
    float bestValue = -1.0F;
    for (std::int64_t i = start; i <= end; ++i) {
        const float v = std::abs(at(i));
        if (v > bestValue) {
            bestValue = v;
            best = i;
        }
    }
    return static_cast<double>(best);
}

bool PsolaAnalyzer::nearestAvailableMark(double position, Mark& out) const noexcept {
    const auto limit = static_cast<double>(written_) - 3.0;
    bool found = false;
    double bestDistance = 0.0;
    for (std::size_t n = 0; n < markCount_; ++n) {
        const Mark& m = marks_[(markHead_ + kMarkCapacity - 1 - n) % kMarkCapacity];
        if (m.position + m.period > limit) {
            continue; // grain not fully written yet
        }
        const double distance = std::abs(m.position - position);
        if (!found || distance < bestDistance) {
            found = true;
            bestDistance = distance;
            out = m;
        } else if (m.position < position) {
            break; // marks are sorted; older ones are only farther away
        }
    }
    return found;
}

// ---------------------------------------------------------------------------
// PsolaVoice

void PsolaVoice::prepare(const PsolaAnalyzer& analyzer) {
    sampleRate_ = analyzer.sampleRate();
    latency_ = static_cast<std::int64_t>(std::ceil(2.25 * analyzer.maxPeriod())) + 8;
    const auto size = nextPowerOfTwo(static_cast<std::size_t>(latency_) +
                                     static_cast<std::size_t>(8.0 * analyzer.maxPeriod()) + 8192);
    accumulator_.assign(size, 0.0F);
    mask_ = size - 1;
    reset();
}

void PsolaVoice::reset() noexcept {
    std::fill(accumulator_.begin(), accumulator_.end(), 0.0F);
    synthesisPosition_ = 0.0;
    emitted_ = -latency_; // the first emitted block starts one latency before input 0
    lateSamples_ = 0;
    currentNote_ = -1.0;
}

void PsolaVoice::setPitchRatio(float ratio) noexcept {
    pitchRatio_ = std::clamp(ratio, 0.25F, 4.0F);
}

void PsolaVoice::setFormantRatio(float ratio) noexcept {
    formantRatio_ = std::clamp(ratio, 0.5F, 2.0F);
}

void PsolaVoice::setScale(int key, std::uint16_t mask, float retuneMs) noexcept {
    key_ = ((key % 12) + 12) % 12;
    scaleMask_ =
        (mask & 0x0FFFU) == 0 ? std::uint16_t{0x0FFF} : static_cast<std::uint16_t>(mask & 0x0FFFU);
    retuneMs_ = std::max(retuneMs, 0.0F);
}

double PsolaVoice::targetRatio(double inputPeriod, double spacingHint) noexcept {
    const double inputHz = sampleRate_ / inputPeriod;
    double targetHz = inputHz * static_cast<double>(pitchRatio_);
    switch (mode_) {
    case PitchMode::Ratio:
        break;
    case PitchMode::Fixed:
        targetHz = static_cast<double>(fixedHz_) * static_cast<double>(pitchRatio_);
        break;
    case PitchMode::Quantize: {
        const double note = 69.0 + 12.0 * std::log2(inputHz / 440.0);
        const double rounded = std::round(note);
        double snapped = rounded;
        double bestDistance = 1e9;
        for (int offset = -6; offset <= 6; ++offset) {
            const double candidate = rounded + offset;
            const int pitchClass = ((static_cast<int>(candidate) - key_) % 12 + 12) % 12;
            if ((scaleMask_ >> static_cast<unsigned>(pitchClass) & 1U) == 0U) {
                continue;
            }
            const double distance = std::abs(candidate - note);
            if (distance < bestDistance) {
                bestDistance = distance;
                snapped = candidate;
            }
        }
        if (currentNote_ < 0.0 || retuneMs_ <= 0.0F) {
            currentNote_ = snapped;
        } else {
            const double tau = static_cast<double>(retuneMs_) * 0.001 * sampleRate_;
            const double a = std::exp(-spacingHint / tau);
            currentNote_ = snapped + a * (currentNote_ - snapped);
        }
        targetHz =
            440.0 * std::exp2((currentNote_ - 69.0) / 12.0) * static_cast<double>(pitchRatio_);
        break;
    }
    }
    return std::clamp(targetHz / inputHz, 0.25, 4.0);
}

void PsolaVoice::addGrain(const PsolaAnalyzer& analyzer, const PsolaAnalyzer::Mark& mark,
                          double centre, double outputSpacing) noexcept {
    const auto f = static_cast<double>(formantRatio_);
    const double half = mark.period / std::max(f, 1.0); // output half-length
    const double overlap = half / outputSpacing;        // mean window sum
    const double gain = overlap >= 1.0 ? 1.0 / overlap : 1.0 / std::sqrt(overlap);
    auto start = static_cast<std::int64_t>(std::ceil(centre - half));
    const auto end = static_cast<std::int64_t>(std::floor(centre + half));
    if (start < emitted_) {
        lateSamples_ += emitted_ - start;
        start = emitted_;
    }
    const double invHalf = 1.0 / half;
    for (std::int64_t p = start; p <= end; ++p) {
        const double k = static_cast<double>(p) - centre;
        const double w = 0.5 + 0.5 * std::cos(kPiD * k * invHalf);
        const float x = analyzer.sampleAt(mark.position + k * f);
        accumulator_[static_cast<std::size_t>(p) & mask_] += static_cast<float>(gain * w) * x;
    }
}

void PsolaVoice::render(const PsolaAnalyzer& analyzer, double horizon) noexcept {
    if (synthesisPosition_ < static_cast<double>(emitted_) - analyzer.maxPeriod()) {
        synthesisPosition_ = static_cast<double>(emitted_); // resynchronize after a stall
    }
    PsolaAnalyzer::Mark mark;
    while (synthesisPosition_ <= horizon &&
           analyzer.nearestAvailableMark(synthesisPosition_, mark)) {
        if (synthesisPosition_ > mark.position + mark.period) {
            break; // wait for newer marks
        }
        double spacing = mark.period;
        if (mark.voiced) {
            spacing = mark.period / targetRatio(mark.period, mark.period);
        }
        addGrain(analyzer, mark, synthesisPosition_, spacing);
        synthesisPosition_ += spacing;
    }
}

void PsolaVoice::emit(std::span<float> out, std::int64_t start, float gain,
                      bool accumulate) noexcept {
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::int64_t p = start + static_cast<std::int64_t>(i);
        float v = 0.0F;
        if (p >= 0) {
            float& slot = accumulator_[static_cast<std::size_t>(p) & mask_];
            v = slot;
            slot = 0.0F;
        }
        out[i] = accumulate ? out[i] + gain * v : gain * v;
    }
    emitted_ = std::max(emitted_, start + static_cast<std::int64_t>(out.size()));
}

// ---------------------------------------------------------------------------
// PsolaShifter

void PsolaShifter::prepare(const Config& config) {
    analyzer_.prepare({config.sampleRate, config.minHz, config.maxHz, config.maxBlockSize});
    voice_.prepare(analyzer_);
    maxBlock_ = config.maxBlockSize;
}

void PsolaShifter::reset() noexcept {
    analyzer_.reset();
    voice_.reset();
}

void PsolaShifter::setPitchSemitones(float semitones) noexcept {
    voice_.setPitchRatio(semitonesToRatio(semitones));
}

void PsolaShifter::setFormantSemitones(float semitones) noexcept {
    voice_.setFormantRatio(semitonesToRatio(semitones));
}

void PsolaShifter::process(std::span<float> block) noexcept {
    for (std::size_t pos = 0; pos < block.size(); pos += maxBlock_) {
        const auto chunk = block.subspan(pos, std::min(maxBlock_, block.size() - pos));
        analyzer_.write(chunk);
        const std::int64_t start =
            analyzer_.written() - static_cast<std::int64_t>(chunk.size()) - voice_.latency();
        const auto end = static_cast<double>(start + static_cast<std::int64_t>(chunk.size()));
        voice_.render(analyzer_, end + analyzer_.maxPeriod());
        voice_.emit(chunk, start, 1.0F, false);
    }
}

} // namespace vox::dsp
