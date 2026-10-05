#include "ai_island_client.hpp"
#include "ai_island_protocol.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
    constexpr int hotkey_id = 0x4D46;
}

namespace mirrorfly
{
    AiIslandClient::AiIslandClient(
        const QString& server, const QString& token, QObject* parent, bool enable_hotkey)
        : QObject(parent), token_(token)
    {
        sync_timer_.setInterval(2000);
        connect(&sync_timer_, &QTimer::timeout, this, [this]()
        {
            send({{"type", "sync"}, {"requestId", pending_command_}});
        });
        connect(&socket_, &QLocalSocket::connected, this, [this]()
        {
            send({{"type", "hello"}, {"token", token_}});
            token_.clear();
            if (ui_ready_pending_)
                send({{"type", "ui_ready"}, {"hotkey", hotkey_registered_}});
        });
        connect(&socket_, &QLocalSocket::readyRead, this, &AiIslandClient::readState);
        connect(&socket_, &QLocalSocket::disconnected, QCoreApplication::instance(), &QCoreApplication::quit);
        connect(
            &socket_, &QLocalSocket::errorOccurred, QCoreApplication::instance(), &QCoreApplication::quit);
        socket_.connectToServer(server);
#ifdef Q_OS_WIN
        if (enable_hotkey)
        {
            QCoreApplication::instance()->installNativeEventFilter(this);
            hotkey_registered_ =
                RegisterHotKey(nullptr, hotkey_id, MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'A') != 0;
        }
#else
        Q_UNUSED(enable_hotkey)
#endif
    }

    AiIslandClient::~AiIslandClient()
    {
#ifdef Q_OS_WIN
        if (hotkey_registered_)
            UnregisterHotKey(nullptr, hotkey_id);
        QCoreApplication::instance()->removeNativeEventFilter(this);
#endif
    }

    bool AiIslandClient::ready() const
    {
        return ready_;
    }
    bool AiIslandClient::expanded() const
    {
        return expanded_;
    }
    bool AiIslandClient::configured() const
    {
        return state_.value("configured").toBool();
    }
    bool AiIslandClient::busy() const
    {
        return pending_command_ != 0 || state_.value("busy").toBool();
    }
    bool AiIslandClient::resumable() const
    {
        return state_.value("resumable").toBool();
    }
    QString AiIslandClient::status() const
    {
        return state_.value("status").toString();
    }
    QString AiIslandClient::statusDetail() const
    {
        return state_.value("statusDetail").toString();
    }

    QString AiIslandClient::answer() const
    {
        return state_.value("answer").toString();
    }
    QString AiIslandClient::usageText() const
    {
        return state_.value("usageText").toString();
    }
    QString AiIslandClient::contextText() const
    {
        return state_.value("contextText").toString();
    }
    QString AiIslandClient::location() const
    {
        return state_.value("location").toString();
    }
    QVariantList AiIslandClient::trace() const
    {
        return trace_;
    }

    void AiIslandClient::toggle()
    {
        if (!ready_)
            return;
        if (compact_ && expanded_)
            compact_ = false;
        else
            expanded_ = !expanded_;
        emit stateChanged();
    }

    void AiIslandClient::open()
    {
        if (!ready_)
            return;
        expanded_ = true;
        compact_ = false;
        emit stateChanged();
    }

    void AiIslandClient::markUiReady()
    {
        ui_ready_pending_ = true;
        if (socket_.state() == QLocalSocket::ConnectedState)
            send({{"type", "ui_ready"}, {"hotkey", hotkey_registered_}});
    }

    void AiIslandClient::collapse()
    {
        expanded_ = false;
        emit stateChanged();
    }

    bool AiIslandClient::sendPrompt(const QString& prompt)
    {
        const QString cleaned = prompt.trimmed();
        if (cleaned.size() > ai_prompt_limit)
        {
            state_["status"] = QStringLiteral("要求过长");
            state_["statusDetail"] = QStringLiteral("请输入不超过 4000 字的制作要求，原输入已保留。");
            emit stateChanged();
            return false;
        }
        if (!ready_ || !configured() || busy() || cleaned.isEmpty() ||
            socket_.state() != QLocalSocket::ConnectedState)
            return false;
        beginCommand("send", cleaned);
        return true;
    }

    void AiIslandClient::beginCommand(const QString& type, const QString& prompt)
    {
        pending_command_ = ++next_command_;
        pending_prompt_ = prompt;
        send({{"type", type}, {"prompt", prompt}, {"requestId", pending_command_}});
        sync_timer_.start();
        compact_ = true;
        emit stateChanged();
    }

    void AiIslandClient::cancel()
    {
        if (ready_ && busy())
            send({{"type", "cancel"}});
    }

    void AiIslandClient::resume()
    {
        if (ready_ && resumable() && !busy())
        {
            expanded_ = true;
            beginCommand("resume");
        }
    }

    bool AiIslandClient::nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result)
    {
        Q_UNUSED(event_type)
        Q_UNUSED(result)
#ifdef Q_OS_WIN
        const auto* native = static_cast<MSG*>(message);
        if (native->message == WM_EXITSIZEMOVE && window_ &&
            native->hwnd == reinterpret_cast<HWND>(window_->winId()))
        {
            emit dragFinished(window_->x() + window_->width() / 2, window_->y());
            window_->requestUpdate();
        }
        if (native->message == WM_HOTKEY && native->wParam == hotkey_id)
        {
            toggle();
            return true;
        }
#else
        Q_UNUSED(message)
#endif
        return false;
    }

    bool AiIslandClient::compact() const
    {
        return compact_;
    }

    QString AiIslandClient::thinkingEffort() const
    {
        return state_.value("thinkingEffort").toString(QStringLiteral("none"));
    }

    QString AiIslandClient::effortPreview() const
    {
        return pending_effort_.isEmpty() ? thinkingEffort() : pending_effort_;
    }

    bool AiIslandClient::effortPending() const
    {
        return pending_effort_id_ != 0;
    }

    void AiIslandClient::setThinkingEffort(const QString& effort)
    {
        if (ready_ && !busy() && QStringList{"none", "low", "high", "max"}.contains(effort))
        {
            pending_effort_id_ = ++next_command_;
            pending_effort_ = effort;
            send({{"type", "effort"}, {"value", effort}, {"requestId", pending_effort_id_}});
            emit stateChanged();
        }
    }

    void AiIslandClient::attachWindow(QWindow* window)
    {
        window_ = window;
    }

    bool AiIslandClient::startWindowDrag()
    {
        return window_ && window_->startSystemMove();
    }

    void AiIslandClient::readState()
    {
        incoming_ += socket_.readAll();
        qsizetype newline = -1;
        while ((newline = incoming_.indexOf('\n')) >= 0)
        {
            // The limit applies to one frame, not a backlog of valid state updates.
            if (newline > ai_island_message_limit)
            {
                socket_.disconnectFromServer();
                return;
            }
            const QByteArray line = incoming_.left(newline);
            incoming_.remove(0, newline + 1);
            const QJsonObject message = QJsonDocument::fromJson(line).object();
            if (message.value("type") == "state")
            {
                const QVariantList next_trace = message.value("trace").toArray().toVariantList();
                if (trace_ != next_trace)
                {
                    trace_ = next_trace;
                    emit traceChanged();
                }
                state_ = message;
                if (pending_command_ != 0 && message.value("requestId").toInteger() == pending_command_)
                {
                    const QString rejected = pending_prompt_;
                    pending_command_ = 0;
                    pending_prompt_.clear();
                    if (!message.value("accepted").toBool() && !rejected.isEmpty())
                    {
                        compact_ = false;
                        expanded_ = true;
                        emit promptRejected(rejected);
                    }
                }
                if (!busy())
                    sync_timer_.stop();
                else if (!sync_timer_.isActive())
                    sync_timer_.start();
                ready_ = configured();
                emit stateChanged();
            }
            else if (message.value("type") == "effort_ack" &&
                message.value("requestId").toInteger() == pending_effort_id_)
            {
                state_["thinkingEffort"] = message.value("value");
                pending_effort_id_ = 0;
                pending_effort_.clear();
                emit stateChanged();
            }
            else if (message.value("type") == "toggle")
                toggle();
            else if (message.value("type") == "open")
                open();
        }
        if (incoming_.size() > ai_island_message_limit)
            socket_.disconnectFromServer();
    }

    bool AiIslandClient::send(const QJsonObject& message)
    {
        if (socket_.state() == QLocalSocket::ConnectedState)
            return socket_.write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n') >= 0;
        return false;
    }
}
