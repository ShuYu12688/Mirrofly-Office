#include "desktop_resident.hpp"

#include <QAction>
#include <QCryptographicHash>
#include <QDir>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QMenu>
#include <QQuickWindow>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace mirrorfly
{
    DesktopResident::DesktopResident(const QString& directory, QObject* parent)
        : QObject(parent), lock_(QDir(directory).filePath("office-session.lock"))
    {
        const auto hash =
            QCryptographicHash::hash(QDir(directory).absolutePath().toUtf8(), QCryptographicHash::Sha256);
        server_name_ = "mirrorfly-office-" + QString::fromLatin1(hash.toHex().left(32));
        QDir().mkpath(directory);
        connect(&server_, &QLocalServer::newConnection, this, [this]()
        {
            while (server_.hasPendingConnections())
            {
                auto* socket = server_.nextPendingConnection();
                socket->setParent(this);
                auto incoming = std::make_shared<QByteArray>();
                const auto read = [this, socket, incoming]()
                {
                    incoming->append(socket->readAll());
                    if (incoming->size() > 32768)
                    {
                        socket->abort();
                        return;
                    }
                    if (!incoming->contains('\n'))
                        return;
                    const auto message =
                        QJsonDocument::fromJson(incoming->left(incoming->indexOf('\n'))).object();
                    incoming->clear();
                    if (message.value("type") == "restore")
                    {
                        restoreMain();
                        const QString file = message.value("file").toString();
                        if (!file.isEmpty())
                            emit fileRequested(file);
                    }
                    socket->disconnectFromServer();
                };
                connect(socket, &QLocalSocket::readyRead, this, read);
                connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
                QTimer::singleShot(3000, socket, [socket]()
                {
                    socket->abort();
                });
                read();
            }
        });
    }

    DesktopResident::~DesktopResident() = default;

    DesktopResident::Startup DesktopResident::prepare(const QString& file)
    {
        if (!lock_.tryLock())
        {
#ifdef Q_OS_WIN
            qint64 process_id = 0;
            QString host_name;
            QString application_name;
            if (lock_.getLockInfo(&process_id, &host_name, &application_name) && process_id > 0)
                AllowSetForegroundWindow(static_cast<DWORD>(process_id));
#endif
            QLocalSocket socket;
            // Allow the first process to finish creating its local endpoint.
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                socket.connectToServer(server_name_);
                if (socket.waitForConnected(200))
                {
                    const QJsonObject request{{"type", "restore"}, {"file", file}};
                    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
                    const bool sent = socket.bytesToWrite() == 0 || socket.waitForBytesWritten(1000);
                    socket.disconnectFromServer();
                    return sent ? Startup::Forwarded : Startup::Failed;
                }
                socket.abort();
                QThread::msleep(40);
            }
            return Startup::Failed;
        }
        QLocalServer::removeServer(server_name_);
        server_.setSocketOptions(QLocalServer::UserAccessOption);
        return server_.listen(server_name_) ? Startup::Primary : Startup::Failed;
    }

    bool DesktopResident::attach(QQuickWindow* window, bool enable_tray)
    {
        window_ = window;
        if (!enable_tray || !QSystemTrayIcon::isSystemTrayAvailable())
            return false;
        menu_ = std::make_unique<QMenu>();
        connect(menu_->addAction(QStringLiteral("打开 Mirrorfly Office")), &QAction::triggered, this,
            &DesktopResident::restoreMain);
        connect(menu_->addAction(QStringLiteral("唤出灵动岛")), &QAction::triggered, this,
            &DesktopResident::islandRequested);
        menu_->addSeparator();
        connect(menu_->addAction(QStringLiteral("退出 Mirrorfly Office")), &QAction::triggered, this,
            &DesktopResident::exitRequested);
        tray_ = std::make_unique<QSystemTrayIcon>(QGuiApplication::windowIcon());
        tray_->setToolTip(QStringLiteral("Mirrorfly Office · 双击打开"));
        tray_->setContextMenu(menu_.get());
        connect(tray_.get(), &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason)
        {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                restoreMain();
        });
        tray_->show();
        QGuiApplication::setQuitOnLastWindowClosed(false);
        return true;
    }

    void DesktopResident::hideMain()
    {
        if (window_)
            window_->hide();
    }

    void DesktopResident::restoreMain()
    {
        if (!window_)
            return;
        if (window_->visibility() == QWindow::Minimized)
            window_->showNormal();
        else
            window_->show();
        window_->raise();
        window_->requestActivate();
    }

    void DesktopResident::completeExit()
    {
        if (tray_)
            tray_->hide();
        QTimer::singleShot(0, QCoreApplication::instance(), &QCoreApplication::quit);
    }
}
