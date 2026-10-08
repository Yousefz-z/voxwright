#include "voice_metrics.hpp"

#include <vox/dsp/meters.hpp>
#include <vox/plugins/voice_chain.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/render.hpp>
#include <vox/testing/signals.hpp>

#include <chrono>
#include <cmath>

namespace vox::plugins::test {
namespace {

constexpr double kFs = 48000.0;

struct Reference {
    std::vector<float> audio;
    double loudness = 0.0;
    double medianF0 = 0.0;
    double centroid = 0.0;
    double highBand = 0.0;
    double lowBand = 0.0;
};

const Reference& reference() {
    static const Reference kReference = [] {
        Reference r;
        // Two phrases back to back, then 0.6 s of silence for tails.
        const auto phrase = vox::testing::synthPhrase(kPhraseF0);
        r.audio = phrase.audio;
        r.audio.insert(r.audio.end(), phrase.audio.begin(), phrase.audio.end());
        r.audio.resize(r.audio.size() + static_cast<std::size_t>(0.6 * kFs), 0.0F);
        const auto speech = std::span<const float>(r.audio).first(2 * phrase.audio.size());
        r.loudness = vox::dsp::integratedLoudness(speech, kFs);
        r.medianF0 = vox::testing::medianVoiced(vox::testing::f0Track(speech, kFs, 40.0, 1000.0));
        r.centroid = vox::testing::spectralCentroidHz(speech, kFs);
        r.highBand = vox::testing::energyAbove(speech, kFs, 4000.0);
        r.lowBand = 1.0 - vox::testing::energyAbove(speech, kFs, 250.0);
        return r;
    }();
    return kReference;
}

} // namespace

const std::vector<float>& testPhrase() {
    return reference().audio;
}

VoiceMetrics measureVoice(const VoicePreset& preset, const EffectRegistry& registry,
                          std::vector<float>* rendered) {
    VoiceMetrics m;
    m.id = preset.id;
    const Reference& ref = reference();
    PrepareContext context;
    context.sampleRate = kFs;
    context.maxBlockSize = 512;
    auto chain = VoiceChain::build(preset, VoiceSettings{}, registry, context);
    if (!chain) {
        m.error = chain.error().message;
        return m;
    }
    m.built = true;
    VoiceChain& c = *chain.value();
    m.latencyMs = static_cast<double>(c.latencySamples()) / kFs * 1000.0;

    const auto t0 = std::chrono::steady_clock::now();
    const auto out = vox::testing::renderOffline(c, ref.audio, 256);
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    m.realtimeFactor = seconds / (static_cast<double>(ref.audio.size()) / kFs);
    m.finite = vox::testing::allFinite(out);
    m.peak = static_cast<double>(vox::testing::peakAbs(out));

    const std::size_t speechLength = ref.audio.size() - static_cast<std::size_t>(0.6 * kFs);
    const std::size_t latency = c.latencySamples();
    const auto speech = std::span<const float>(out).subspan(latency, speechLength);
    if (rendered != nullptr) {
        rendered->assign(out.begin() + static_cast<std::ptrdiff_t>(latency), out.end());
    }
    m.loudnessChangeLu = vox::dsp::integratedLoudness(speech, kFs) - ref.loudness;
    const auto track = vox::testing::f0Track(speech, kFs, 40.0, 1000.0);
    std::size_t voiced = 0;
    for (const double f : track) {
        voiced += f > 0.0 ? 1U : 0U;
    }
    m.voicedFraction = static_cast<double>(voiced) / static_cast<double>(track.size());
    m.outputMedianF0 = vox::testing::medianVoiced(track);
    m.pitchShiftSemitones =
        m.outputMedianF0 > 0.0 ? 12.0 * std::log2(m.outputMedianF0 / ref.medianF0) : 0.0;
    // Aperiodicity of the loudest vowel region (/a/ of the first phrase).
    m.aperiodicity =
        vox::testing::estimateF0(speech.subspan(9600, 14400), kFs, 40.0, 1000.0).aperiodicity;
    m.centroidRatio = vox::testing::spectralCentroidHz(speech, kFs) / ref.centroid;
    m.highBandChange = vox::testing::energyAbove(speech, kFs, 4000.0) - ref.highBand;
    m.lowBandChange = (1.0 - vox::testing::energyAbove(speech, kFs, 250.0)) - ref.lowBand;
    const auto tail =
        std::span<const float>(out).subspan(out.size() - static_cast<std::size_t>(0.3 * kFs));
    m.tailDb =
        vox::testing::toDb(vox::testing::rms(tail) / std::max(vox::testing::rms(speech), 1e-9));
    m.tailCentroidHz = m.tailDb > -60.0 ? vox::testing::spectralCentroidHz(tail, kFs) : 0.0;
    return m;
}

double voiceDistance(const VoiceMetrics& a, const VoiceMetrics& b) {
    const double pitch = (a.pitchShiftSemitones - b.pitchShiftSemitones) / 3.0;
    const double voicing = (a.voicedFraction - b.voicedFraction) * 2.0;
    const double periodic = (std::min(a.aperiodicity, 0.5) - std::min(b.aperiodicity, 0.5)) * 4.0;
    const double bright = std::log2(a.centroidRatio / b.centroidRatio) * 2.0;
    const double high = (a.highBandChange - b.highBandChange) * 5.0;
    const double low = (a.lowBandChange - b.lowBandChange) * 5.0;
    const double tail = (std::max(a.tailDb, -60.0) - std::max(b.tailDb, -60.0)) / 10.0;
    // Background character: only comparable when both voices have a tail.
    const double background = a.tailCentroidHz > 0.0 && b.tailCentroidHz > 0.0
                                  ? std::log2(a.tailCentroidHz / b.tailCentroidHz)
                                  : 0.0;
    return std::sqrt(pitch * pitch + voicing * voicing + periodic * periodic + bright * bright +
                     high * high + tail * tail + background * background + low * low);
}

} // namespace vox::plugins::test
