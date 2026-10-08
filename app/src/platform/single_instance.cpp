#include "single_instance.hpp"

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QLocalSocket>
#include <QThread>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace vox::app {
namespace {

/// A lock left by a process that is gone is taken over (QLockFile checks the
/// process); a running instance holds it however long it runs.
bool takeLock(QLockFile& lock) {
    lock.setStaleLockTime(0);
    return lock.tryLock(0);
}

} // namespace

SingleInstance::SingleInstance(const QString& key, QObject* parent)
    : QObject(parent)
    , key_(key)
    , lock_(QDir::temp().filePath(key + QStringLiteral(".lock")))
    , primary_(takeLock(lock_)) {
    if (!primary_) {
        return;
    }
    // Any connection is a request for the window: the socket is this user's
    // only, and a later instance has nothing else to say.
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket* socket = server_.nextPendingConnection()) {
            socket->deleteLater();
            emit activationRequested();
        }
    });
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    // On Unix a crash leaves the socket file behind, which blocks listening.
    QLocalServer::removeServer(key_);
    static_cast<void>(server_.listen(key_));
}

QString SingleInstance::defaultKey() {
    // Per user: the lock lives in the user's temporary folder, but socket
    // names can be machine wide (Windows named pipes).
    const QByteArray user =
        QCryptographicHash::hash(QDir::homePath().toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("voxwright-") + QString::fromLatin1(user.left(16));
}

bool SingleInstance::activatePrimary(int timeoutMs) {
#if defined(Q_OS_WIN)
    // Windows lets only the foreground process bring a window forward. This
    // one was just started by the user, so it passes that right on.
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    // The running instance may still be starting and not listen yet.
    const QDeadlineTimer deadline(timeoutMs);
    QLocalSocket socket;
    while (true) {
        socket.connectToServer(key_);
        if (socket.waitForConnected(static_cast<int>(deadline.remainingTime()))) {
            socket.disconnectFromServer();
            return true;
        }
        if (deadline.hasExpired()) {
            return false;
        }
        QThread::msleep(50);
    }
}

} // namespace vox::app
