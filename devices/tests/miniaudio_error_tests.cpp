// Each miniaudio failure maps to its own error code and an actionable message.

#include "miniaudio_errors.hpp"

#include <catch2/catch_test_macros.hpp>

using vox::ErrorCode;
using vox::devices::DeviceKind;
using vox::devices::mapOpenError;

TEST_CASE("Device open failures map to specific errors", "[devices][errors]") {
    struct Case {
        ma_result result;
        DeviceKind kind;
        bool exclusive;
        ErrorCode expected;
        const char* mustMention;
    };
    for (const Case c : {
             Case{MA_BUSY, DeviceKind::Playback, false, ErrorCode::DeviceInUseExclusive,
                  "exclusive control"},
             Case{MA_SHARE_MODE_NOT_SUPPORTED, DeviceKind::Playback, true,
                  ErrorCode::DeviceInUseExclusive, "Turn off exclusive mode"},
             Case{MA_ACCESS_DENIED, DeviceKind::Capture, false,
                  ErrorCode::MicrophonePermissionDenied, "Privacy"},
             Case{MA_ACCESS_DENIED, DeviceKind::Playback, false, ErrorCode::DeviceStartFailed,
                  "refused access"},
             Case{MA_DEVICE_TYPE_NOT_SUPPORTED, DeviceKind::Capture, false,
                  ErrorCode::DeviceStartFailed, "cannot be used as a microphone"},
             Case{MA_FORMAT_NOT_SUPPORTED, DeviceKind::Playback, true,
                  ErrorCode::UnsupportedSampleFormat, "32-bit float"},
             Case{MA_INVALID_DEVICE_CONFIG, DeviceKind::Playback, false,
                  ErrorCode::UnsupportedSampleRate, "48000 Hz"},
             Case{MA_NO_DEVICE, DeviceKind::Capture, false, ErrorCode::DeviceNotFound,
                  "not connected"},
             Case{MA_DOES_NOT_EXIST, DeviceKind::Playback, false, ErrorCode::DeviceNotFound,
                  "Settings > Audio"},
             Case{MA_ERROR, DeviceKind::Playback, false, ErrorCode::DeviceStartFailed,
                  "unplug and reconnect"},
         }) {
        const auto e = mapOpenError(c.result, c.kind, "USB Headset", c.exclusive);
        INFO("miniaudio result " << static_cast<int>(c.result) << ": " << e.message);
        CHECK(e.code == c.expected);
        CHECK(e.message.find(c.mustMention) != std::string::npos);
        CHECK(e.detail.rfind("miniaudio: ", 0) == 0);
    }
}

TEST_CASE("Messages name the device, or say default when there is none", "[devices][errors]") {
    CHECK(mapOpenError(MA_NO_DEVICE, DeviceKind::Capture, "Blue Yeti", false)
              .message.find("\"Blue Yeti\"") != std::string::npos);
    CHECK(mapOpenError(MA_NO_DEVICE, DeviceKind::Capture, "", false)
              .message.rfind("The default microphone", 0) == 0);
}
