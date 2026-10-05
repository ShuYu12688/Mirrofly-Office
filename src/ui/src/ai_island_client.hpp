#pragma once

#include <QAbstractNativeEventFilter>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>

class QWindow;

namespace mirrorfly
{
    class AiIslandClient final : public QObject, public QAbstractNativeEventFilter
    {
        Q_OBJECT
        Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
        Q_PROPERTY(bool expanded READ expanded NOTIFY stateChanged)
        Q_PROPERTY(bool compact READ compact NOTIFY stateChanged)
        Q_PROPERTY(QString thinkingEffort READ thinkingEffort NOTIFY stateChanged)
        Q_PROPERTY(QString effortPreview READ effortPreview NOTIFY stateChanged)
        Q_PROPERTY(bool effortPending READ effortPending NOTIFY stateChanged)
        Q_PROPERTY(bool configured READ configured NOTIFY stateChanged)
        Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
        Q_PROPERTY(bool resumable READ resumable NOTIFY stateChanged)
        Q_PROPERTY(QString status READ status NOTIFY stateChanged)
        Q_PROPERTY(QString statusDetail READ statusDetail NOTIFY stateChanged)
        Q_PROPERTY(QString answer READ answer NOTIFY stateChanged)
        Q_PROPERTY(QString usageText READ usageText NOTIFY stateChanged)
        Q_PROPERTY(QString contextText READ contextText NOTIFY stateChanged)
        Q_PROPERTY(QString location READ location NOTIFY stateChanged)
        Q_PROPERTY(QVariantList trace READ trace NOTIFY traceChanged)

    public:
        explicit AiIslandClient(const QString& server, const QString& token, QObject* parent = nullptr,
            bool enable_hotkey = true);
        ~AiIslandClient() override;

        bool ready() const;
        bool expanded() const;
        bool compact() const;
        QString thinkingEffort() const;
        QString effortPreview() const;
        bool effortPending() const;
        Q_INVOKABLE void setThinkingEffort(const QString& effort);
        void attachWindow(QWindow* window);
        Q_INVOKABLE bool startWindowDrag();
        bool configured() const;
        bool busy() const;
        bool resumable() const;
        QString status() const;
        QString statusDetail() const;
        QString answer() const;
        QString usageText() const;
        QString contextText() const;
        QString location() const;
        QVariantList trace() const;
        Q_INVOKABLE void toggle();
        Q_INVOKABLE void open();
        Q_INVOKABLE void collapse();
        void markUiReady();
        Q_INVOKABLE bool sendPrompt(const QString& prompt);
        Q_INVOKABLE void cancel();
        Q_INVOKABLE void resume();
        bool nativeEventFilter(const QByteArray& event_type, void* message, qintptr* result) override;

    signals:
        void stateChanged();
        void traceChanged();
        void dragFinished(int center_x, int top);
        void promptRejected(const QString& prompt);

    private:
        void readState();
        bool send(const QJsonObject& message);
        void beginCommand(const QString& type, const QString& prompt = {});

        QLocalSocket socket_;
        QByteArray incoming_;
        QString token_;
        QJsonObject state_;
        QVariantList trace_;
        bool ready_ = false;
        bool expanded_ = false;
        bool compact_ = false;
        QPointer<QWindow> window_;
        bool hotkey_registered_ = false;
        bool ui_ready_pending_ = false;
        QTimer sync_timer_;
        qint64 next_command_ = 0;
        qint64 pending_command_ = 0;
        qint64 pending_effort_id_ = 0;
        QString pending_prompt_;
        QString pending_effort_;
    };
}
