#pragma once

#include <QKeyCombination>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace vox::app {

/// Windows virtual-key code for the key of `combination` (VK_*), or nothing
/// for keys that cannot be a global hotkey. Qt::KeypadModifier selects the
/// numeric keypad.
[[nodiscard]] std::optional<std::uint32_t> windowsVirtualKey(QKeyCombination combination);

/// macOS virtual key code (kVK_*, ANSI layout) for the key of `combination`.
[[nodiscard]] std::optional<std::uint32_t> macVirtualKey(QKeyCombination combination);

/// Carbon modifier flags (cmdKey, shiftKey, optionKey, controlKey). Qt calls
/// the Command key Control and the Control key Meta on macOS.
[[nodiscard]] std::uint32_t macModifierFlags(Qt::KeyboardModifiers modifiers);

/// True for keys that are only modifiers (Shift, Control, ...): a hotkey
/// needs a non-modifier key.
[[nodiscard]] bool isModifierKey(Qt::Key key);

/// Matches raw key events from a platform hook against hotkeys and tracks
/// which are held, so every press gets its release, including across a
/// change of bindings (a lost release would leave push-to-talk talking).
/// Not thread-safe: the owner serializes calls.
class KeyMatcher {
public:
    struct Binding {
        int id = 0;
        std::uint32_t key = 0; ///< Platform key code.
        bool ctrl = false;
        bool shift = false;
        bool alt = false;
        bool win = false;
    };
    struct Modifiers {
        bool ctrl = false;
        bool shift = false;
        bool alt = false;
        bool win = false;
    };
    struct Event {
        int id = 0;
        bool pressed = false;
    };

    /// Replaces the bindings. A held hotkey that keeps its id and key stays
    /// held; the ids of held hotkeys that are gone or moved are returned, and
    /// the caller reports their release.
    [[nodiscard]] std::vector<int> setBindings(const std::vector<Binding>& bindings);
    /// Fills `out` with the hotkeys `key` presses or releases and returns how
    /// many. Key repeat does not press again; a release needs no modifiers.
    [[nodiscard]] std::size_t onKey(std::uint32_t key, bool down, bool up, Modifiers held,
                                    std::span<Event> out);

private:
    struct State {
        Binding binding;
        bool down = false;
    };
    std::vector<State> states_;
};

} // namespace vox::app
