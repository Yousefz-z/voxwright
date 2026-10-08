#pragma once

#include "vox/dsp/biquad.hpp"
#include "vox/dsp/noise.hpp"
#include "vox/dsp/oscillator.hpp"
#include "vox/dsp/smoothed_value.hpp"
#include "vox/dsp/svf.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace vox::dsp {

/// Real-time synthesized background soundscapes. Everything is generated
/// from noise and oscillators, so there are no audio assets and no audible
/// loop point. The ambience is added to the signal passing through.
class Ambience {
public:
    enum class Kind {
        Rain,
        Wind,
        Crowd,
        EngineHum,
        RadioStatic,
        SpaceDrone,
        CaveDrips,
        Underwater,
        Fire,
        Traffic,
        Computer,
    };

    void prepare(double sampleRate);
    void reset() noexcept;

    void setKind(Kind kind) noexcept;
    void setLevelDb(float db) noexcept { level_.setTarget(db); }
    /// Brightness of the soundscape (low-pass cutoff).
    void setToneHz(float hz) noexcept;
    /// Global "background effects" switch; fades over 200 ms.
    void setEnabled(bool enabled) noexcept { enabled_.setTarget(enabled ? 1.0F : 0.0F); }

    void process(std::span<float> block) noexcept;
    /// Generates one sample of the soundscape (exposed for tests).
    [[nodiscard]] float generate() noexcept;

private:
    struct Ping {
        float phase = 0.0F;
        float increment = 0.0F;
        float glide = 1.0F;
        float amplitude = 0.0F;
        float decay = 0.0F;
    };

    float triggerPing(float minHz, float maxHz, float decayMs, float glide,
                      float amplitude) noexcept;
    [[nodiscard]] float pings() noexcept;
    [[nodiscard]] bool chance(float perSecond) noexcept;

    double sampleRate_ = 48000.0;
    Kind kind_ = Kind::Rain;
    FastRandom random_{0xB0B0U};
    PinkNoise pink_{0x1234U};
    BrownNoise brown_{0x4321U};
    std::array<StateVariableFilter, 4> filters_{};
    std::array<Lfo, 4> lfos_{};
    std::array<Oscillator, 4> oscillators_{};
    std::array<Ping, 8> pings_{};
    Biquad tone_;
    Biquad lowCut_;
    SmoothedValue level_;
    SmoothedValue enabled_;
    float envelope_ = 0.0F;
    float burst_ = 0.0F;
    int counter_ = 0;
};

/// Glitch effect: at random slice boundaries the last slice is repeated a
/// few times (stutter), with short crossfades at every splice.
class Stutter {
public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setSliceMs(float ms) noexcept;
    void setProbability(float p) noexcept { probability_ = std::clamp(p, 0.0F, 1.0F); }
    void setMaxRepeats(int n) noexcept { maxRepeats_ = std::clamp(n, 1, 8); }
    void process(std::span<float> block) noexcept;

private:
    double sampleRate_ = 48000.0;
    static constexpr std::size_t kBufferSize = std::size_t{1} << 16U;
    std::vector<float> buffer_;
    std::size_t written_ = 0;
    std::size_t slice_ = 4800;
    std::size_t fade_ = 96;
    std::size_t sinceBoundary_ = 0;
    std::size_t repeatStart_ = 0;
    std::size_t repeatOffset_ = 0;
    int repeatsLeft_ = 0;
    float probability_ = 0.15F;
    int maxRepeats_ = 3;
    float liveGain_ = 1.0F;
    FastRandom random_{0x51u};
};

} // namespace vox::dsp
