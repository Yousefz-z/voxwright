// Platforms without a global hotkey implementation (Linux, a development
// and CI platform only; see docs/decisions.md D3).

#include "global_hotkeys.hpp"

namespace vox::app {

std::unique_ptr<GlobalHotkeys> createPlatformHotkeys() {
    return std::make_unique<ManualHotkeys>(false);
}

} // namespace vox::app
