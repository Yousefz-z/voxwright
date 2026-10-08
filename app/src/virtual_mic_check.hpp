#pragma once

#include <vox/core/ring_buffer.hpp>
#include <vox/devices/audio_backend.hpp>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace vox::engine {
class AudioEngine;
}

namespace vox::app {

/// Checks the virtual microphone end to end: plays a short chirp into the
/// virtual cable through the engine and listens on the cable's recording
/// side, the device chat apps use, for the same chirp.
class VirtualMicCheck : public QObject, private devices::CaptureHandler {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by the application")
    Q_PROPERTY(int state READ stateValue NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)

public:
    enum class State { Idle, Running, Passed, Silent, Mismatch, Failed };
    Q_ENUM(State)

    VirtualMicCheck(devices::AudioBackend& backend, engine::AudioEngine& engine,
                    QObject* parent = nullptr);
    VirtualMicCheck(const VirtualMicCheck&) = delete;
    VirtualMicCheck& operator=(const VirtualMicCheck&) = delete;
    VirtualMicCheck(VirtualMicCheck&&) = delete;
    VirtualMicCheck& operator=(VirtualMicCheck&&) = delete;
    ~VirtualMicCheck() override;

    /// Listens on the capture device called `recordingName` (for VB-CABLE,
    /// "CABLE Output") while the engine plays the chirp.
    Q_INVOKABLE void start(const QString& recordingName);
    Q_INVOKABLE void cancel();

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] int stateValue() const { return static_cast<int>(state_); }
    [[nodiscard]] QString message() const { return message_; }

    /// The chirp (mono, 48 kHz).
    [[nodiscard]] static std::vector<float> probe();
    /// Normalized cross-correlation peak of `probe` within `recording`
    /// (both 48 kHz), in [0, 1]; 1 means an exact scaled copy is present.
    [[nodiscard]] static double matchStrength(const std::vector<float>& recording,
                                              const std::vector<float>& probe);

    /// For tests: how long to listen, in milliseconds of wall-clock time.
    void setListenMs(int ms) { listen_.setInterval(ms); }

signals:
    void stateChanged();

private:
    void onCapture(const float* interleaved, std::size_t frames,
                   std::uint32_t channels) noexcept override;
    void drain();
    void finish();
    void setState(State state, const QString& message);

    devices::AudioBackend& backend_;
    engine::AudioEngine& engine_;
    std::unique_ptr<devices::Stream> stream_;
    SpscRingBuffer<float> captured_;
    std::vector<float> recording_;
    QString deviceName_;
    QTimer listen_;
    QTimer poll_;
    State state_ = State::Idle;
    QString message_;
};

} // namespace vox::app
