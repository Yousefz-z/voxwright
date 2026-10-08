#include "miniaudio_errors.hpp"

namespace vox::devices {

std::string kindWord(DeviceKind kind) {
    return kind == DeviceKind::Capture ? "microphone" : "output device";
}

Error mapOpenError(ma_result result, DeviceKind kind, const std::string& deviceName,
                   bool exclusive) {
    const std::string device =
        deviceName.empty() ? "The default " + kindWord(kind) : "\"" + deviceName + "\"";
    const std::string detail = std::string("miniaudio: ") + ma_result_description(result);
    switch (result) {
    case MA_DEVICE_ALREADY_INITIALIZED:
    case MA_BUSY:
    case MA_SHARE_MODE_NOT_SUPPORTED:
        if (exclusive) {
            return makeError(ErrorCode::DeviceInUseExclusive,
                             device +
                                 " cannot be opened in exclusive mode. Turn off exclusive mode in "
                                 "Settings > Advanced, or close the app that holds the device.",
                             detail);
        }
        return makeError(
            ErrorCode::DeviceInUseExclusive,
            device + " is being used by another app in exclusive mode. Close that app, or in "
                     "Windows Sound settings open the device's Properties > Advanced and turn "
                     "off \"Allow applications to take exclusive control of this device\".",
            detail);
    case MA_ACCESS_DENIED:
        if (kind == DeviceKind::Capture) {
            return makeError(
                ErrorCode::MicrophonePermissionDenied,
                "Voxwright is not allowed to use the microphone. Allow it in System Settings > "
                "Privacy & Security > Microphone (macOS) or Settings > Privacy & security > "
                "Microphone (Windows), then restart Voxwright.",
                detail);
        }
        return makeError(ErrorCode::DeviceStartFailed,
                         device + " refused access. Another app or a system policy may be blocking "
                                  "it; choose another output device or restart the computer.",
                         detail);
    case MA_DEVICE_TYPE_NOT_SUPPORTED:
        return makeError(ErrorCode::DeviceStartFailed,
                         device + " cannot be used as a " + kindWord(kind) +
                             ". Choose another device in Settings > Audio.",
                         detail);
    case MA_FORMAT_NOT_SUPPORTED:
        return makeError(ErrorCode::UnsupportedSampleFormat,
                         device +
                             " does not accept 32-bit float audio in this mode. Turn off exclusive "
                             "mode or choose another device.",
                         detail);
    case MA_INVALID_DEVICE_CONFIG:
        return makeError(
            ErrorCode::UnsupportedSampleRate,
            device + " rejected the sample rate or channel layout. Set the device to 48000 Hz "
                     "in the system sound settings, or turn off exclusive mode.",
            detail);
    case MA_NO_DEVICE:
    case MA_DOES_NOT_EXIST:
    case MA_DEVICE_NOT_INITIALIZED:
        return makeError(ErrorCode::DeviceNotFound,
                         device + " is not connected. Plug it in or choose another " +
                             kindWord(kind) + " in Settings > Audio.",
                         detail);
    default:
        return makeError(
            ErrorCode::DeviceStartFailed,
            device + " could not be opened. Try another device, or unplug and reconnect it.",
            detail);
    }
}

} // namespace vox::devices
