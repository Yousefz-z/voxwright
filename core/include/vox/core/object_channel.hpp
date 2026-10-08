#pragma once

#include <vox/core/spsc_queue.hpp>

#include <cstddef>
#include <memory>
#include <optional>

namespace vox {

/// Hands heap objects from the control thread to the audio thread and back.
///
/// The control thread builds an object (allocation allowed there) and calls
/// send(). The audio thread adopts it with receive(), uses it, and later calls
/// retire() so that the destructor runs on the control thread when it calls
/// collectGarbage(). The audio thread therefore never allocates or frees.
template <class T, std::size_t Capacity = 64>
class ObjectChannel {
public:
    ObjectChannel() = default;
    ObjectChannel(const ObjectChannel&) = delete;
    ObjectChannel& operator=(const ObjectChannel&) = delete;
    ObjectChannel(ObjectChannel&&) = delete;
    ObjectChannel& operator=(ObjectChannel&&) = delete;

    /// Destroys anything still in flight. Only call once both threads stopped.
    ~ObjectChannel() {
        while (auto p = outbound_.tryPop()) {
            delete *p;
        }
        collectGarbage();
    }

    /// Control thread. Returns the object back on failure (queue full).
    std::unique_ptr<T> send(std::unique_ptr<T> object) noexcept {
        T* raw = object.get();
        if (outbound_.tryPush(raw)) {
            static_cast<void>(object.release());
            return nullptr;
        }
        return object;
    }

    /// Audio thread. The caller owns the returned pointer until it retires it.
    T* receive() noexcept {
        auto p = outbound_.tryPop();
        return p ? *p : nullptr;
    }

    /// Audio thread. Returns false if the return queue is full; the caller must
    /// keep the pointer and retry later.
    bool retire(T* object) noexcept { return object == nullptr || inbound_.tryPush(object); }

    /// Control thread. Destroys every retired object.
    void collectGarbage() noexcept {
        while (auto p = inbound_.tryPop()) {
            delete *p;
        }
    }

private:
    SpscQueue<T*, Capacity> outbound_;
    SpscQueue<T*, Capacity> inbound_;
};

} // namespace vox
