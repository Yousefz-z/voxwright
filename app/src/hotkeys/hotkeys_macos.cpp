// Global hotkeys on macOS through Carbon's RegisterEventHotKey. It reports
// presses and releases and needs no accessibility permission; events arrive
// on the main thread through the application's run loop.

#include "global_hotkeys.hpp"
#include "hotkey_keys.hpp"

#include <Carbon/Carbon.h>

#include <QKeySequence>

#include <array>
#include <string>
#include <vector>

namespace vox::app {
namespace {

constexpr OSType kSignature = (static_cast<OSType>('V') << 24U) |
                              (static_cast<OSType>('X') << 16U) | (static_cast<OSType>('W') << 8U) |
                              static_cast<OSType>('R');

class MacHotkeys final : public GlobalHotkeys {
public:
    MacHotkeys() {
        const std::array<EventTypeSpec, 2> types{{{kEventClassKeyboard, kEventHotKeyPressed},
                                                  {kEventClassKeyboard, kEventHotKeyReleased}}};
        handlerUpp_ = NewEventHandlerUPP(&MacHotkeys::handle);
        InstallEventHandler(GetApplicationEventTarget(), handlerUpp_,
                            static_cast<ItemCount>(types.size()), types.data(), this, &handler_);
    }
    MacHotkeys(const MacHotkeys&) = delete;
    MacHotkeys& operator=(const MacHotkeys&) = delete;
    MacHotkeys(MacHotkeys&&) = delete;
    MacHotkeys& operator=(MacHotkeys&&) = delete;
    ~MacHotkeys() override {
        unregisterAll();
        if (handler_ != nullptr) {
            RemoveEventHandler(handler_);
        }
        if (handlerUpp_ != nullptr) {
            DisposeEventHandlerUPP(handlerUpp_);
        }
    }

    [[nodiscard]] Status setBindings(const std::vector<HotkeyBinding>& bindings) override {
        unregisterAll();
        std::string taken;
        std::string refused;
        const auto name = [](const HotkeyBinding& b) {
            return QKeySequence(b.combination).toString(QKeySequence::NativeText).toStdString();
        };
        for (const HotkeyBinding& b : bindings) {
            const auto code = macVirtualKey(b.combination);
            if (!code) {
                refused += (refused.empty() ? "" : ", ") + name(b);
                continue;
            }
            const EventHotKeyID hotkeyId{kSignature, static_cast<UInt32>(ids_.size())};
            EventHotKeyRef ref = nullptr;
            const OSStatus status =
                RegisterEventHotKey(*code, macModifierFlags(b.combination.keyboardModifiers()),
                                    hotkeyId, GetApplicationEventTarget(), 0, &ref);
            if (status != noErr) {
                std::string& list = status == eventHotKeyExistsErr ? taken : refused;
                list += (list.empty() ? "" : ", ") + name(b);
                continue;
            }
            ids_.push_back(b.id);
            refs_.push_back(ref);
        }
        if (!taken.empty()) {
            return makeError(ErrorCode::HotkeyConflict, "macOS or another app already uses " +
                                                            taken +
                                                            ". Choose a different combination.");
        }
        if (!refused.empty()) {
            return makeError(ErrorCode::HotkeyRegistrationFailed,
                             "These keys cannot be global hotkeys: " + refused +
                                 ". Choose a letter, number, or function key.");
        }
        return {};
    }

    [[nodiscard]] bool supported() const override { return true; }

private:
    void unregisterAll() {
        for (EventHotKeyRef ref : refs_) {
            UnregisterEventHotKey(ref);
        }
        refs_.clear();
        ids_.clear();
    }

    static OSStatus handle(EventHandlerCallRef /*next*/, EventRef event, void* userData) {
        EventHotKeyID hotkeyId{};
        if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                              sizeof(hotkeyId), nullptr, &hotkeyId) != noErr ||
            hotkeyId.signature != kSignature) {
            return eventNotHandledErr;
        }
        auto* self = static_cast<MacHotkeys*>(userData);
        if (hotkeyId.id < self->ids_.size()) {
            self->deliver(self->ids_[hotkeyId.id], GetEventKind(event) == kEventHotKeyPressed);
        }
        return noErr;
    }

    EventHandlerUPP handlerUpp_ = nullptr;
    EventHandlerRef handler_ = nullptr;
    std::vector<int> ids_;
    std::vector<EventHotKeyRef> refs_;
};

} // namespace

std::unique_ptr<GlobalHotkeys> createPlatformHotkeys() {
    return std::make_unique<MacHotkeys>();
}

} // namespace vox::app
