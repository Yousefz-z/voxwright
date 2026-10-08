// Every DSP block must be allocation-free in process() once prepared.

#include "test_helpers.hpp"

#include <vox/dsp/ambience.hpp>
#include <vox/dsp/distortion.hpp>
#include <vox/dsp/dynamics.hpp>
#include <vox/dsp/echo.hpp>
#include <vox/dsp/equalizer.hpp>
#include <vox/dsp/meters.hpp>
#include <vox/dsp/modulation.hpp>
#include <vox/dsp/multimode_filter.hpp>
#include <vox/dsp/noise_suppressor.hpp>
#include <vox/dsp/resonator.hpp>
#include <vox/dsp/ring_modulator.hpp>
#include <vox/dsp/vocoder.hpp>
#include <vox/dsp/whisper.hpp>
#include <vox/testing/alloc_trap.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace vox::dsp::test;

namespace {

template <class Effect>
void checkNoAllocations(Effect& effect, const char* name) {
    auto in = voice(130.0, 0.6);
    std::size_t allocations = 0;
    std::size_t deallocations = 0;
    {
        // Catch macros allocate, so counts are read before any assertion.
        const vox::testing::AllocationTrap trap;
        for (std::size_t pos = 0; pos + 200 <= in.size(); pos += 200) {
            effect.process(std::span<float>(in).subspan(pos, 200));
        }
        allocations = trap.allocations();
        deallocations = trap.deallocations();
    }
    INFO(name);
    CHECK(allocations == 0);
    CHECK(deallocations == 0);
    CHECK(vox::testing::allFinite(in));
}

} // namespace

TEST_CASE("Effects do not allocate on the audio thread", "[dsp][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    vox::dsp::Equalizer eq;
    eq.prepare(kFs);
    eq.setGainDb(vox::dsp::Equalizer::Peak1, 4.0F); // keep coefficients moving
    checkNoAllocations(eq, "Equalizer");

    vox::dsp::MultimodeFilter filter;
    filter.prepare(kFs);
    filter.setLfo(2.0F, 1.0F, vox::dsp::LfoShape::Sine);
    filter.setEnvelope(1.5F, 5.0F, 80.0F);
    checkNoAllocations(filter, "MultimodeFilter");

    vox::dsp::Distortion distortion;
    distortion.prepare(kFs);
    checkNoAllocations(distortion, "Distortion");

    vox::dsp::RingModulator ring;
    ring.prepare(kFs);
    checkNoAllocations(ring, "RingModulator");

    vox::dsp::FrequencyShifter shifter;
    shifter.prepare(kFs);
    shifter.setShiftHz(80.0F);
    checkNoAllocations(shifter, "FrequencyShifter");

    vox::dsp::Chorus chorus;
    chorus.prepare(kFs);
    checkNoAllocations(chorus, "Chorus");
    vox::dsp::Flanger flanger;
    flanger.prepare(kFs);
    checkNoAllocations(flanger, "Flanger");
    vox::dsp::Phaser phaser;
    phaser.prepare(kFs);
    checkNoAllocations(phaser, "Phaser");
    vox::dsp::Tremolo tremolo;
    tremolo.prepare(kFs);
    checkNoAllocations(tremolo, "Tremolo");
    vox::dsp::Vibrato vibrato;
    vibrato.prepare(kFs);
    checkNoAllocations(vibrato, "Vibrato");

    vox::dsp::Echo echo;
    echo.prepare(kFs);
    checkNoAllocations(echo, "Echo");
    vox::dsp::Reverb reverb;
    reverb.prepare(kFs);
    checkNoAllocations(reverb, "Reverb");

    vox::dsp::Compressor compressor;
    compressor.prepare(kFs);
    checkNoAllocations(compressor, "Compressor");
    vox::dsp::Limiter limiter;
    limiter.prepare(kFs);
    checkNoAllocations(limiter, "Limiter");
    vox::dsp::NoiseGate gate;
    gate.prepare(kFs);
    checkNoAllocations(gate, "NoiseGate");
    vox::dsp::NoiseSuppressor suppressor;
    suppressor.prepare(kFs);
    checkNoAllocations(suppressor, "NoiseSuppressor");

    vox::dsp::CombResonator comb;
    comb.prepare(kFs);
    checkNoAllocations(comb, "CombResonator");
    vox::dsp::ChannelVocoder vocoder;
    vocoder.prepare(kFs, 512);
    vocoder.setFollowPitch(true, 0.0F);
    checkNoAllocations(vocoder, "ChannelVocoder");
    vox::dsp::Harmonizer harmonizer;
    harmonizer.prepare(kFs, 512);
    harmonizer.setVoice(0, true, 4.0F, -3.0F, 0.0F);
    harmonizer.setVoice(1, true, -12.0F, -3.0F, 0.0F);
    checkNoAllocations(harmonizer, "Harmonizer");
    vox::dsp::Whisper whisper;
    whisper.prepare(kFs);
    checkNoAllocations(whisper, "Whisper");
    vox::dsp::Ambience ambience;
    ambience.prepare(kFs);
    checkNoAllocations(ambience, "Ambience");
    vox::dsp::Stutter stutter;
    stutter.prepare(kFs);
    checkNoAllocations(stutter, "Stutter");

    vox::dsp::FeedbackDetector feedback;
    feedback.prepare(kFs);
    auto in = voice(130.0, 0.5);
    std::size_t allocations = 0;
    {
        const vox::testing::AllocationTrap trap;
        feedback.process(in);
        allocations = trap.allocations();
    }
    CHECK(allocations == 0);
}
