#include "global_hotkeys.hpp"

#include <algorithm>

namespace vox::app {

GlobalHotkeys::GlobalHotkeys(QObject* parent)
    : QObject(parent) {
    // Queued: `detected` may come from the hook thread, `activated` reaches
    // receivers on this object's thread.
    connect(this, &GlobalHotkeys::detected, this, &GlobalHotkeys::activated, Qt::QueuedConnection);
}

void GlobalHotkeys::deliver(int id, bool pressed) {
    if (immediate_) {
        immediate_(id, pressed);
    }
    emit detected(id, pressed);
}

ManualHotkeys::ManualHotkeys(bool supported, QObject* parent)
    : GlobalHotkeys(parent)
    , supported_(supported) {}

Status ManualHotkeys::setBindings(const std::vector<HotkeyBinding>& bindings) {
    if (!supported_ && !bindings.empty()) {
        return makeError(ErrorCode::HotkeysUnsupported,
                         "Global hotkeys are not available on this system. Use the buttons in "
                         "the window instead.");
    }
    bindings_ = bindings;
    return {};
}

bool ManualHotkeys::press(QKeyCombination combination, bool pressed) {
    const auto it = std::find_if(bindings_.begin(), bindings_.end(), [&](const HotkeyBinding& b) {
        return b.combination == combination;
    });
    if (it == bindings_.end()) {
        return false;
    }
    deliver(it->id, pressed);
    return true;
}

} // namespace vox::app
