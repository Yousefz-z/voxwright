// Registration of the built-in effect blocks: one descriptor (parameter
// metadata) and one adapter (parameter index -> DSP setter) per effect.

#include "vox/plugins/registry.hpp"

#include <vox/dsp/ambience.hpp>
#include <vox/dsp/distortion.hpp>
#include <vox/dsp/dynamics.hpp>
#include <vox/dsp/echo.hpp>
#include <vox/dsp/equalizer.hpp>
#include <vox/dsp/math.hpp>
#include <vox/dsp/modulation.hpp>
#include <vox/dsp/multimode_filter.hpp>
#include <vox/dsp/psola.hpp>
#include <vox/dsp/resonator.hpp>
#include <vox/dsp/ring_modulator.hpp>
#include <vox/dsp/smoothed_value.hpp>
#include <vox/dsp/vocoder.hpp>
#include <vox/dsp/whisper.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace vox::plugins {
namespace {

using Values = std::span<const float>;

template <class Effect>
struct Definition {
    void (*prepare)(Effect&, const PrepareContext&) = nullptr;
    void (*apply)(Effect&, std::size_t, Values) = nullptr;
    std::size_t (*latency)(const Effect&) = nullptr;
    void (*background)(Effect&, bool) = nullptr;
};

/// Generic adapter: keeps the current parameter values and forwards each
/// change to the DSP object through the effect's apply function.
template <class Effect>
class Node final : public EffectNode {
public:
    Node(std::vector<float> defaults, Definition<Effect> definition)
        : values_(std::move(defaults))
        , definition_(definition) {}

    void prepare(const PrepareContext& context) override {
        definition_.prepare(effect_, context);
        prepared_ = true;
        for (std::size_t i = 0; i < values_.size(); ++i) {
            definition_.apply(effect_, i, values_);
        }
    }
    void reset() noexcept override { effect_.reset(); }
    void setParameter(std::size_t index, float value) noexcept override {
        if (index < values_.size()) {
            values_[index] = value;
            // DSP setters may depend on prepared state; before prepare() the
            // value is only stored and applied by prepare().
            if (prepared_) {
                definition_.apply(effect_, index, values_);
            }
        }
    }
    [[nodiscard]] float parameter(std::size_t index) const noexcept override {
        return index < values_.size() ? values_[index] : 0.0F;
    }
    void process(std::span<float> block) noexcept override { effect_.process(block); }
    [[nodiscard]] std::size_t latencySamples() const noexcept override {
        return definition_.latency != nullptr ? definition_.latency(effect_) : 0;
    }
    void setBackgroundEnabled(bool enabled) noexcept override {
        if (definition_.background != nullptr) {
            definition_.background(effect_, enabled);
        }
    }

private:
    Effect effect_{};
    std::vector<float> values_;
    Definition<Effect> definition_;
    bool prepared_ = false;
};

template <class Effect>
void addEffect(EffectRegistry& registry, EffectDescriptor&& descriptor,
               Definition<Effect> definition) {
    std::vector<float> defaults;
    defaults.reserve(descriptor.params.size());
    for (const ParamSpec& p : descriptor.params) {
        defaults.push_back(p.defaultValue);
    }
    registry.add(std::move(descriptor), [defaults, definition]() -> std::unique_ptr<EffectNode> {
        return std::make_unique<Node<Effect>>(defaults, definition);
    });
}

ParamSpec number(std::string id, std::string name, std::string unit, float min, float max,
                 float defaultValue, ParamScale scale = ParamScale::Linear) {
    ParamSpec p;
    p.id = std::move(id);
    p.name = std::move(name);
    p.unit = std::move(unit);
    p.min = min;
    p.max = max;
    p.defaultValue = defaultValue;
    p.scale = scale;
    return p;
}

ParamSpec choice(std::string id, std::string name, std::vector<std::string> choices,
                 float defaultValue) {
    ParamSpec p;
    p.id = std::move(id);
    p.name = std::move(name);
    p.min = 0.0F;
    p.max = static_cast<float>(choices.size() - 1);
    p.defaultValue = defaultValue;
    p.kind = ParamKind::Choice;
    p.choices = std::move(choices);
    return p;
}

ParamSpec toggle(std::string id, std::string name, bool defaultValue) {
    ParamSpec p;
    p.id = std::move(id);
    p.name = std::move(name);
    p.min = 0.0F;
    p.max = 1.0F;
    p.defaultValue = defaultValue ? 1.0F : 0.0F;
    p.kind = ParamKind::Toggle;
    return p;
}

constexpr ParamScale kLog = ParamScale::Logarithmic;

std::size_t index(float value) {
    return static_cast<std::size_t>(std::lround(value));
}

dsp::LfoShape lfoShape(float value) {
    constexpr std::array<dsp::LfoShape, 4> kShapes{dsp::LfoShape::Sine, dsp::LfoShape::Triangle,
                                                   dsp::LfoShape::Square,
                                                   dsp::LfoShape::SmoothRandom};
    return kShapes[std::min<std::size_t>(index(value), kShapes.size() - 1)];
}

/// Small utility block: smoothed gain in dB.
class GainEffect {
public:
    void prepare(double sampleRate) {
        gain_.prepare(sampleRate, 30.0F);
        gain_.setImmediate(1.0F);
    }
    void reset() noexcept {}
    void setGainDb(float db) noexcept { gain_.setTarget(dsp::dbToGain(db)); }
    void process(std::span<float> block) noexcept {
        for (float& s : block) {
            s *= gain_.next();
        }
    }

private:
    dsp::SmoothedValue gain_;
};

// ---------------------------------------------------------------- pitch

void registerPitch(EffectRegistry& r) {
    EffectDescriptor d{
        "pitch",
        "Pitch & Formant",
        "Pitch",
        "Formant-preserving pitch shift (PSOLA) with independent formant control, "
        "monotone robot and scale-snapping modes.",
        {number("semitones", "Pitch", "st", -24.0F, 24.0F, 0.0F),
         number("formant", "Formant", "st", -12.0F, 12.0F, 0.0F),
         choice("mode", "Mode", {"Shift", "Robot", "Tune"}, 0.0F),
         number("robotHz", "Robot pitch", "Hz", 40.0F, 500.0F, 110.0F, kLog),
         choice("key", "Key", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"},
                0.0F),
         choice("scale", "Scale", {"Chromatic", "Major", "Minor", "Pentatonic", "Blues"}, 1.0F),
         number("retune", "Retune speed", "ms", 0.0F, 400.0F, 0.0F)}};
    addEffect<dsp::PsolaShifter>(
        r, std::move(d),
        {[](dsp::PsolaShifter& e, const PrepareContext& c) {
             e.prepare({c.sampleRate, c.minVoiceHz, 800.0F, c.maxBlockSize});
         },
         [](dsp::PsolaShifter& e, std::size_t i, Values v) {
             constexpr std::array<std::uint16_t, 5> kScales{0x0FFF, 0x0AB5, 0x05AD, 0x0295, 0x04E9};
             switch (i) {
             case 0:
                 e.setPitchSemitones(v[0]);
                 break;
             case 1:
                 e.setFormantSemitones(v[1]);
                 break;
             case 2: {
                 constexpr std::array<dsp::PitchMode, 3> kModes{
                     dsp::PitchMode::Ratio, dsp::PitchMode::Fixed, dsp::PitchMode::Quantize};
                 e.setPitchMode(kModes[std::min<std::size_t>(index(v[2]), 2)]);
                 break;
             }
             case 3:
                 e.setFixedFrequency(v[3]);
                 break;
             default:
                 e.setScale(static_cast<int>(index(v[4])),
                            kScales[std::min<std::size_t>(index(v[5]), 4)], v[6]);
                 break;
             }
         },
         [](const dsp::PsolaShifter& e) {
             return e.latencySamples();
         }});
}

// ---------------------------------------------------------------- harmonizer

void registerHarmonizer(EffectRegistry& r) {
    EffectDescriptor d{
        "harmonizer",
        "Harmonizer",
        "Pitch",
        "Up to three pitch-shifted copies of the voice over the dry voice: harmonies, "
        "octave doubling, choirs.",
        {number("dry", "Dry level", "dB", -60.0F, 6.0F, 0.0F),
         number("interval1", "Voice 1 interval", "st", -24.0F, 24.0F, 7.0F),
         number("level1", "Voice 1 level", "dB", -60.0F, 6.0F, -3.0F),
         number("interval2", "Voice 2 interval", "st", -24.0F, 24.0F, 12.0F),
         number("level2", "Voice 2 level", "dB", -60.0F, 6.0F, -60.0F),
         number("interval3", "Voice 3 interval", "st", -24.0F, 24.0F, -12.0F),
         number("level3", "Voice 3 level", "dB", -60.0F, 6.0F, -60.0F),
         number("formant", "Formant", "st", -12.0F, 12.0F, 0.0F),
         number("detune", "Detune", "cents", 0.0F, 50.0F, 0.0F)}};
    addEffect<dsp::Harmonizer>(
        r, std::move(d),
        {[](dsp::Harmonizer& e, const PrepareContext& c) {
             e.prepare(c.sampleRate, c.maxBlockSize);
         },
         [](dsp::Harmonizer& e, std::size_t i, Values v) {
             if (i == 0) {
                 e.setDryGainDb(v[0] <= -59.5F ? -120.0F : v[0]);
                 return;
             }
             constexpr std::array<float, 3> kSpread{1.0F, -1.0F, 0.5F};
             for (std::size_t voice = 0; voice < 3; ++voice) {
                 const float level = v[2 + 2 * voice];
                 const float interval = v[1 + 2 * voice] + kSpread[voice] * v[8] / 100.0F;
                 e.setVoice(static_cast<int>(voice), level > -59.5F, interval, level, v[7]);
             }
         },
         [](const dsp::Harmonizer& e) {
             return e.latencySamples();
         }});
}

// ---------------------------------------------------------------- vocoder

void registerVocoder(EffectRegistry& r) {
    EffectDescriptor d{
        "vocoder",
        "Vocoder",
        "Synthesis",
        "Channel vocoder with a built-in synthesizer carrier that can follow your pitch.",
        {number("bands", "Bands", "", 8.0F, 32.0F, 20.0F),
         choice("carrier", "Carrier", {"Saw", "Square", "Pulse", "Noise", "Chord"}, 0.0F),
         toggle("follow", "Follow my pitch", true),
         number("carrierHz", "Carrier pitch", "Hz", 40.0F, 800.0F, 110.0F, kLog),
         number("interval", "Interval", "st", -24.0F, 24.0F, 0.0F),
         number("formant", "Formant", "st", -12.0F, 12.0F, 0.0F),
         number("sibilance", "Consonants", "", 0.0F, 1.0F, 0.3F),
         number("mix", "Mix", "", 0.0F, 1.0F, 1.0F)}};
    addEffect<dsp::ChannelVocoder>(
        r, std::move(d),
        {[](dsp::ChannelVocoder& e, const PrepareContext& c) {
             e.prepare(c.sampleRate, c.maxBlockSize);
         },
         [](dsp::ChannelVocoder& e, std::size_t i, Values v) {
             constexpr std::array<dsp::ChannelVocoder::Carrier, 5> kCarriers{
                 dsp::ChannelVocoder::Carrier::Saw, dsp::ChannelVocoder::Carrier::Square,
                 dsp::ChannelVocoder::Carrier::Pulse, dsp::ChannelVocoder::Carrier::Noise,
                 dsp::ChannelVocoder::Carrier::Chord};
             switch (i) {
             case 0:
                 e.setBands(static_cast<int>(index(v[0])));
                 break;
             case 1:
                 e.setCarrier(kCarriers[std::min<std::size_t>(index(v[1]), 4)]);
                 break;
             case 2:
             case 4:
                 e.setFollowPitch(v[2] >= 0.5F, v[4]);
                 break;
             case 3:
                 e.setCarrierHz(v[3]);
                 break;
             case 5:
                 e.setFormantSemitones(v[5]);
                 break;
             case 6:
                 e.setSibilance(v[6]);
                 break;
             default:
                 e.setMix(v[7]);
                 break;
             }
         }});
}

// ---------------------------------------------------------------- ring and frequency shift

void registerRingAndShift(EffectRegistry& r) {
    EffectDescriptor ring{"ringmod",
                          "Ring Modulator",
                          "Modulation",
                          "Multiplies the voice with a tone: metallic, robotic, alien.",
                          {number("frequency", "Frequency", "Hz", 1.0F, 2000.0F, 80.0F, kLog),
                           choice("waveform", "Waveform", {"Sine", "Square", "Triangle"}, 0.0F),
                           number("lfoRate", "Wobble rate", "Hz", 0.0F, 20.0F, 0.0F),
                           number("lfoDepth", "Wobble depth", "Hz", 0.0F, 500.0F, 0.0F),
                           number("mix", "Mix", "", 0.0F, 1.0F, 1.0F)}};
    addEffect<dsp::RingModulator>(
        r, std::move(ring),
        {[](dsp::RingModulator& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::RingModulator& e, std::size_t i, Values v) {
             constexpr std::array<dsp::Waveform, 3> kWaves{
                 dsp::Waveform::Sine, dsp::Waveform::Square, dsp::Waveform::Triangle};
             switch (i) {
             case 0:
                 e.setFrequency(v[0]);
                 break;
             case 1:
                 e.setWaveform(kWaves[std::min<std::size_t>(index(v[1]), 2)]);
                 break;
             case 2:
             case 3:
                 e.setLfo(v[2], v[3]);
                 break;
             default:
                 e.setMix(v[4]);
                 break;
             }
         }});

    EffectDescriptor shift{
        "freqshift",
        "Frequency Shifter",
        "Modulation",
        "Moves every frequency by the same number of hertz; breaks harmonics apart.",
        {number("shift", "Shift", "Hz", -1000.0F, 1000.0F, 50.0F),
         number("mix", "Mix", "", 0.0F, 1.0F, 1.0F)}};
    addEffect<dsp::FrequencyShifter>(
        r, std::move(shift),
        {[](dsp::FrequencyShifter& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::FrequencyShifter& e, std::size_t i, Values v) {
             if (i == 0) {
                 e.setShiftHz(v[0]);
             } else {
                 e.setMix(v[1]);
             }
         }});
}

// ---------------------------------------------------------------- distortion

void registerDistortion(EffectRegistry& r) {
    EffectDescriptor d{
        "distortion",
        "Distortion",
        "Distortion",
        "Oversampled saturation curves plus bit and sample-rate reduction.",
        {choice("mode", "Mode", {"Soft", "Hard", "Fold", "Tube", "Rectify", "Crush"}, 0.0F),
         number("drive", "Drive", "dB", 0.0F, 40.0F, 12.0F),
         number("tone", "Tone", "Hz", 500.0F, 20000.0F, 8000.0F, kLog),
         number("bits", "Bits", "", 1.0F, 16.0F, 8.0F),
         number("downsample", "Downsample", "x", 1.0F, 32.0F, 1.0F, kLog),
         number("mix", "Mix", "", 0.0F, 1.0F, 1.0F),
         number("output", "Output", "dB", -24.0F, 12.0F, -6.0F)}};
    addEffect<dsp::Distortion>(
        r, std::move(d),
        {[](dsp::Distortion& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::Distortion& e, std::size_t i, Values v) {
             constexpr std::array<dsp::Distortion::Mode, 6> kModes{
                 dsp::Distortion::Mode::Soft,    dsp::Distortion::Mode::Hard,
                 dsp::Distortion::Mode::Fold,    dsp::Distortion::Mode::Tube,
                 dsp::Distortion::Mode::Rectify, dsp::Distortion::Mode::Crush};
             switch (i) {
             case 0:
                 e.setMode(kModes[std::min<std::size_t>(index(v[0]), 5)]);
                 break;
             case 1:
                 e.setDriveDb(v[1]);
                 break;
             case 2:
                 e.setToneHz(v[2]);
                 break;
             case 3:
                 e.setBits(v[3]);
                 break;
             case 4:
                 e.setDownsample(v[4]);
                 break;
             case 5:
                 e.setMix(v[5]);
                 break;
             default:
                 e.setOutputDb(v[6]);
                 break;
             }
         }});
}

// ---------------------------------------------------------------- eq and filter

void registerEqAndFilter(EffectRegistry& r) {
    EffectDescriptor eq{
        "eq",
        "Equalizer",
        "Tone",
        "Low and high cuts, shelves, and three peaking bands.",
        {toggle("lowCut", "Low cut", false),
         number("lowCutHz", "Low cut", "Hz", 20.0F, 1000.0F, 80.0F, kLog),
         number("lowShelfDb", "Bass", "dB", -24.0F, 24.0F, 0.0F),
         number("lowShelfHz", "Bass frequency", "Hz", 40.0F, 1000.0F, 200.0F, kLog),
         number("mid1Db", "Low mid", "dB", -24.0F, 24.0F, 0.0F),
         number("mid1Hz", "Low mid frequency", "Hz", 100.0F, 8000.0F, 500.0F, kLog),
         number("mid2Db", "Mid", "dB", -24.0F, 24.0F, 0.0F),
         number("mid2Hz", "Mid frequency", "Hz", 100.0F, 8000.0F, 1500.0F, kLog),
         number("mid3Db", "High mid", "dB", -24.0F, 24.0F, 0.0F),
         number("mid3Hz", "High mid frequency", "Hz", 100.0F, 12000.0F, 3500.0F, kLog),
         number("midQ", "Mid width (Q)", "", 0.3F, 10.0F, 0.9F, kLog),
         number("highShelfDb", "Treble", "dB", -24.0F, 24.0F, 0.0F),
         number("highShelfHz", "Treble frequency", "Hz", 1000.0F, 16000.0F, 6000.0F, kLog),
         toggle("highCut", "High cut", false),
         number("highCutHz", "High cut", "Hz", 1000.0F, 20000.0F, 12000.0F, kLog)}};
    addEffect<dsp::Equalizer>(
        r, std::move(eq),
        {[](dsp::Equalizer& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::Equalizer& e, std::size_t i, Values v) {
             using B = dsp::Equalizer;
             switch (i) {
             case 0:
                 e.setEnabled(B::LowCut, v[0] >= 0.5F);
                 break;
             case 1:
                 e.setFrequency(B::LowCut, v[1]);
                 break;
             case 2:
                 e.setGainDb(B::LowShelf, v[2]);
                 break;
             case 3:
                 e.setFrequency(B::LowShelf, v[3]);
                 break;
             case 4:
                 e.setGainDb(B::Peak1, v[4]);
                 break;
             case 5:
                 e.setFrequency(B::Peak1, v[5]);
                 break;
             case 6:
                 e.setGainDb(B::Peak2, v[6]);
                 break;
             case 7:
                 e.setFrequency(B::Peak2, v[7]);
                 break;
             case 8:
                 e.setGainDb(B::Peak3, v[8]);
                 break;
             case 9:
                 e.setFrequency(B::Peak3, v[9]);
                 break;
             case 10:
                 e.setQ(B::Peak1, v[10]);
                 e.setQ(B::Peak2, v[10]);
                 e.setQ(B::Peak3, v[10]);
                 break;
             case 11:
                 e.setGainDb(B::HighShelf, v[11]);
                 break;
             case 12:
                 e.setFrequency(B::HighShelf, v[12]);
                 break;
             case 13:
                 e.setEnabled(B::HighCut, v[13] >= 0.5F);
                 break;
             default:
                 e.setFrequency(B::HighCut, v[14]);
                 break;
             }
         }});

    EffectDescriptor filter{
        "filter",
        "Filter",
        "Tone",
        "Resonant filter swept by an LFO or by your loudness (auto-wah).",
        {choice("mode", "Mode", {"Low-pass", "High-pass", "Band-pass", "Notch"}, 0.0F),
         number("cutoff", "Cutoff", "Hz", 40.0F, 18000.0F, 1000.0F, kLog),
         number("resonance", "Resonance", "", 0.3F, 15.0F, 0.707F, kLog),
         number("lfoRate", "Sweep rate", "Hz", 0.01F, 20.0F, 0.5F, kLog),
         number("lfoDepth", "Sweep depth", "oct", 0.0F, 4.0F, 0.0F),
         choice("lfoShape", "Sweep shape", {"Sine", "Triangle", "Square", "Random"}, 0.0F),
         number("envelope", "Envelope", "oct", -4.0F, 4.0F, 0.0F),
         number("mix", "Mix", "", 0.0F, 1.0F, 1.0F)}};
    addEffect<dsp::MultimodeFilter>(
        r, std::move(filter),
        {[](dsp::MultimodeFilter& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::MultimodeFilter& e, std::size_t i, Values v) {
             constexpr std::array<dsp::MultimodeFilter::Mode, 4> kModes{
                 dsp::MultimodeFilter::Mode::Lowpass, dsp::MultimodeFilter::Mode::Highpass,
                 dsp::MultimodeFilter::Mode::Bandpass, dsp::MultimodeFilter::Mode::Notch};
             switch (i) {
             case 0:
                 e.setMode(kModes[std::min<std::size_t>(index(v[0]), 3)]);
                 break;
             case 1:
                 e.setCutoff(v[1]);
                 break;
             case 2:
                 e.setResonance(v[2]);
                 break;
             case 3:
             case 4:
             case 5:
                 e.setLfo(v[3], v[4], lfoShape(v[5]));
                 break;
             case 6:
                 e.setEnvelope(v[6], 5.0F, 120.0F);
                 break;
             default:
                 e.setMix(v[7]);
                 break;
             }
         }});
}

// ---------------------------------------------------------------- modulation

void registerModulation(EffectRegistry& r) {
    EffectDescriptor chorus{"chorus",
                            "Chorus",
                            "Modulation",
                            "Several slightly delayed, moving copies: thicker, doubled voice.",
                            {number("voices", "Voices", "", 1.0F, 4.0F, 2.0F),
                             number("rate", "Rate", "Hz", 0.05F, 5.0F, 0.8F, kLog),
                             number("depth", "Depth", "ms", 0.0F, 10.0F, 3.0F),
                             number("delay", "Delay", "ms", 5.0F, 40.0F, 20.0F),
                             number("mix", "Mix", "", 0.0F, 1.0F, 0.5F)}};
    addEffect<dsp::Chorus>(
        r, std::move(chorus),
        {[](dsp::Chorus& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::Chorus& e, std::size_t i, Values v) {
             switch (i) {
             case 0:
                 e.setVoices(static_cast<int>(index(v[0])));
                 break;
             case 1:
                 e.setRate(v[1]);
                 break;
             case 2:
                 e.setDepthMs(v[2]);
                 break;
             case 3:
                 e.setDelayMs(v[3]);
                 break;
             default:
                 e.setMix(v[4]);
                 break;
             }
         }});

    EffectDescriptor flanger{"flanger",
                             "Flanger",
                             "Modulation",
                             "Short sweeping delay with feedback: jet and tube sweeps.",
                             {number("rate", "Rate", "Hz", 0.02F, 5.0F, 0.25F, kLog),
                              number("depth", "Depth", "ms", 0.0F, 8.0F, 2.0F),
                              number("delay", "Delay", "ms", 0.3F, 10.0F, 2.5F),
                              number("feedback", "Feedback", "", -0.95F, 0.95F, 0.5F),
                              number("mix", "Mix", "", 0.0F, 1.0F, 0.5F)}};
    addEffect<dsp::Flanger>(
        r, std::move(flanger),
        {[](dsp::Flanger& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::Flanger& e, std::size_t i, Values v) {
             switch (i) {
             case 0:
                 e.setRate(v[0]);
                 break;
             case 1:
                 e.setDepthMs(v[1]);
                 break;
             case 2:
                 e.setDelayMs(v[2]);
                 break;
             case 3:
                 e.setFeedback(v[3]);
                 break;
             default:
                 e.setMix(v[4]);
                 break;
             }
         }});

    EffectDescriptor phaser{"phaser",
                            "Phaser",
                            "Modulation",
                            "Sweeping notches from a chain of all-pass stages.",
                            {number("stages", "Stages", "", 2.0F, 12.0F, 6.0F),
                             number("rate", "Rate", "Hz", 0.02F, 8.0F, 0.4F, kLog),
                             number("low", "Low", "Hz", 50.0F, 2000.0F, 300.0F, kLog),
                             number("high", "High", "Hz", 500.0F, 12000.0F, 3000.0F, kLog),
                             number("feedback", "Feedback", "", -0.9F, 0.9F, 0.4F),
                             number("mix", "Mix", "", 0.0F, 1.0F, 0.5F)}};
    addEffect<dsp::Phaser>(
        r, std::move(phaser),
        {[](dsp::Phaser& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::Phaser& e, std::size_t i, Values v) {
             switch (i) {
             case 0:
                 e.setStages(static_cast<int>(index(v[0])));
                 break;
             case 1:
                 e.setRate(v[1]);
                 break;
             case 2:
             case 3:
                 e.setRange(v[2], v[3]);
                 break;
             case 4:
                 e.setFeedback(v[4]);
                 break;
             default:
                 e.setMix(v[5]);
                 break;
             }
         }});

    EffectDescriptor tremolo{
        "tremolo",
        "Tremolo",
        "Modulation",
        "Volume wobble; a square shape chops the voice like rotor blades.",
        {number("rate", "Rate", "Hz", 0.1F, 40.0F, 6.0F, kLog),
         number("depth", "Depth", "", 0.0F, 1.0F, 0.5F),
         choice("shape", "Shape", {"Sine", "Triangle", "Square", "Random"}, 0.0F)}};
    addEffect<dsp::Tremolo>(
        r, std::move(tremolo),
        {[](dsp::Tremolo& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::Tremolo& e, std::size_t i, Values v) {
             switch (i) {
             case 0:
                 e.setRate(v[0]);
                 break;
             case 1:
                 e.setDepth(v[1]);
                 break;
             default:
                 e.setShape(lfoShape(v[2]));
                 break;
             }
         }});

    EffectDescriptor vibrato{"vibrato",
                             "Vibrato",
                             "Modulation",
                             "Pitch wobble: trembling, wavering, warped tape.",
                             {number("rate", "Rate", "Hz", 0.1F, 15.0F, 5.0F, kLog),
                              number("depth", "Depth", "cents", 0.0F, 100.0F, 20.0F)}};
    addEffect<dsp::Vibrato>(
        r, std::move(vibrato),
        {[](dsp::Vibrato& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::Vibrato& e, std::size_t i, Values v) {
             if (i == 0) {
                 e.setRate(v[0]);
             } else {
                 e.setDepthCents(v[1]);
             }
         },
         [](const dsp::Vibrato& e) {
             return e.latencySamples();
         }});
}

// ---------------------------------------------------------------- space

void registerSpace(EffectRegistry& r) {
    EffectDescriptor echo{"echo",
                          "Echo",
                          "Space",
                          "Repeating delay with darkening repeats.",
                          {number("time", "Time", "ms", 20.0F, 2000.0F, 300.0F, kLog),
                           number("feedback", "Repeats", "", 0.0F, 0.95F, 0.35F),
                           number("damping", "Darkening", "Hz", 500.0F, 20000.0F, 5000.0F, kLog),
                           number("mix", "Mix", "", 0.0F, 1.0F, 0.35F)}};
    addEffect<dsp::Echo>(r, std::move(echo),
                         {[](dsp::Echo& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
                          [](dsp::Echo& e, std::size_t i, Values v) {
                              switch (i) {
                              case 0:
                                  e.setTimeMs(v[0]);
                                  break;
                              case 1:
                                  e.setFeedback(v[1]);
                                  break;
                              case 2:
                                  e.setDampingHz(v[2]);
                                  break;
                              default:
                                  e.setMix(v[3]);
                                  break;
                              }
                          }});

    EffectDescriptor reverb{"reverb",
                            "Reverb",
                            "Space",
                            "Room to hall reverberation (8-line feedback delay network).",
                            {number("size", "Size", "", 0.0F, 1.0F, 0.5F),
                             number("decay", "Decay", "s", 0.2F, 10.0F, 1.5F, kLog),
                             number("damping", "Damping", "Hz", 1000.0F, 20000.0F, 6000.0F, kLog),
                             number("predelay", "Pre-delay", "ms", 0.0F, 150.0F, 10.0F),
                             number("mix", "Mix", "", 0.0F, 1.0F, 0.3F)}};
    addEffect<dsp::Reverb>(
        r, std::move(reverb),
        {[](dsp::Reverb& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::Reverb& e, std::size_t i, Values v) {
             switch (i) {
             case 0:
                 e.setSize(v[0]);
                 break;
             case 1:
                 e.setDecaySeconds(v[1]);
                 break;
             case 2:
                 e.setDampingHz(v[2]);
                 break;
             case 3:
                 e.setPredelayMs(v[3]);
                 break;
             default:
                 e.setMix(v[4]);
                 break;
             }
         }});

    EffectDescriptor comb{"comb",
                          "Resonator",
                          "Space",
                          "Tuned metallic resonance: tin can, pipe, robot chest.",
                          {number("frequency", "Pitch", "Hz", 40.0F, 2000.0F, 220.0F, kLog),
                           number("feedback", "Ring", "", -0.98F, 0.98F, 0.7F),
                           number("damping", "Damping", "Hz", 500.0F, 20000.0F, 6000.0F, kLog),
                           number("mix", "Mix", "", 0.0F, 1.0F, 0.5F)}};
    addEffect<dsp::CombResonator>(
        r, std::move(comb),
        {[](dsp::CombResonator& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::CombResonator& e, std::size_t i, Values v) {
             switch (i) {
             case 0:
                 e.setFrequency(v[0]);
                 break;
             case 1:
                 e.setFeedback(v[1]);
                 break;
             case 2:
                 e.setDampingHz(v[2]);
                 break;
             default:
                 e.setMix(v[3]);
                 break;
             }
         }});
}

// ---------------------------------------------------------------- dynamics and texture

void registerDynamicsAndTexture(EffectRegistry& r) {
    EffectDescriptor compressor{"compressor",
                                "Compressor",
                                "Dynamics",
                                "Evens out loud and quiet words.",
                                {number("threshold", "Threshold", "dB", -60.0F, 0.0F, -18.0F),
                                 number("ratio", "Ratio", ":1", 1.0F, 20.0F, 3.0F, kLog),
                                 number("attack", "Attack", "ms", 0.1F, 100.0F, 5.0F, kLog),
                                 number("release", "Release", "ms", 10.0F, 1000.0F, 80.0F, kLog),
                                 number("makeup", "Makeup", "dB", 0.0F, 24.0F, 0.0F),
                                 number("knee", "Knee", "dB", 0.0F, 12.0F, 6.0F)}};
    addEffect<dsp::Compressor>(
        r, std::move(compressor),
        {[](dsp::Compressor& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::Compressor& e, std::size_t i, Values v) {
             switch (i) {
             case 0:
                 e.setThresholdDb(v[0]);
                 break;
             case 1:
                 e.setRatio(v[1]);
                 break;
             case 2:
             case 3:
                 e.setTimes(v[2], v[3]);
                 break;
             case 4:
                 e.setMakeupDb(v[4]);
                 break;
             default:
                 e.setKneeDb(v[5]);
                 break;
             }
         }});

    EffectDescriptor whisper{"whisper",
                             "Whisper",
                             "Synthesis",
                             "Replaces the vocal cords with breath while keeping the words.",
                             {number("mix", "Amount", "", 0.0F, 1.0F, 1.0F)}};
    addEffect<dsp::Whisper>(
        r, std::move(whisper),
        {[](dsp::Whisper& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::Whisper& e, std::size_t /*i*/, Values v) {
             e.setMix(v[0]);
         }});

    EffectDescriptor ambience{
        "ambience",
        "Background",
        "Background",
        "Synthesized background soundscape (follows the global background switch).",
        {choice("kind", "Scene",
                {"Rain", "Wind", "Crowd", "Engine hum", "Radio static", "Space drone", "Cave drips",
                 "Underwater", "Fire", "Traffic", "Computer"},
                0.0F),
         number("level", "Level", "dB", -60.0F, 0.0F, -24.0F),
         number("tone", "Tone", "Hz", 300.0F, 20000.0F, 8000.0F, kLog)}};
    addEffect<dsp::Ambience>(
        r, std::move(ambience),
        {[](dsp::Ambience& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::Ambience& e, std::size_t i, Values v) {
             constexpr std::array<dsp::Ambience::Kind, 11> kKinds{
                 dsp::Ambience::Kind::Rain,        dsp::Ambience::Kind::Wind,
                 dsp::Ambience::Kind::Crowd,       dsp::Ambience::Kind::EngineHum,
                 dsp::Ambience::Kind::RadioStatic, dsp::Ambience::Kind::SpaceDrone,
                 dsp::Ambience::Kind::CaveDrips,   dsp::Ambience::Kind::Underwater,
                 dsp::Ambience::Kind::Fire,        dsp::Ambience::Kind::Traffic,
                 dsp::Ambience::Kind::Computer};
             switch (i) {
             case 0:
                 e.setKind(kKinds[std::min<std::size_t>(index(v[0]), kKinds.size() - 1)]);
                 break;
             case 1:
                 e.setLevelDb(v[1]);
                 break;
             default:
                 e.setToneHz(v[2]);
                 break;
             }
         },
         nullptr,
         [](dsp::Ambience& e, bool enabled) {
             e.setEnabled(enabled);
         }});

    EffectDescriptor stutter{
        "stutter",
        "Glitch",
        "Texture",
        "Randomly repeats short slices: broken transmission, malfunctioning machine.",
        {number("slice", "Slice", "ms", 20.0F, 500.0F, 90.0F, kLog),
         number("probability", "Chance", "", 0.0F, 1.0F, 0.15F),
         number("repeats", "Repeats", "", 1.0F, 8.0F, 3.0F)}};
    addEffect<dsp::Stutter>(
        r, std::move(stutter),
        {[](dsp::Stutter& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
         [](dsp::Stutter& e, std::size_t i, Values v) {
             switch (i) {
             case 0:
                 e.setSliceMs(v[0]);
                 break;
             case 1:
                 e.setProbability(v[1]);
                 break;
             default:
                 e.setMaxRepeats(static_cast<int>(index(v[2])));
                 break;
             }
         }});

    EffectDescriptor gain{"gain",
                          "Gain",
                          "Utility",
                          "Level adjustment.",
                          {number("gain", "Gain", "dB", -24.0F, 24.0F, 0.0F)}};
    addEffect<GainEffect>(r, std::move(gain),
                          {[](GainEffect& e, const PrepareContext& c) { e.prepare(c.sampleRate); },
                           [](GainEffect& e, std::size_t /*i*/, Values v) {
                               e.setGainDb(v[0]);
                           }});
}

} // namespace

void registerBuiltinEffects(EffectRegistry& registry) {
    registerPitch(registry);
    registerHarmonizer(registry);
    registerVocoder(registry);
    registerRingAndShift(registry);
    registerDistortion(registry);
    registerEqAndFilter(registry);
    registerModulation(registry);
    registerSpace(registry);
    registerDynamicsAndTexture(registry);
}

} // namespace vox::plugins
