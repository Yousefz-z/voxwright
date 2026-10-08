#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QString>

namespace vox::app {

/// Keeps one Voxwright running per user. The first instance holds a lock
/// file and listens on a local socket; a later one finds the lock taken and
/// connects to the socket, which asks the first to show its window.
class SingleInstance : public QObject {
    Q_OBJECT

public:
    /// `key` names the lock file and the socket; tests pass their own.
    explicit SingleInstance(const QString& key, QObject* parent = nullptr);

    /// The key for the current user's Voxwright.
    [[nodiscard]] static QString defaultKey();

    /// True for the instance that keeps running.
    [[nodiscard]] bool isPrimary() const { return primary_; }

    /// From a later instance: asks the running one to show its window.
    /// Returns whether it was reached within `timeoutMs`.
    bool activatePrimary(int timeoutMs = 3000);

signals:
    /// In the running instance: another start asked for the window.
    void activationRequested();

private:
    QString key_;
    QLockFile lock_;
    QLocalServer server_;
    bool primary_ = false;
};

} // namespace vox::app
