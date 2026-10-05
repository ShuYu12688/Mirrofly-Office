#pragma once

#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QProcess>
#include <QTimer>

class QLocalSocket;

namespace mirrorfly
{
    class OfficeAiAgent;

    class AiIslandHost final : public QObject
    {
        Q_OBJECT
        Q_PROPERTY(QString status READ status NOTIFY statusChanged)

    public:
        explicit AiIslandHost(OfficeAiAgent& agent, QObject* parent = nullptr);
        ~AiIslandHost() override;

        Q_INVOKABLE void setAvailable(bool available);
        Q_INVOKABLE void setLocation(const QString& location);
        Q_INVOKABLE void toggle();
        Q_INVOKABLE void open();
        QString status() const;

    signals:
        void promptRequested(const QString& prompt);
        void statusChanged();

    private:
        void openConnection();
        void readCommands();
        void send(const QJsonObject& message);
        void sendState();
        void stop();
        void start();
        void setStatus(const QString& status);

        OfficeAiAgent& agent_;
        QLocalServer server_;
        QLocalSocket* socket_ = nullptr;
        QProcess process_;
        QTimer update_timer_;
        QString server_name_;
        QString token_;
        QString location_ = QStringLiteral("首页");
        QByteArray incoming_;
        bool available_ = false;
        bool authenticated_ = false;
        bool ui_ready_ = false;
        bool pending_open_ = false;
        QString status_ = QStringLiteral("等待配置模型");
        int restart_attempts_ = 0;
        qint64 command_id_ = 0;
        bool command_accepted_ = false;
    };
}
