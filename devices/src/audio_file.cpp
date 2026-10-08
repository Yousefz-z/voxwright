#include "vox/devices/audio_file.hpp"

#include <vox/dsp/resampler.hpp>

#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <system_error>

namespace vox::devices {
namespace {

std::string megabytes(std::uintmax_t bytes) {
    return std::to_string((bytes + 512ULL * 1024ULL) / (1024ULL * 1024ULL)) + " MB";
}

/// Reads every frame from an initialized decoder and finishes the clip.
Result<DecodedClip> drain(ma_decoder& decoder, const std::string& label, double targetRate,
                          const DecodeLimits& limits) {
    const ma_uint32 channels = decoder.outputChannels;
    const ma_uint32 rate = decoder.outputSampleRate;
    if (channels == 0 || rate == 0) {
        return makeError(ErrorCode::DecodeFailed,
                         "\"" + label +
                             "\" has no audio channels or an invalid sample rate. Re-export it as "
                             "WAV or MP3 and import it again.");
    }
    const auto maxFrames =
        static_cast<ma_uint64>(limits.maxSeconds * static_cast<double>(rate)) + 1;
    std::vector<float> mono;
    std::vector<float> chunk(4096 * static_cast<std::size_t>(channels));
    for (;;) {
        ma_uint64 read = 0;
        const ma_result r = ma_decoder_read_pcm_frames(&decoder, chunk.data(), 4096, &read);
        for (ma_uint64 f = 0; f < read; ++f) {
            float sum = 0.0F;
            for (ma_uint32 c = 0; c < channels; ++c) {
                sum += chunk[static_cast<std::size_t>(f) * channels + c];
            }
            mono.push_back(sum / static_cast<float>(channels));
        }
        if (mono.size() > maxFrames) {
            return makeError(
                ErrorCode::ClipTooLong,
                "\"" + label + "\" is longer than " +
                    std::to_string(static_cast<int>(limits.maxSeconds / 60.0)) +
                    " minutes. Trim it in an audio editor and import the shorter version.");
        }
        if (r == MA_AT_END || read == 0) {
            break;
        }
        if (r != MA_SUCCESS) {
            return makeError(ErrorCode::DecodeFailed,
                             "\"" + label +
                                 "\" is damaged partway through and could not be read completely. "
                                 "Re-export it and import it again.",
                             std::string("miniaudio: ") + ma_result_description(r));
        }
    }
    if (mono.empty()) {
        return makeError(ErrorCode::DecodeFailed, "\"" + label + "\" contains no audio.");
    }
    DecodedClip clip;
    clip.sourceSampleRate = rate;
    clip.sourceChannels = channels;
    clip.sampleRate = targetRate;
    clip.samples =
        dsp::resample(mono, static_cast<double>(rate), targetRate, dsp::ResamplerQuality::Best);
    clip.durationSeconds = static_cast<double>(clip.samples.size()) / targetRate;
    return clip;
}

Error unsupported(const std::string& label, ma_result r) {
    return makeError(
        ErrorCode::UnsupportedAudioFile,
        "\"" + label +
            "\" is not a WAV, MP3, FLAC, or OGG file that Voxwright can read. Convert it "
            "to WAV or MP3 and import it again.",
        std::string("miniaudio: ") + ma_result_description(r));
}

} // namespace

const std::vector<std::string>& supportedAudioExtensions() {
    static const std::vector<std::string> kExtensions{".wav", ".mp3", ".flac", ".ogg"};
    return kExtensions;
}

Result<DecodedClip> decodeAudioFile(const std::filesystem::path& path, double targetRate,
                                    const DecodeLimits& limits) {
    const std::string label = path.filename().string();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return makeError(ErrorCode::FileNotFound,
                         "\"" + path.string() +
                             "\" was not found. It may have been moved or deleted; import it "
                             "again from its new location.");
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return makeError(ErrorCode::FileUnreadable,
                         "\"" + label +
                             "\" could not be read. Check that you have permission to open it.",
                         ec.message());
    }
    if (size > limits.maxFileBytes) {
        return makeError(ErrorCode::FileTooLarge,
                         "\"" + label + "\" is " + megabytes(size) + "; the limit is " +
                             megabytes(limits.maxFileBytes) +
                             ". Trim it or export it as MP3 and import it again.");
    }
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
#if defined(_WIN32)
    const ma_result r = ma_decoder_init_file_w(path.c_str(), &config, &decoder);
#else
    const ma_result r = ma_decoder_init_file(path.c_str(), &config, &decoder);
#endif
    if (r != MA_SUCCESS) {
        if (r == MA_ACCESS_DENIED) {
            return makeError(
                ErrorCode::FileUnreadable,
                "\"" + label +
                    "\" could not be opened. Check that you have permission to read it.");
        }
        return unsupported(label, r);
    }
    auto clip = drain(decoder, label, targetRate, limits);
    ma_decoder_uninit(&decoder);
    return clip;
}

Result<DecodedClip> decodeAudioMemory(std::span<const std::uint8_t> encoded,
                                      const std::string& label, double targetRate,
                                      const DecodeLimits& limits) {
    if (encoded.size() > limits.maxFileBytes) {
        return makeError(ErrorCode::FileTooLarge,
                         "\"" + label + "\" is " + megabytes(encoded.size()) + "; the limit is " +
                             megabytes(limits.maxFileBytes) + ".");
    }
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    const ma_result r = ma_decoder_init_memory(encoded.data(), encoded.size(), &config, &decoder);
    if (r != MA_SUCCESS) {
        return unsupported(label, r);
    }
    auto clip = drain(decoder, label, targetRate, limits);
    ma_decoder_uninit(&decoder);
    return clip;
}

} // namespace vox::devices
