#include "engine_test_helpers.hpp"

#include <vox/engine/soundboard_player.hpp>
#include <vox/testing/alloc_trap.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace vox::engine;
using namespace vox::engine::test;

namespace {

/// A player with clips 0..2 installed: 0 = 0.1 s at 0.5, 1 = 0.1 s at 0.25,
/// 2 = a 0.1 s ramp from 0 to 1 (position is readable from the value).
class PlayerFixture {
public:
    PlayerFixture() {
        player.prepare(kFs);
        clips.push_back(std::make_unique<Clip>(Clip{std::vector<float>(4800, 0.5F)}));
        clips.push_back(std::make_unique<Clip>(Clip{std::vector<float>(4800, 0.25F)}));
        std::vector<float> ramp(4800);
        for (std::size_t i = 0; i < ramp.size(); ++i) {
            ramp[i] = static_cast<float>(i) / 4800.0F;
        }
        clips.push_back(std::make_unique<Clip>(Clip{std::move(ramp)}));
        for (std::uint32_t id = 0; id < clips.size(); ++id) {
            CHECK(player.install(id, clips[id].get()) == nullptr);
        }
    }
    PlayerFixture(const PlayerFixture&) = delete;
    PlayerFixture& operator=(const PlayerFixture&) = delete;
    PlayerFixture(PlayerFixture&&) = delete;
    PlayerFixture& operator=(PlayerFixture&&) = delete;
    ~PlayerFixture() {
        for (std::uint32_t id = 0; id < clips.size(); ++id) {
            static_cast<void>(player.remove(id));
        }
    }

    /// Renders `frames` and returns the virtual-mic bus.
    std::vector<float> render(std::size_t frames) {
        std::vector<float> all(frames);
        monitor.assign(frames, 0.0F);
        for (std::size_t pos = 0; pos < frames; pos += 128) {
            const std::size_t n = std::min<std::size_t>(128, frames - pos);
            player.process(std::span<float>(all).subspan(pos, n),
                           std::span<float>(monitor).subspan(pos, n));
            for (const std::uint32_t id : player.takeFinished()) {
                finished.push_back(id);
            }
        }
        return all;
    }

    SoundboardPlayer player;
    std::vector<std::unique_ptr<Clip>> clips;
    std::vector<float> monitor;
    std::vector<std::uint32_t> finished;
};

SoundOptions mode(PlayMode m) {
    SoundOptions o;
    o.mode = m;
    return o;
}

} // namespace

TEST_CASE("A sound plays once with 5 ms fades and reports when it ends", "[engine][sounds]") {
    PlayerFixture f;
    f.player.trigger(0, mode(PlayMode::Restart), true);
    const auto out = f.render(9600);
    CHECK(out[0] < 0.01F);                    // fade in, no click
    CHECK(std::abs(out[480] - 0.5F) < 1e-6F); // full level after 5 ms
    CHECK(out[4700] == 0.5F);
    CHECK(out[4800] == 0.0F); // clip ended
    CHECK(f.finished == std::vector<std::uint32_t>{0});
    CHECK(f.player.activeVoices() == 0);
}

TEST_CASE("Restart mode starts over and fades the old copy out", "[engine][sounds]") {
    PlayerFixture f;
    f.player.trigger(2, mode(PlayMode::Restart), true);
    static_cast<void>(f.render(2400)); // halfway through the ramp
    f.player.trigger(2, mode(PlayMode::Restart), true);
    CHECK(f.player.activeVoices() == 2);
    const auto out = f.render(2400);
    CHECK(f.player.activeVoices() == 1);
    // The new copy is near the start of the ramp: values are small again.
    CHECK(out[1000] < 0.25F);
    CHECK(maxStep(out) < 0.06F);
    CHECK(f.finished.empty()); // a copy is still playing
}

TEST_CASE("Toggle mode stops on the second press", "[engine][sounds]") {
    PlayerFixture f;
    f.player.trigger(0, mode(PlayMode::Toggle), true);
    static_cast<void>(f.render(1200));
    CHECK(f.player.isPlaying(0));
    f.player.trigger(0, mode(PlayMode::Toggle), false); // key up does nothing
    CHECK(f.player.isPlaying(0));
    f.player.trigger(0, mode(PlayMode::Toggle), true);
    const auto out = f.render(1200);
    CHECK(maxStep(out) < 0.0021F); // 5 ms fade from 0.5: 0.5 / 240 per sample
    CHECK(out[300] == 0.0F);
    CHECK(f.finished == std::vector<std::uint32_t>{0});
}

TEST_CASE("Pause mode pauses and resumes where it left off", "[engine][sounds]") {
    PlayerFixture f;
    f.player.trigger(2, mode(PlayMode::Pause), true);
    static_cast<void>(f.render(1200));
    f.player.trigger(2, mode(PlayMode::Pause), true); // pause
    const auto paused = f.render(2400);
    CHECK(paused[2000] == 0.0F);
    CHECK(f.finished.empty());
    f.player.trigger(2, mode(PlayMode::Pause), true); // resume
    const auto resumed = f.render(1200);
    // Playback continues where it stopped: 1200 samples, plus 240 more
    // played during the pause fade, plus 600 now (ramp value 2040 / 4800),
    // not from the start.
    CHECK(std::abs(resumed[600] - 2040.0F / 4800.0F) < 0.002F);
}

TEST_CASE("Overlap mode layers copies", "[engine][sounds]") {
    PlayerFixture f;
    for (int i = 0; i < 3; ++i) {
        f.player.trigger(1, mode(PlayMode::Overlap), true);
    }
    const auto out = f.render(1200);
    CHECK(f.player.activeVoices() == 3);
    CHECK(std::abs(out[1000] - 0.75F) < 1e-5F);
}

TEST_CASE("Hold mode loops while held and stops on release", "[engine][sounds]") {
    PlayerFixture f;
    f.player.trigger(0, mode(PlayMode::Hold), true);
    const auto held = f.render(14400); // three clip lengths
    CHECK(held[12000] == 0.5F);
    f.player.trigger(0, mode(PlayMode::Hold), false);
    const auto released = f.render(1200);
    CHECK(released[600] == 0.0F);
    CHECK(f.finished == std::vector<std::uint32_t>{0});
}

TEST_CASE("Sound options route, duck, and stop other sounds", "[engine][sounds]") {
    PlayerFixture f;
    SECTION("mute for me keeps the sound off the monitor") {
        SoundOptions o;
        o.muteForMe = true;
        f.player.trigger(0, o, true);
        const auto out = f.render(2400);
        CHECK(out[1000] == 0.5F);
        CHECK(f.monitor[1000] == 0.0F);
    }
    SECTION("mute others ducks every other sound") {
        f.player.trigger(0, {}, true);
        SoundOptions o;
        o.muteOthers = true;
        f.player.trigger(1, o, true);
        const auto out = f.render(2400);
        CHECK(std::abs(out[2000] - 0.25F) < 1e-6F); // only sound 1 remains
    }
    SECTION("stop others ends the sounds already playing") {
        f.player.trigger(0, {}, true);
        static_cast<void>(f.render(480));
        SoundOptions o;
        o.stopOthers = true;
        f.player.trigger(1, o, true);
        static_cast<void>(f.render(960));
        CHECK_FALSE(f.player.isPlaying(0));
        CHECK(f.player.isPlaying(1));
    }
    SECTION("mute voice ducks the microphone by 20 dB") {
        SoundOptions o;
        o.muteVoice = true;
        o.loop = true;
        f.player.trigger(0, o, true);
        static_cast<void>(f.render(4800));
        CHECK(std::abs(f.player.voiceDuck() - 0.1F) < 1e-6F);
        f.player.stopAll();
        static_cast<void>(f.render(4800));
        CHECK(f.player.voiceDuck() == 1.0F);
    }
    SECTION("gain") {
        SoundOptions o;
        o.gainDb = -6.0F;
        f.player.trigger(0, o, true);
        const auto out = f.render(1200);
        CHECK(std::abs(out[1000] - 0.5F * 0.501187F) < 1e-5F);
    }
}

TEST_CASE("The 33rd overlapping sound steals the voice nearest its end", "[engine][sounds]") {
    PlayerFixture f;
    for (int i = 0; i < 33; ++i) {
        f.player.trigger(1, mode(PlayMode::Overlap), true);
        static_cast<void>(f.render(16));
    }
    CHECK(f.player.activeVoices() == SoundboardPlayer::kMaxVoices);
}

TEST_CASE("Removing or replacing a clip stops it and returns the old one", "[engine][sounds]") {
    PlayerFixture f;
    f.player.trigger(0, {}, true);
    static_cast<void>(f.render(256));
    const Clip* old = f.player.remove(0);
    CHECK(old == f.clips[0].get());
    CHECK(f.player.activeVoices() == 0);
    CHECK(f.player.install(0, f.clips[1].get()) == nullptr);
    CHECK(f.player.install(0, f.clips[0].get()) == f.clips[1].get());
    // Out-of-range slots hand the clip straight back.
    CHECK(f.player.install(SoundboardPlayer::kMaxClips, f.clips[2].get()) == f.clips[2].get());
    f.player.trigger(5, {}, true); // empty slot: nothing happens
    CHECK(f.player.activeVoices() == 0);
}

TEST_CASE("Soundboard playback does not allocate", "[engine][sounds][realtime]") {
    if (!vox::testing::AllocationTrap::available()) {
        SKIP("Allocation tracking is unavailable in this sanitizer build.");
    }
    PlayerFixture f;
    std::vector<float> all(128);
    std::vector<float> monitor(128);
    std::size_t allocations = 0;
    {
        const vox::testing::AllocationTrap trap;
        for (int i = 0; i < 200; ++i) {
            if (i % 7 == 0) {
                f.player.trigger(static_cast<std::uint32_t>(i % 3), mode(PlayMode::Overlap), true);
            }
            f.player.process(all, monitor);
            static_cast<void>(f.player.takeFinished());
        }
        allocations = trap.allocations();
    }
    CHECK(allocations == 0);
}
