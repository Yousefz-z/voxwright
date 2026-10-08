#include <vox/devices/audio_file.hpp>
#include <vox/testing/analysis.hpp>
#include <vox/testing/signals.hpp>
#include <vox/testing/wav.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace {

/// A file in the temp directory, removed when the test ends.
class TempFile {
public:
    explicit TempFile(const std::string& name)
        : path_(std::filesystem::temp_directory_path() /
                ("vox_devices_" + std::to_string(std::random_device{}()) + "_" + name)) {}
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

double strongestHz(std::span<const float> x, double rate) {
    constexpr std::size_t kFft = 16384;
    const auto db = vox::testing::powerSpectrumDb(x, kFft);
    const auto best =
        static_cast<std::size_t>(std::max_element(db.begin() + 1, db.end()) - db.begin());
    return static_cast<double>(best) * rate / static_cast<double>(kFft);
}

} // namespace

TEST_CASE("WAV at 44.1 kHz decodes to 48 kHz with pitch and length intact", "[devices][file]") {
    const TempFile file("tone.wav");
    const auto tone = vox::testing::sine(1000.0, 1.0, 44100.0, 0.5);
    REQUIRE(vox::testing::writeWav(file.path(), tone, 44100));

    const auto clip = vox::devices::decodeAudioFile(file.path());
    REQUIRE(clip);
    CHECK(clip.value().sourceSampleRate == 44100);
    CHECK(clip.value().sourceChannels == 1);
    CHECK(clip.value().sampleRate == 48000.0);
    CHECK(std::abs(clip.value().durationSeconds - 1.0) < 0.002);
    const auto middle = std::span<const float>(clip.value().samples).subspan(4800, 38400);
    CHECK(std::abs(strongestHz(middle, 48000.0) - 1000.0) < 3.0);
    CHECK(std::abs(vox::testing::peakAbs(middle) - 0.5F) < 0.01F);
}

TEST_CASE("Stereo files are mixed to mono by averaging", "[devices][file]") {
    const TempFile file("stereo.wav");
    const auto left = vox::testing::sine(440.0, 0.5, 48000.0, 0.8F);
    std::vector<float> interleaved(left.size() * 2, 0.0F);
    for (std::size_t i = 0; i < left.size(); ++i) {
        interleaved[2 * i] = left[i]; // right channel silent
    }
    REQUIRE(vox::testing::writeWav(file.path(), interleaved, 48000, 2));
    const auto clip = vox::devices::decodeAudioFile(file.path());
    REQUIRE(clip);
    CHECK(clip.value().sourceChannels == 2);
    REQUIRE(clip.value().samples.size() == left.size());
    CHECK(std::abs(vox::testing::peakAbs(clip.value().samples) - 0.4F) < 0.001F);
}

TEST_CASE("Decoding from memory gives the same result as from a file", "[devices][file]") {
    const TempFile file("memory.wav");
    const auto tone = vox::testing::sine(300.0, 0.25, 48000.0, 0.5);
    REQUIRE(vox::testing::writeWav(file.path(), tone, 48000));
    std::ifstream in(file.path(), std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
    const auto clip = vox::devices::decodeAudioMemory(bytes, "memory.wav");
    REQUIRE(clip);
    REQUIRE(clip.value().samples.size() == tone.size());
    for (std::size_t i = 0; i < tone.size(); i += 997) {
        CHECK(clip.value().samples[i] == tone[i]);
    }
}

TEST_CASE("Each decode failure has its own error", "[devices][file][errors]") {
    using vox::ErrorCode;
    SECTION("missing file") {
        const auto clip = vox::devices::decodeAudioFile("/nonexistent/folder/sound.wav");
        REQUIRE_FALSE(clip);
        CHECK(clip.error().code == ErrorCode::FileNotFound);
        CHECK(clip.error().message.find("not found") != std::string::npos);
    }
    SECTION("file over the size limit") {
        const TempFile file("big.wav");
        REQUIRE(vox::testing::writeWav(file.path(), vox::testing::silence(0.1), 48000));
        vox::devices::DecodeLimits limits;
        limits.maxFileBytes = 1000;
        const auto clip = vox::devices::decodeAudioFile(file.path(), 48000.0, limits);
        REQUIRE_FALSE(clip);
        CHECK(clip.error().code == ErrorCode::FileTooLarge);
    }
    SECTION("clip over the length limit") {
        const TempFile file("long.wav");
        REQUIRE(vox::testing::writeWav(file.path(), vox::testing::sine(200.0, 1.0), 48000));
        vox::devices::DecodeLimits limits;
        limits.maxSeconds = 0.5;
        const auto clip = vox::devices::decodeAudioFile(file.path(), 48000.0, limits);
        REQUIRE_FALSE(clip);
        CHECK(clip.error().code == ErrorCode::ClipTooLong);
    }
    SECTION("not an audio file") {
        const TempFile file("notes.wav");
        {
            std::ofstream out(file.path());
            out << "These are meeting notes, not audio.\n";
        }
        const auto clip = vox::devices::decodeAudioFile(file.path());
        REQUIRE_FALSE(clip);
        CHECK(clip.error().code == ErrorCode::UnsupportedAudioFile);
        CHECK(clip.error().message.find("notes.wav") != std::string::npos);
    }
    SECTION("valid header without audio") {
        const TempFile file("empty.wav");
        REQUIRE(vox::testing::writeWav(file.path(), std::vector<float>{}, 48000));
        const auto clip = vox::devices::decodeAudioFile(file.path());
        REQUIRE_FALSE(clip);
        CHECK(clip.error().code == ErrorCode::DecodeFailed);
    }
}

TEST_CASE("Supported extensions cover the soundboard formats", "[devices][file]") {
    const auto& ext = vox::devices::supportedAudioExtensions();
    for (const char* e : {".wav", ".mp3", ".flac", ".ogg"}) {
        CHECK(std::find(ext.begin(), ext.end(), e) != ext.end());
    }
}
