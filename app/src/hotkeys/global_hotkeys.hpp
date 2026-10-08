#pragma once

#include <vox/core/result.hpp>

#include <QKeyCombination>
#include <QObject>

#include <functional>
#include <memory>
#include <vector>

namespace vox::app {

/// One global hotkey: an id the owner chooses and its key combination.
struct HotkeyBinding {
    int id = 0;
    QKeyCombination combination;
};

/// System-wide hotkeys that work while another app (a game, a chat app) has
/// focus. Reports both the press and the release, so a key can be held
/// (push-to-talk, hold-to-play sounds). Keys are never taken away from the
/// focused app.
class GlobalHotkeys : public QObject {
    Q_OBJECT

public:
    using ImmediateHandler = std::function<void(int id, bool pressed)>;

    explicit GlobalHotkeys(QObject* parent = nullptr);
    GlobalHotkeys(const GlobalHotkeys&) = delete;
    GlobalHotkeys& operator=(const GlobalHotkeys&) = delete;
    GlobalHotkeys(GlobalHotkeys&&) = delete;
    GlobalHotkeys& operator=(GlobalHotkeys&&) = delete;
    ~GlobalHotkeys() override = default;

    /// Replaces every binding. On failure the error names the hotkeys the
    /// system refused (the others still work).
    [[nodiscard]] virtual Status setBindings(const std::vector<HotkeyBinding>& bindings) = 0;
    [[nodiscard]] virtual bool supported() const = 0;

    /// Called on the thread that detects the key, before `activated` reaches
    /// the UI thread, for actions that must not wait for the UI (push-to-talk
    /// writes an atomic). Set it once, before the first setBindings().
    void setImmediateHandler(ImmediateHandler handler) { immediate_ = std::move(handler); }

signals:
    /// On the UI thread.
    void activated(int id, bool pressed);
    /// On the thread that saw the key; forwarded to `activated`.
    void detected(int id, bool pressed);

protected:
    /// For implementations: report a press or release from any thread.
    void deliver(int id, bool pressed);

private:
    ImmediateHandler immediate_;
};

/// The implementation for this platform: a low-level keyboard hook on
/// Windows, Carbon hotkeys on macOS, and an unsupported stub elsewhere.
[[nodiscard]] std::unique_ptr<GlobalHotkeys> createPlatformHotkeys();

/// For tests and platforms without global hotkeys: keys arrive through
/// press() instead of the operating system.
class ManualHotkeys final : public GlobalHotkeys {
    Q_OBJECT

public:
    explicit ManualHotkeys(bool supported = true, QObject* parent = nullptr);
    [[nodiscard]] Status setBindings(const std::vector<HotkeyBinding>& bindings) override;
    [[nodiscard]] bool supported() const override { return supported_; }
    /// Simulates the combination going down (or up); false if not bound.
    bool press(QKeyCombination combination, bool pressed);
    [[nodiscard]] const std::vector<HotkeyBinding>& bindings() const { return bindings_; }

private:
    bool supported_;
    std::vector<HotkeyBinding> bindings_;
};

} // namespace vox::app
