#pragma once

#include <string>
#include <string_view>

namespace vox {

/// Every failure mode the application distinguishes. Each code maps to a
/// specific, actionable message created at the point of failure; there is no
/// generic "something went wrong" code.
enum class ErrorCode {
    // Audio devices and backends.
    BackendInitFailed,
    DeviceNotFound,
    DeviceInUseExclusive,
    DeviceDisconnected,
    DeviceStartFailed,
    UnsupportedSampleRate,
    UnsupportedChannelCount,
    UnsupportedSampleFormat,
    MicrophonePermissionDenied,
    // Real-time engine.
    BufferUnderrun,
    BufferOverrun,
    EngineNotRunning,
    CommandQueueFull,
    // Files and sounds.
    FileNotFound,
    FileUnreadable,
    FileTooLarge,
    ClipTooLong,
    UnsupportedAudioFile,
    DecodeFailed,
    FileWriteFailed,
    // Voices and presets.
    PresetParseError,
    UnknownEffect,
    UnknownParameter,
    ParameterOutOfRange,
    InvalidPreset,
    // Hotkeys and platform integration.
    HotkeyConflict,
    HotkeyRegistrationFailed,
    HotkeysUnsupported,
    AutostartFailed,
    // Programming errors surfaced at API boundaries.
    InvalidArgument,
};

/// Stable identifier for logs and tests, for example "DeviceNotFound".
[[nodiscard]] std::string_view toString(ErrorCode code) noexcept;

/// A failure with a message meant for the user (what happened and what to do)
/// and an optional technical detail meant for logs.
struct Error {
    ErrorCode code = ErrorCode::InvalidArgument;
    std::string message;
    std::string detail;
};

inline std::string_view toString(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::BackendInitFailed:
        return "BackendInitFailed";
    case ErrorCode::DeviceNotFound:
        return "DeviceNotFound";
    case ErrorCode::DeviceInUseExclusive:
        return "DeviceInUseExclusive";
    case ErrorCode::DeviceDisconnected:
        return "DeviceDisconnected";
    case ErrorCode::DeviceStartFailed:
        return "DeviceStartFailed";
    case ErrorCode::UnsupportedSampleRate:
        return "UnsupportedSampleRate";
    case ErrorCode::UnsupportedChannelCount:
        return "UnsupportedChannelCount";
    case ErrorCode::UnsupportedSampleFormat:
        return "UnsupportedSampleFormat";
    case ErrorCode::MicrophonePermissionDenied:
        return "MicrophonePermissionDenied";
    case ErrorCode::BufferUnderrun:
        return "BufferUnderrun";
    case ErrorCode::BufferOverrun:
        return "BufferOverrun";
    case ErrorCode::EngineNotRunning:
        return "EngineNotRunning";
    case ErrorCode::CommandQueueFull:
        return "CommandQueueFull";
    case ErrorCode::FileNotFound:
        return "FileNotFound";
    case ErrorCode::FileUnreadable:
        return "FileUnreadable";
    case ErrorCode::FileTooLarge:
        return "FileTooLarge";
    case ErrorCode::ClipTooLong:
        return "ClipTooLong";
    case ErrorCode::UnsupportedAudioFile:
        return "UnsupportedAudioFile";
    case ErrorCode::DecodeFailed:
        return "DecodeFailed";
    case ErrorCode::FileWriteFailed:
        return "FileWriteFailed";
    case ErrorCode::PresetParseError:
        return "PresetParseError";
    case ErrorCode::UnknownEffect:
        return "UnknownEffect";
    case ErrorCode::UnknownParameter:
        return "UnknownParameter";
    case ErrorCode::ParameterOutOfRange:
        return "ParameterOutOfRange";
    case ErrorCode::InvalidPreset:
        return "InvalidPreset";
    case ErrorCode::HotkeyConflict:
        return "HotkeyConflict";
    case ErrorCode::HotkeyRegistrationFailed:
        return "HotkeyRegistrationFailed";
    case ErrorCode::HotkeysUnsupported:
        return "HotkeysUnsupported";
    case ErrorCode::AutostartFailed:
        return "AutostartFailed";
    case ErrorCode::InvalidArgument:
        return "InvalidArgument";
    }
    return "Unknown";
}

} // namespace vox
