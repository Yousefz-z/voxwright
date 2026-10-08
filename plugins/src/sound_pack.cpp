#include "vox/plugins/sound_pack.hpp"

#include <vox/dsp/dynamics.hpp>
#include <vox/dsp/echo.hpp>
#include <vox/dsp/math.hpp>
#include <vox/dsp/meters.hpp>
#include <vox/dsp/noise.hpp>
#include <vox/dsp/one_pole.hpp>
#include <vox/dsp/oscillator.hpp>
#include <vox/dsp/svf.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <tuple>

namespace vox::plugins {
namespace {

using dsp::Waveform;

constexpr std::array<BuiltinSound, 18> kSounds{{
    {"stadium-horn", "Stadium Horn", "Reactions", "megaphone", "#FF7A59"},
    {"applause", "Applause", "Reactions", "hands", "#F5B841"},
    {"ta-da", "Ta-da", "Reactions", "sparkle", "#F2C14E"},
    {"deflate", "Deflate", "Reactions", "balloon", "#9B8CFF"},
    {"rimshot", "Rimshot", "Music", "drum", "#5AA9F5"},
    {"drum-roll", "Drum Roll", "Music", "drum", "#4C8DE0"},
    {"fanfare", "Fanfare", "Music", "trumpet", "#FFB347"},
    {"laser", "Laser", "Effects", "bolt", "#3DD68C"},
    {"boing", "Boing", "Effects", "spring", "#7BD389"},
    {"whoosh", "Whoosh", "Effects", "wind", "#8CC8E8"},
    {"explosion", "Explosion", "Effects", "burst", "#F2545B"},
    {"glitch", "Glitch", "Effects", "glitch", "#C77DFF"},
    {"crickets", "Crickets", "Ambience", "moon", "#6FCF97"},
    {"heartbeat", "Heartbeat", "Ambience", "heart", "#E85D75"},
    {"wrong-answer", "Wrong Answer", "Alerts", "cross", "#F2545B"},
    {"right-answer", "Right Answer", "Alerts", "check", "#3DD68C"},
    {"alarm", "Alarm", "Alerts", "bell", "#FF8C42"},
    {"censor-bleep", "Censor Bleep", "Alerts", "bleep", "#A6ACBF"},
}};

/// A mono buffer with the sample rate, plus the small toolkit the sounds
/// are built from. All times are in seconds.
class Canvas {
public:
    Canvas(double sampleRate, double seconds)
        : fs_(sampleRate)
        , data_(frames(seconds), 0.0F) {}

    [[nodiscard]] double rate() const noexcept { return fs_; }
    [[nodiscard]] std::size_t frames(double seconds) const {
        return static_cast<std::size_t>(std::max(0.0, seconds) * fs_);
    }
    [[nodiscard]] std::vector<float>& data() noexcept { return data_; }

    /// Adds `src` at `at` seconds, scaled by `gain` (grows the canvas if needed).
    void add(const std::vector<float>& src, double at, float gain = 1.0F) {
        const std::size_t start = frames(at);
        if (data_.size() < start + src.size()) {
            data_.resize(start + src.size(), 0.0F);
        }
        for (std::size_t i = 0; i < src.size(); ++i) {
            data_[start + i] += src[i] * gain;
        }
    }

    /// An oscillator whose frequency glides exponentially from f0 to f1,
    /// with an attack and an exponential decay (time constant `decay`).
    [[nodiscard]] std::vector<float> tone(Waveform w, double f0, double f1, double seconds,
                                          double attack, double decay) const {
        dsp::Oscillator osc;
        osc.prepare(fs_);
        osc.setWaveform(w);
        std::vector<float> out(frames(seconds));
        const double n = static_cast<double>(std::max<std::size_t>(out.size(), 1));
        for (std::size_t i = 0; i < out.size(); ++i) {
            const double t = static_cast<double>(i) / fs_;
            osc.setFrequency(
                static_cast<float>(f0 * std::pow(f1 / f0, static_cast<double>(i) / n)));
            out[i] = osc.next() * envelope(t, seconds, attack, decay);
        }
        return out;
    }

    /// White noise with the same envelope shape.
    [[nodiscard]] std::vector<float> noise(double seconds, double attack, double decay,
                                           std::uint32_t seed) const {
        dsp::FastRandom rng(seed);
        std::vector<float> out(frames(seconds));
        for (std::size_t i = 0; i < out.size(); ++i) {
            const double t = static_cast<double>(i) / fs_;
            out[i] = rng.nextBipolar() * envelope(t, seconds, attack, decay);
        }
        return out;
    }

    /// State-variable filter whose cutoff follows `cutoff(t)`; `mode` picks
    /// low (0), band (1), or high (2) pass.
    void filter(std::vector<float>& x, const std::function<double(double)>& cutoff, float q,
                int mode) const {
        dsp::StateVariableFilter f;
        f.prepare(fs_);
        for (std::size_t i = 0; i < x.size(); ++i) {
            if (i % 32 == 0) {
                const double hz =
                    std::clamp(cutoff(static_cast<double>(i) / fs_), 20.0, fs_ * 0.45);
                f.setParameters(static_cast<float>(hz), q);
            }
            const auto o = f.processSample(x[i]);
            float y = o.high;
            if (mode == 0) {
                y = o.low;
            } else if (mode == 1) {
                y = o.band;
            }
            x[i] = y;
        }
    }

    /// Room reverb on the whole canvas (a tail is appended).
    void reverb(double size, double decaySeconds, float mix) {
        data_.resize(data_.size() + frames(decaySeconds), 0.0F);
        dsp::Reverb r;
        r.prepare(fs_);
        r.setSize(static_cast<float>(size));
        r.setDecaySeconds(static_cast<float>(decaySeconds));
        r.setDampingHz(6000.0F);
        r.setMix(mix);
        for (std::size_t pos = 0; pos < data_.size(); pos += 512) {
            r.process(std::span<float>(data_).subspan(
                pos, std::min<std::size_t>(512, data_.size() - pos)));
        }
    }

private:
    static float envelope(double t, double length, double attack, double decay) {
        const double a = attack > 0.0 ? std::min(1.0, t / attack) : 1.0;
        const double d = decay > 0.0 ? std::exp(-std::max(0.0, t - attack) / decay) : 1.0;
        // Release at the very end so nothing stops abruptly: 30 ms, or a
        // quarter of very short sounds.
        const double release = std::min(0.03, length / 4.0);
        const double tail = std::clamp((length - t) / release, 0.0, 1.0);
        return static_cast<float>(a * d * tail);
    }

    double fs_;
    std::vector<float> data_;
};

/// Removes DC, fades the edges, trims trailing silence, and brings the
/// sound to the pack loudness. Sounds whose peaks would then exceed -1 dBFS
/// go through a look-ahead limiter, as a mastering engineer would.
std::vector<float> finish(std::vector<float> x, double fs) {
    dsp::DcBlocker dc;
    dc.prepare(fs);
    for (float& s : x) {
        s = dc.processSample(s);
    }
    const auto peakOf = [](const std::vector<float>& v) {
        float peak = 0.0F;
        for (const float s : v) {
            peak = std::max(peak, std::abs(s));
        }
        return peak;
    };
    // Trim the tail once it stays 70 dB below the peak.
    const float floor = peakOf(x) * 3.2e-4F;
    std::size_t end = x.size();
    while (end > 0 && std::abs(x[end - 1]) < floor) {
        --end;
    }
    x.resize(std::max<std::size_t>(end, 1));
    const auto fadeIn = std::min<std::size_t>(x.size(), static_cast<std::size_t>(0.002 * fs));
    const auto fadeOut = std::min<std::size_t>(x.size(), static_cast<std::size_t>(0.01 * fs));
    for (std::size_t i = 0; i < fadeIn; ++i) {
        x[i] *= static_cast<float>(i) / static_cast<float>(fadeIn);
    }
    for (std::size_t i = 0; i < fadeOut; ++i) {
        x[x.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fadeOut);
    }
    const double loudness = dsp::integratedLoudness(x, fs);
    const double gain =
        std::isfinite(loudness) ? std::pow(10.0, (kBuiltinSoundLufs - loudness) / 20.0) : 1.0;
    for (float& s : x) {
        s = static_cast<float>(static_cast<double>(s) * gain);
    }
    constexpr float kCeiling = 0.891F; // -1 dBFS
    if (peakOf(x) > kCeiling) {
        dsp::Limiter limiter;
        limiter.prepare(fs);
        limiter.setCeilingDb(-1.0F);
        const std::size_t latency = limiter.latencySamples();
        x.resize(x.size() + latency, 0.0F);
        for (std::size_t pos = 0; pos < x.size(); pos += 512) {
            limiter.process(
                std::span<float>(x).subspan(pos, std::min<std::size_t>(512, x.size() - pos)));
        }
        x.erase(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(latency));
    }
    return x;
}

double uniform(dsp::FastRandom& rng) {
    return static_cast<double>(rng.nextUnipolar());
}

double midiHz(int note) {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

// ---------------------------------------------------------------- sounds

std::vector<float> stadiumHorn(double fs) {
    // A bright, slightly detuned chord through a horn-like resonance,
    // three blasts, the last one long.
    Canvas c(fs, 2.6);
    const std::array<double, 4> chord{midiHz(57), midiHz(61), midiHz(64), midiHz(69)};
    const std::array<std::pair<double, double>, 3> blasts{{{0.0, 0.32}, {0.42, 0.32}, {0.84, 1.4}}};
    for (const auto& [at, length] : blasts) {
        std::vector<float> blast(c.frames(length), 0.0F);
        for (std::size_t k = 0; k < chord.size(); ++k) {
            const double detune = 1.0 + 0.004 * (static_cast<double>(k) - 1.5);
            const auto voice = c.tone(Waveform::Saw, chord[k] * detune, chord[k] * detune * 0.985,
                                      length, 0.02, 4.0);
            for (std::size_t i = 0; i < blast.size(); ++i) {
                blast[i] += 0.25F * voice[i];
            }
        }
        c.filter(blast, [](double) { return 1900.0; }, 2.2F, 0);
        c.add(blast, at);
    }
    c.reverb(0.6, 1.2, 0.18F);
    return c.data();
}

std::vector<float> applause(double fs) {
    // Hundreds of single claps (short band-passed noise bursts) at random
    // times, swelling in and dying away.
    Canvas c(fs, 3.6);
    dsp::FastRandom rng(4242);
    for (int i = 0; i < 900; ++i) {
        const double t = 3.2 * std::pow(uniform(rng), 0.85);
        const double swell = std::sin(dsp::kPiD * std::min(1.0, t / 3.2));
        auto clap = c.noise(0.03, 0.0005, 0.006, static_cast<std::uint32_t>(i + 1));
        const double centre = 900.0 + 1800.0 * uniform(rng);
        c.filter(clap, [centre](double) { return centre; }, 1.4F, 1);
        c.add(clap, t, static_cast<float>(0.5 * swell * (0.4 + 0.6 * uniform(rng))));
    }
    c.reverb(0.7, 1.0, 0.22F);
    return c.data();
}

std::vector<float> taDa(double fs) {
    // A quick rising major arpeggio landing on a bright chord.
    Canvas c(fs, 2.4);
    const std::array<int, 4> notes{60, 64, 67, 72};
    for (std::size_t k = 0; k < notes.size(); ++k) {
        const double f = midiHz(notes[k]);
        auto pluck = c.tone(Waveform::Triangle, f, f, 0.5, 0.004, 0.18);
        c.add(pluck, 0.07 * static_cast<double>(k), 0.5F);
    }
    for (const int n : {72, 76, 79, 84}) {
        const double f = midiHz(n);
        auto ring = c.tone(Waveform::Sine, f, f, 1.8, 0.01, 0.6);
        const auto shimmer = c.tone(Waveform::Sine, f * 2.0, f * 2.0, 1.8, 0.01, 0.3);
        for (std::size_t i = 0; i < ring.size(); ++i) {
            ring[i] += 0.3F * shimmer[i];
        }
        c.add(ring, 0.32, 0.3F);
    }
    c.reverb(0.5, 1.2, 0.2F);
    return c.data();
}

std::vector<float> deflate(double fs) {
    // A wobbling slide down, like air escaping from a balloon.
    Canvas c(fs, 1.6);
    auto& out = c.data();
    dsp::Oscillator osc;
    osc.prepare(fs);
    osc.setWaveform(Waveform::Pulse25);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i) / fs;
        const double wobble = 1.0 + 0.06 * std::sin(dsp::kTwoPiD * 9.0 * t);
        osc.setFrequency(static_cast<float>(620.0 * std::pow(0.2, t / 1.6) * wobble));
        const double env = std::min(1.0, t / 0.03) * (1.0 - 0.6 * t / 1.6);
        out[i] = static_cast<float>(static_cast<double>(osc.next()) * env);
    }
    c.filter(out, [](double t) { return 2400.0 - 1200.0 * t; }, 1.0F, 0);
    return c.data();
}

std::vector<float> drumHit(const Canvas& c, double f0, double f1, double decay, float noiseMix,
                           std::uint32_t seed) {
    auto body = c.tone(Waveform::Sine, f0, f1, decay * 5.0, 0.001, decay);
    auto snap = c.noise(decay * 5.0, 0.0005, decay * 0.4, seed);
    c.filter(snap, [](double) { return 3500.0; }, 0.8F, 1);
    for (std::size_t i = 0; i < body.size(); ++i) {
        body[i] += noiseMix * snap[i];
    }
    return body;
}

std::vector<float> cymbal(const Canvas& c, double seconds, double decay, std::uint32_t seed) {
    auto x = c.noise(seconds, 0.001, decay, seed);
    c.filter(x, [](double) { return 7000.0; }, 0.7F, 2);
    return x;
}

std::vector<float> rimshot(double fs) {
    Canvas c(fs, 1.8);
    c.add(drumHit(c, 190.0, 150.0, 0.09, 0.3F, 7), 0.0, 0.8F);
    c.add(drumHit(c, 150.0, 115.0, 0.11, 0.3F, 8), 0.18, 0.8F);
    c.add(cymbal(c, 1.4, 0.35, 9), 0.42, 0.45F);
    c.reverb(0.4, 0.6, 0.15F);
    return c.data();
}

std::vector<float> drumRoll(double fs) {
    // A snare roll that speeds up and swells, then a crash.
    Canvas c(fs, 3.4);
    double t = 0.0;
    std::uint32_t seed = 11;
    while (t < 2.1) {
        const double progress = t / 2.1;
        auto hit = drumHit(c, 230.0, 200.0, 0.025, 1.4F, seed++);
        c.add(hit, t, static_cast<float>(0.15 + 0.35 * progress));
        t += 0.075 - 0.035 * progress;
    }
    c.add(drumHit(c, 110.0, 60.0, 0.18, 0.2F, 99), 2.15, 1.0F);
    c.add(cymbal(c, 1.6, 0.5, 100), 2.15, 0.6F);
    c.reverb(0.5, 0.8, 0.15F);
    return c.data();
}

std::vector<float> fanfare(double fs) {
    // A short brass call on an original rhythm: three pickups and a held
    // top note, as filtered saw "brass" in thirds.
    Canvas c(fs, 2.4);
    struct Note {
        int pitch;
        double at;
        double length;
    };
    const std::array<Note, 6> phrase{{{67, 0.0, 0.14},
                                      {67, 0.16, 0.14},
                                      {67, 0.32, 0.14},
                                      {72, 0.48, 0.32},
                                      {71, 0.82, 0.16},
                                      {76, 1.0, 1.1}}};
    for (const auto& n : phrase) {
        for (const int interval : {0, 4}) {
            const double f = midiHz(n.pitch + interval - 12);
            auto brass = c.tone(Waveform::Saw, f, f, n.length, 0.03, 2.0);
            c.filter(
                brass, [](double t) { return 900.0 + 2600.0 * std::min(1.0, t / 0.06); }, 1.1F, 0);
            c.add(brass, n.at, 0.3F);
        }
    }
    c.reverb(0.7, 1.4, 0.2F);
    return c.data();
}

std::vector<float> laser(double fs) {
    // Two fast falling zaps with a ring-modulated edge.
    Canvas c(fs, 0.9);
    for (int k = 0; k < 2; ++k) {
        auto zap = c.tone(Waveform::Square, 2200.0, 180.0, 0.32, 0.002, 0.12);
        const auto ring = c.tone(Waveform::Sine, 3100.0, 900.0, 0.32, 0.0, 0.0);
        for (std::size_t i = 0; i < zap.size(); ++i) {
            zap[i] *= 0.6F + 0.4F * ring[i];
        }
        c.add(zap, 0.36 * k, 0.5F);
    }
    c.filter(c.data(), [](double) { return 6000.0; }, 0.7F, 0);
    return c.data();
}

std::vector<float> boing(double fs) {
    // A spring: a low sine with fast, decaying vibrato and a pitch rise.
    Canvas c(fs, 1.2);
    auto& out = c.data();
    double phase = 0.0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const double t = static_cast<double>(i) / fs;
        const double vibrato = 1.0 + 0.35 * std::exp(-t / 0.35) * std::sin(dsp::kTwoPiD * 14.0 * t);
        const double f = (110.0 + 140.0 * std::min(1.0, t / 0.25)) * vibrato;
        phase += f / fs;
        const double env = std::min(1.0, t / 0.004) * std::exp(-t / 0.35);
        out[i] = static_cast<float>(std::sin(dsp::kTwoPiD * phase) * env +
                                    0.3 * std::sin(dsp::kTwoPiD * 2.0 * phase) * env);
    }
    return c.data();
}

std::vector<float> whoosh(double fs) {
    // Noise through a resonant band-pass sweeping up and back down.
    Canvas c(fs, 0.9);
    auto x = c.noise(0.9, 0.35, 0.18, 77);
    c.filter(
        x, [](double t) { return 300.0 + 2600.0 * std::sin(dsp::kPiD * std::min(1.0, t / 0.9)); },
        3.0F, 1);
    c.add(x, 0.0, 1.0F);
    return c.data();
}

std::vector<float> explosion(double fs) {
    // A sub-bass thump, a low-passed crackling burst that darkens, and a
    // long rumble.
    Canvas c(fs, 3.0);
    c.add(c.tone(Waveform::Sine, 70.0, 32.0, 1.2, 0.002, 0.25), 0.0, 0.9F);
    auto burst = c.noise(4.0, 0.004, 0.55, 5);
    c.filter(burst, [](double t) { return 4000.0 * std::exp(-t / 0.35) + 120.0; }, 0.8F, 0);
    c.add(burst, 0.0, 1.0F);
    dsp::BrownNoise rumble;
    std::vector<float> low(c.frames(4.0));
    for (std::size_t i = 0; i < low.size(); ++i) {
        const double t = static_cast<double>(i) / fs;
        const double fade = std::clamp((4.0 - t) / 0.8, 0.0, 1.0);
        low[i] = rumble.next() * static_cast<float>(std::exp(-t / 0.9) * fade);
    }
    c.add(low, 0.02, 0.8F);
    c.reverb(0.9, 2.0, 0.25F);
    return c.data();
}

std::vector<float> glitch(double fs) {
    // Stuttering fragments of a bit-crushed square, held and repeated.
    Canvas c(fs, 1.2);
    dsp::FastRandom rng(2024);
    double t = 0.0;
    while (t < 1.0) {
        const double length = 0.03 + 0.07 * uniform(rng);
        const double f = 200.0 * std::pow(2.0, 4.0 * uniform(rng));
        auto frag = c.tone(Waveform::Square, f, f * (0.7 + 0.6 * uniform(rng)), length, 0.001, 0.0);
        const float step = 0.25F;
        for (float& s : frag) {
            s = std::round(s / step) * step;
        }
        const int repeats = 1 + static_cast<int>(rng.nextUnipolar() * 3.0F);
        for (int r = 0; r < repeats; ++r) {
            c.add(frag, t, 0.35F);
            t += length;
        }
        t += 0.02 * uniform(rng);
    }
    return c.data();
}

std::vector<float> crickets(double fs) {
    // Night chirps: pulses of a ~4.6 kHz tone in groups of three, from two
    // insects at different pitches and rhythms.
    Canvas c(fs, 4.0);
    for (const auto& [pitch, period, offset] : std::array<std::tuple<double, double, double>, 2>{
             {{4600.0, 0.62, 0.0}, {4350.0, 0.81, 0.27}}}) {
        for (int group = 0; offset + group * period < 3.6; ++group) {
            const double t = offset + group * period;
            for (int p = 0; p < 3; ++p) {
                auto chirp = c.tone(Waveform::Sine, pitch, pitch * 1.01, 0.03, 0.004, 0.01);
                c.add(chirp, t + 0.045 * p, 0.25F);
            }
        }
    }
    c.reverb(0.8, 1.2, 0.3F);
    return c.data();
}

std::vector<float> heartbeat(double fs) {
    // "Lub-dub" three times: two soft low thumps per beat.
    Canvas c(fs, 3.0);
    for (int beat = 0; beat < 3; ++beat) {
        const double at = 0.85 * beat;
        c.add(c.tone(Waveform::Sine, 62.0, 45.0, 0.4, 0.01, 0.07), at, 1.0F);
        c.add(c.tone(Waveform::Sine, 75.0, 52.0, 0.35, 0.01, 0.06), at + 0.23, 0.75F);
    }
    c.filter(c.data(), [](double) { return 300.0; }, 0.7F, 0);
    return c.data();
}

std::vector<float> wrongAnswer(double fs) {
    // Two low, beating square tones: a flat buzz, twice.
    Canvas c(fs, 1.2);
    for (int k = 0; k < 2; ++k) {
        const double length = k == 0 ? 0.28 : 0.6;
        auto a = c.tone(Waveform::Square, 110.0, 108.0, length, 0.005, 3.0);
        const auto b = c.tone(Waveform::Square, 116.5, 114.0, length, 0.005, 3.0);
        for (std::size_t i = 0; i < a.size(); ++i) {
            a[i] = 0.5F * (a[i] + b[i]);
        }
        c.filter(a, [](double) { return 1500.0; }, 0.8F, 0);
        c.add(a, 0.38 * k, 0.6F);
    }
    return c.data();
}

std::vector<float> rightAnswer(double fs) {
    // Two bell tones a fifth apart, with inharmonic partials.
    Canvas c(fs, 2.0);
    for (const auto& [note, at] : std::array<std::pair<int, double>, 2>{{{81, 0.0}, {88, 0.13}}}) {
        const double f = midiHz(note);
        for (const auto& [ratio, gain, decay] : std::array<std::tuple<double, float, double>, 3>{
                 {{1.0, 0.6F, 0.6}, {2.76, 0.2F, 0.25}, {5.4, 0.08F, 0.12}}}) {
            c.add(c.tone(Waveform::Sine, f * ratio, f * ratio, 1.6, 0.002, decay), at, gain);
        }
    }
    c.reverb(0.5, 1.0, 0.15F);
    return c.data();
}

std::vector<float> alarm(double fs) {
    // Alternating two-tone siren.
    Canvas c(fs, 2.2);
    for (int k = 0; k < 8; ++k) {
        const double f = k % 2 == 0 ? 880.0 : 660.0;
        auto beep = c.tone(Waveform::Square, f, f, 0.25, 0.005, 0.0);
        c.filter(beep, [](double) { return 3000.0; }, 0.7F, 0);
        c.add(beep, 0.26 * k, 0.4F);
    }
    return c.data();
}

std::vector<float> censorBleep(double fs) {
    Canvas c(fs, 0.65);
    c.add(c.tone(Waveform::Sine, 1000.0, 1000.0, 0.6, 0.005, 0.0), 0.0, 0.5F);
    return c.data();
}

using Renderer = std::vector<float> (*)(double);

Renderer rendererFor(std::string_view id) {
    struct Entry {
        std::string_view id;
        Renderer render;
    };
    static constexpr std::array<Entry, 18> kRenderers{{
        {"stadium-horn", &stadiumHorn},
        {"applause", &applause},
        {"ta-da", &taDa},
        {"deflate", &deflate},
        {"rimshot", &rimshot},
        {"drum-roll", &drumRoll},
        {"fanfare", &fanfare},
        {"laser", &laser},
        {"boing", &boing},
        {"whoosh", &whoosh},
        {"explosion", &explosion},
        {"glitch", &glitch},
        {"crickets", &crickets},
        {"heartbeat", &heartbeat},
        {"wrong-answer", &wrongAnswer},
        {"right-answer", &rightAnswer},
        {"alarm", &alarm},
        {"censor-bleep", &censorBleep},
    }};
    for (const Entry& e : kRenderers) {
        if (e.id == id) {
            return e.render;
        }
    }
    return nullptr;
}

} // namespace

std::span<const BuiltinSound> builtinSounds() noexcept {
    return kSounds;
}

Result<std::vector<float>> renderBuiltinSound(std::string_view id, double sampleRate) {
    const Renderer render = rendererFor(id);
    if (render == nullptr) {
        return makeError(ErrorCode::InvalidArgument,
                         "There is no built-in sound called \"" + std::string(id) + "\".");
    }
    return finish(render(sampleRate), sampleRate);
}

} // namespace vox::plugins
