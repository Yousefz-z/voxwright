#include "hotkey_keys.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace vox::app {
namespace {

using KeyCode = std::pair<Qt::Key, std::uint32_t>;

// Windows virtual-key codes (winuser.h).
constexpr std::array<KeyCode, 31> kWindowsSpecial{{
    {Qt::Key_Space, 0x20},        {Qt::Key_PageUp, 0x21},      {Qt::Key_PageDown, 0x22},
    {Qt::Key_End, 0x23},          {Qt::Key_Home, 0x24},        {Qt::Key_Left, 0x25},
    {Qt::Key_Up, 0x26},           {Qt::Key_Right, 0x27},       {Qt::Key_Down, 0x28},
    {Qt::Key_Insert, 0x2D},       {Qt::Key_Delete, 0x2E},      {Qt::Key_Pause, 0x13},
    {Qt::Key_ScrollLock, 0x91},   {Qt::Key_Return, 0x0D},      {Qt::Key_Enter, 0x0D},
    {Qt::Key_Tab, 0x09},          {Qt::Key_Backspace, 0x08},   {Qt::Key_Escape, 0x1B},
    {Qt::Key_Semicolon, 0xBA},    {Qt::Key_Equal, 0xBB},       {Qt::Key_Comma, 0xBC},
    {Qt::Key_Minus, 0xBD},        {Qt::Key_Period, 0xBE},      {Qt::Key_Slash, 0xBF},
    {Qt::Key_QuoteLeft, 0xC0},    {Qt::Key_BracketLeft, 0xDB}, {Qt::Key_Backslash, 0xDC},
    {Qt::Key_BracketRight, 0xDD}, {Qt::Key_Apostrophe, 0xDE},  {Qt::Key_Print, 0x2C},
    {Qt::Key_Menu, 0x5D},
}};

// macOS kVK_* codes (Carbon HIToolbox Events.h), ANSI layout.
constexpr std::array<std::uint32_t, 26> kMacLetters{
    0x00, 0x0B, 0x08, 0x02, 0x0E, 0x03, 0x05, 0x04, 0x22, 0x26, 0x28, 0x25, 0x2E,
    0x2D, 0x1F, 0x23, 0x0C, 0x0F, 0x01, 0x11, 0x20, 0x09, 0x0D, 0x07, 0x10, 0x06};
constexpr std::array<std::uint32_t, 10> kMacDigits{0x1D, 0x12, 0x13, 0x14, 0x15,
                                                   0x17, 0x16, 0x1A, 0x1C, 0x19};
constexpr std::array<std::uint32_t, 10> kMacKeypad{0x52, 0x53, 0x54, 0x55, 0x56,
                                                   0x57, 0x58, 0x59, 0x5B, 0x5C};
constexpr std::array<std::uint32_t, 20> kMacFunction{0x7A, 0x78, 0x63, 0x76, 0x60, 0x61, 0x62,
                                                     0x64, 0x65, 0x6D, 0x67, 0x6F, 0x69, 0x6B,
                                                     0x71, 0x6A, 0x40, 0x4F, 0x50, 0x5A};
constexpr std::array<KeyCode, 24> kMacSpecial{{
    {Qt::Key_Space, 0x31},       {Qt::Key_Return, 0x24},     {Qt::Key_Enter, 0x4C},
    {Qt::Key_Tab, 0x30},         {Qt::Key_Backspace, 0x33},  {Qt::Key_Escape, 0x35},
    {Qt::Key_Delete, 0x75},      {Qt::Key_Home, 0x73},       {Qt::Key_End, 0x77},
    {Qt::Key_PageUp, 0x74},      {Qt::Key_PageDown, 0x79},   {Qt::Key_Left, 0x7B},
    {Qt::Key_Right, 0x7C},       {Qt::Key_Down, 0x7D},       {Qt::Key_Up, 0x7E},
    {Qt::Key_Equal, 0x18},       {Qt::Key_Minus, 0x1B},      {Qt::Key_BracketRight, 0x1E},
    {Qt::Key_BracketLeft, 0x21}, {Qt::Key_Apostrophe, 0x27}, {Qt::Key_Semicolon, 0x29},
    {Qt::Key_Backslash, 0x2A},   {Qt::Key_Comma, 0x2B},      {Qt::Key_Slash, 0x2C},
}};
constexpr std::array<KeyCode, 2> kMacMore{{{Qt::Key_Period, 0x2F}, {Qt::Key_QuoteLeft, 0x32}}};

template <std::size_t N>
std::optional<std::uint32_t> lookup(const std::array<KeyCode, N>& table, Qt::Key key) {
    const auto it = std::find_if(table.begin(), table.end(),
                                 [key](const KeyCode& k) { return k.first == key; });
    return it == table.end() ? std::nullopt : std::optional<std::uint32_t>(it->second);
}

int offset(Qt::Key key, Qt::Key first) {
    return static_cast<int>(key) - static_cast<int>(first);
}

} // namespace

bool isModifierKey(Qt::Key key) {
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Hyper_L:
    case Qt::Key_Hyper_R:
        return true;
    default:
        return false;
    }
}

std::optional<std::uint32_t> windowsVirtualKey(QKeyCombination combination) {
    const Qt::Key key = combination.key();
    const bool keypad = combination.keyboardModifiers().testFlag(Qt::KeypadModifier);
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        const auto n = static_cast<std::uint32_t>(offset(key, Qt::Key_0));
        return keypad ? 0x60 + n : 0x30 + n; // VK_NUMPAD0 or '0'
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return 0x41 + static_cast<std::uint32_t>(offset(key, Qt::Key_A));
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return 0x70 + static_cast<std::uint32_t>(offset(key, Qt::Key_F1));
    }
    if (keypad) {
        switch (key) {
        case Qt::Key_Asterisk:
            return 0x6A;
        case Qt::Key_Plus:
            return 0x6B;
        case Qt::Key_Minus:
            return 0x6D;
        case Qt::Key_Period:
            return 0x6E;
        case Qt::Key_Slash:
            return 0x6F;
        default:
            break;
        }
    }
    return lookup(kWindowsSpecial, key);
}

std::optional<std::uint32_t> macVirtualKey(QKeyCombination combination) {
    const Qt::Key key = combination.key();
    const bool keypad = combination.keyboardModifiers().testFlag(Qt::KeypadModifier);
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        const auto n = static_cast<std::size_t>(offset(key, Qt::Key_0));
        return keypad ? kMacKeypad[n] : kMacDigits[n];
    }
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return kMacLetters[static_cast<std::size_t>(offset(key, Qt::Key_A))];
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F20) {
        return kMacFunction[static_cast<std::size_t>(offset(key, Qt::Key_F1))];
    }
    if (auto special = lookup(kMacSpecial, key)) {
        return special;
    }
    return lookup(kMacMore, key);
}

std::uint32_t macModifierFlags(Qt::KeyboardModifiers modifiers) {
    constexpr std::uint32_t kCmd = 0x0100;     // cmdKey
    constexpr std::uint32_t kShift = 0x0200;   // shiftKey
    constexpr std::uint32_t kOption = 0x0800;  // optionKey
    constexpr std::uint32_t kControl = 0x1000; // controlKey
    std::uint32_t flags = 0;
    if (modifiers.testFlag(Qt::ControlModifier)) {
        flags |= kCmd;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        flags |= kShift;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        flags |= kOption;
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        flags |= kControl;
    }
    return flags;
}

} // namespace vox::app
