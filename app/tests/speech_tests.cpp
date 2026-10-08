#include "app_test_support.hpp"
#include "speech_controller.hpp"
#include "virtual_mic_check.hpp"

#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <QAudioFormat>
#include <QTest>
#include <QTextToSpeech>

#include <cstdint>
#include <cstring>

using namespace vox::app;
using namespace vox::app::test;
using Catch::Approx;

namespace {

template <class T>
QByteArray bytesOf(const std::vector<T>& v) {
    return {reinterpret_cast<const char*>(v.data()), static_cast<qsizetype>(v.size() * sizeof(T))};
}

QAudioFormat format(int rate, int channels, QAudioFormat::SampleFormat sample) {
    QAudioFormat f;
    f.setSampleRate(rate);
    f.setChannelCount(channels);
    f.setSampleFormat(sample);
    return f;
}

bool hasFlite() {
    return QTextToSpeech::availableEngines().contains(QStringLiteral("flite"));
}

} // namespace

TEST_CASE("Rendered speech in any format becomes mono at the engine rate", "[app][speech]") {
    // Stereo 16-bit at 16 kHz: left full scale, right silent, so mono is 0.5.
    std::vector<std::int16_t> stereo;
    for (int i = 0; i < 1600; ++i) {
        stereo.push_back(16384);
        stereo.push_back(-16384);
    }
    const auto mono =
        SpeechController::toEngineFormat(format(16000, 2, QAudioFormat::Int16), bytesOf(stereo));
    CHECK(mono.size() == Approx(4800).margin(48));
    CHECK(std::abs(mono[mono.size() / 2]) < 1e-3F);

    const std::vector<float> floats(480, 0.25F);
    const auto same =
        SpeechController::toEngineFormat(format(48000, 1, QAudioFormat::Float), bytesOf(floats));
    REQUIRE(same.size() == 480);
    CHECK(same[100] == 0.25F);

    const std::vector<std::uint8_t> eight(441, 192);
    const auto converted =
        SpeechController::toEngineFormat(format(44100, 1, QAudioFormat::UInt8), bytesOf(eight));
    CHECK(converted.size() == Approx(480).margin(5));
    CHECK(converted[240] == Approx(0.5F).margin(0.02F));

    CHECK(SpeechController::toEngineFormat(QAudioFormat{}, bytesOf(floats)).empty());
    CHECK(SpeechController::toEngineFormat(format(48000, 1, QAudioFormat::Float), {}).empty());
}

TEST_CASE("Typed text is refused with a reason when it cannot be spoken", "[app][speech][errors]") {
    TestApp t;
    auto* speech = t.context().speech();
    CHECK(speech->speak(QStringLiteral("   ")).contains(QStringLiteral("Type")));
    CHECK(speech->speak(QString(SpeechController::kMaxCharacters + 1, QLatin1Char('a')))
              .contains(QString::number(SpeechController::kMaxCharacters)));
}

TEST_CASE("Text is spoken into the virtual microphone", "[app][speech]") {
    if (!hasFlite()) {
        SKIP("The flite speech engine is not installed here.");
    }
    TestApp t({.cable = true, .speechEngine = QStringLiteral("flite")});
    auto* speech = t.context().speech();
    REQUIRE(QTest::qWaitFor([&] { return speech->available(); }, 5000));
    CHECK_FALSE(speech->voiceNames().isEmpty());
    REQUIRE(speech->speak(QStringLiteral("Hello from the voice changer.")).isEmpty());
    CHECK(speech->busy());
    // Rendering finishes on the event loop, then the engine plays it.
    REQUIRE(QTest::qWaitFor([&] { return !speech->rendering(); }, 10000));
    CHECK_FALSE(t.context().notifications()->contains(QStringLiteral("speech")));
    const auto out = t.record(3.0, {});
    CHECK(vox::testing::peakAbs(out) > 0.05F);
    // About two seconds of speech, then silence.
    const auto tail = std::span<const float>(out).last(24000);
    CHECK(vox::testing::peakAbs(tail) < 0.01F);
    CHECK_FALSE(speech->busy());
}
