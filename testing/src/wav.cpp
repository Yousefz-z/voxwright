#include "vox/testing/wav.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace vox::testing {
namespace {

std::uint32_t readU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) | (static_cast<std::uint32_t>(p[3]) << 24U);
}

std::uint16_t readU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8U));
}

void putU32(std::ofstream& out, std::uint32_t v) {
    const std::array<char, 4> b{static_cast<char>(v & 0xFFU), static_cast<char>((v >> 8U) & 0xFFU),
                                static_cast<char>((v >> 16U) & 0xFFU),
                                static_cast<char>((v >> 24U) & 0xFFU)};
    out.write(b.data(), b.size());
}

void putU16(std::ofstream& out, std::uint16_t v) {
    const std::array<char, 2> b{static_cast<char>(v & 0xFFU), static_cast<char>((v >> 8U) & 0xFFU)};
    out.write(b.data(), b.size());
}

} // namespace

Result<WavData> readWav(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return makeError(ErrorCode::FileNotFound, "Cannot open WAV file: " + path.string());
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return makeError(ErrorCode::UnsupportedAudioFile, "Not a RIFF/WAVE file: " + path.string());
    }
    std::uint16_t format = 0;
    std::uint16_t bits = 0;
    WavData wav;
    const std::uint8_t* data = nullptr;
    std::size_t dataSize = 0;
    std::size_t pos = 12;
    while (pos + 8 <= bytes.size()) {
        const std::uint8_t* chunk = bytes.data() + pos;
        const std::size_t size = readU32(chunk + 4);
        const std::size_t available = bytes.size() - pos - 8;
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16 && available >= 16) {
            format = readU16(chunk + 8);
            wav.channels = readU16(chunk + 10);
            wav.sampleRate = static_cast<int>(readU32(chunk + 12));
            bits = readU16(chunk + 22);
            if (format == 0xFFFE && size >= 40 && available >= 40) {
                format = readU16(chunk + 32); // WAVE_FORMAT_EXTENSIBLE sub-format
            }
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data = chunk + 8;
            dataSize = std::min(size, available);
        }
        pos += 8 + size + (size & 1U);
    }
    if (data == nullptr || wav.channels <= 0 || wav.sampleRate <= 0) {
        return makeError(ErrorCode::DecodeFailed,
                         "WAV file has no fmt or data chunk: " + path.string());
    }
    const std::size_t bytesPerSample = bits / 8U;
    if (bytesPerSample == 0) {
        return makeError(ErrorCode::DecodeFailed,
                         "WAV file has 0 bits per sample: " + path.string());
    }
    const std::size_t count = dataSize / bytesPerSample;
    wav.samples.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t* s = data + i * bytesPerSample;
        if (format == 3 && bits == 32) {
            float f = 0.0F;
            std::memcpy(&f, s, 4);
            wav.samples[i] = f;
        } else if (format == 1 && bits == 16) {
            wav.samples[i] = static_cast<float>(static_cast<std::int16_t>(readU16(s))) / 32768.0F;
        } else if (format == 1 && bits == 24) {
            const auto v = static_cast<std::int32_t>((static_cast<std::uint32_t>(s[0]) << 8U) |
                                                     (static_cast<std::uint32_t>(s[1]) << 16U) |
                                                     (static_cast<std::uint32_t>(s[2]) << 24U));
            wav.samples[i] = static_cast<float>(v >> 8) / 8388608.0F; // arithmetic shift (C++20)
        } else if (format == 1 && bits == 32) {
            const auto v = static_cast<std::int32_t>(readU32(s));
            wav.samples[i] = static_cast<float>(static_cast<double>(v) / 2147483648.0);
        } else {
            return makeError(ErrorCode::UnsupportedAudioFile,
                             "Unsupported WAV encoding (format " + std::to_string(format) + ", " +
                                 std::to_string(bits) + " bits): " + path.string());
        }
    }
    return wav;
}

Status writeWav(const std::filesystem::path& path, std::span<const float> samples, int sampleRate,
                int channels) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return makeError(ErrorCode::FileWriteFailed, "Cannot create WAV file: " + path.string());
    }
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));
    out.write("RIFF", 4);
    putU32(out, 36 + dataBytes);
    out.write("WAVEfmt ", 8);
    putU32(out, 16);
    putU16(out, 3); // IEEE float
    putU16(out, static_cast<std::uint16_t>(channels));
    putU32(out, static_cast<std::uint32_t>(sampleRate));
    putU32(out, static_cast<std::uint32_t>(sampleRate * channels) * 4U);
    putU16(out, static_cast<std::uint16_t>(channels * 4));
    putU16(out, 32);
    out.write("data", 4);
    putU32(out, dataBytes);
    for (const float s : samples) {
        std::array<char, 4> b{};
        std::memcpy(b.data(), &s, 4);
        out.write(b.data(), 4);
    }
    if (!out) {
        return makeError(ErrorCode::FileWriteFailed,
                         "Failed while writing WAV file: " + path.string());
    }
    return {};
}

} // namespace vox::testing
