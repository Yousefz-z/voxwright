#pragma once

#include "vox/devices/audio_backend.hpp"

#include <miniaudio.h>

#include <string>

namespace vox::devices {

/// "microphone" or "output device", for messages.
[[nodiscard]] std::string kindWord(DeviceKind kind);

/// Maps a miniaudio failure while opening or starting a device to a
/// specific error whose message says what to do about it.
[[nodiscard]] Error mapOpenError(ma_result result, DeviceKind kind, const std::string& deviceName,
                                 bool exclusive);

} // namespace vox::devices
