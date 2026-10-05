#include "ai_island_host.hpp"
#include "ai_island_protocol.hpp"
#include "ai_island_trace.hpp"
#include "office_ai_agent.hpp"

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{

    QString random_hex()
    {
        return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
    }

    void allow_helper_focus(qint64 process_id)
    {
#ifdef Q_OS_WIN
        if (process_id > 0)
            AllowSetForegroundWindow(static_cast<DWORD>(process_id));
#else
        Q_UNUSED(process_id)
#endif
    }
}

namespace mirrorfly
{
    AiIslandHost::AiIslandHost(OfficeAiAgent& agent, QObject* parent) : QObject(parent), agent_(agent)
    {
        update_timer_.setSingleShot(true);
        update_timer_.setInterval(45);
        connect(&update_timer_, &QTimer::timeout, this, &AiIslandHost::sendState);
        connect(&agent_, &OfficeAiAgent::stateChanged, this, [this]()
        {
            if (available_ && !update_timer_.isActive())
                update_timer_.start();
        });
        connect(&server_, &QLocalServer::newConnection, this, &AiIslandHost::openConnection);
        connect(&process_, &QProcess::finished, this, [this]()
        {
            if (!available_)
                return;
            authenticated_ = false;
            ui_ready_ = false;
            setStatus(QStringLiteral("灵动岛意外退出，正在重新启动"));
            if (++restart_attempts_ > 2)
            {
                setStatus(QStringLiteral("灵动岛启动失败，请点击“开启灵动岛”重试"));
                return;
            }
            QTimer::singleShot(1000, this, [this]()
            {
                if (!available_)
                    return;
                stop();
                start();
            });
        });
    }

    AiIslandHost::~AiIslandHost()
    {
        stop();
    }

    void AiIslandHost::setAvailable(bool available)
    {
        if (available_ == available)
            return;
        available_ = available;
        if (!available_)
        {
            restart_attempts_ = 0;
            pending_open_ = false;
            setStatus(QStringLiteral("等待配置模型"));
            stop();
            return;
        }
        if (QStandardPaths::isTestModeEnabled())
            return;
        start();
    }

    void AiIslandHost::start()
    {
        if (process_.state() != QProcess::NotRunning)
            return;
        server_.close();
        const QString process_id = QString::number(QCoreApplication::applicationPid());
        server_name_ = QStringLiteral("mirrorfly-ai-") + process_id + '-' + random_hex().left(12);
        token_ = random_hex();
        if (!server_.listen(server_name_))
        {
            setStatus(QStringLiteral("灵动岛通信启动失败：") + server_.errorString());
            return;
        }
        const QString helper =
            QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("MirrorflyAiIsland.exe"));
        process_.setProgram(helper);
        process_.setArguments({server_name_, token_});
        setStatus(QStringLiteral("正在启动灵动岛"));
        process_.start();
        if (!process_.waitForStarted(1500))
        {
            setStatus(QStringLiteral("灵动岛程序无法启动：") + process_.errorString());
            stop();
        }
    }

    void AiIslandHost::setLocation(const QString& location)
    {
        if (location_ == location)
            return;
        location_ = location.left(160);
        if (available_ && !update_timer_.isActive())
            update_timer_.start();
    }

    void AiIslandHost::toggle()
    {
        if (available_ && ui_ready_)
        {
            allow_helper_focus(process_.processId());
            send({{"type", "toggle"}});
        }
        else
            open();
    }

    void AiIslandHost::open()
    {
        if (!available_ || !agent_.configured())
            return;
        if (ui_ready_)
        {
            allow_helper_focus(process_.processId());
            send({{"type", "open"}});
            return;
        }
        pending_open_ = true;
        if (process_.state() == QProcess::NotRunning)
        {
            restart_attempts_ = 0;
            stop();
            start();
        }
    }

    QString AiIslandHost::status() const
    {
        return status_;
    }

    void AiIslandHost::setStatus(const QString& status)
    {
        if (status_ == status)
            return;
        status_ = status;
        emit statusChanged();
    }

    void AiIslandHost::openConnection()
    {
        while (server_.hasPendingConnections())
        {
            QLocalSocket* candidate = server_.nextPendingConnection();
            if (socket_ != nullptr)
            {
                candidate->disconnectFromServer();
                candidate->deleteLater();
                continue;
            }
            socket_ = candidate;
            authenticated_ = false;
            ui_ready_ = false;
            incoming_.clear();
            command_id_ = 0;
            command_accepted_ = false;
            connect(socket_, &QLocalSocket::readyRead, this, &AiIslandHost::readCommands);
            connect(socket_, &QLocalSocket::disconnected, this, [this, candidate]()
            {
                if (socket_ == candidate)
                {
                    socket_ = nullptr;
                    authenticated_ = false;
                    ui_ready_ = false;
                }
                candidate->deleteLater();
            });
        }
    }

    void AiIslandHost::readCommands()
    {
        if (socket_ == nullptr)
            return;
        incoming_ += socket_->readAll();
        qsizetype newline = -1;
        while ((newline = incoming_.indexOf('\n')) >= 0)
        {
            if (newline > ai_island_message_limit)
            {
                socket_->disconnectFromServer();
                return;
            }
            const QByteArray line = incoming_.left(newline);
            incoming_.remove(0, newline + 1);
            const QJsonObject message = QJsonDocument::fromJson(line).object();
            if (!authenticated_)
            {
                if (message.value("type") != "hello" || message.value("token").toString() != token_)
                {
                    socket_->disconnectFromServer();
                    return;
                }
                authenticated_ = true;
                sendState();
                continue;
            }
            if (message.value("type") == "ui_ready")
            {
                ui_ready_ = true;
                restart_attempts_ = 0;
                setStatus(message.value("hotkey").toBool()
                        ? QStringLiteral("灵动岛已就绪 · Ctrl+Shift+A")
                        : QStringLiteral("灵动岛已就绪；全局快捷键被占用，可点击小岛或设置页按钮"));
                if (pending_open_)
                {
                    pending_open_ = false;
                    allow_helper_focus(process_.processId());
                    send({{"type", "open"}});
                }
                continue;
            }
            if (!available_ || !agent_.configured())
                continue;
            const QString type = message.value("type").toString();
            if (type == "send")
            {
                const QString prompt = message.value("prompt").toString().trimmed();
                command_id_ = message.value("requestId").toInteger();
                command_accepted_ = false;
                if (!agent_.busy() && !prompt.isEmpty() && prompt.size() <= ai_prompt_limit)
                {
                    emit promptRequested(prompt);
                    command_accepted_ = agent_.busy();
                }
                sendState();
            }
            else if (type == "cancel" && agent_.busy())
                agent_.cancel();
            else if (type == "resume")
            {
                command_id_ = message.value("requestId").toInteger();
                if (agent_.resumable() && !agent_.busy())
                    agent_.resume();
                command_accepted_ = agent_.busy();
                sendState();
            }
            else if (type == "effort")
            {
                agent_.setThinkingEffort(message.value("value").toString());
                send({{"type", "effort_ack"}, {"requestId", message.value("requestId")},
                    {"value", agent_.thinkingEffort()}});
                sendState();
            }
            else if (type == "sync")
                sendState();
        }
        if (incoming_.size() > ai_island_message_limit)
            socket_->disconnectFromServer();
    }

    void AiIslandHost::send(const QJsonObject& message)
    {
        if (socket_ == nullptr || !authenticated_)
            return;
        const QByteArray bytes = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
        if (bytes.size() <= ai_island_message_limit)
            socket_->write(bytes);
    }

    void AiIslandHost::sendState()
    {
        if (!available_ || !agent_.configured())
            return;
        QJsonArray trace;
        auto* model = agent_.trace();
        const int start = qMax(0, model->rowCount() - 100);
        for (int row = model->rowCount() - 1; row >= start; --row)
        {
            const QVariantMap raw = model->data(model->index(row, 0), Qt::UserRole).toMap();
            if (raw.value("kind") != "tool" && raw.value("kind") != "office")
                continue;
            const QVariantMap item = ai_island_trace_entry(raw);
            if (!item.isEmpty())
                trace.append(QJsonObject::fromVariantMap(item));
            break;
        }
        QJsonObject state{{"type", "state"}, {"configured", agent_.configured()}, {"busy", agent_.busy()},
            {"resumable", agent_.resumable()}, {"status", agent_.islandStatus()},
            {"statusDetail", agent_.status().left(256)}, {"thinkingEffort", agent_.thinkingEffort()},
            {"location", location_}, {"trace", trace}, {"requestId", command_id_},
            {"accepted", command_accepted_}};
        if (command_id_ != 0 && !command_accepted_)
        {
            state["status"] = QStringLiteral("任务未启动");
            state["statusDetail"] = QStringLiteral("请先完成当前输入或文档确认，再重新发送；原输入已保留。");
        }
        while (QJsonDocument(state).toJson(QJsonDocument::Compact).size() > ai_island_message_limit &&
            !trace.isEmpty())
        {
            trace.removeAt(0);
            state["trace"] = trace;
        }
        send(state);
    }

    void AiIslandHost::stop()
    {
        update_timer_.stop();
        if (socket_ != nullptr)
        {
            socket_->disconnectFromServer();
            socket_ = nullptr;
        }
        authenticated_ = false;
        ui_ready_ = false;
        server_.close();
        if (process_.state() != QProcess::NotRunning)
        {
            process_.terminate();
            if (!process_.waitForFinished(1000))
            {
                process_.kill();
                process_.waitForFinished(1000);
            }
        }
    }
}
