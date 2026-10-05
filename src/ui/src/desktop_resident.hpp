#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <QPointer>

#include <memory>

class QMenu;
class QQuickWindow;
class QSystemTrayIcon;

namespace mirrorfly
{
    class DesktopResident final : public QObject
    {
        Q_OBJECT

    public:
        enum class Startup
        {
            Primary,
            Forwarded,
            Failed
        };

        explicit DesktopResident(const QString& directory, QObject* parent = nullptr);
        ~DesktopResident() override;
        Startup prepare(const QString& file = {});
        bool attach(QQuickWindow* window, bool enable_tray = true);
        void hideMain();
        void restoreMain();
        void completeExit();

    signals:
        void islandRequested();
        void exitRequested();
        void fileRequested(const QString& path);

    private:
        QLocalServer server_;
        QLockFile lock_;
        QString server_name_;
        QPointer<QQuickWindow> window_;
        std::unique_ptr<QMenu> menu_;
        std::unique_ptr<QSystemTrayIcon> tray_;
    };
}
