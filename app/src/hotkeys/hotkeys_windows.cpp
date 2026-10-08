// Global hotkeys on Windows through a low-level keyboard hook (WH_KEYBOARD_LL)
// on a dedicated thread. Unlike RegisterHotKey it reports releases (needed
// for push-to-talk), and the hook passes every key on, so the focused app
// (a game, a chat app) still receives it.

#include "global_hotkeys.hpp"
#include "hotkey_keys.hpp"

#include <windows.h>

#include <QKeySequence>

#include <array>
#include <atomic>
#include <cstdint>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace vox::app {
namespace {

class WindowsHotkeys final : public GlobalHotkeys {
public:
    WindowsHotkeys() = default;
    WindowsHotkeys(const WindowsHotkeys&) = delete;
    WindowsHotkeys& operator=(const WindowsHotkeys&) = delete;
    WindowsHotkeys(WindowsHotkeys&&) = delete;
    WindowsHotkeys& operator=(WindowsHotkeys&&) = delete;
    ~WindowsHotkeys() override { stop(); }

    [[nodiscard]] Status setBindings(const std::vector<HotkeyBinding>& bindings) override {
        std::vector<Native> natives;
        std::string refused;
        for (const HotkeyBinding& b : bindings) {
            const auto vk = windowsVirtualKey(b.combination);
            if (!vk) {
                refused += (refused.empty() ? "" : ", ") +
                           QKeySequence(b.combination).toString().toStdString();
                continue;
            }
            const Qt::KeyboardModifiers m = b.combination.keyboardModifiers();
            natives.push_back({b.id, *vk, m.testFlag(Qt::ControlModifier),
                               m.testFlag(Qt::ShiftModifier), m.testFlag(Qt::AltModifier),
                               m.testFlag(Qt::MetaModifier), false});
        }
        const bool any = !natives.empty();
        {
            const std::scoped_lock lock(mutex_);
            bindings_ = std::move(natives);
        }
        if (any) {
            if (auto started = ensureStarted(); !started) {
                return started;
            }
        }
        if (!refused.empty()) {
            return makeError(ErrorCode::HotkeyRegistrationFailed,
                             "These keys cannot be global hotkeys: " + refused +
                                 ". Choose a letter, number, or function key, with Ctrl, Alt, "
                                 "Shift, or Win if you like.");
        }
        return {};
    }

    [[nodiscard]] bool supported() const override { return true; }

private:
    struct Native {
        int id;
        std::uint32_t vk;
        bool ctrl;
        bool shift;
        bool alt;
        bool win;
        bool down;
    };

    Status ensureStarted() {
        if (thread_.joinable()) {
            return {};
        }
        std::promise<DWORD> ready;
        std::future<DWORD> result = ready.get_future();
        thread_ = std::thread([this, promise = std::move(ready)]() mutable { run(promise); });
        const DWORD error = result.get();
        if (error != 0) {
            thread_.join();
            return makeError(ErrorCode::HotkeyRegistrationFailed,
                             "Windows refused the keyboard hook that global hotkeys need "
                             "(error " +
                                 std::to_string(error) +
                                 "). Restart Voxwright; if security software blocks keyboard "
                                 "hooks, allow Voxwright in it.");
        }
        return {};
    }

    void run(std::promise<DWORD>& ready) {
        MSG msg{};
        // Create this thread's message queue before anyone posts to it.
        PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        threadId_.store(GetCurrentThreadId());
        active_.store(this);
        HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, &WindowsHotkeys::hookProc,
                                       GetModuleHandleW(nullptr), 0);
        if (hook == nullptr) {
            const DWORD error = GetLastError();
            active_.store(nullptr);
            ready.set_value(error != 0 ? error : DWORD{1});
            return;
        }
        ready.set_value(0);
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            DispatchMessageW(&msg);
        }
        UnhookWindowsHookEx(hook);
        active_.store(nullptr);
    }

    void stop() {
        if (thread_.joinable()) {
            PostThreadMessageW(threadId_.load(), WM_QUIT, 0, 0);
            thread_.join();
        }
    }

    static LRESULT CALLBACK hookProc(int code, WPARAM wParam, LPARAM lParam) {
        if (code == HC_ACTION) {
            if (WindowsHotkeys* self = active_.load()) {
                const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
                const bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
                const bool up = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;
                self->onKey(static_cast<std::uint32_t>(key->vkCode), down, up);
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    static bool held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

    void onKey(std::uint32_t vk, bool down, bool up) {
        std::array<std::pair<int, bool>, 8> events{};
        std::size_t count = 0;
        {
            const bool ctrl = held(VK_CONTROL);
            const bool shift = held(VK_SHIFT);
            const bool alt = held(VK_MENU);
            const bool win = held(VK_LWIN) || held(VK_RWIN);
            const std::scoped_lock lock(mutex_);
            for (Native& b : bindings_) {
                if (b.vk != vk || count == events.size()) {
                    continue;
                }
                // Key repeat sends more key-downs: only the first counts.
                if (down && !b.down && b.ctrl == ctrl && b.shift == shift && b.alt == alt &&
                    b.win == win) {
                    b.down = true;
                    events[count++] = {b.id, true};
                } else if (up && b.down) {
                    b.down = false;
                    events[count++] = {b.id, false};
                }
            }
        }
        for (std::size_t i = 0; i < count; ++i) {
            deliver(events[i].first, events[i].second);
        }
    }

    static inline std::atomic<WindowsHotkeys*> active_{nullptr};
    std::mutex mutex_;
    std::vector<Native> bindings_;
    std::thread thread_;
    std::atomic<DWORD> threadId_{0};
};

} // namespace

std::unique_ptr<GlobalHotkeys> createPlatformHotkeys() {
    return std::make_unique<WindowsHotkeys>();
}

} // namespace vox::app
