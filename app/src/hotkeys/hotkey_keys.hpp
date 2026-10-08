#pragma once

#include <QKeyCombination>

#include <cstdint>
#include <optional>

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

} // namespace vox::app
