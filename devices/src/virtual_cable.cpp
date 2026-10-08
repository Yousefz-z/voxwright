#include "vox/devices/audio_backend.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <string_view>

namespace vox::devices {

double AudioBackend::now() const noexcept {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool looksLikeVirtualCable(const std::string& deviceName) {
    std::string lower(deviceName);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr std::array<std::string_view, 7> kMarkers{
        "cable input",    "cable output",        "vb-audio", "blackhole", "voicemeeter",
        "loopback audio", "voxwright microphone"};
    return std::any_of(kMarkers.begin(), kMarkers.end(),
                       [&lower](std::string_view m) { return lower.find(m) != std::string::npos; });
}

} // namespace vox::devices
